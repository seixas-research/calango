#pragma once

#include <string>

namespace calango::core {

/// ASE calculators exposed in the GUI. EMT and Lennard-Jones ship with ASE
/// and run out of the box; the DFT entries generate script hooks that the
/// user completes (pseudopotentials, parallel launch command, ...); MACE
/// requires `pip install mace-torch` in the job environment.
enum class CalculatorKind {
    EMT,
    LennardJones,
    QuantumEspresso,
    Vasp,
    Mace,
};

/// Which MACE model the calculator loads. Foundation models are fetched
/// automatically by mace-torch on first use (cached in ~/.cache/mace);
/// CustomFile points at a user-trained checkpoint (.model / .pt).
enum class MaceModelSource {
    FoundationMP,  ///< MACE-MP-0 family (materials, periodic systems)
    FoundationOFF, ///< MACE-OFF family (organic molecules)
    CustomFile,
};

enum class TaskKind {
    SinglePoint,
    GeometryOptimization,
    MolecularDynamics,
};

enum class MdEnsemble {
    LangevinNVT,
    VelocityVerletNVE,
    // NPT (Parrinello-Rahman / Nose-Hoover) planned — see ROADMAP.md Phase 4.
};

/// Plain parameter bag filled in by CalculatorDialog and consumed by
/// AseScriptGenerator. Deliberately UI-free so scripts can also be
/// generated headlessly (e.g. future batch/CLI mode).
struct CalculatorConfig {
    CalculatorKind calculator = CalculatorKind::EMT;
    TaskKind task = TaskKind::SinglePoint;

    // Geometry optimization
    double fmax = 0.05;      ///< eV/Å convergence criterion
    int maxSteps = 200;

    // Molecular dynamics
    MdEnsemble ensemble = MdEnsemble::LangevinNVT;
    double temperatureK = 300.0;
    double timestepFs = 1.0;
    int mdSteps = 1000;

    // DFT common knobs (used by the QE/VASP templates)
    double planeWaveCutoffEv = 550.0;
    int kpts[3] = {4, 4, 4};

    // MACE machine-learning potential
    MaceModelSource maceSource = MaceModelSource::FoundationMP;
    std::string maceSize = "medium";   ///< "small" | "medium" | "large"
    std::string maceModelPath;         ///< custom checkpoint (CustomFile)
    std::string maceDevice = "cpu";    ///< "cpu" | "cuda" | "mps"
};

std::string toString(CalculatorKind kind);
std::string toString(TaskKind kind);

} // namespace calango::core
