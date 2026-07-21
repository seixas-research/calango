#pragma once

#include "core/Structure.hpp"

#include <vector>

namespace calango::core {

struct CoordinationOptions {
    /// How the neighbor cutoff between a pair of atoms is chosen.
    enum class CutoffMode {
        CovalentScaled, ///< d < tolerance * (r_cov(i) + r_cov(j)) — element-aware
        Fixed,          ///< d < fixedCutoff for every pair
    };

    CutoffMode cutoffMode = CutoffMode::CovalentScaled;
    double tolerance = 1.15;   ///< covalent-radius scaling factor
    double fixedCutoff = 3.0;  ///< Å, used in Fixed mode

    /// Bulk reference coordination cn_max that normalizes the GCN
    /// (12 for fcc/hcp, 8 for bcc, 4 for diamond...). Values <= 0 pick
    /// the maximum CN found in the structure automatically.
    double bulkCoordination = 0.0;
};

struct CoordinationResult {
    std::vector<int> cn;     ///< per-atom coordination number
    std::vector<double> gcn; ///< per-atom generalized coordination number
    double bulkCoordinationUsed = 0.0; ///< cn_max actually applied
};

/// Coordination number CN(i) and generalized coordination number GCN(i).
///
/// CN(i) counts neighbor *sites* within the cutoff; with a periodic cell
/// every lattice image is enumerated explicitly (like the RDF), so small
/// cells are handled exactly — an atom in a primitive fcc cell reports
/// CN = 12 even though all 12 neighbors are images of itself.
///
/// GCN follows Calle-Vallejo et al. (generalized coordination numbers,
/// see e.g. DOI 10.1002/advs.202207644): the CN of each neighbor is
/// summed and normalized by the bulk coordination cn_max,
///     GCN(i) = Σ_{j ∈ N(i)} CN(j) / cn_max,
/// so bulk atoms recover GCN = cn_max while surface, edge and vertex
/// sites are ranked by how undercoordinated their environment is.
CoordinationResult computeCoordination(const Structure& structure,
                                       const CoordinationOptions& options = {});

} // namespace calango::core
