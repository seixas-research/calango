#pragma once

#include <string>

namespace calango::core {

/// Which quasiparticle engine runs the G₀W₀ step.
enum class GwEngine {
    /// GPAW's native `gpaw.response.g0w0.G0W0`, restarting from a `.gpw`.
    Gpaw,
    /// Yambo, driven from a Quantum ESPRESSO `.save` directory through `p2y`.
    Yambo,
};

/// How the frequency dependence of the screened interaction W is treated.
enum class GwFrequencyTreatment {
    /// Plasmon-pole approximation: one effective plasmon frequency per G-vector
    /// pair. Cheap, and accurate for sp semiconductors; unreliable for systems
    /// with low-energy structure in the loss function (metals, some oxides).
    PlasmonPole,
    /// Full frequency integration on the real axis. Several times more
    /// expensive and the honest choice when the plasmon-pole assumption is in
    /// doubt.
    RealAxis,
};

struct GwConfig {
    GwEngine engine = GwEngine::Gpaw;
    GwFrequencyTreatment frequency = GwFrequencyTreatment::PlasmonPole;

    /// ABSOLUTE path to the baseline ground state: a `.gpw` for GPAW, the
    /// Quantum ESPRESSO run's directory (containing `*.save`) for Yambo.
    /// Mandatory — G₀W₀ is a perturbative correction ON TOP of a specific DFT
    /// solution, so the quasiparticle energies mean nothing without knowing
    /// which one.
    std::string baselinePath;

    /// Screened-interaction (dielectric matrix) cutoff, eV. This is THE
    /// convergence parameter of a GW calculation: quasiparticle gaps drift by
    /// several tenths of an eV with it, far more than with most DFT knobs.
    double screeningCutoffEv = 100.0;
    /// Bands included in the polarizability / self-energy sums. Converges
    /// jointly with the cutoff above — raising one alone is a common way to
    /// get a plausible-looking wrong answer.
    int bands = 0; ///< 0 = derive from the baseline's electron count

    /// Correct this many bands either side of the Fermi level. The band edges
    /// are what a gap renormalization needs; correcting the whole spectrum
    /// costs far more for information rarely used.
    int correctedBandsBelow = 4;
    int correctedBandsAbove = 4;

    /// Yambo only: MPI ranks for the yambo executable. GPAW inherits its rank
    /// count from the launch command (Preferences → Run).
    int yamboCores = 1;
};

/// Standalone `run.py` implementing the selected engine's G₀W₀ pipeline.
///
/// GPAW: restarts the baseline `.gpw`, runs a fixed-density NSCF to add the
/// empty bands the self-energy sums need, then `G0W0(...)` and writes
/// `gw.json`.
///
/// Yambo: the run is a sequence of external programs rather than a Python API,
/// so the script orchestrates them — `p2y` to convert the QE `.save` into a
/// Yambo `SAVE/` database, `yambo -g n -p p` to generate the G₀W₀ input, then
/// `yambo` to execute it — and parses the resulting `o-*.qp` report.
///
/// Both write `gw.json` in one schema: DFT and quasiparticle band edges, the
/// two gaps and the renormalization between them, so a single viewer reads
/// either engine's output.
std::string generateGwScript(const GwConfig& config);

} // namespace calango::core
