#pragma once

#include "core/Structure.hpp"

#include <array>
#include <string>
#include <vector>

namespace calango::core {

/// Planar defects and grain structures: the constructions that put two pieces
/// of crystal next to each other.
///
/// All five are the same operation seen from different distances — decide, for
/// every point of space, which crystal orientation occupies it, then fill each
/// region with that lattice and reconcile the seams.
///
///   StackingFault  Two regions, same orientation, offset in-plane by a
///                  fraction of a lattice vector. The cheapest planar defect
///                  there is, and the one whose energy sets how easily a
///                  dislocation dissociates.
///   TwinBoundary   Two regions related by a mirror through the boundary. A
///                  coherent twin: every atom on the plane is shared, so the
///                  boundary carries no free volume.
///   Bicrystal      Two regions with independent orientations, joined at a
///                  plane. The general grain boundary.
///   Polycrystal    N regions carved out by a Voronoi tessellation of N random
///                  seeds, each with a random orientation.
///   MultiPhase...  The same, with each grain drawing its lattice from a list
///                  of phases rather than all from one.
///
/// PERIODICITY, AND THE THING EVERY ONE OF THESE GETS WRONG
///
/// A periodic cell cannot contain an odd number of parallel interfaces. Insert
/// one boundary at the middle of a cell and you have inserted TWO: the one you
/// asked for, and the one where the top face meets the bottom face of the next
/// image. That is not a defect of this implementation, it is what periodic
/// boundary conditions mean, and every result here reports both. A calculation
/// that attributes the whole excess energy of the cell to "the" boundary is
/// out by a factor of two.
///
/// COMMENSURABILITY
///
/// Rotating a lattice and demanding it still fit the box is a strong
/// condition: only the coincidence-site (CSL) misorientations satisfy it
/// exactly. For anything else the two crystals meet the periodic boundary out
/// of register, and the mismatch is absorbed by whatever the atoms nearest the
/// seam do. The residual is measured and reported rather than hidden, so a
/// bicrystal built at an arbitrary angle announces what it is.
///
/// Nothing here is relaxed. Grain boundaries have structure — free volume,
/// reconstruction, segregation — that no geometric construction produces.
/// These are STARTING POINTS.
class SolidInterfaceBuilder {
public:
    enum class Kind {
        StackingFault,
        TwinBoundary,
        Bicrystal,
        Polycrystal,
        MultiPhasePolycrystal,
    };

    /// Which lattice vector the boundary normal follows. Named for the lattice
    /// rather than for x/y/z so the boundary plane is spanned by the other two
    /// lattice vectors and is therefore commensurate with the cell by
    /// construction.
    enum class Axis { A = 0, B = 1, C = 2 };

    struct Params {
        Kind kind = Kind::StackingFault;

        // -- Planar defects (StackingFault, TwinBoundary, Bicrystal) --------
        Axis axis = Axis::C;
        /// Where the boundary sits along `axis`, as a cell fraction.
        double boundaryPosition = 0.5;
        /// In-plane displacement of everything above the boundary, as
        /// FRACTIONS of the two in-plane lattice vectors. {1/3, 0} is the
        /// classic partial-dislocation fault vector of a close-packed plane.
        std::array<double, 2> faultVector{1.0 / 3.0, 0.0};
        /// Extra separation (Å) opened at the boundary, measured
        /// perpendicular to it. Zero for a coherent interface; a small
        /// positive value gives a relaxation a starting gap instead of a
        /// pair of overlapping planes.
        double gap = 0.0;
        /// Atoms on opposite sides of a seam closer than this are merged (one
        /// of the pair is deleted). Set to zero to keep every atom.
        double mergeTolerance = 0.5;

        // -- Bicrystal ------------------------------------------------------
        /// Rotation of each grain about the boundary normal, in degrees. The
        /// MISORIENTATION is the difference; both are offered because the
        /// absolute orientation relative to the box matters too.
        double rotationA = 0.0;
        double rotationB = 36.87; ///< the Sigma-5 twist misorientation
        /// Size of the constructed box, in multiples of the parent cell.
        /// Applies to Bicrystal and both polycrystals; the planar defects
        /// operate on the input cell as it stands.
        std::array<int, 3> repeat{4, 4, 4};

        // -- Polycrystals ---------------------------------------------------
        int grainCount = 8;
        unsigned seed = 42;
        /// Relative abundance of each supplied phase, normalized internally.
        /// Empty means equal weights. Read only by MultiPhasePolycrystal.
        std::vector<double> phaseWeights;

        /// Refuse to build past this many atoms rather than allocating a cell
        /// nobody asked for. A 12x12x12 repeat of a modest cell is already
        /// six figures.
        int atomBudget = 400000;
    };

    struct GrainReport {
        int index = 0;
        int phase = 0;      ///< which entry of `lattices` this grain came from
        Vec3 seed{};        ///< Voronoi seed position (Cartesian)
        int atomCount = 0;
    };

    struct Result {
        Structure structure;
        std::vector<GrainReport> grains;
        /// Atoms deleted because they landed on top of another one across a
        /// seam. A large number relative to the total means the merge
        /// tolerance is eating the structure, not tidying it.
        int mergedAtoms = 0;
        /// Interfaces actually present in the periodic cell (2 for a single
        /// planar defect; the grain count for a polycrystal).
        int interfaceCount = 0;
        /// Closest approach between any two atoms afterwards (Å).
        double minSeparation = 0.0;
        /// How far the rotated lattice misses the box periods, in Å. Zero for
        /// an exact coincidence-site relationship; anything above a fraction
        /// of a bond length means the two crystals meet the periodic boundary
        /// out of register.
        double commensurabilityResidual = 0.0;
        double density = 0.0; ///< g/cm³ of the finished cell
        std::string description;
        std::vector<std::string> warnings;
    };

    /// `lattices` supplies the parent crystal(s). Every kind reads
    /// lattices[0]; MultiPhasePolycrystal draws each grain from the whole
    /// list. Throws std::invalid_argument when the request cannot be built:
    /// no lattice, no cell, a boundary position outside (0, 1), a twin plane
    /// that is not perpendicular to its lattice vector, fewer grains than 1,
    /// or an atom budget the request would blow through.
    static Result generate(const std::vector<Structure>& lattices,
                           const Params& params);

    static std::string toString(Kind kind);
    static std::string toString(Axis axis);
};

} // namespace calango::core
