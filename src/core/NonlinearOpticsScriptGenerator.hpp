#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Gauge for the second-harmonic sum over bands.
///
/// The two are formally equivalent and numerically are not: the length gauge
/// works with position matrix elements and their generalized derivatives, the
/// velocity gauge with momentum matrix elements alone. The velocity gauge
/// carries low-frequency divergences that cancel only in the limit of a
/// complete band set, so a truncated sum leaves it visibly wrong as ω → 0,
/// while the length gauge converges from above with far fewer bands.
///
/// Both are offered because their DISAGREEMENT is the standard convergence
/// test for a χ⁽²⁾ spectrum — running the two and overlaying them says more
/// about whether the band summation is converged than any single number does.
enum class NlOpticsGauge {
    Length,   ///< 'lg' — GPAW's default and the better-converging choice
    Velocity, ///< 'vg' — the independent check
};

const char* toString(NlOpticsGauge gauge);

/// Parameters of the nonlinear-optics workflow (GPAW's `gpaw.nlopt`).
///
/// Unlike every other response module here, this one is SELF-CONTAINED rather
/// than inheriting a completed Single-Point Calculation. That is not a
/// stylistic difference: `gpaw.nlopt.matrixel.make_nlodata` asserts that
/// point-group symmetry is off, and the sum over intermediate states needs a
/// large set of CONVERGED empty bands and a k-mesh several times denser than a
/// total energy needs. An ordinary baseline satisfies none of the three, and
/// the first of them fails as a bare AssertionError. So the ground state is
/// converged here, with the settings the nonlinear response actually requires.
struct NonlinearOpticsConfig {
    /// The GPAW ground state. The generator overrides three of its fields
    /// regardless of what the wizard collected, because they are requirements
    /// of the method rather than preferences: point-group symmetry off, a
    /// band count large enough to sum over, and an explicit converged-band
    /// count so those empty states are not left as SCF noise.
    CalculatorConfig calculator;

    /// Second-harmonic generation, χ⁽²⁾(−2ω; ω, ω) — `gpaw.nlopt.shg.get_shg`.
    bool computeShg = true;
    /// Shift current, the ballistic photocurrent response σ⁽²⁾(0; ω, −ω) —
    /// `gpaw.nlopt.shift.get_shift`.
    bool computeShift = false;
    /// The full linear susceptibility tensor χ⁽¹⁾(ω) —
    /// `gpaw.nlopt.linear.get_chi_tensor`. Cheap once the matrix elements
    /// exist, and worth having beside the nonlinear spectra: the χ⁽²⁾ peaks of
    /// a semiconductor sit at ω and 2ω of the χ⁽¹⁾ absorption edge, so reading
    /// one without the other invites assigning a resonance to the wrong
    /// process.
    bool computeLinear = false;

    NlOpticsGauge gauge = NlOpticsGauge::Length;

    /// Tensor components to evaluate, each three letters from "xyz" (e.g.
    /// "yyy", "xxy"). Both χ⁽²⁾ and the shift current are third-rank tensors,
    /// so both use this list; the linear tensor is computed whole and ignores
    /// it.
    ///
    /// There is no "all 27" option. Most vanish by symmetry, evaluating one
    /// costs a full sum over bands and k-points, and a user who does not know
    /// which components their point group allows is better served by the
    /// symmetry analysis than by 27 spectra of which 24 are numerical noise.
    std::vector<std::string> components{"yyy"};

    double broadeningEv = 0.05;  ///< η, the Lorentzian broadening (eV)
    double omegaMinEv = 0.0;     ///< lower photon energy of the spectrum, eV
    double omegaMaxEv = 6.0;     ///< upper photon energy, eV
    int npoints = 500;           ///< samples across the window

    /// Scissors shift applied to the band gap before the sums, eV.
    ///
    /// Semilocal DFT understates the gap, and χ⁽²⁾ is far more sensitive to
    /// that than the linear response is: the two-photon resonance sits at
    /// half the gap, so an error there moves a peak by half as much again and
    /// changes its height through the 1/ω factors. Applied as GPAW's
    /// `eshift`, and reported in the output so a spectrum never silently
    /// carries one.
    double scissorsEv = 0.0;

    /// Band window handed to `make_nlodata` as (ni, nf) — the states the
    /// matrix elements are built for. `bandsLast` at 0 means "up to the last
    /// band"; negative values count from the top, as GPAW reads them.
    int bandsFirst = 0;
    int bandsLast = 0;

    /// Out-of-plane vacuum axis for a 2D sheet (0=x, 1=y, 2=z), or -1 for
    /// bulk. A supercell χ⁽²⁾ is diluted by whatever vacuum was used — double
    /// it and the number halves — so for a monolayer the reportable quantity
    /// is the SHEET susceptibility χ⁽²⁾ × L, which is what the GPAW tutorial
    /// plots and what the literature quotes in nm²/V.
    int vacuumAxis = -1;
};

/// Standalone run.py: converges a ground state with the symmetry, band count
/// and convergence the nonlinear response requires, builds the momentum matrix
/// elements with `make_nlodata` (saved as `mml.npz` and reused by every
/// requested spectrum), then evaluates SHG / shift current / χ⁽¹⁾ and writes
/// `nlopt.json` plus the `CALANGO_RESULT nlopt=nlopt.json` marker.
std::string generateNonlinearOpticsScript(const NonlinearOpticsConfig& cfg);

} // namespace calango::core
