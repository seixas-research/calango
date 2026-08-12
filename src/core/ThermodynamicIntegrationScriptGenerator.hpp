#pragma once

// <cstdint> must stay even when clangd calls it unused: libstdc++ from GCC 13
// no longer pulls the fixed-width integer types in transitively, so removing it
// breaks the Linux .deb build while the macOS build stays green.
#include <cstdint>
#include <string>
#include <vector>

#include "core/CalculatorConfig.hpp"
#include "core/ThermodynamicIntegration.hpp"

namespace calango::core {

/// Everything the generated thermodynamic-integration script needs.
struct TiRunConfig {
    /// The TARGET Hamiltonian: the engine whose free energy is wanted. It goes
    /// through CalculatorConfig and AseScriptGenerator::calculatorSnippet like
    /// every other module, so MACE, xTB, GPAW, LAMMPS and the rest are reached
    /// the same way here as anywhere else — there is no second engine table.
    CalculatorConfig calculator;

    TiReference reference = TiReference::IdealGas;
    TiReferenceParameters referenceParameters;
    /// Cut-off of the Lennard-Jones REFERENCE potential, Å. Only read when the
    /// reference is the LJ fluid.
    double ljCutoffA = 10.0;

    TiLambdaSchedule schedule = TiLambdaSchedule::GaussLegendre;
    TiQuadrature quadrature = TiQuadrature::GaussLegendre;
    double scheduleExponent = 2.0;
    int windows = 12;

    /// Steps discarded before any sampling starts, per window.
    ///
    /// Separate from production and NOT optional. Averaging over the
    /// equilibration transient biases every window in the SAME direction — the
    /// system is still relaxing towards the λ-coupled ensemble — so the bias
    /// survives the λ integral instead of cancelling, and it does not look like
    /// noise in any plot.
    int equilibrationSteps = 2000;
    int productionSteps = 10000;
    /// Record ⟨∂U/∂λ⟩ every N steps during production.
    int sampleInterval = 10;

    /// Also traverse the path backwards, for the hysteresis check.
    ///
    /// Only meaningful when one job owns every window: the sweep has to be
    /// sequential, each window inheriting the previous window's configuration,
    /// or there is no history for the two directions to disagree about.
    bool hysteresis = false;

    /// The windows THIS job is responsible for. Empty means all of them.
    ///
    /// Splitting is what makes the windows dispatchable as separate jobs: every
    /// job writes its own per-window files into the shared `resultsDir`, and
    /// whichever job finishes last finds a complete set and writes the summary.
    std::vector<int> windowIndices;

    std::string structureFile = "structure.extxyz";
    /// Directory every job of this run writes its per-window files into.
    /// ABSOLUTE, because separate jobs have separate working directories and
    /// the only thing they share is this path.
    std::string resultsDir;
    /// Summary file name, written inside `resultsDir`.
    std::string resultsJson = "ti.json";
};

/// Generates the self-contained TI script.
///
/// SELF-CONTAINED is a hard rule in this project: the script imports nothing
/// from Calango, embeds its own JSON logging block, and runs unchanged on a
/// cluster that has never heard of this application.
///
/// DIVISION OF LABOUR. The script SAMPLES and nothing else: it runs the MD,
/// accumulates ⟨U_target − U_ref⟩ per window, and writes the per-window series.
/// The quadrature, the error propagation, the reference free energy and the
/// endpoint diagnostics are all in core::ThermodynamicIntegration, evaluated
/// when the results are read. That split exists so there is exactly ONE
/// implementation of the physics, and it is the one the tests pin against
/// closed forms. The script does compute a plain trapezoid ΔF as a convenience
/// for somebody running it standalone, and labels it as such.
class ThermodynamicIntegrationScriptGenerator {
public:
    static std::string generate(const TiRunConfig& config);

    /// Split `windows` indices into `jobs` slices of contiguous indices.
    ///
    /// Contiguous rather than round-robin: within one job the windows chain,
    /// each starting from the configuration the previous one left, and
    /// neighbouring λ values are the ones whose configurations are actually
    /// worth inheriting.
    static std::vector<std::vector<int>> splitWindows(int windows, int jobs);
};

} // namespace calango::core
