#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Parameters for the Maximally Localized Wannier Functions (MLWF) post-process,
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

    /// Disentanglement energy window: when true, states up to `energyWindowEv`
    /// (in eV, relative to the Fermi level) are kept fixed in the localization,
    /// which is ASE's `Wannier(fixedenergy=…)`. This is the outer/disentangle
    /// bound separating the frozen occupied manifold from the states that are
    /// allowed to mix. When false ASE picks the window from `nWannier`.
    bool useEnergyWindow = false;
    double energyWindowEv = 0.0;

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

    /// Frozen energy window → ASE's Wannier(fixedenergy=…): states up to this
    /// energy (eV, relative to E_F) stay frozen in the localization.
    bool useFrozenWindow = false;
    double frozenEnergyEv = 0.0;

    /// Inner / outer disentanglement windows (eV, relative to E_F). ASE's
    /// Wannier has only a limited disentanglement, so these are recorded in the
    /// script header and bound the energy range shown; full Wannier90-style
    /// disentanglement is out of scope.
    bool useDisentangle = false;
    double innerWindowEv = 0.0; ///< inner (frozen) window upper bound
    double outerWindowEv = 5.0; ///< outer (disentanglement) window upper bound
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
