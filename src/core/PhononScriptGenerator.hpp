#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Parameters of a finite-displacement vibrational calculation. Filled by
/// PhononBuilderDialog and consumed by PhononScriptGenerator; UI-free so
/// scripts can also be generated headlessly.
struct PhononConfig {
    /// Only the calculator-selection fields are consulted (calculator kind
    /// plus the MACE model/device knobs) — the task fields are ignored.
    CalculatorConfig calculator;

    /// Whether the structure is periodic: periodic systems run through
    /// ase.phonons.Phonons (supercell force constants → dispersion + DOS),
    /// isolated molecules through ase.vibrations.Vibrations (Γ-point
    /// normal modes only).
    bool periodic = true;

    int supercell[3] = {2, 2, 2}; ///< finite-displacement supercell
    double deltaAngstrom = 0.01;  ///< ± displacement per atom and direction

    int bandPathPoints = 100; ///< k-points along the ASE-suggested path
    int dosKptGrid = 20;      ///< Monkhorst-Pack n×n×n grid for the DOS
};

/// Generates the standalone ASE Python script for a PhononConfig, in the
/// same spirit as AseScriptGenerator: finite displacements (±δ along x, y,
/// z for every atom — 6N + 1 force evaluations, no symmetry reduction),
/// force constants, dynamical matrix, and vibrational frequencies.
///
/// Periodic systems write phonon_bands.csv (dispersion along the
/// ASE-suggested Brillouin-zone path) and phonon_dos.csv; molecules write
/// vibrations.txt and per-mode animation trajectories (vib.<n>.traj) that
/// Calango can open directly. Γ-point / normal-mode frequencies are also
/// emitted as CALANGO_RESULT lines for the job console.
class PhononScriptGenerator {
public:
    static std::string generate(const PhononConfig& config,
                                const std::string& structureFile);
};

} // namespace calango::core
