#pragma once

#include <vector>

namespace calango::core {

/// Harmonic vibrational thermodynamics from a phonon density of states.
///
/// Every property below is an integral over g(ω), the PhDOS, evaluated in the
/// harmonic approximation:
///
///   U(T) = ∫ ħω [ ½ + 1/(e^{ħω/k_BT} − 1) ] g(ω) dω
///   F(T) = k_BT ∫ ln( 2 sinh(ħω / 2k_BT) ) g(ω) dω
///   S(T) = (U − F) / T
///
/// The ½ term in U is the zero-point energy: it does not vanish at T = 0, and
/// F carries the same contribution through the sinh (ln 2sinh(x) → x as
/// T → 0). So U(0) = F(0) = E_ZPE and S(0) = 0, which is the Third Law and a
/// useful check on any implementation.
///
/// Imaginary modes are conventionally stored as NEGATIVE frequencies. They are
/// excluded from the integrals rather than silently contributing: the harmonic
/// expressions are undefined for ω < 0 (sinh of a negative argument makes the
/// logarithm complex), and a structure with imaginary modes is not at a minimum
/// so its harmonic thermodynamics are not meaningful anyway. `imaginaryWeight`
/// reports how much of the DOS was discarded so a caller can warn.
struct PhononThermoPoint {
    double temperatureK = 0.0;
    double internalEnergyEv = 0.0; ///< U_vib per cell (eV)
    double freeEnergyEv = 0.0;     ///< F_vib per cell (eV)
    double entropyEvPerK = 0.0;    ///< S_vib per cell (eV/K)
    double heatCapacityEvPerK = 0.0; ///< C_v per cell (eV/K)
};

struct PhononThermoResult {
    std::vector<PhononThermoPoint> points;
    /// Zero-point energy ½∫ħω g(ω)dω (eV per cell) — the T = 0 limit of both
    /// U and F.
    double zeroPointEnergyEv = 0.0;
    /// Fraction of the DOS weight at ω <= 0 that was excluded (0 for a
    /// well-converged structure at a minimum).
    double imaginaryWeight = 0.0;
    /// ∫g(ω)dω over the retained (positive) modes. A converged PhDOS
    /// integrates to 3N; the caller can use this to report the mode count or
    /// to normalize.
    double totalModes = 0.0;
};

/// Evaluate the harmonic properties on a temperature grid.
///
/// `frequenciesCm` and `dos` are the PhDOS as written by the phonon scripts:
/// frequency in cm⁻¹ (negative = imaginary) and states per cm⁻¹. The integrals
/// use the trapezoidal rule on that grid, so the result is only as good as the
/// DOS sampling — which is the honest behavior, since the DOS is the input.
///
/// The temperature grid runs from `minTemperatureK` to `maxTemperatureK` in
/// `steps` points (inclusive). T = 0 is handled analytically (the Bose factor
/// underflows) and yields exactly the zero-point energy.
PhononThermoResult computePhononThermodynamics(
    const std::vector<double>& frequenciesCm, const std::vector<double>& dos,
    double minTemperatureK = 0.0, double maxTemperatureK = 1000.0,
    int steps = 101);

} // namespace calango::core
