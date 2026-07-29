#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Plane-wave cutoff convergence: the same single-point calculation repeated
/// over an ascending list of PW cutoffs, on the same geometry.
///
/// The question this answers is "which cutoff is enough?", and the only honest
/// yardstick is the best calculation in the set: the run at the HIGHEST cutoff
/// is taken as the reference, and every other point is recorded as its
/// distance from that reference — in total energy per atom and in the maximum
/// force magnitude, the two quantities whose convergence a production run
/// actually depends on. Absolute totals are meaningless here (the PAW total
/// energy has an arbitrary zero); the *differences* are the deliverable.
struct CutoffConvergenceRunConfig {
    /// Structure the sweep evaluates (staged next to run.py).
    std::string structureFile = "structure.extxyz";
    /// Per-cutoff records plus the reference summary.
    std::string resultsJson = "cutoff_convergence.json";

    /// The cutoffs to evaluate, in eV, ascending. The last entry is the
    /// reference. The generator sorts and de-duplicates defensively — an
    /// unsorted list would silently pick a mid-quality run as the yardstick.
    std::vector<double> cutoffsEv;

    /// The engine and its settings. `task` is ignored (always a single point
    /// per cutoff) and `gpawMode` is forced to PlaneWave — a cutoff sweep in
    /// FD or LCAO mode would vary nothing.
    CalculatorConfig calculator;

    /// Carry on past a cutoff whose SCF raised, recording it as failed. One
    /// diverging low-cutoff point should not lose the rest of the curve.
    bool continueOnFailure = true;
};

class CutoffConvergenceScriptGenerator {
public:
    /// Full ASE/GPAW script: reads the structure, runs one fixed-geometry SCF
    /// per cutoff (fresh calculator each time), and writes the results JSON
    /// with the highest-cutoff run as the convergence reference.
    static std::string generate(const CutoffConvergenceRunConfig& config);
};

} // namespace calango::core
