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
/// Both need the Γ-point force constants. How all three quantities are
/// obtained depends on the engine, and the three routes differ in cost by
/// orders of magnitude:
///
///   GPAW    finite displacements throughout — 6N force evaluations for the
///           Hessian, 6N self-consistent runs plus six dielectric evaluations
///           each for ∂α/∂u. Z* is INHERITED from a Born Effective Charges
///           run, because GPAW's own route to it is a further 6N Berry-phase
///           runs for a quantity the Raman spectrum never uses.
///
///   VASP    one DFPT run (IBRION=8 with LEPSILON) returns the force
///           constants, every Z* and ε∞ together, so the IR spectrum costs a
///           single job. Raman still needs 6N displaced LEPSILON runs — VASP
///           computes no Raman tensor of its own.
///
///   QE      one ph.x run at q = 0 returns all of it: `epsil` gives the force
///           constants, Z* and ε∞, and `lraman` adds the Raman tensor as an
///           analytic third-order response. Restricted to norm-conserving
///           pseudopotentials, which the generated script reports rather than
///           works around.
struct RamanIrConfig {
    /// The engine, and — for VASP and Quantum ESPRESSO — its ground-state
    /// knobs in full: those runs are self-contained, so ENCUT / ecutwfc, the
    /// k-grid, the functional and the pseudopotential library all come from
    /// here.
    ///
    /// For GPAW it is provenance only. Mode, cutoff, xc and k-grid all come
    /// back from the inherited `.gpw`, and re-declaring them here would let
    /// the wizard silently disagree with the baseline.
    CalculatorConfig calculator;

    /// GPAW only. ABSOLUTE path to a completed Single-Point Calculation's
    /// `.gpw` (written with mode="all"): it supplies the converged geometry
    /// the displacements are taken about and the calculator every displaced
    /// run is rebuilt from. Mandatory for that engine, unused by the others,
    /// which converge their own ground state.
    std::string baselinePath;

    /// GPAW only. ABSOLUTE path to a completed Born Effective Charges run's
    /// `born_charges.json`; empty drops the IR column (see the class comment).
    /// VASP and QE obtain Z* from their own linear-response run, so nothing
    /// has to be selected for them.
    std::string bornChargesPath;

    /// GPAW only. ABSOLUTE path to a completed Optics run's `optics.json`, or
    /// empty.
    ///
    /// Optional and used for its SETTINGS, not its numbers: the broadening and
    /// the response k-sampling that were validated on the same material, so
    /// the static polarizability computed here is consistent with the spectrum
    /// the user already looked at. The dielectric tensors themselves have to be
    /// recomputed at each displaced geometry; a finished spectrum at the
    /// equilibrium geometry cannot supply a derivative.
    std::string opticsPath;

    /// Compute the Raman spectrum as well as the IR one.
    ///
    /// What turning it off saves is engine-dependent, and the difference is
    /// large: on GPAW roughly 3–5× (the vibrational finite differences remain,
    /// the 6N dielectric evaluations go away); on VASP the whole 6N displaced
    /// LEPSILON sweep, leaving a single DFPT run; on QE nothing measurable,
    /// since ph.x returns the Raman tensor from the run it was doing anyway.
    bool computeRaman = true;

    /// Finite-difference displacement, Å. Used for the force constants and
    /// ∂α/∂u on GPAW, and for ∂α/∂u alone on VASP. Ignored by Quantum
    /// ESPRESSO, whose answer is an analytic derivative — which is why
    /// `raman_ir.json` reports `displacement_A = 0` there.
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
