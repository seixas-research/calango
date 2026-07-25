#pragma once

#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Water and ice structure generation, natively in C++ (no Python, no GenIce).
///
/// The hard part is not placing the oxygens — those come from crystallography —
/// but placing the protons. A real ice crystal is PROTON-DISORDERED: the oxygen
/// sublattice is periodic, yet the hydrogens are not, subject to the
/// Bernal-Fowler ice rules:
///
///   1. Each oxygen has exactly two covalently bonded hydrogens (an intact
///      water molecule).
///   2. Each O–O contact carries exactly one hydrogen (one donor, one acceptor).
///
/// Equivalently: orient every O–O bond so that each oxygen has in-degree 2 and
/// out-degree 2 on the hydrogen-bond graph. That is an Eulerian orientation of
/// a 4-regular graph, and this file solves it directly rather than by rejection
/// sampling — see solveIceRules() in the implementation.
///
/// The residual (Pauling) entropy of such a configuration is k_B ln(3/2) per
/// molecule, which is the measured value; a generator that quietly produced an
/// ordered proton arrangement would give a structure with the right oxygen
/// positions and the wrong physics.
class IceBuilder {
public:
    /// Which structure to build. Only phases whose oxygen sublattice can be
    /// generated from first principles or from well-established lattice
    /// parameters are offered — see the note in the implementation about the
    /// polymorphs deliberately not listed.
    enum class Phase {
        LiquidWater,   ///< amorphous packing at a target density
        IceIh,         ///< hexagonal ice — ordinary ice, proton-disordered
        IceIc,         ///< cubic ice — diamond oxygen lattice, proton-disordered
        IceVII,        ///< two interpenetrating Ic networks, proton-disordered
    };
    // Deliberately NOT offered: III, V, VI and IX have low-symmetry oxygen
    // sublattices this file would have to hard-code from published refinements,
    // and the clathrates (sI, sII) need 46- and 136-water cages. Generating
    // them from approximate coordinates would produce cells that look right and
    // are not — a worse outcome than not offering them. The proton-ORDERED
    // phases (XI, VIII) are likewise absent: ordering them properly needs a
    // dipole-driven search over ice-rule-preserving cycle flips, and a
    // configuration merely labelled "ordered" while being an arbitrary Eulerian
    // orientation would misrepresent the physics.

    /// Rigid-monomer geometries. These are STRUCTURAL presets only: the point
    /// charges and Lennard-Jones parameters that make a force field a force
    /// field are not written anywhere — only the O–H length and H–O–H angle
    /// that set the coordinates.
    enum class WaterGeometry {
        Rigid,  ///< experimental gas-phase monomer: 0.9572 Å, 104.52°
        Tip3p,  ///< 0.9572 Å, 104.52° (same geometry as the experimental monomer)
        Tip4p,  ///< 0.9572 Å, 104.52°; the M site is massless and not emitted
        Spce,   ///< 1.0 Å, 109.47° (tetrahedral)
    };

    struct Params {
        Phase phase = Phase::IceIh;
        WaterGeometry geometry = WaterGeometry::Rigid;
        int nx = 2, ny = 2, nz = 2; ///< cell replication (crystalline phases)
        /// Liquid water only: target density and how the box size is chosen.
        double densityGCm3 = 0.997;
        /// Liquid water only: number of molecules. When > 0 the box is sized to
        /// hold this many at `densityGCm3`; when 0 the explicit box below is
        /// used and the count follows from the density.
        int moleculeCount = 256;
        double boxLx = 20.0, boxLy = 20.0, boxLz = 20.0; ///< Å (liquid, count = 0)
        /// Minimum O–O separation enforced while packing the liquid (Å). 2.6 Å
        /// is just inside the first peak of the real O–O radial distribution.
        double minOODistance = 2.6;
        unsigned seed = 42;
    };

    struct Result {
        Structure structure;
        int moleculeCount = 0;
        double densityGCm3 = 0.0;
        /// Proton-disordered phases: how many O–O bonds violate the ice rules
        /// after the solver ran. Zero for a correct configuration — reported so
        /// a caller can surface a failure rather than shipping a bad cell.
        int iceRuleViolations = 0;
        /// Net dipole per molecule of the generated proton arrangement (Debye).
        /// A well-disordered cell is near zero; a large value means the solver
        /// landed on an ordered-looking configuration.
        double netDipolePerMolecule = 0.0;
        std::string description;
    };

    /// Throws std::invalid_argument when the request cannot be satisfied.
    static Result generate(const Params& params);

    /// Human-readable phase name (also the structure label).
    static std::string toString(Phase phase);
    /// O–H bond length (Å) and H–O–H angle (degrees) of a geometry preset.
    static void geometryOf(WaterGeometry geometry, double& ohLength,
                           double& hohAngleDeg);
};

} // namespace calango::core
