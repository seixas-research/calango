#pragma once

#include <string>
#include <vector>

namespace calango::core {

/// What field is written for each selected state. Note there is no separate
/// "signed" option: a real orbital's Real quantity already carries both
/// signs, and the volumetric pipeline already gives ANY field with negative
/// values the two-lobe (positive/negative) isosurface treatment
/// (VolumetricPanel::pumpIsoExtraction() gates on `bmin < 0.0`, not on a
/// per-field flag) — so "signed psi with two-lobe coloring" is exactly what
/// Real already is for a real-valued orbital, not a fourth quantity.
enum class WavefunctionQuantity {
    Density, ///< |psi(r)|^2 — always real and non-negative
    Real,    ///< Re(psi(r)) — the whole field for a real (Gamma-point or
             ///< molecular) orbital
    Imaginary, ///< Im(psi(r)) — identically zero for a real orbital,
               ///< written anyway ("for completeness", matching a k != Gamma
               ///< state's own nonzero imaginary part)
};

/// One selected Kohn-Sham state.
struct WavefunctionState {
    int spin = 0;
    int kpt = 0;
    int band = 0;
};

/// Parameters for the Wavefunctions module: real-space Kohn-Sham orbitals as
/// volumetric data. Shares the wavefunction-access layer with LDOS
/// (AseScriptGenerator::gpawRestartFromBaselineScript /
/// gpawWaveFunctionHelperScript) — this is Task 4's half of that shared
/// design, LdosConfig/generateLdosScript is Task 1's.
struct WavefunctionsConfig {
    /// Absolute directory of a completed GPAW single-point that saved its
    /// wavefunctions (`*.gpw`, `mode='all'`). Mandatory, like LdosConfig.
    std::string baselineDir;

    /// One or more states to export — multiple states in one pass write
    /// multiple cubes from a single restart, cheaper than one job per
    /// orbital. An EMPTY list makes generateWavefunctionsScript() emit a
    /// script that refuses immediately with a clear RuntimeError, before
    /// even restarting the baseline — the wizard's state table starts with
    /// every row unchecked, and running with none ticked would otherwise
    /// exit 0 having written nothing, with no visible sign anywhere of why
    /// the Volumetric Data dock stayed empty.
    std::vector<WavefunctionState> states;

    WavefunctionQuantity quantity = WavefunctionQuantity::Density;

    /// Pseudo (default) vs all-electron PAW reconstruction. All-electron
    /// needs the NEW GPAW engine's internal state
    /// (calc.dft.ibzwfs.get_all_electron_wave_function) — no legacy-engine
    /// equivalent; gpawWaveFunctionHelperScript raises a specific
    /// RuntimeError rather than failing opaquely if this is requested under
    /// a legacy restart.
    bool allElectron = false;
    /// Real-space grid spacing (Angstrom) for the all-electron
    /// reconstruction; unused for the pseudo path (which reuses the SCF's
    /// own grid).
    double allElectronGridSpacing = 0.05;

    std::string resultsJson = "wavefunctions.json";
};

/// Turns a WavefunctionsConfig into a standalone ASE/GPAW script: restarts
/// the baseline, and for every requested state writes one cube
/// (`psi_n<band>_k<kpt>_spin-<up|down>_<quantity>.cube`) plus
/// `wavefunctions.json` (one entry per state: the file written, its
/// spin/kpt/band, energy, occupation, and whether the parent is periodic —
/// the periodic-continuation eligibility flag the GUI needs when
/// registering the cube) and the `CALANGO_RESULT
/// wavefunctions=wavefunctions.json` marker.
std::string generateWavefunctionsScript(const WavefunctionsConfig& config);

} // namespace calango::core
