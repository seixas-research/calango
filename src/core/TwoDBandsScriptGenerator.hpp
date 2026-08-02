#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Parameters for a 2D band-surface calculation: E_n(k_x, k_y) sampled over a
/// grid covering the two-dimensional Brillouin zone.
///
/// Different from the 1D Electronic Structure run in one way that matters:
/// there is no k-path. A 2D material's dispersion IS a surface over the
/// k_x-k_y plane, and a path through it is a set of cuts through that surface —
/// useful, but not the object. Everything else (restart from a converged
/// baseline density, evaluate non-self-consistently, inherit cutoff/XC/mode)
/// is deliberately the same workflow.
///
/// UI-free so the script can also be generated headlessly.
struct TwoDBandsConfig {
    /// Engine + backend knobs. Only the GPAW fields are read: the whole method
    /// rests on `calc.fixed_density()`, which no other backend in this
    /// application exposes.
    CalculatorConfig gpaw;

    /// Absolute path to the converged `.gpw` this run restarts from. Required —
    /// the wizard refuses to open without a completed single point, for the
    /// same reason the Electronic Structure wizard does.
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

    /// Total bands the fixed-density run diagonalizes. 0 ⇒ leave GPAW's own
    /// choice, which is the occupied set plus a few; raise it when the
    /// conduction bands of interest are not converged.
    int totalBands = 0;

    /// Re-diagonalize in the spinor basis (gpaw.spinorbit). The reason a 2D
    /// surface plot is often wanted in the first place: Rashba splitting and
    /// spin-orbit-induced gaps at band touchings are invisible in a scalar
    /// relativistic calculation.
    bool spinOrbit = false;

    /// Also evaluate every band on a Monkhorst-Pack mesh spanning the
    /// primitive 2D reciprocal cell and export it as the JSON's "bz_map"
    /// object — the input of the results window's flat first-Brillouin-zone
    /// map view. Off by default, and off is a compatibility contract: without
    /// it the generated script is byte-identical to what this generator has
    /// always produced, so old runs and new ones stay interchangeable.
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
