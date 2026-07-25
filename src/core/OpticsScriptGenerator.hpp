#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Parameters for the linear-optics (dielectric response) workflow. The
/// calculator carries the GPAW ground-state knobs (plane-wave cutoff, xc,
/// k-grid); the remaining fields describe the frequency sampling of the
/// dielectric function and which Cartesian directions to evaluate.
struct OpticsConfig {
    /// Retained for provenance only (which engine the baseline ran under, for
    /// the script header and the interpreter the job binds to). The generator
    /// does NOT emit any of these values: mode, cutoff, xc, k-grid and smearing
    /// all come back from the inherited .gpw on restart, and re-declaring them
    /// here would let the wizard silently disagree with the baseline.
    CalculatorConfig calculator;
    /// ABSOLUTE path to the baseline GPAW restart file (`.gpw`, written with
    /// mode="all") from a completed Single-Point Calculation. Mandatory: the
    /// optics run loads that converged ground state and evaluates the response
    /// at FIXED DENSITY, never re-running the SCF cycle.
    ///
    /// This is not merely a saving. Re-converging the ground state inside the
    /// optics job would silently give a spectrum from a different SCF solution
    /// than the one the user validated — different smearing, a different
    /// k-grid, possibly a different magnetic state. Inheriting the baseline
    /// makes the spectrum attributable to a specific, inspected ground state.
    std::string baselineDensityPath;
    /// Out-of-plane vacuum axis for a 2D sheet (0=x, 1=y, 2=z), or -1 for a
    /// bulk 3D system. When set, the script also derives the 2D observables:
    /// the sheet polarizability α₂D, the 2D conductivity σ₂D and the
    /// absorbance A(ω) — quantities that are only meaningful once the
    /// arbitrary vacuum thickness is divided back out.
    int vacuumAxis = -1;
    double broadeningEv = 0.1;  ///< Lorentzian broadening η, eV
    double omegaMinEv = 0.0;    ///< lower photon energy of the spectrum, eV
    double omegaMaxEv = 20.0;   ///< upper photon energy of the spectrum, eV
    int npoints = 500;          ///< frequency-grid samples
    bool dirX = true;           ///< εxx (light polarized along x)
    bool dirY = true;           ///< εyy
    bool dirZ = true;           ///< εzz
};

/// Standalone run.py: loads the baseline ground state, runs a fixed-density
/// NSCF step with extra empty bands, then uses GPAW's response module
/// (gpaw.response.df.DielectricFunction) to evaluate the frequency-dependent
/// dielectric function for each requested direction. Derives ε₁/ε₂, the complex
/// refractive index (n, k), the absorption coefficient α(ω), reflectivity R(ω)
/// and the energy-loss function L(ω), and writes them to `optics.json` for the
/// OpticsResultsWindow to read back.
std::string generateOpticsScript(const OpticsConfig& cfg);

} // namespace calango::core
