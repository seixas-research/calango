#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Parameters of the Raman / infrared spectroscopy post-process, filled in by
/// the Raman & IR wizard and consumed by generateRamanIrScript(). UI-free, so
/// the script can also be generated headlessly.
///
/// The two spectra answer different questions about the same Γ-point phonons
/// and are obtained from two different response quantities, which is why this
/// module inherits from two upstream processes rather than one:
///
///   IR    intensity of mode ν is |Σ_κ Z*_κ · e_κ(ν)/√M_κ|², the change in
///         macroscopic POLARIZATION the mode produces. Z* is exactly what the
///         Born Effective Charges process computes, so that run is a hard
///         requirement — there is no route to an IR intensity in a periodic
///         solid without it (a molecular dipole is not defined for a crystal).
///
///   Raman activity of mode ν is built from ∂χ/∂Q, the change in
///         POLARIZABILITY. That is the same electronic response the Optics
///         process evaluates, taken in the static limit ω → 0 and
///         differentiated with respect to the mode coordinate — which means
///         evaluating the dielectric tensor at 6N displaced geometries. Hence
///         "if necessary": switch Raman off and the run needs only the Born
///         charges and is cheap by comparison.
///
/// Both need the Γ-point force constants, which the script obtains by finite
/// differences of the forces with ase.vibrations.
struct RamanIrConfig {
    /// Provenance only — the engine the baseline ran under. Mode, cutoff, xc
    /// and k-grid all come back from the inherited .gpw, and re-declaring them
    /// here would let the wizard silently disagree with the baseline.
    CalculatorConfig calculator;

    /// ABSOLUTE path to a completed Single-Point Calculation's `.gpw` (written
    /// with mode="all"). Mandatory: it supplies the converged geometry the
    /// displacements are taken about and the calculator every displaced run is
    /// rebuilt from.
    std::string baselinePath;

    /// ABSOLUTE path to a completed Born Effective Charges run's
    /// `born_charges.json`. Mandatory — see the class comment.
    std::string bornChargesPath;

    /// ABSOLUTE path to a completed Optics run's `optics.json`, or empty.
    ///
    /// Optional and used for its SETTINGS, not its numbers: the broadening and
    /// the response k-sampling that were validated on the same material, so
    /// the static polarizability computed here is consistent with the spectrum
    /// the user already looked at. The dielectric tensors themselves have to be
    /// recomputed at each displaced geometry; a finished spectrum at the
    /// equilibrium geometry cannot supply a derivative.
    std::string opticsPath;

    /// Compute the Raman spectrum as well as the IR one. Off makes the run
    /// roughly 3–5× cheaper: the vibrational finite differences remain, but the
    /// 6N dielectric evaluations go away.
    bool computeRaman = true;

    /// Finite-difference displacement for the force constants, Å.
    double displacement = 0.01;

    /// Laser excitation wavelength, nm (the (ω_L − ω_ν)⁴ prefactor of the
    /// Stokes intensity). 532 nm is the usual green line.
    double laserWavelengthNm = 532.0;
    /// Sample temperature, K — sets the Bose occupation factor n(ω) + 1.
    double temperatureK = 300.0;

    /// Lorentzian half-width applied to both spectra, cm⁻¹.
    double broadeningCm = 4.0;
    /// Plotted frequency window and sampling, cm⁻¹.
    double frequencyMinCm = 0.0;
    double frequencyMaxCm = 1600.0;
    int npoints = 1600;
};

/// Standalone run.py: restores the baseline ground state, builds the Γ-point
/// Hessian by finite differences, reads Z* from the Born-charges run for the IR
/// intensities, optionally differentiates the static dielectric tensor for the
/// Raman activities, and writes `raman_ir.json` plus the
/// `CALANGO_RESULT raman_ir=raman_ir.json` marker the controller watches for.
std::string generateRamanIrScript(const RamanIrConfig& cfg);

} // namespace calango::core
