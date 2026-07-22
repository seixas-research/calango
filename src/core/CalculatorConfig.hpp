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
    Gpaw,
    Siesta,
    Orca, ///< quantum chemistry (requires the ORCA binary)
    Asap, ///< asap3 fast C++ EMT / OpenKIM (requires the asap3 package)
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

/// Local optimizers ASE ships for structural relaxation. Enum order is the
/// optimizer combo order in the Geometry Optimization dialog; the value maps
/// directly to the `ase.optimize` class name via toString(Optimizer).
enum class Optimizer {
    BFGS,   ///< quasi-Newton, robust general default
    LBFGS,  ///< limited-memory BFGS, cheaper for large systems
    FIRE,   ///< fast inertial relaxation engine (MD-like, no Hessian)
    GPMin,  ///< Gaussian-process minimizer (few, expensive steps)
    MDMin,  ///< velocity-quench molecular-dynamics minimizer
};

/// Cell filter used for variable-cell (stress) relaxation.
enum class CellFilter {
    FrechetCell, ///< ase.filters.FrechetCellFilter (recommended, well-behaved)
    UnitCell,    ///< ase.filters.UnitCellFilter (classic)
};

/// The full set of MD integrators/thermostats ASE ships. Enum order is
/// the ensemble combo order in the calculator dialog.
enum class MdEnsemble {
    VelocityVerletNVE,   ///< microcanonical
    LangevinNVT,
    AndersenNVT,
    BerendsenNVT,
    NoseHooverChainNVT,
    BerendsenNPT,
    MelchionnaNPT,       ///< ase.md.npt.NPT (Nosé-Hoover + Parrinello-Rahman)
};

/// True for every thermostatted ensemble (all but NVE) — drives the
/// CALANGO_TARGET_TEMP marker and the Temperature-tab reference line.
constexpr bool isConstantTemperature(MdEnsemble ensemble)
{
    return ensemble != MdEnsemble::VelocityVerletNVE;
}

/// True for every barostatted ensemble — drives the CALANGO_PRESSURE /
/// CALANGO_TARGET_PRESSURE markers and the Pressure-tab reference line
/// (constant-volume ensembles never report a pressure series).
constexpr bool isConstantPressure(MdEnsemble ensemble)
{
    return ensemble == MdEnsemble::BerendsenNPT
        || ensemble == MdEnsemble::MelchionnaNPT;
}

/// Plain parameter bag filled in by CalculatorDialog and consumed by
/// AseScriptGenerator. Deliberately UI-free so scripts can also be
/// generated headlessly (e.g. future batch/CLI mode).
struct CalculatorConfig {
    CalculatorKind calculator = CalculatorKind::EMT;
    TaskKind task = TaskKind::SinglePoint;

    // Geometry optimization
    Optimizer optimizer = Optimizer::BFGS;
    double fmax = 0.05;      ///< eV/Å convergence criterion
    int maxSteps = 200;
    /// Variable-cell relaxation: also relax the unit cell via a cell filter.
    bool relaxCell = false;
    CellFilter cellFilter = CellFilter::FrechetCell;
    /// Constrain the cell strain to hydrostatic (isotropic) rather than the
    /// full anisotropic stress relaxation.
    bool cellHydrostatic = false;

    // Single-point / electronic convergence (DFT backends only; ignored by
    // the classical potentials). scfMaxSteps caps SCF iterations;
    // scfEnergyTolEv is the electronic-energy convergence threshold.
    int scfMaxSteps = 100;
    double scfEnergyTolEv = 1e-4;

    // Molecular dynamics
    MdEnsemble ensemble = MdEnsemble::LangevinNVT;
    double temperatureK = 300.0;
    double timestepFs = 1.0;
    int mdSteps = 1000;
    double frictionPerFs = 0.01; ///< Langevin friction (fs⁻¹)
    double andersenProb = 0.05;  ///< Andersen collision probability
    double tautFs = 100.0;       ///< thermostat coupling time (Berendsen/NHC)
    double taupFs = 1000.0;      ///< barostat coupling time (NPT)
    double pressureGPa = 0.0;    ///< external pressure (NPT; 0 ≈ ambient)
    /// Trajectory / metric sampling frequency (record every N MD steps).
    /// 0 means auto (~400 streamed frames over the whole run).
    int mdSampleInterval = 0;

    // DFT common knobs (used by the QE/VASP templates)
    double planeWaveCutoffEv = 550.0;
    int kpts[3] = {4, 4, 4};

    // MACE machine-learning potential
    MaceModelSource maceSource = MaceModelSource::FoundationMP;
    std::string maceSize = "medium";   ///< "small" | "medium" | "large"
    std::string maceModelPath;         ///< custom checkpoint (CustomFile)
    std::string maceDevice = "cpu";    ///< "cpu" | "cuda" | "mps"

    // -- ORCA (quantum chemistry) ------------------------------------------
    std::string orcaMethod = "B3LYP";   ///< functional / method keyword
    std::string orcaBasis = "def2-SVP"; ///< basis set keyword
    int charge = 0;
    int multiplicity = 1;               ///< 2S+1
    /// "" (gas phase), "CPCM" or "SMD" — with `orcaSolvent` naming the
    /// solvent (water, acetonitrile, ...).
    std::string orcaSolvationModel;
    std::string orcaSolvent = "water";
};

std::string toString(CalculatorKind kind);
std::string toString(TaskKind kind);
/// The `ase.optimize` class name for the optimizer (e.g. "BFGS", "LBFGS").
std::string toString(Optimizer optimizer);

} // namespace calango::core
