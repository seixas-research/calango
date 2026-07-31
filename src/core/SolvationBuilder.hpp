#pragma once

#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Solid-liquid and solid-gas interface construction: open a vacuum region
/// along one lattice direction of an existing structure and fill it with a
/// packed molecular fluid — one species, a mixture, or an ionic solution.
///
/// WHAT THIS PRODUCES, AND WHAT IT DOES NOT
///
/// The packing is hard-sphere rejection sampling: molecules are proposed at
/// random positions with random orientations inside the region and kept when
/// they clear everything already placed. That yields a geometry with the right
/// composition, the right density and no overlaps — a legitimate STARTING
/// POINT for an MD equilibration or a DFT relaxation.
///
/// It is NOT an equilibrated liquid. A random packing has no hydrogen-bond
/// network, no radial structure beyond the exclusion hole, and a potential
/// energy far above the equilibrium one. Quoting a property computed directly
/// on this cell — a diffusion coefficient, an interface energy, a work of
/// adhesion — would be reporting a property of the random number generator.
/// Equilibrate first. The generated structure's description says so too, so
/// the statement travels with the cell rather than living only here.
///
/// The exclusion test is between molecular CENTRES, using a tabulated
/// effective radius per species (Species::contactRadius) rather than an
/// all-atom criterion. That is the same simplification IceBuilder makes for
/// liquid water, and for the same reason: an all-atom tolerance large enough
/// to keep hydrogens apart also excludes the interlocking configurations a
/// dense molecular liquid is actually made of, so a strict all-atom packer
/// saturates well below the target density and silently returns a rarefied
/// cell. Contact against the SUBSTRATE is checked atom by atom, because that
/// surface is fixed and a molecule fused into it cannot relax out.
class SolvationBuilder {
public:
    /// What a species is offered as in the UI, and how its amount is meant.
    enum class Category {
        Liquid, ///< condensed phase, quoted with a liquid density
        Gas,    ///< quoted with a gas-phase density (or an explicit count)
        Ion,    ///< a single ionic species, inserted by count
        Salt,   ///< a formula unit that expands into several ions
    };

    /// A rigid molecular, atomic or ionic species the region can be filled
    /// with. Geometries are exact: every one is fixed by symmetry plus a bond
    /// length (and, for the bent and pyramidal cases, one angle). Species
    /// whose geometry cannot be written down that way are deliberately absent
    /// rather than approximated — see the note in the implementation.
    struct Species {
        std::string key;     ///< stable identifier ("water", "nh4+", "nacl")
        std::string name;    ///< display name
        std::string formula; ///< Hill-ish formula for the UI
        Category category = Category::Liquid;
        /// Formal charge in e. Non-zero only for ions; a salt carries the net
        /// charge of its formula unit, which is zero for every salt listed.
        double charge = 0.0;
        /// Reference density (g/cm³) offered as the default fill target.
        /// Liquids: the density at their normal boiling point or at 25 °C.
        /// Gases: the density at 1 bar and 273.15 K. Zero for ions and salts,
        /// which are inserted by count rather than by density.
        double referenceDensity = 0.0;
        /// Effective hard-sphere radius (Å) used by the packer. Half the
        /// first-neighbour separation in the corresponding fluid, so two
        /// molecules of the same species come no closer than that separation.
        double contactRadius = 1.4;
        /// Rigid geometry, referred to the molecular centroid.
        std::vector<int> numbers;
        std::vector<Vec3> positions;
        /// A salt expands into these species keys, repeated per formula unit
        /// (NaCl -> {"na+", "cl-"}, (NH4)2SO4 -> {"nh4+", "nh4+", "so4--"}).
        /// Empty for everything that is placed directly.
        std::vector<std::string> expandsTo;

        /// Molar mass of one unit (u), summed from the standard atomic
        /// weights. Zero for a salt, whose mass is its expansion's.
        double molarMassU() const;
    };

    /// Every species the builder can place, in UI order.
    static const std::vector<Species>& library();
    /// Look-up by key; null when absent.
    static const Species* find(const std::string& key);

    /// Which lattice direction the region is opened along. Named for the
    /// lattice vectors rather than for x/y/z because the region is bounded by
    /// planes normal to a x b — with a tilted cell those are not the Cartesian
    /// planes, and using the lattice vector is what keeps the region
    /// commensurate with the cell.
    enum class Axis { A = 0, B = 1, C = 2 };

    /// How the number of solvent molecules is decided.
    enum class Amount {
        Density, ///< from a target mass density over the filled volume
        Count,   ///< an explicit total molecule count
    };

    /// One entry of the solvent mixture. Amounts are MOLE FRACTIONS: they are
    /// normalized internally, so {water 3, ammonia 1} and {water 0.75,
    /// ammonia 0.25} request the same mixture.
    struct Component {
        std::string key;
        double fraction = 1.0;
    };

    /// An ionic species (or a salt) inserted into the region by count. Ions
    /// are placed BEFORE the solvent and their mass counts against the density
    /// target, which is what makes "1 M NaCl in water" come out at the density
    /// of brine rather than of water plus extra.
    struct IonicComponent {
        std::string key;
        int units = 0; ///< formula units
    };

    struct Params {
        // -- Region ---------------------------------------------------------
        Axis axis = Axis::C;
        /// Thickness (Å) of the fluid region to create, measured PERPENDICULAR
        /// to the other two lattice vectors — i.e. the gap between the
        /// substrate's top face and the bottom face of its own periodic image.
        /// The cell is grown (or shrunk) along `axis` to make this exact,
        /// whatever vacuum the input already carried.
        double regionThickness = 20.0;
        /// Lateral replication, applied to the two lattice vectors that are
        /// NOT `axis`, in their cyclic order.
        int lateral[2] = {1, 1};
        /// Closest approach (Å) between any fluid atom and any substrate atom.
        double surfaceClearance = 2.2;
        /// Move the substrate so it sits at the bottom of the cell along
        /// `axis`, leaving the whole region contiguous. Off keeps the input
        /// coordinates, which is what you want when the slab is already
        /// positioned deliberately.
        bool anchorSubstrate = true;

        // -- Fill -----------------------------------------------------------
        Amount amount = Amount::Density;
        double targetDensity = 0.997;  ///< g/cm³, used when amount = Density
        int moleculeCount = 0;         ///< used when amount = Count
        std::vector<Component> components{{"water", 1.0}};
        std::vector<IonicComponent> ions;

        // -- Packing --------------------------------------------------------
        /// Extra separation (Å) added to every pair of contact radii. Raise it
        /// for a looser cell that relaxes more easily; lower it to reach a
        /// higher density at the cost of more rejected attempts.
        double packingTolerance = 0.0;
        unsigned seed = 42;
    };

    struct Placement {
        std::string key;
        std::string name;
        int requested = 0;
        int placed = 0;
    };

    struct Result {
        Structure structure;
        /// Per species, in the order they were placed.
        std::vector<Placement> placements;
        int totalMolecules = 0;
        /// Volume (Å³) of the region that was filled, and the mass density
        /// (g/cm³) actually achieved inside it.
        double regionVolume = 0.0;
        double density = 0.0;
        /// Net charge (e) of the inserted ions. Non-zero means the cell is
        /// charged — legal in a calculation with a compensating background,
        /// but almost always a mistake, so it is reported rather than fixed.
        double netCharge = 0.0;
        /// Perpendicular thickness (Å) of the region actually created.
        double regionThickness = 0.0;
        std::string description;
        /// Non-fatal problems: a saturated packing, a shrunk cell, ions that
        /// exhausted the mass budget. Empty on a clean run.
        std::vector<std::string> warnings;
    };

    /// Throws std::invalid_argument when the request cannot be satisfied at
    /// all (no components, an unknown species key, a non-positive region, a
    /// substrate with no cell to grow).
    static Result generate(const Structure& substrate, const Params& params);

    /// Human-readable axis name ("c") — shared by the wizard and the
    /// generated description so the two cannot disagree.
    static std::string toString(Axis axis);
};

} // namespace calango::core
