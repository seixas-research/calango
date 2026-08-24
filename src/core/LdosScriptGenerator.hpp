#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>
#include <vector>

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

    /// The engine, plus the knobs the VASP branch needs (ENCUT, k-points,
    /// PREC, XC, POTCAR path). Inherited from the parent's `calculator.json`
    /// rather than re-specified: an LPARD run reads the parent's own WAVECAR,
    /// and VASP redefines the plane-wave basis from the INCAR's ENCUT when it
    /// does (ISTART = 1, [VASP wiki: ISTART]), so a mismatch is silent
    /// re-padding rather than an error.
    ///
    /// `calculator.calculator == CalculatorKind::Gpaw` selects the
    /// wavefunction-summing GPAW branch above; `::Vasp` selects the LPARD
    /// branch. Nothing else is supported: no other engine in Calango exposes
    /// a state-resolved density at all.
    CalculatorConfig calculator;

    /// VASP only — LSEPB / LSEPK ([VASP wiki: LSEPB]). Off/off writes one
    /// summed `PARCHG`; either on splits the output per band and/or per
    /// k-point into `PARCHG.<band>.<kpt>` files, which the volumetric
    /// pipeline then registers individually.
    bool separateBands = false;
    bool separateKpoints = false;

    /// VASP only — an explicit band list (`IBAND`), which OVERRIDES the
    /// energy window: setting IBAND makes VASP set `NBMOD` to the number of
    /// entries and ignore `EINT` entirely ([VASP wiki: NBMOD]). Empty is the
    /// normal case.
    std::vector<int> bands;

    /// VASP only — an explicit k-point list (`KPUSE`, 1-based). Empty means
    /// every k-point contributes.
    std::vector<int> kpoints;
};

/// Turns an LdosConfig into a standalone ASE/GPAW script: restarts the
/// baseline (AseScriptGenerator::gpawRestartFromBaselineScript), sums
/// w_k*|psi_nks(r)|^2 over every stored state whose eigenvalue falls in the
/// selected window via the shared wavefunction-access helper
/// (AseScriptGenerator::gpawWaveFunctionHelperScript — always the pseudo
/// wavefunction; LDOS has no all-electron mode), writes `ldos.cube`, and
/// emits `ldos.json` (the window actually used, the states selected and
/// their weights) plus the `CALANGO_RESULT ldos=ldos.json` marker.
///
/// With `config.calculator.calculator == CalculatorKind::Vasp` it emits the
/// LPARD script instead — an entirely different shape, see
/// generateVaspLdosScript()'s own doc comment.
std::string generateLdosScript(const LdosConfig& config);

/// The VASP branch of generateLdosScript(): a **post-processing** run, not a
/// sum Calango performs.
///
/// VASP computes the state-resolved density itself. `LPARD = .TRUE.` makes it
/// "evaluate partial (band and/or k-point-decomposed) charge densities" from
/// the orbitals in a WAVECAR, and the VASP wiki is explicit that "an LPARD run
/// is a postprocessing step that requires a pre-converged calculation" and
/// that "no electronic (or ionic) minimization is performed, so the
/// calculation is rapid" (wiki: LPARD; Partial charge densities and STM
/// simulations). The selection is by energy window: `NBMOD = -3` means "use an
/// energy interval to select contributing bands and add the Fermi energy to
/// the passed values", `NBMOD = -2` the same interval read as absolute
/// eigenvalues (wiki: NBMOD), and `EINT` carries the two bounds in eV (wiki:
/// EINT). The result is `PARCHG`, in CHGCAR format.
///
/// THE THREE WAYS THIS DIFFERS FROM THE GPAW PATH, all of them visible to a
/// user and all of them documented rather than papered over:
///
///  1. **No cheap re-windowing.** The GPAW path holds every selected state's
///     |psi|^2 and can re-sum a new window in the viewer. VASP recomputes from
///     the WAVECAR for each window, so a new window is a new job — the wizard
///     exposes that as an explicit action rather than a slider that silently
///     queues a run.
///  2. **The parent must have written a WAVECAR.** `LWAVE = .TRUE.` is not
///     VASP's default for every workflow Calango generates, and an LPARD run
///     against a missing or truncated WAVECAR fails deep inside VASP. The
///     script checks for it by name and size, and says which parent setting
///     produces it.
///  3. **Noncollinear parents are refused.** "LPARD is not supported for
///     noncollinear calculations" (wiki: LPARD) — so a spin-orbit parent is a
///     refusal with a reason, not a crash.
std::string generateVaspLdosScript(const LdosConfig& config);

} // namespace calango::core
