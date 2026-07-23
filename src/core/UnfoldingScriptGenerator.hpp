#pragma once

#include "core/BandUnfolding.hpp"
#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Backends able to expose the plane-wave coefficients the Popescu-Zunger
/// projection needs. GPAW is the reference implementation here (its PW mode
/// gives direct access via get_pseudo_wave_function / wave-function
/// coefficients); the other two emit an editable template because their ASE
/// calculators do not expose the coefficients without extra tooling.
enum class UnfoldingBackend {
    Gpaw,
    Espresso,
    Siesta,
};

struct UnfoldingConfig {
    UnfoldingBackend backend = UnfoldingBackend::Gpaw;

    /// Staged input files (written next to run.py).
    std::string supercellFile = "structure.extxyz";
    std::string primitiveFile = "primitive.extxyz";

    /// supercell = M · primitive. Deduced by the wizard from the two cells,
    /// editable by the user.
    SupercellMatrix matrix;

    /// High-symmetry path on the PRIMITIVE lattice ("GXWK,UX"); empty lets
    /// ASE suggest one.
    std::string kpath;
    int pointsPerSegment = 40;

    /// Spectral-function sampling.
    SpectralFunctionOptions spectral;

    /// GPAW parameter set (mode/xc/eigensolver/mixer/convergence/k-grid),
    /// shared with every other DFT workflow.
    CalculatorConfig calculator;
};

/// Standalone run.py: converges the supercell, evaluates the eigenstates at
/// the folded wavevectors, projects them back onto the primitive Bloch basis,
/// and writes `effective_bands.json` (path coordinates, labels, energies and
/// spectral weights) for the Results heatmap.
std::string generateUnfoldingScript(const UnfoldingConfig& config);

} // namespace calango::core
