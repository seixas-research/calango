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

} // namespace calango::core
