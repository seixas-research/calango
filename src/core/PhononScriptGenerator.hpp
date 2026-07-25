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

    int bandPathPoints = 100; ///< k-points along the path
    /// High-symmetry q-path string ("GXMG", …); empty lets ASE suggest the
    /// path for the structure's Bravais lattice (same convention as the
    /// electronic band path).
    std::string kpath;
    int dosKptGrid = 20;      ///< Monkhorst-Pack n×n×n grid for the DOS (mesh)
    /// Gaussian broadening applied when sampling the phonon DOS onto its
    /// frequency grid (cm⁻¹). Too small and the DOS is a comb of delta
    /// spikes from the finite k-mesh; too large and real van Hove features
    /// wash out. Scale it with the mesh: a denser kgrid supports a sharper
    /// sigma.
    double dosWidthCm = 2.0;

    /// Enforce the acoustic sum rule when reading the force constants, so the
    /// three acoustic branches vanish at Γ (ase.phonons Phonons.read(acoustic=…)).
    bool acousticSumRule = true;

    /// Use the crystal's space-group symmetry to cut the finite-displacement
    /// set from the naive 6N (±δ along x/y/z for every atom) down to the
    /// symmetry-irreducible displacements, reconstructing the full force
    /// constant matrix from them. Uses spglib (through phonopy) to find the
    /// site-symmetry irreps; falls back to the plain ASE 6N path when phonopy
    /// is unavailable, so the script always runs.
    bool symmetryReducedDisplacements = false;

    /// Compute forces on the UN-displaced geometry and subtract them from
    /// every displaced configuration. A relaxation stops at a finite fmax, so
    /// the reference geometry carries small residual forces; leaving them in
    /// contaminates the force constants and shows up as spurious non-zero
    /// acoustic frequencies at Γ. Costs one extra force evaluation.
    bool removeResidualForces = false;
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
