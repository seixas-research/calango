#pragma once

#include "core/Structure.hpp"

#include <vector>

namespace calango::core {

struct WarrenCowleyOptions {
    /// Outer radii of the coordination shells (Å), ascending. Shell k
    /// counts neighbors with r in (cutoffs[k-1], cutoffs[k]] (shell 0
    /// starts at contact). Two shells is the common choice for fcc/bcc
    /// alloys: first ~ nearest-neighbor distance + margin.
    std::vector<double> shellCutoffs{3.2, 4.8};
};

/// α values for one coordination shell: alpha[i][j] over the species list
/// of the result (row i = central species, column j = neighbor species).
struct WarrenCowleyShell {
    double rMin = 0.0;
    double rMax = 0.0;
    std::vector<std::vector<double>> alpha;
    double meanNeighbors = 0.0; ///< average neighbor count per atom

    /// The raw trial counts alpha[i][j] was computed from — pairCounts[i][j]
    /// is the number of (central = i, neighbor = j) pairs found in this
    /// shell, and neighborsOfSpecies[i] = sum_j pairCounts[i][j] is the
    /// total neighbor count any i-type atom has here (the denominator of
    /// p_ij). Exposed so a caller can put a counting-statistics error bar on
    /// alpha[i][j] — sigma_p = sqrt(p(1-p)/neighborsOfSpecies[i]),
    /// sigma_alpha = sigma_p / c_j — without re-deriving the counts alpha
    /// itself was built from. 0 for a species pair/shell with no data, same
    /// as alpha[i][j] being NaN in that case.
    std::vector<std::vector<double>> pairCounts;
    std::vector<double> neighborsOfSpecies;
};

/// Warren-Cowley short-range order parameters
///     α_ij = 1 − p_ij / c_j
/// where p_ij is the probability that a neighbor of an i-type atom is of
/// type j and c_j the overall concentration of j. α = 0 for the ideal
/// random alloy, α < 0 signals i–j ordering (unlike pairs preferred),
/// α > 0 signals clustering/segregation of like species.
struct WarrenCowleyResult {
    std::vector<int> species;           ///< sorted atomic numbers present
    std::vector<double> concentrations; ///< c_j, same order as `species`
    std::vector<WarrenCowleyShell> shells;
};

/// Periodic images are enumerated like the RDF, so shells wider than the
/// cell remain correct. O(N² · images).
WarrenCowleyResult computeWarrenCowley(const Structure& structure,
                                       const WarrenCowleyOptions& options);

} // namespace calango::core
