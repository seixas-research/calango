#pragma once

#include "core/Structure.hpp"

#include <vector>

namespace calango::core {

struct RdfOptions {
    double rMax = 10.0;   ///< Å
    int bins = 200;
    bool usePbc = true;   ///< periodic images within rMax (exact, not capped
                          ///< at L/2 — neighbor cells are enumerated)
    int elementA = 0;     ///< atomic number filter for the first partner (0 = any)
    int elementB = 0;     ///< atomic number filter for the second partner (0 = any)
};

struct RdfResult {
    std::vector<double> r; ///< bin centers, Å
    std::vector<double> g; ///< g(r), dimensionless
};

/// Pair radial distribution function g(r).
///
/// Total RDF (elementA = elementB = 0) or partial RDF g_AB for a specific
/// element pair. Ordered pairs (i in A, j in B, i != j) are histogrammed
/// and normalized by N_A · ρ_B · V_shell, so partials satisfy the usual
/// Faber-Ziman convention. With PBC enabled all periodic images within
/// rMax are enumerated explicitly (valid for any rMax and triclinic
/// cells); without PBC the density uses the bounding-box volume, which is
/// the customary choice for isolated molecules.
RdfResult computeRdf(const Structure& structure, const RdfOptions& options);

} // namespace calango::core
