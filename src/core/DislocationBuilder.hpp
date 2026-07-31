#pragma once

#include "core/Structure.hpp"

#include <array>
#include <string>
#include <vector>

namespace calango::core {

/// Line defects: Volterra dislocations inserted into an existing crystal by
/// displacing its atoms.
///
/// WHAT A DISLOCATION IS, OPERATIONALLY
///
/// Cut the crystal along a half-plane bounded by a line, slide the two faces
/// past each other by a lattice vector b, and weld them back together. The
/// elastic field that remains is the dislocation. Everything here is that one
/// operation: a closed-form displacement field u(x) is evaluated at every atom
/// and added to its position. No relaxation is performed and none is implied —
/// linear elasticity is singular at the line, so the handful of atoms nearest
/// the core arrive at positions that are qualitatively right and numerically
/// meaningless. RELAX THE CORE before quoting anything about it.
///
/// The formulas are the standard Volterra / Stroh results (Hirth & Lothe,
/// *Theory of Dislocations*); the anisotropic case is the sextic eigenvalue
/// formalism due to Stroh. They are implemented here from those results
/// directly — no external dislocation code is linked, called or vendored.
///
/// PERIODICITY IS THE HARD PART, AND IT IS WHY THERE ARE FIVE TYPES
///
/// A single dislocation has a net Burgers vector, so its displacement field is
/// multivalued: it cannot be made periodic in the two directions normal to the
/// line, whatever the cell. Edge, Screw and Anisotropic therefore produce a
/// cell that is periodic ONLY along the dislocation line, and say so. That is
/// the correct setup for a cylinder with free lateral surfaces, which is how
/// core structures are usually computed.
///
/// A DIPOLE — two dislocations of opposite sign — has zero net Burgers vector,
/// so its field is single-valued outside the ribbon joining the two cores, and
/// the cell stays periodic in all three directions. Glide and Climb are the
/// two ways a dipole can be arranged, and the difference between them is the
/// difference between the two ways a dislocation moves:
///
///   Glide  — the two cores lie in the SAME glide plane, separated along b.
///            Conservative: not one atom is created or destroyed. This is the
///            configuration left behind by a dislocation that has glided a
///            distance d; the crystal above the glide plane has slipped by b
///            relative to the crystal below it, but only between the cores.
///
///   Climb  — the two cores are stacked NORMAL to the glide plane. Between
///            them sits a platelet of missing material one Burgers vector
///            thick: a collapsed vacancy disc. Climb is non-conservative by
///            definition — it is mass transport — so this is the one type that
///            changes the atom count, and that change is how you tell it
///            happened.
///
/// Only the vacancy sense of climb is built (material removed). The
/// interstitial sense would mean inserting a partial plane of atoms, which
/// needs the lattice periodicity of the host and not merely its Burgers
/// vector; asking for it is refused rather than approximated.
class DislocationBuilder {
public:
    enum class Type {
        Edge,        ///< single Volterra edge, isotropic elasticity
        Screw,       ///< single Volterra screw, isotropic elasticity
        Glide,       ///< conservative edge dipole in one glide plane
        Climb,       ///< vacancy-platelet edge dipole (atoms removed)
        Anisotropic, ///< single dislocation via the Stroh sextic formalism
    };

    /// Cartesian axis the dislocation LINE runs along. The remaining two axes
    /// form the plane the field is written in, in cyclic (right-handed) order:
    /// line = Z gives (e1, e2) = (x, y); line = X gives (y, z); line = Y gives
    /// (z, x). e1 is the Burgers direction of an edge dislocation and e2 is
    /// the glide-plane normal, so naming the line names everything.
    enum class Axis { X = 0, Y = 1, Z = 2 };

    /// Which set of independent constants the anisotropic tensor is built from.
    enum class ElasticSymmetry {
        Cubic,      ///< C11, C12, C44
        Hexagonal,  ///< C11, C12, C13, C33, C44 (C66 = (C11 - C12)/2)
        Isotropic,  ///< C11, C12 with C44 = (C11 - C12)/2
    };

    struct Params {
        Type type = Type::Edge;
        Axis lineAxis = Axis::Z;
        /// |b| in Å. The SIGN of the dislocation is `burgersSign`; keeping the
        /// magnitude separate is what lets a dipole be described by one length.
        double burgers = 2.5;
        int burgersSign = +1;
        /// Poisson ratio for the three isotropic types. 0 <= nu < 0.5.
        double poisson = 0.33;
        /// Where the line sits, as a FRACTION of the cell along e1 and e2.
        /// {0.5, 0.5} is the centre. Fractional so the same parameters work on
        /// a cell of any size.
        std::array<double, 2> center{0.5, 0.5};

        /// Separation of the two cores of a dipole (Å): along e1 for Glide,
        /// along e2 for Climb. Zero or negative defaults to a third of the
        /// cell extent in that direction.
        double dipoleSeparation = 0.0;

        // -- Anisotropic ----------------------------------------------------
        ElasticSymmetry symmetry = ElasticSymmetry::Cubic;
        /// Elastic constants in GPa. Only the entries the chosen symmetry uses
        /// are read. The absolute scale is irrelevant to the displacement
        /// field — it depends on the RATIOS — but keeping physical units means
        /// numbers can be pasted in from a table.
        double c11 = 168.4;
        double c12 = 121.4;
        double c44 = 75.4;
        double c13 = 0.0; ///< hexagonal only
        double c33 = 0.0; ///< hexagonal only
        /// Burgers vector components in the dislocation frame (e1, e2, e3),
        /// as FRACTIONS of `burgers`. {1, 0, 0} is a pure edge, {0, 0, 1} a
        /// pure screw, anything between a mixed dislocation. Normalized
        /// internally, so {1, 0, 1} means a 45-degree mixed dislocation of
        /// length `burgers`. Read only by the Anisotropic type; the isotropic
        /// types are pure by construction.
        std::array<double, 3> burgersDirection{1.0, 0.0, 0.0};

        /// Wrap the displaced atoms back into the cell. Off leaves them where
        /// the field put them, which is what you want when the lateral
        /// directions are free surfaces rather than periodic.
        bool wrapIntoCell = false;
    };

    struct Result {
        Structure structure;
        /// Cores actually inserted, as Cartesian positions on the plane normal
        /// to the line, paired with the sign of their Burgers vector.
        std::vector<std::pair<Vec3, int>> cores;
        int atomsRemoved = 0;
        /// Largest displacement any atom received (Å). A value much larger
        /// than |b| means atoms passed close to the singular line.
        double maxDisplacement = 0.0;
        /// Closest approach between any two atoms afterwards (Å), computed
        /// under the cell. The core-quality number: if this is far below the
        /// nearest-neighbour distance the core is not merely unrelaxed, it is
        /// broken.
        double minSeparation = 0.0;
        /// Net Burgers vector of everything inserted (Å). Zero for a dipole.
        Vec3 netBurgers{};
        std::string description;
        std::vector<std::string> warnings;
    };

    /// Throws std::invalid_argument when the request is not buildable: no
    /// cell, an empty structure, a non-positive Burgers vector, a Poisson
    /// ratio outside [0, 0.5), an elastic tensor that is not positive
    /// definite, or a dipole separation that does not fit in the cell.
    static Result generate(const Structure& source, const Params& params);

    /// The three isotropic displacement fields, exposed so a test can check
    /// them point by point rather than only through a finished structure.
    /// `r` is measured FROM the core, in the (e1, e2) plane; the returned
    /// vector is in the (e1, e2, e3) frame.
    static Vec3 screwDisplacement(double x1, double x2, double burgers);
    static Vec3 edgeDisplacement(double x1, double x2, double burgers,
                                 double poisson);

    /// The anisotropic field, in the dislocation frame, for a Burgers vector
    /// `b` (frame components) and a 6x6 Voigt elastic tensor already expressed
    /// in that frame. Throws std::invalid_argument if the sextic cannot be
    /// solved.
    ///
    /// Exposed because the one property worth testing is that it REPRODUCES
    /// the isotropic closed form when handed isotropic constants, and that
    /// check needs the field, not a structure.
    static Vec3 anisotropicDisplacement(double x1, double x2, const Vec3& b,
                                        const std::array<std::array<double, 6>, 6>& voigt);

    /// Voigt tensor for `symmetry` from the named constants, in the CRYSTAL
    /// frame (x, y, z).
    static std::array<std::array<double, 6>, 6> elasticTensor(
        ElasticSymmetry symmetry, double c11, double c12, double c44,
        double c13, double c33);

    /// `voigt` rotated into the frame whose rows are e1, e2, e3.
    static std::array<std::array<double, 6>, 6> rotateVoigt(
        const std::array<std::array<double, 6>, 6>& voigt,
        const std::array<Vec3, 3>& frame);

    /// The right-handed frame (e1, e2, e3) belonging to a line along `axis`.
    static std::array<Vec3, 3> frameFor(Axis axis);

    static std::string toString(Type type);
    static std::string toString(Axis axis);
};

} // namespace calango::core
