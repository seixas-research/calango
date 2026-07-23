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

/// Floating-point precision MACE runs in. float64 matches the training
/// checkpoints exactly and is what ASE's optimizers/vibrations expect for
/// tight force convergence; float32 roughly halves memory and is markedly
/// faster on GPUs, at ~1e-4 eV/Å noise in the forces.
enum class MacePrecision {
    Float64,
    Float32,
};

/// GPAW's three discretizations of the wavefunctions.
enum class GpawMode {
    FiniteDifference, ///< real-space grid — robust, good for molecules/slabs
    PlaneWave,        ///< PW(ecut) — the usual choice for periodic solids
    Lcao,             ///< atomic-orbital basis — fastest, least accurate
};

/// GPAW SCF eigensolvers. Enum order is the combo order in the wizard.
enum class GpawEigensolver {
    Davidson,
    ConjugateGradient,
    RmmDiis,
    Direct,
};

/// GPAW density-mixing scheme. MixerSum/MixerDif are the spin-polarized
/// variants (sum = mix total density, dif = also mix the magnetization).
enum class GpawMixerKind {
    Mixer,
    MixerSum,
    MixerDif,
};

enum class TaskKind {
    SinglePoint,
    GeometryOptimization,
    MolecularDynamics,
};

/// Occupation-number broadening for the electronic states (DFT backends).
/// Enum order is the smearing-method combo order in the Single-point wizard.
enum class SmearingMethod {
    None,             ///< fixed occupations (insulators / molecules)
    Gaussian,
    FermiDirac,
    MethfesselPaxton,
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

/// Plain parameter bag filled in by the simulation wizards and consumed by
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
    /// Use a custom per-component Voigt stress mask instead of the isotropic /
    /// anisotropic presets (e.g. relax only the z axis for 2D materials).
    bool cellCustomMask = false;
    /// Voigt-order mask [xx, yy, zz, yz, xz, xy]: true = relax that component.
    bool cellMask[6] = {true, true, true, true, true, true};

    // Single-point / electronic convergence (DFT backends only; ignored by
    // the classical potentials). scfMaxSteps caps SCF iterations;
    // scfEnergyTolEv is the electronic-energy convergence threshold.
    int scfMaxSteps = 100;
    double scfEnergyTolEv = 1e-4;
    /// Spin polarization: seed every atom with an initial magnetic moment so
    /// the SCF can converge to a magnetic solution.
    bool spinPolarized = false;
    double initialMagMoment = 1.0; ///< initial moment per atom (μB)
    /// Electronic occupation smearing (DFT backends; classical potentials
    /// ignore it). smearingWidthEv is the broadening / electronic temperature.
    SmearingMethod smearing = SmearingMethod::Gaussian;
    double smearingWidthEv = 0.1;

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
    /// Custom checkpoint (.model / .pt). Used when maceSource is CustomFile;
    /// may also name a downloaded foundation checkpoint
    /// (e.g. "mace-off23-small.model") the user wants to pin.
    std::string maceModelPath;
    std::string maceDevice = "cpu";    ///< "cpu" | "cuda" | "mps"
    MacePrecision macePrecision = MacePrecision::Float64;

    // -- GPAW (DFT) ---------------------------------------------------------
    // planeWaveCutoffEv above is the PW() cutoff; gpawGridSpacing is the FD
    // grid spacing (h) and gpawBasis the LCAO basis — GPAW takes exactly one
    // of the three, selected by gpawMode.
    GpawMode gpawMode = GpawMode::PlaneWave;
    double gpawGridSpacing = 0.20;   ///< Å, FD mode
    std::string gpawBasis = "dzp";   ///< LCAO mode
    std::string gpawXc = "PBE";
    GpawEigensolver gpawEigensolver = GpawEigensolver::Davidson;
    GpawMixerKind gpawMixer = GpawMixerKind::Mixer;
    double gpawMixerBeta = 0.05;     ///< linear mixing (damping) parameter
    int gpawMixerNmaxold = 5;        ///< densities kept for Pulay mixing
    double gpawMixerWeight = 50.0;   ///< metric weight for long-wavelength modes
    /// SCF convergence targets. GPAW's convergence dict takes them per
    /// electron / per valence electron; 0 leaves that criterion at GPAW's
    /// default rather than writing an explicit value.
    double gpawConvEigenstates = 4e-8; ///< eV²/electron
    double gpawConvDensity = 1e-4;     ///< electrons/valence electron

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
/// MACE's `default_dtype` keyword ("float64" / "float32").
std::string toString(MacePrecision precision);
/// GPAW's eigensolver keyword ("dav", "cg", "rmm-diis", "direct").
std::string toString(GpawEigensolver solver);
/// GPAW's mixer class name ("Mixer", "MixerSum", "MixerDif").
std::string toString(GpawMixerKind mixer);
/// Human-readable smearing-method name for script comments/labels.
std::string toString(SmearingMethod method);
/// The `ase.optimize` class name for the optimizer (e.g. "BFGS", "LBFGS").
std::string toString(Optimizer optimizer);

} // namespace calango::core
