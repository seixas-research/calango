#pragma once

#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Amorphous and single-chain polymer construction, natively in C++.
///
/// A chain is grown one monomer at a time along a backbone walk. The backbone
/// carries the chain's shape (extended, helical, random or self-avoiding); each
/// monomer's side groups are then attached to the backbone carbons with
/// tetrahedral geometry, and the tacticity decides which side of the backbone
/// plane each substituent goes on.
///
/// The distinction that matters physically: a RANDOM walk may pass through
/// itself, giving overlapping atoms and an unusable cell, while a SELF-AVOIDING
/// walk rejects steps that come too close to the existing chain. The latter is
/// what an amorphous cell needs; the former is only useful as a fast
/// approximation for very short chains.
class PolymerBuilder {
public:
    /// Monomer chemistries. Each is defined by its backbone repeat (one or two
    /// carbons) plus the substituents hanging off them.
    enum class Monomer {
        Polyethylene,   ///< -[CH2-CH2]-
        Polypropylene,  ///< -[CH2-CH(CH3)]-  (tacticity applies)
        Polystyrene,    ///< -[CH2-CH(C6H5)]- (tacticity applies)
        Ptfe,           ///< -[CF2-CF2]-
        PolyvinylChloride, ///< -[CH2-CHCl]-  (tacticity applies)
        Nylon66,        ///< -[NH-(CH2)6-NH-CO-(CH2)4-CO]- repeat unit
    };

    /// Which side of the backbone each substituent sits on. Only meaningful for
    /// monomers with a stereocentre (a substituted backbone carbon).
    enum class Tacticity {
        Isotactic,    ///< every substituent on the same side
        Syndiotactic, ///< alternating sides
        Atactic,      ///< random sides — the usual laboratory product
    };

    /// Backbone shape.
    enum class Conformation {
        Extended,       ///< all-trans zig-zag: the crystalline chain
        Helical,        ///< a uniform torsion per step (PTFE, isotactic PP)
        RandomWalk,     ///< freely-jointed with a fixed valence angle
        SelfAvoidingWalk, ///< as RandomWalk, rejecting steps that self-overlap
    };

    /// Chemical group terminating each chain end.
    enum class EndCap {
        Hydrogen, ///< -H
        Methyl,   ///< -CH3
        Hydroxyl, ///< -OH
    };

    struct Params {
        Monomer monomer = Monomer::Polyethylene;
        Tacticity tacticity = Tacticity::Atactic;
        Conformation conformation = Conformation::SelfAvoidingWalk;
        EndCap endCap = EndCap::Hydrogen;
        int degreeOfPolymerization = 20; ///< monomers per chain
        int chainCount = 1;              ///< 1 = single chain, >1 = amorphous pack
        /// Simulation box. When `useDensityTarget` the box edge is derived from
        /// the total chain mass and `densityGCm3`; otherwise the explicit
        /// dimensions are used.
        bool useDensityTarget = true;
        double densityGCm3 = 0.92;
        double boxLx = 30.0, boxLy = 30.0, boxLz = 30.0;
        /// Minimum separation enforced between atoms of different chains (and,
        /// for a self-avoiding walk, along one chain). Below ~2 Å the cell will
        /// not survive the first minimization step.
        double minAtomDistance = 2.2;
        /// Helical torsion per backbone step (degrees). 180 is all-trans;
        /// PTFE's 15/7 helix is near 165.
        double helixTorsionDeg = 165.0;
        unsigned seed = 42;
    };

    struct Result {
        Structure structure;
        int chains = 0;
        int atomsPerChain = 0;
        double densityGCm3 = 0.0;
        /// Chains that could not be placed without overlapping an existing one
        /// before the attempt budget ran out. Non-zero means the box is too
        /// dense for the packer — reported rather than hidden.
        int failedChains = 0;
        /// Backbone steps rejected by the self-avoiding walk. A large number
        /// relative to the chain length means the walk is struggling and the
        /// conformation is closer to a compact globule than a random coil.
        long rejectedSteps = 0;
        std::string description;
    };

    /// Throws std::invalid_argument when the request cannot be satisfied.
    static Result generate(const Params& params);

    static std::string toString(Monomer monomer);
    static std::string toString(Tacticity tacticity);
    /// Whether a monomer has a stereocentre, i.e. whether tacticity applies.
    static bool hasTacticity(Monomer monomer);
    /// Molar mass of one repeat unit (u) — used for the density target.
    static double monomerMassU(Monomer monomer);
};

} // namespace calango::core
