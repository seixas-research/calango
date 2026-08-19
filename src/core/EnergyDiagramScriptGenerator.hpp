#pragma once

#include <string>

namespace calango::core {

/// Parameters for the Energy Diagrams module (molecules / non-periodic
/// systems): the discrete Kohn-Sham level diagram, plus, optionally,
/// electric-dipole transition moments between the frontier levels. UI-free
/// so the script can also be generated headlessly.
struct EnergyDiagramConfig {
    /// Absolute directory of a completed GPAW single-point that saved its
    /// wavefunctions (`*.gpw`, `mode='all'`) on a NON-PERIODIC (or Gamma-
    /// only) structure. Mandatory, like LdosConfig — this is a
    /// post-process, not a fresh calculation.
    std::string baselineDir;

    /// Compute transition dipole matrix elements between the occupied and
    /// virtual states named below, via gpaw.utilities.dipole (Gamma-only,
    /// same restriction as the parent itself — GPAW's own function asserts
    /// it). When false only the level diagram is written.
    bool computeTransitions = true;

    /// How many occupied states below (and including) the HOMO, and how
    /// many virtual states from the LUMO up, are shown in the LEVEL DIAGRAM
    /// *and* enter the transition-dipole window — one window, both
    /// consumers. A completed SCF routinely converges far more empty bands
    /// than this (GPAW's own `nbands` default, or an explicit request in
    /// the parent Single-Point wizard); those extra, chemically
    /// uninteresting high-lying bands are excluded from the diagram rather
    /// than being drawn alongside the frontier levels, where they would
    /// dominate its shared linear energy axis and squeeze everything near
    /// the gap into an unreadable cluster (the "many empty bands" scenario
    /// in `energy_diagram_benchmark.py` covers this directly, end to end,
    /// against a real GPAW run). The transitions table itself lists only
    /// OCCUPIED -> VIRTUAL pairs inside the window (a one-electron
    /// excitation); occupied-occupied and virtual-virtual entries in the
    /// underlying matrix are computed (GPAW returns the whole block) but
    /// not reported, since neither is a physical transition in this
    /// picture.
    int occupiedBandsBelowHomo = 5;
    int virtualBandsAboveLumo = 5;

    /// Below this oscillator strength a transition is reported "forbidden"
    /// rather than "allowed" — a numerical threshold on the COMPUTED
    /// matrix element, not a symmetry label (see FUTURE.md: point-group
    /// irrep labeling was investigated and deferred).
    double oscillatorStrengthThreshold = 1e-4;

    /// Degenerate levels (within this tolerance, eV) are grouped into one
    /// row with a degeneracy count rather than listed as separate levels
    /// a diagonalization happened to split by a few microvolts.
    double degeneracyToleranceEv = 0.01;

    std::string resultsJson = "energy_diagram.json";
};

/// Turns an EnergyDiagramConfig into a standalone ASE/GPAW script: restarts
/// the baseline (AseScriptGenerator::gpawRestartFromBaselineScript), REFUSES
/// a periodic (more-than-Gamma) baseline with a specific message, reads back
/// EVERY stored state's eigenvalue/occupation to determine HOMO/LUMO (which
/// must never depend on how wide the display window below is), then builds
/// the level diagram and degeneracy groups from only the
/// occupiedBandsBelowHomo/virtualBandsAboveLumo band-index window around
/// that HOMO/LUMO — and, when requested, the transition-dipole matrix
/// (gpaw.utilities.dipole.dipole_matrix_elements_from_calc) over the SAME
/// window, classifying each occupied->virtual pair allowed/forbidden by its
/// oscillator strength. Writes `energy_diagram.json` (`levels`/`groups`
/// windowed; `levels_total` records how many stored bands existed before
/// windowing) and the `CALANGO_RESULT energy_diagram=...` marker.
///
/// These are KOHN-SHAM EIGENVALUE-DIFFERENCE transitions, not TDDFT or BSE
/// excitation energies — the script's own header comment says so, and so
/// does the viewer.
std::string generateEnergyDiagramScript(const EnergyDiagramConfig& config);

} // namespace calango::core
