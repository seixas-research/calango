#pragma once

#include <string>

namespace calango::core {

/// Parameters for the 2D work-function workflow: Φ = E_vac − E_F, with E_vac
/// the plateau of the planar-averaged electrostatic potential in the vacuum
/// and E_F the Fermi level — both read off a completed ground state.
///
/// Deliberately no CalculatorConfig here: the run is a pure post-process of
/// an inherited GPAW ground state. Every SCF parameter (mode, cutoff, xc,
/// k-grid, smearing) comes back from the .gpw on restart, and a config field
/// re-declaring any of them would let the wizard silently disagree with the
/// baseline it claims to read.
struct WorkfunctionConfig {
    /// ABSOLUTE path to the baseline GPAW restart file (`.gpw`, written with
    /// mode="all") from a completed Single-Point Calculation. Mandatory: the
    /// work function is a property of a specific converged SCF solution —
    /// its electrostatic potential and its Fermi level — so this run loads
    /// that state and computes nothing self-consistently.
    std::string baselineDensityPath;
    /// The cell axis that carries the vacuum (0=x, 1=y, 2=z). The potential
    /// is planar-averaged over the OTHER two axes, so getting this wrong
    /// averages across the vacuum instead of the sheet and reads a "vacuum
    /// level" from inside the material — which is why the wizard asks rather
    /// than guesses (the geometry heuristic only seeds the combo).
    int vacuumAxis = 2;
    /// Fraction of the vacuum gap, measured inward from each cell edge, over
    /// which the plateau flatness |dV̄/dz| is evaluated. The vacuum level is
    /// only meaningful where V̄(z) is flat; a slope over this window means
    /// the gap is too thin for the potential to reach its asymptote, and the
    /// script reports the worst slope so the viewer can say so.
    double plateauFraction = 0.15;
};

/// Standalone run.py: loads the baseline ground state, planar-averages the
/// electrostatic potential over the two in-plane axes, and reports the work
/// function of BOTH slab faces — Φ = E_vac − E_F with E_vac read at each cell
/// edge — plus the plateau-flatness diagnostic. Writes `workfunction.json`
/// for the WorkfunctionWindow to read back.
///
/// Two faces, always: with a dipole correction in the baseline an asymmetric
/// slab genuinely has two vacuum levels, one per face. Without one, periodic
/// boundary conditions force a single continuous potential across the gap,
/// so the two edges share one ARTIFICIAL average — the script still reports
/// both numbers so that their equality is visible for what it is (a missing
/// correction, not a symmetric slab).
std::string generateWorkfunctionScript(const WorkfunctionConfig& cfg);

} // namespace calango::core
