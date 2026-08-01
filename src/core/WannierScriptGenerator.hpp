#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Parameters for the Wannier Functions post-process,
/// filled in by the Wannier wizard and consumed by generateWannierScript().
/// UI-free so the script can also be generated headlessly.
struct WannierConfig {
    /// Engine + backend knobs (cutoff / k-points / GPAW discretization) chosen
    /// in the wizard's Calculator Settings stage. `calculator.calculator`
    /// selects which SCF engine builds the Bloch wavefunctions; the
    /// Marzari-Vanderbilt localization runs through ASE's `ase.dft.wannier`.
    CalculatorConfig calculator;

    /// Absolute directory of a completed single-point that already holds GPAW
    /// wavefunctions (`*.gpw`). When set the localization restarts GPAW from
    /// that directory instead of running a fresh SCF. Empty ⇒ fresh SCF.
    std::string baselineDir;

    /// Number of Wannier functions to localize (≈ occupied bands / valence
    /// orbitals).
    int nWannier = 4;

    /// Trial-orbital initializer passed to ASE's `initialwannier` argument.
    /// The atomic sets (s, p, d, sp3, …) collapse to "orbitals"; "bloch" and
    /// "random" pass straight through.
    std::string initialWannier = "orbitals";

    /// How the FIXED (frozen) part of the Hilbert space is chosen — the states
    /// that are reproduced exactly, as opposed to the extra degrees of freedom
    /// that are allowed to mix. ASE takes this as `fixedenergy` OR
    /// `fixedstates`, and raises `RuntimeError` if given both, so this is one
    /// choice rather than two independent settings.
    enum class FixedStatesMode {
        /// Neither is passed: ASE fixes exactly `nWannier` states per k-point
        /// and there are no extra degrees of freedom.
        FromWannierCount,
        /// `fixedenergy=…` — every state below an energy cutoff is fixed.
        EnergyWindow,
        /// `fixedstates=…` — an explicit band count per k-point.
        BandCount,
    };
    FixedStatesMode fixedMode = FixedStatesMode::FromWannierCount;

    /// Cutoff for FixedStatesMode::EnergyWindow, in eV.
    ///
    /// NOT simply "above E_F". ASE's reference level is the CONDUCTION BAND
    /// MINIMUM whenever the system has a finite gap (> 0.01 eV) and this value
    /// is >= 0.01 eV; only for a metal — or for a cutoff below 0.01 eV — is it
    /// the Fermi level. See `choose_states()` in ase/dft/wannier.py:
    ///
    ///     if calcdata.gap < 0.01 or fixedenergy < 0.01:
    ///         cutoff = fixedenergy + calcdata.fermi_level
    ///     else:
    ///         cutoff = fixedenergy + calcdata.lumo
    ///
    /// The UI must say so: a user who reads "above E_F" and types 2.0 for
    /// silicon gets CBM + 2 eV, roughly a gap higher than they asked for, with
    /// nothing in the output to reveal it.
    double energyWindowEv = 0.0;

    /// Band count for FixedStatesMode::BandCount → ASE's `fixedstates`. The
    /// same number is used at every k-point (ASE also accepts a per-k list;
    /// there is no sensible UI for that and a uniform count is what a fixed
    /// occupied manifold means).
    int fixedStates = 0;

    /// Maximum Marzari-Vanderbilt minimization iterations (the localize() loop
    /// cap). The loop still exits early once the spread functional converges.
    int maxIterations = 50;
};

/// Turns a WannierConfig into a standalone ASE/GPAW script that writes
/// `wannier.json` (+ per-orbital `wannier_<n>.cube`) into the job directory and
/// emits the `CALANGO_RESULT wannier=wannier.json` marker the controller
/// watches for.
std::string generateWannierScript(const WannierConfig& cfg);


/// Configuration for the interactive Wannier-interpolation dialog: a band path
/// (E_n(k)) plus a dense k-mesh (PDOS), and the frozen/disentanglement energy
/// windows. UI-free so the script can be generated headlessly.
struct WannierInterpolationConfig {
    /// ASE band-path string ("GXWK,UX"); empty ⇒ ASE's suggested path.
    std::string kpath;
    int bandPoints = 200;      ///< samples along the band path
    int kmesh[3] = {8, 8, 8};  ///< Monkhorst-Pack grid for the PDOS
    double pdosWidth = 0.1;     ///< Gaussian broadening of the PDOS (eV)

    /// INNER (frozen) window → ASE's `Wannier(fixedenergy=…)`: states below the
    /// cutoff are reproduced exactly by the Wannier manifold.
    ///
    /// Same reference-level caveat as WannierConfig::energyWindowEv — the CBM
    /// for a gapped system, the Fermi level for a metal.
    ///
    /// This used to be TWO controls, "frozen window" and "inner window", which
    /// were the same ASE parameter: the first was passed and the second was
    /// written into the script as a comment. One control, one parameter.
    bool useInnerWindow = false;
    double innerWindowEv = 0.0;

    /// OUTER (disentanglement) window → ASE's `Wannier(nbands=…)`: the Bloch
    /// states the manifold may be drawn from at all. The script counts the
    /// bands lying below this cutoff and passes that count, because `nbands` is
    /// exactly "bands to include in localization" — truncating from the top is
    /// what an outer window does.
    ///
    /// It used to be emitted as a comment and nothing else, so the control
    /// changed no number in the calculation. A window that silently does
    /// nothing is worse than no window at all: it reads as a converged result
    /// obtained under a constraint that was never applied.
    bool useOuterWindow = false;
    double outerWindowEv = 5.0;
};

/// ASE/GPAW script for Wannier-interpolated electronic properties: it restarts
/// from the MLWF run's `*.gpw` in `mlwfDir`, rebuilds the localization (with the
/// requested frozen window), interpolates the band structure H(R)→H(k) along
/// `cfg.kpath` into `bands.json`, and builds a Wannier-projected PDOS on the
/// `cfg.kmesh` into `pdos.json` — both in the schema the standard band/PDOS
/// viewer reads.
std::string generateWannierInterpolationScript(
    const std::string& mlwfDir, const WannierInterpolationConfig& cfg);

} // namespace calango::core
