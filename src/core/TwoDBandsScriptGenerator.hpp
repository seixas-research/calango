#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Which engine samples the plane, and how it gets there. Mirrors
/// core::ElectronicBackend's naming — the "ordinary" 1D band structure's own
/// backend enum — deliberately: this reuses that module's established
/// per-engine chaining pattern (see ElectronicScriptGenerator.cpp) rather
/// than inventing a second one. Every engine samples the SAME explicit
/// fractional k-point grid (`_kpts`, computed once, engine-agnostically);
/// only how each one is HANDED that grid and how the resulting eigenvalues
/// come back differs — see generateTwoDBandsScript()'s per-engine sections.
enum class TwoDBandsBackend {
    Gpaw,
    Espresso,
    Siesta,
    Vasp,
};

/// Parameters for a 2D band-surface calculation: E_n(k_x, k_y) sampled over a
/// grid covering the two-dimensional Brillouin zone.
///
/// Different from the 1D Electronic Structure run in one way that matters:
/// there is no k-path. A 2D material's dispersion IS a surface over the
/// k_x-k_y plane, and a path through it is a set of cuts through that surface —
/// useful, but not the object. Everything else (restart from a converged
/// baseline density where the engine supports one, evaluate non-self-
/// consistently, inherit cutoff/XC/mode) is deliberately the same workflow
/// as the 1D case, per engine.

/// UI-free so the script can also be generated headlessly.
struct TwoDBandsConfig {
    TwoDBandsBackend backend = TwoDBandsBackend::Gpaw;

    /// Engine + backend knobs. Only read by the Gpaw backend: the whole
    /// method rests on `calc.fixed_density()`, which no other backend in
    /// this application exposes the same way.
    CalculatorConfig gpaw;

    /// Plane-wave cutoff (eV), Espresso/Vasp self-contained-SCF path only —
    /// converted to Ry for Espresso. Vasp's baseline-restart path also reads
    /// this, since ICHARG=11 still needs ENCUT set for the NSCF pass.
    double ecutEv = 500.0;
    /// Monkhorst-Pack SCF k-grid (n x n x n), Espresso/Siesta/Vasp
    /// self-contained-SCF path only.
    int scfKpts = 7;

    /// Pseudopotential directory (Preferences → External Files), Espresso
    /// only. Empty leaves the generated script's own placeholder in place —
    /// see SimulationWizardBase::espressoPseudoDirectory() and
    /// AseScriptGenerator.cpp's own emitEspresso(), which this mirrors.
    std::string espressoPseudoDir;
    /// Same, for Siesta — but via `SIESTA_PP_PATH`, not a constructor
    /// keyword, and with a real runtime check when unset rather than a
    /// silent placeholder; see AseScriptGenerator.cpp's own emitSiesta(),
    /// which this mirrors exactly.
    std::string siestaPseudoDir;
    /// The configured VASP POTCAR directory, Vasp only — see
    /// AseScriptGenerator::vaspPotcarResolutionSnippet(), which this feeds.
    /// Added Task 3, 2026-08-22 alongside the same fix in
    /// ElectronicScriptGenerator.cpp (proc_4's own bug): the Vasp branch
    /// used to read no configured path at all, relying purely on whatever
    /// raw VASP_PP_PATH the launcher's environment already carried, which
    /// fails outright for a flat-layout POTCAR library.
    std::string vaspPotcarPath;

    /// Restart point, meaning depends on the engine — see the class docs for
    /// which engines require one and which run self-contained instead:
    ///   Gpaw:  a converged `.gpw`         (REQUIRED)
    ///   Vasp:  a converged `CHGCAR`       (REQUIRED)
    ///   Espresso, Siesta: not used — always self-contained, since neither
    ///     engine's ASE calculator (as this application drives it — see
    ///     ElectronicScriptGenerator's own Espresso/Siesta branches, which
    ///     this mirrors) exposes a single portable restart artifact the way
    ///     a `.gpw` or a `CHGCAR` is one.
    std::string baselineDensityPath;

    /// Samples along each reciprocal-lattice direction: the grid is N×N.
    ///
    /// Cost is quadratic, and this is the knob that decides whether a Dirac
    /// cone reads as a cone or as a staircase. 24 is a reasonable overview;
    /// resolving a band touching wants 48+.
    int gridSamples = 24;

    /// How many bands either side of the Fermi level are exported. Every band
    /// that CROSSES E_F is always kept — those are the ones the plot exists
    /// for, and dropping one because the count ran out would silently remove
    /// the Fermi surface.
    int bandsBelow = 4;
    int bandsAbove = 4;

    /// Total bands the fixed-density run diagonalizes. 0 ⇒ leave the engine's
    /// own choice, which is the occupied set plus a few; raise it when the
    /// conduction bands of interest are not converged. Read by Gpaw
    /// (`nbands`), Vasp (`NBANDS`) and Espresso (`nbnd`) — the three engines
    /// with a direct plane-wave/PAW band count to set. Not read by Siesta:
    /// its finite atomic-orbital basis sets the band count implicitly (one
    /// state per basis orbital), with no separate "how many bands" dial the
    /// 1D Electronic Structure module's own Siesta branch sets either.
    int totalBands = 0;

    /// Re-diagonalize in the spinor basis (gpaw.spinorbit). GPAW ONLY — the
    /// reason a 2D surface plot is often wanted in the first place, since
    /// Rashba splitting and spin-orbit-induced gaps at band touchings are
    /// invisible in a scalar relativistic calculation, but no other engine
    /// this application drives exposes a comparable non-perturbative SOC
    /// re-diagonalization of an already-converged NSCF state. Ignored (no
    /// effect, no error) on the other three backends.
    bool spinOrbit = false;

    /// Also evaluate every band on a Monkhorst-Pack mesh spanning the
    /// primitive 2D reciprocal cell and export it as the JSON's "bz_map"
    /// object — the input of the results window's flat first-Brillouin-zone
    /// map view. GPAW ONLY, for the same reason as `spinOrbit`: it costs a
    /// second full N×N fixed-density pass, which only GPAW's baseline-restart
    /// route makes cheap enough to default sensibly; ignored on the other
    /// three backends. Off by default, and off is a compatibility contract:
    /// without it the generated GPAW script is byte-identical to what this
    /// generator has always produced, so old runs and new ones stay
    /// interchangeable.
    bool bzMap = false;

    /// Samples per direction of that map mesh (N×N). Quadratic cost, same as
    /// gridSamples, and evaluated at fixed density like everything else in
    /// this run. Deliberately a Monkhorst-Pack mesh rather than the inclusive
    /// grid above: it tiles the cell with no duplicated seam, which is what
    /// the viewer's periodic fold into the first Brillouin zone (the
    /// Wigner-Seitz cell, constructed at render time) wants.
    int bzMapSamples = 24;
};

/// Turns a TwoDBandsConfig into a standalone ASE/GPAW script that writes
/// `bands_2d.json` into the job directory and emits the
/// `CALANGO_RESULT bands_2d=bands_2d.json` marker the controller watches for.
///
/// The JSON holds the Cartesian k-grid (Å⁻¹, 2π included) rather than
/// fractional coordinates, because the Brillouin zone of anything but a square
/// lattice is not a square: plotting a hexagonal cell's bands over fractional
/// k shears the Dirac cones into the corners of a rhombus.
///
/// With `bzMap` set the JSON additionally carries a "bz_map" object — the
/// fractional Monkhorst-Pack mesh, every band at every mesh point, the Fermi
/// level and the in-plane reciprocal rows — everything the viewer needs to
/// fold the sampled cell into the first Brillouin zone without re-deriving
/// the cell.
std::string generateTwoDBandsScript(const TwoDBandsConfig& cfg);

} // namespace calango::core
