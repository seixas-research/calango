#pragma once

#include "core/Structure.hpp"

#include <vector>

namespace calango::core {

struct LocalEntropyOptions {
    double cutoff = 5.0; ///< Å — integration/neighbor cutoff r_c
    double sigma = 0.15; ///< Å — gaussian broadening of the local g_i(r)
    int gridPoints = 100; ///< radial grid resolution for the integral
    /// Replace each s_i by the mean over the atom and its neighbors
    /// within `cutoff` — sharpens the solid/liquid (ordered/disordered)
    /// contrast, as in the original fingerprint's averaged variant.
    bool averageOverNeighbors = false;
};

/// Per-atom pair-entropy fingerprint s_S^i (Piaggi & Parrinello,
/// J. Chem. Phys. 147, 114112 (2017)), in units of k_B:
///
///     s_S^i = −2π ρ ∫₀^rc [ g_i(r) ln g_i(r) − g_i(r) + 1 ] r² dr
///
/// with g_i(r) the gaussian-smoothed radial distribution around atom i.
/// The sharper the local structure, the more negative the entropy:
/// crystalline environments sit lowest, liquid-like ones higher, and an
/// ideal-gas environment (g ≡ 1) gives exactly 0. Periodic
/// images are enumerated like the RDF; for non-periodic structures the
/// density ρ is estimated per atom from the neighbor count inside r_c.
std::vector<double> computeLocalEntropy(const Structure& structure,
                                        const LocalEntropyOptions& options);

} // namespace calango::core
