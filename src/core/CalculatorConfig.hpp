#pragma once

#include <string>
#include <vector>

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
    // -- Machine-learning interatomic potentials (append only) --------------
    // Each needs its own package in the job environment; the generated script
    // names it in a comment. Grouped after the classical/DFT engines so the
    // existing enum values keep the meaning they have in saved projects.
    DeepMd,    ///< DeepMD-kit — frozen .pb graph
    NequIp,    ///< NequIP — deployed TorchScript .pth
    Allegro,   ///< Allegro — NequIP's strictly-local architecture
    ChgNet,    ///< CHGNet — pretrained universal potential with magmoms
    MatterSim, ///< MatterSim — Microsoft's universal potential (M3GNet-family)
    FairChem,  ///< FAIRChem / OCP — EquiformerV2, eSCN checkpoints
    /// LAMMPS — the classical molecular-dynamics engine, driven through ASE.
    ///
    /// Appended last deliberately: the enum is serialized into saved projects
    /// by VALUE, so inserting it next to the other classical potentials would
    /// silently reinterpret every stored calculator after it.
    ///
    /// Unlike every other entry here, LAMMPS brings its own force field rather
    /// than being one: what it computes is decided entirely by the pair style
    /// and coefficients, which is why it needs its own settings group instead
    /// of a single constructor line.
    Lammps,
};

/// How the LAMMPS calculator talks to LAMMPS. ASE ships two interfaces, and
/// which one works depends on how LAMMPS was installed — so this is a real
/// choice the user has to make, not an implementation detail.
enum class LammpsInterface {
    /// `ase.calculators.lammpslib.LAMMPSlib` — in-process, through the LAMMPS
    /// Python module. No file I/O per step, so it is the right choice for MD
    /// and relaxation; needs LAMMPS built with `-DBUILD_SHARED_LIBS` and its
    /// Python package installed (conda-forge's `lammps` provides both).
    Library,
    /// `ase.calculators.lammpsrun.LAMMPS` — spawns the `lmp` binary per
    /// evaluation, exchanging data files. Works with ANY LAMMPS build,
    /// including a plain distro package, at the cost of process startup and
    /// file I/O on every force call.
    Run,
};

/// True for the machine-learning interatomic potentials — the engines that
/// take a model/checkpoint rather than a basis set and k-points. Drives which
/// wizard groups are shown and which script block is emitted.
constexpr bool isMlipCalculator(CalculatorKind kind)
{
    return kind == CalculatorKind::Mace || kind == CalculatorKind::DeepMd
        || kind == CalculatorKind::NequIp || kind == CalculatorKind::Allegro
        || kind == CalculatorKind::ChgNet || kind == CalculatorKind::MatterSim
        || kind == CalculatorKind::FairChem;
}

/// Compute device shared by every MLIP backend. Enum order is the combo order.
enum class MlipDevice {
    Cpu,
    Cuda, ///< NVIDIA GPU (needs a CUDA build of the framework)
    Mps,  ///< Apple-silicon GPU (no float64 kernels in PyTorch)
};

/// CHGNet pretrained weight sets. CHGNet ships versioned checkpoints; pinning
/// one keeps a run reproducible when the package is updated.
enum class ChgNetWeights {
    V0_3_0, ///< "0.3.0" — the widely-cited published checkpoint
    Latest, ///< whatever the installed chgnet release considers current
};

/// MatterSim model size. The 5M-parameter model is the fast default; 1M trades
/// accuracy for speed on large cells.
enum class MatterSimModel {
    M3,   ///< MatterSim-v1.0.0-1M
    M100, ///< MatterSim-v1.0.0-5M (the larger, more accurate release)
};

/// FAIRChem / OCP architectures. The checkpoint must match the architecture.
enum class FairChemModel {
    EquiformerV2,
    EScn,
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

/// VASP PREC. Enum order is the combo order in the VASP settings group.
enum class VaspPrecision { Normal, Accurate, Single };

/// VASP ALGO — the electronic minimization algorithm. Enum order is the combo
/// order.
enum class VaspAlgo { Normal, Fast, VeryFast, All, Damped };

/// Spin treatment for the DFT SCF. Enum order is the "Spin Configurations"
/// dropdown order in the Single-point wizard.
enum class SpinMode {
    Unpolarized,   ///< spin-restricted (no spin degree of freedom)
    Collinear,     ///< spin-polarized, ↑/↓ densities (scalar magmoms)
    NonCollinear,  ///< spinor / non-collinear magnetism (vector magmoms)
};

/// Which density GPAW writes when exporting a `.cube` (charge-density export).
enum class GpawDensityType {
    Pseudo,       ///< calc.get_pseudo_density() — smooth valence pseudodensity
    AllElectron,  ///< calc.get_all_electron_density() — full nuclear-cusp density
};

/// The volumetric fields a GPAW run can write out after the SCF, each to its
/// own `.cube`. Independent flags rather than one choice: they cost one grid
/// evaluation each on an already-converged calculation, and comparing (say) the
/// ELF against the density that produced it means having both.
struct GpawDensityExports {
    bool allElectron = false;   ///< get_all_electron_density(gridrefinement=2)
    bool pseudo = false;        ///< get_pseudo_density()
    /// n(up) - n(down). Zero by construction in a spin-restricted run, so the
    /// generated script skips it with a note rather than writing a null field.
    bool spin = false;
    bool hartree = false;       ///< get_electrostatic_potential(), eV
    bool elf = false;           ///< gpaw.elf.elf_from_dft_calculation
    /// tau(r), the positive-definite kinetic-energy density the ELF is built
    /// from. Read off the density object after update_ked().
    bool kineticEnergy = false;

    bool any() const
    {
        return allElectron || pseudo || spin || hartree || elf || kineticEnergy;
    }
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
/// One DFT+U correction: a Hubbard U applied to a named orbital shell of a
/// chemical element. GPAW spells this as a `setups` entry, e.g.
/// `setups={"Fe": ":d,3.5"}` — the leading colon keeps the default PAW dataset
/// and appends the correction to it.
struct HubbardU {
    std::string element;          ///< chemical symbol, e.g. "Fe"
    std::string orbital = "d";    ///< shell the correction acts on: s/p/d/f
    double u = 0.0;               ///< U value, eV
    /// Scale the correction by the number of electrons in the shell. GPAW's
    /// third, optional field; off by default, which is the usual convention.
    bool scale = false;
};

/// One "hold these degrees of freedom still" rule for a relaxation, emitted as
/// an ASE constraint object.
///
/// Two orthogonal choices, which is why this is one struct rather than three:
/// WHICH atoms (an explicit index list, or everything inside a slab of space
/// along one Cartesian axis — the classic "freeze the bottom two layers,
/// z < 5 Å"), and WHICH directions of those atoms are frozen. Freezing all
/// three directions is `FixAtoms(indices=…)`; a partial mask is
/// `FixCartesian(indices, mask=…)`, where a true entry means that coordinate is
/// held (ASE's own convention).
///
/// A region rule keeps its BOUNDS rather than the indices they select, so the
/// generated script re-evaluates the selection against the geometry it actually
/// reads. That matters because a relaxation is often re-run on a slightly
/// different cell, where a frozen index list silently freezes the wrong atoms.
struct GeometryConstraint {
    enum class Selection {
        Indices, ///< the explicit `indices` list
        Region,  ///< every atom whose `axis` coordinate is within the bounds
    };
    Selection selection = Selection::Indices;
    /// 0-based atom indices (Selection::Indices only).
    std::vector<int> indices;
    /// Cartesian axis the region bounds apply to: 0 = x, 1 = y, 2 = z.
    int axis = 2;
    /// Half-open bounds in Å; each side is optional, so "z > 5" needs no upper
    /// limit. With neither set, a region rule selects every atom.
    bool hasMin = false;
    double minValue = 0.0;
    bool hasMax = false;
    double maxValue = 0.0;
    /// Which Cartesian directions are frozen: {x, y, z}. All three (the
    /// default) is a plain fixed atom.
    bool fix[3] = {true, true, true};

    bool fixesAllDirections() const { return fix[0] && fix[1] && fix[2]; }
    bool fixesAnyDirection() const { return fix[0] || fix[1] || fix[2]; }
};

struct CalculatorConfig {
    CalculatorKind calculator = CalculatorKind::EMT;
    TaskKind task = TaskKind::SinglePoint;

    /// Frozen-degree-of-freedom rules applied to `atoms` before the optimizer
    /// runs (Geometry Optimization). Empty means a fully free relaxation.
    std::vector<GeometryConstraint> constraints;

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
    /// Runaway guard on the SCF, not a target — the convergence thresholds are
    /// what normally end the cycle, so a cap that stops a converging run costs
    /// the whole job and saves nothing. 500 matches the wizard default and is
    /// enough for the magnetic and metallic systems that routinely need a few
    /// hundred iterations.
    int scfMaxSteps = 500;
    double scfEnergyTolEv = 1e-4;
    /// Spin treatment (unpolarized / collinear / non-collinear). `spinPolarized`
    /// is kept in sync (true for collinear + non-collinear) for the many callers
    /// that only need the boolean; `spinMode` carries the finer distinction.
    SpinMode spinMode = SpinMode::Unpolarized;
    /// Spin polarization: seed every atom with an initial magnetic moment so
    /// the SCF can converge to a magnetic solution.
    bool spinPolarized = false;
    /// Uniform fallback moment per atom (μB), used ONLY when the structure
    /// itself carries no initial moments.
    ///
    /// The per-atom moments are a property of the structure — they are set in
    /// Edit Structure, ride on the staged geometry's `initial_magmoms` column,
    /// and arrive on the ASE atoms object before the calculator is built. This
    /// is the seed for the case where nobody set any, because a spin-polarized
    /// run starting from all zeros converges straight back to the non-magnetic
    /// solution.
    double initialMagMoment = 1.0;
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
    double planeWaveCutoffEv = 500.0;
    int kpts[3] = {7, 7, 7};

    // MACE machine-learning potential
    MaceModelSource maceSource = MaceModelSource::FoundationMP;
    std::string maceSize = "medium";   ///< "small" | "medium" | "large"
    /// Custom checkpoint (.model / .pt). Used when maceSource is CustomFile;
    /// may also name a downloaded foundation checkpoint
    /// (e.g. "mace-off23-small.model") the user wants to pin.
    std::string maceModelPath;
    std::string maceDevice = "cpu";    ///< "cpu" | "cuda" | "mps"
    MacePrecision macePrecision = MacePrecision::Float64;

    // -- Machine-learning interatomic potentials (DeepMD … FAIRChem) --------
    // One shared device selector: every backend below is a PyTorch/TF model
    // that runs on the same hardware, and duplicating the control per engine
    // would let two of them disagree about which GPU a job uses.
    MlipDevice mlipDevice = MlipDevice::Cpu;

    /// DeepMD-kit: frozen graph (`.pb`, or a `.pth` for the PyTorch backend).
    std::string deepmdModelPath;
    /// NequIP / Allegro: *deployed* TorchScript model (`.pth`), i.e. the
    /// output of `nequip-deploy build`, not a training checkpoint.
    std::string nequipModelPath;
    /// Unit names the deployed NequIP/Allegro model was trained in. ASE works
    /// in eV and Å, so the calculator rescales by these — a model trained in
    /// kcal/mol silently reports wrong energies if they are left at eV.
    std::string nequipEnergyUnits = "eV";
    std::string nequipLengthUnits = "Angstrom";

    ChgNetWeights chgnetWeights = ChgNetWeights::V0_3_0;
    /// Ask CHGNet for the stress tensor as well as energy/forces (needed for
    /// variable-cell relaxation; costs an extra head evaluation).
    bool chgnetStress = true;

    MatterSimModel matterSimModel = MatterSimModel::M3;
    /// Thermodynamic state passed to MatterSim's finite-temperature head.
    /// Ignored unless `matterSimThermal` is set.
    bool matterSimThermal = false;
    double matterSimTemperatureK = 300.0;
    double matterSimPressureGPa = 0.0;

    FairChemModel fairChemModel = FairChemModel::EquiformerV2;
    /// FAIRChem checkpoint (`.pt`). Required — FAIRChem has no default model.
    std::string fairChemCheckpointPath;

    // -- VASP (DFT) ---------------------------------------------------------

    /// Directory holding the PAW pseudopotential sets, i.e. `VASP_PP_PATH`.
    ///
    /// Not a hard-coded path and not derived from the environment: VASP's
    /// POTCARs are licensed and live wherever the group put them, so this is a
    /// per-installation setting the user points at once. Empty falls back to
    /// whatever `VASP_PP_PATH` the environment already carries.
    std::string vaspPotcarPath;
    /// Exchange-correlation set (ASE's `xc`, which expands to GGA/METAGGA plus
    /// the matching defaults).
    std::string vaspXc = "PBE";
    VaspPrecision vaspPrec = VaspPrecision::Accurate;
    VaspAlgo vaspAlgo = VaspAlgo::Normal;
    /// NELM — SCF iteration cap. Shared with `scfMaxSteps` in the UI so one
    /// control drives every engine, but stored separately because VASP's
    /// default differs.
    int vaspNelm = 500;
    double vaspEdiff = 1e-6;      ///< EDIFF (eV)
    /// LREAL — real-space projection. "Auto" for anything over ~20 atoms,
    /// False for small cells where the extra accuracy is free.
    std::string vaspLreal = "Auto";
    /// Ionic relaxation. Only written for the relaxation / MD tasks; a
    /// single-point emits NSW = 0 whatever is set here.
    int vaspIbrion = 2;           ///< 2 = conjugate gradient, 1 = quasi-Newton
    int vaspIsif = 2;             ///< 2 = ions only, 3 = ions + cell + volume
    double vaspEdiffg = -0.02;    ///< eV/Å when negative (force criterion)
    /// Output control. LCHARG on by default because CHGCAR is what every
    /// downstream density analysis needs; LWAVE off because WAVECAR is large
    /// and rarely wanted.
    bool vaspLwave = false;
    bool vaspLcharg = true;
    bool vaspLaechg = false;      ///< AECCAR0/2 — required by Bader analysis
    bool vaspLorbit = false;      ///< LORBIT = 11 (site/l-projected DOS)
    /// Parallelization. 0 means "leave it to VASP", which is the right default
    /// — a wrong NCORE is a performance cliff, not an error.
    int vaspNcore = 0;
    int vaspKpar = 0;
    /// Free-form INCAR tags appended verbatim, one per line ("LDAU = .TRUE.").
    /// No UI can cover 300 INCAR flags; this is the escape hatch that stops
    /// the wizard from being a ceiling.
    std::string vaspExtraIncar;

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
    /// Disable point-group symmetry reduction of the k-point set: emits
    /// `symmetry="off"` in the GPAW constructor. Off by default (GPAW folds the
    /// k-points by symmetry). Needed when the full, unsymmetrized Brillouin
    /// zone is required downstream — e.g. a Single-Point whose .gpw feeds an
    /// MLWF localization (ase.dft.wannier needs the unfolded BZ).
    bool gpawSymmetryOff = false;
    /// Gamma-centered k-point mesh: emits `kpts={'size': (...), 'gamma': True}`
    /// so the Monkhorst-Pack grid is shifted to include Γ. Off by default.
    bool gpawGammaCentered = false;
    /// Export the charge density to `density.cube` after the SCF (GPAW
    /// single-point only). Off by default; `gpawDensityType` picks pseudo vs
    /// all-electron.
    bool gpawExportDensity = false;
    GpawDensityType gpawDensityType = GpawDensityType::AllElectron;
    /// Per-field export selection. Supersedes the single-choice pair above,
    /// which is kept because saved projects and the headless script path still
    /// carry it; when `gpawDensityExports.any()` is true it wins.
    GpawDensityExports gpawDensityExports;

    /// DFT+U corrections, emitted as GPAW's `setups={...}` dictionary.
    /// `useHubbardU` gates the whole block so a populated table can be turned
    /// off without losing it.
    ///
    /// A U is a property of an element's shell in a given chemical
    /// environment, not a universal constant — the same Fe 3d takes different
    /// values in an oxide and in a metal. Nothing here validates the numbers;
    /// they are the user's to justify.
    bool useHubbardU = false;
    std::vector<HubbardU> hubbardU;

    // -- Dispersion --------------------------------------------------------
    /// Wrap the configured calculator in ASE's DFTD4 calculator, adding
    /// Grimme's D4 van der Waals energy and forces on top of it.
    ///
    /// Semilocal functionals have no long-range correlation, so layered and
    /// molecular-crystal systems come out under-bound without a dispersion
    /// correction. D4 is charge-dependent, which is what distinguishes it from
    /// D3. `dispersionD4Method` names the parent functional the damping
    /// parameters are fitted for; it must match the functional actually in use
    /// or the correction is parameterized for a different theory than the one
    /// it corrects. Empty means "follow the calculator's own xc".
    bool dispersionD4 = false;
    std::string dispersionD4Method;

    // -- LAMMPS (classical molecular dynamics) ------------------------------
    //
    // LAMMPS is not a force field, it is an engine that runs one — so unlike
    // EMT or a foundation MLIP there is no meaningful default. The pair style
    // and its coefficients ARE the physics, and they are the user's to supply.
    LammpsInterface lammpsInterface = LammpsInterface::Library;
    /// `pair_style` argument line, e.g. "eam/alloy", "tersoff",
    /// "lj/cut 10.0", "sw". Everything after the style name is passed through
    /// verbatim, which is how cutoffs and style options reach LAMMPS.
    std::string lammpsPairStyle = "lj/cut 10.0";
    /// `pair_coeff` lines, one per entry, WITHOUT the leading keyword — e.g.
    /// "* * Cu_u3.eam.alloy Cu" or "1 1 0.0103 3.4".
    ///
    /// A list rather than one string because multi-element systems routinely
    /// need several, and joining them into a single field would make the
    /// generated script depend on how the user chose to punctuate.
    std::vector<std::string> lammpsPairCoeff{"* * 0.0103 3.4"};
    /// Potential files the pair style reads (EAM tables, Tersoff parameter
    /// files, ...). lammpsrun copies these into its scratch directory; the
    /// library interface reads them from wherever LAMMPS's cwd is, so absolute
    /// paths are the safe form and the generated script says so.
    std::vector<std::string> lammpsPotentialFiles;
    /// The `lmp` executable, for the Run interface only. Empty leaves ASE to
    /// find it through $ASE_LAMMPSRUN_COMMAND / $PATH.
    std::string lammpsCommand;
    /// LAMMPS `units` style. "metal" (eV, Å, ps) is the ONLY one that matches
    /// what ASE expects; anything else silently returns energies in the wrong
    /// unit, so the generated script refuses rather than converting.
    std::string lammpsUnits = "metal";
    /// `atom_style` for the data file / box creation.
    std::string lammpsAtomStyle = "atomic";
    /// Extra LAMMPS commands appended after the pair setup (neighbor lists,
    /// `pair_modify`, per-style `fix` commands). One command per entry.
    std::vector<std::string> lammpsExtraCommands;
    /// Write the LAMMPS log to `lammps.log` rather than discarding it. On by
    /// default: when a pair style rejects its coefficients, that log is the
    /// only place the reason appears.
    bool lammpsKeepLog = true;

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
/// The device keyword the MLIP frameworks take ("cpu", "cuda", "mps").
std::string toString(MlipDevice device);
/// CHGNet's `model_name` keyword ("0.3.0" / "latest").
std::string toString(ChgNetWeights weights);
/// MatterSim's bundled checkpoint name ("MatterSim-v1.0.0-1M.pth", …).
std::string toString(MatterSimModel model);
/// FAIRChem architecture label, used in the generated script's comment.
std::string toString(FairChemModel model);
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
