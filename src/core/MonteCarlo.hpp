#pragma once

#include "core/Structure.hpp"

#include <vector>

namespace calango::core {

/// Parameters for the native swap-atoms (lattice) Monte Carlo sampler.
struct SwapMonteCarloOptions {
    double temperatureK = 500.0;
    long steps = 100000;
    /// Unlike-pair interaction energy V (eV). Every neighbor bond between two
    /// different species contributes V; like-pairs contribute nothing. V < 0
    /// favors ordering (more unlike bonds), V > 0 favors clustering /
    /// segregation (fewer unlike bonds).
    double interactionEv = -0.05;
    /// Neighbor cutoff (Å) that defines a "bond" for the energy model.
    double neighborCutoff = 3.2;
    /// Record a trajectory snapshot every this many accepted-or-rejected steps.
    long snapshotInterval = 2000;
    unsigned seed = 42;
};

struct SwapMonteCarloResult {
    Structure finalStructure;
    Structure bestStructure; ///< lowest-energy configuration visited
    std::vector<Structure> snapshots;
    std::vector<int> stepTrace;      ///< step index for each energy sample
    std::vector<double> energyTrace; ///< model energy (eV) at each sample
    double initialEnergy = 0.0;
    double finalEnergy = 0.0;
    double bestEnergy = 0.0;
    double acceptanceRatio = 0.0;
    long acceptedMoves = 0;
    /// Fraction of neighbor bonds that are between unlike species in the final
    /// configuration (a simple short-range-order / mixing indicator).
    double finalUnlikeFraction = 0.0;
    std::string note;
};

/// Metropolis swap-atoms Monte Carlo for alloy ordering / segregation.
///
/// A pure-C++ lattice sampler: it repeatedly picks two sites of different
/// species, swaps them, evaluates the change in a nearest-neighbor bond-energy
/// model, and accepts by the Metropolis criterion at the requested
/// temperature. No calculator or subprocess is involved, so it is fast and
/// self-contained; use it to study site preference, chemical ordering and
/// segregation trends.
SwapMonteCarloResult runSwapMonteCarlo(const Structure& start,
                                       const SwapMonteCarloOptions& options);

} // namespace calango::core
