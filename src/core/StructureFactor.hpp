#pragma once

#include "core/Rdf.hpp"
#include "core/Structure.hpp"

#include <vector>

namespace calango::core {

struct StructureFactorOptions {
    double qMin = 0.3;  ///< Å⁻¹ (S(q→0) diverges numerically for finite rMax)
    double qMax = 12.0; ///< Å⁻¹
    int qPoints = 400;
    /// The underlying g(r) evaluation (rMax bounds the Fourier integral —
    /// larger rMax = better low-q fidelity).
    RdfOptions rdf;
};

struct StructureFactorResult {
    std::vector<double> q; ///< Å⁻¹
    std::vector<double> s; ///< S(q), dimensionless
};

/// Static (isotropic) structure factor from a frame-averaged pair
/// distribution over a trajectory selection (a single structure is the
/// one-frame case):
///     S(q) = 1 + 4πρ ∫₀^rMax [g(r) − 1] r² sin(qr)/(qr) dr,
/// evaluated by trapezoidal quadrature on the RDF grid with a Lorch
/// window damping the truncation ripples at rMax. ρ is the number
/// density from the cell volume (bounding box for isolated systems, as
/// in the RDF). Suited to liquids/amorphous systems and powder-like
/// averages of crystals.
StructureFactorResult
computeStructureFactorAveraged(const std::vector<Structure>& frames,
                               const StructureFactorOptions& options);

} // namespace calango::core
