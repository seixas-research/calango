#pragma once

#include <string>

namespace calango::core {

/// Which spin channel(s) contribute to the LDOS sum.
enum class LdosSpinChannel {
    Sum,  ///< every channel the baseline has (both, on a spin-polarized
          ///< parent; the only one, on an unpolarized or molecular one)
    Up,   ///< spin channel 0 only
    Down, ///< spin channel 1 only
};

/// Parameters for the Local Density of States (LDOS) module, filled in by
/// the LDOS wizard's energy-window widget and consumed by
/// generateLdosScript(). UI-free so the script can also be generated
/// headlessly (Orchestration nodes, tests).
///
/// LDOS(r) = sum over (n, k, s) with E_nks in [energyMin, energyMax] of
/// w_k * |psi_nks(r)|^2 — a plain sum over the pseudo-wavefunctions the
/// parent SCF already computed, weighted only by GPAW's own k-point
/// weights. Deliberately NOT weighted by occupation: an "unoccupied near
/// E_F" window is a real, useful LDOS (it is what an STM image above the
/// Fermi level shows) and would be silently zero everywhere if occupation
/// were a multiplicative factor. See ldos_test.cpp for the closed-form
/// check this implies: integrating over every doubly-occupied valence
/// state reproduces the pseudo density up to the KNOWN spin-degeneracy
/// factor (2, for a non-spin-polarized system), not exactly — LDOS and
/// the charge density are different quantities that coincide only up to
/// that understood factor.
///
/// Because the sum runs over stored eigenstates rather than the
/// irreducible wedge specifically, LDOS carries no "Symmetry: off"
/// pre-condition the way Wannierization does — GPAW's k-point weights
/// already account for whatever symmetry reduction the baseline used.
struct LdosConfig {
    /// Absolute directory of a completed GPAW single-point that saved its
    /// wavefunctions (`*.gpw`, `mode='all'`). Always set — LDOS is a
    /// post-process on an existing calculation with no "fresh SCF"
    /// fallback, like Wannier's baseline branch.
    std::string baselineDir;

    /// Energy window, in eV. Relative to the parent's Fermi level when
    /// `relativeToFermi` is true (the common case: "states within 0.5 eV
    /// of E_F"); otherwise interpreted as absolute Kohn-Sham eigenvalues.
    double energyMin = -0.5;
    double energyMax = 0.5;
    bool relativeToFermi = true;

    LdosSpinChannel spin = LdosSpinChannel::Sum;

    /// Output file names, written into the job directory.
    std::string outputCube = "ldos.cube";
    std::string resultsJson = "ldos.json";
};

/// Turns an LdosConfig into a standalone ASE/GPAW script: restarts the
/// baseline (AseScriptGenerator::gpawRestartFromBaselineScript), sums
/// w_k*|psi_nks(r)|^2 over every stored state whose eigenvalue falls in the
/// selected window via the shared wavefunction-access helper
/// (AseScriptGenerator::gpawWaveFunctionHelperScript — always the pseudo
/// wavefunction; LDOS has no all-electron mode), writes `ldos.cube`, and
/// emits `ldos.json` (the window actually used, the states selected and
/// their weights) plus the `CALANGO_RESULT ldos=ldos.json` marker.
std::string generateLdosScript(const LdosConfig& config);

} // namespace calango::core
