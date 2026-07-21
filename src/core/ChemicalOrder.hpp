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
