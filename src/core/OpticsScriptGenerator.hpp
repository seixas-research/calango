#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Parameters for the linear-optics (dielectric response) workflow. The
/// calculator carries the GPAW ground-state knobs (plane-wave cutoff, xc,
/// k-grid); the remaining fields describe the frequency sampling of the
/// dielectric function and which Cartesian directions to evaluate.
struct OpticsConfig {
    CalculatorConfig calculator;
    double broadeningEv = 0.1;  ///< Lorentzian broadening η, eV
    double omegaMinEv = 0.0;    ///< lower photon energy of the spectrum, eV
    double omegaMaxEv = 20.0;   ///< upper photon energy of the spectrum, eV
    int npoints = 500;          ///< frequency-grid samples
    bool dirX = true;           ///< εxx (light polarized along x)
    bool dirY = true;           ///< εyy
    bool dirZ = true;           ///< εzz
};

/// Standalone run.py: converges a plane-wave GPAW ground state, runs a fixed-
/// density NSCF step with extra empty bands, then uses GPAW's response module
/// (gpaw.response.df.DielectricFunction) to evaluate the frequency-dependent
/// dielectric function for each requested direction. Derives ε₁/ε₂, the complex
/// refractive index (n, k), the absorption coefficient α(ω), reflectivity R(ω)
/// and the energy-loss function L(ω), and writes them to `optics.json` for the
/// OpticsResultsWindow to read back.
std::string generateOpticsScript(const OpticsConfig& cfg);

} // namespace calango::core
