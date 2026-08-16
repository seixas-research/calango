#pragma once

#include <algorithm>
#include <cmath>
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
    // -- Appended after Lammps (same append-only rule as above) -------------
    /// xTB — the GFNn-xTB semi-empirical tight-binding family (and the GFN-FF
    /// force field), through the `xtb` Python package's in-process ASE
    /// calculator. Fast and parameterized across most of the periodic table;
    /// a screening / pre-relaxation method for molecules and molecular
    /// crystals, not a DFT replacement.
    Xtb,
    /// DFTB+ — density-functional tight binding via `ase.calculators.dftb`.
    /// Needs the `dftb+` binary and a Slater-Koster parameter set: the
    /// pairwise .skf tables ARE the parameterization, so element coverage is
    /// decided by the chosen set, not by the code.
    DftbPlus,
    /// GROMACS — classical (bio)molecular mechanics through
    /// `ase.calculators.gromacs`, which drives the `gmx` binary and writes
    /// topology/parameter files per evaluation. Like LAMMPS it is an ENGINE:
    /// the force field must be able to type the structure (pdb2gmx residue
    /// recognition), so it targets proteins / water / known ligands — a bare
    /// inorganic crystal will not run.
    Gromacs,

    // -- Appended (same append-only rule as everything above) ---------------
    //
    // Every entry below is reached through an ASE CALCULATOR, like every
    // engine above it. That is the bar for appearing in this list at all: the
    // generated script builds a real `atoms.calc`, so the ASE optimizers,
    // molecular dynamics, vibrational and phonon machinery drive these codes
    // exactly as they drive GPAW or VASP, and nothing in the application has
    // to special-case them.
    //
    // Codes with no ASE calculator are deliberately NOT here. Reaching them
    // would mean generating a bespoke adapter — a script-local Calculator
    // subclass shelling out to a binary and parsing its output — and an engine
    // whose ASE integration is written fresh in every generated file is not
    // the same product as one the ASE project maintains.

    /// ABINIT — plane-wave / PAW DFT, through `ase.calculators.abinit`.
    /// Needs the `abinit` binary and a pseudopotential set; which set is
    /// spelled by `pps` (fhi, paw, jth, pot, hgh…), and the tables it names
    /// are what the run actually uses.
    Abinit,
    /// FHI-aims — all-electron, numeric atom-centred orbitals, through
    /// `ase.calculators.aims`. Its basis is a SPECIES DEFAULTS directory
    /// (light / tight / really_tight), not a cutoff: the tier IS the accuracy
    /// setting, and there is no plane-wave cutoff to converge.
    FhiAims,
    /// NWChem — Gaussian-basis quantum chemistry and plane-wave DFT in one
    /// package, through `ase.calculators.nwchem`. Which of the two runs is
    /// decided by `theory` (dft/scf/mp2/ccsd/tce for molecules; pspw/band/paw
    /// for periodic systems), and picking a molecular theory for a periodic
    /// cell is the standard way an NWChem input is quietly wrong.
    NwChem,
    /// OpenMX — pseudo-atomic-orbital DFT, through `ase.calculators.openmx`.
    /// Like SIESTA it has no plane-wave basis cutoff; its "energy cutoff" is
    /// the real-space grid the Hartree/XC terms are integrated on.
    OpenMx,
    /// FLEUR — full-potential linearized augmented plane wave, through the
    /// `ase-fleur` package. ASE's own `ase.calculators.fleur` is a stub that
    /// raises and points there, so `pip install ase-fleur` IS the ASE
    /// integration for modern FLEUR — an ASE calculator like every other entry
    /// here, just distributed separately.
    Fleur,
    /// CP2K — Gaussian-and-plane-waves DFT / QM-MM, through
    /// `ase.calculators.cp2k`, which talks to a persistent `cp2k_shell`
    /// process. Its cutoff is the PLANE-WAVE GRID cutoff of the GPW auxiliary
    /// basis, not a wavefunction cutoff: the wavefunctions are the Gaussian
    /// basis set, which is chosen separately.
    Cp2k,
    /// Amber — classical biomolecular MM, through `ase.calculators.amber`,
    /// which drives `sander`. An ENGINE, like LAMMPS and GROMACS: the physics
    /// is entirely in the prmtop topology file, which must be built beforehand
    /// (tleap / antechamber) — nothing here can type a structure.
    Amber,

    /// Calango's OWN density-functional engine: all-electron, numerical
    /// atomic orbitals, written in C++ and run IN PROCESS.
    ///
    /// The one entry in this list that is not reached through an ASE
    /// calculator, and therefore the one that breaks the rule stated above.
    /// It has to: there is no external binary and no Python calculator to
    /// build, so a generated script would have nothing to put in `atoms.calc`.
    /// The run is executed by calango::dft::CalangoDFTEngine directly.
    ///
    /// STATUS: a SCAFFOLD. Its self-contained numerics are implemented and
    /// tested (radial mesh and quadrature, the radial Poisson solve, density
    /// mixing); basis generation, the integration grid, matrix assembly and
    /// the eigensolver are not, so a run reports what is missing and produces
    /// no energy. It is exposed anyway so the wiring is exercised and visible
    /// rather than landing all at once — but nothing in the application may
    /// treat a result from it as a number until those pieces exist.
    CalangoDft,
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

/// Which family an engine belongs to.
///
/// This exists to give the engine dropdown ONE ordering, defined here rather
/// than in the order somebody happened to append entries to CalculatorKind.
/// The enum's order is frozen — its values are serialized into saved projects —
/// so a menu that followed it would put a 2026 addition of a DFT code below the
/// machine-learning potentials forever.
enum class CalculatorFamily {
    /// Solves an electronic structure from first principles: DFT, and the
    /// Gaussian-basis quantum-chemistry methods. What they share, and what
    /// puts them at the top of the menu, is that the accuracy knobs are basis
    /// sets, functionals and Brillouin-zone sampling.
    AbInitio,
    /// Semi-empirical / tight-binding: an electronic structure, but from a
    /// fitted parameterization rather than from the Schrödinger equation and a
    /// basis. Between the two — orders of magnitude faster than DFT, and still
    /// carrying electrons, which the potentials below do not.
    SemiEmpirical,
    /// Machine-learning interatomic potentials: a trained model in place of a
    /// Hamiltonian.
    MachineLearning,
    /// Classical force fields and the engines that run them (EMT, Lennard-Jones,
    /// LAMMPS, GROMACS, Amber, CHARMM). No electrons at all.
    Classical,
};

constexpr CalculatorFamily calculatorFamily(CalculatorKind kind)
{
    switch (kind) {
    case CalculatorKind::Gpaw:
    case CalculatorKind::QuantumEspresso:
    case CalculatorKind::Vasp:
    case CalculatorKind::Abinit:
    case CalculatorKind::Cp2k:
    case CalculatorKind::FhiAims:
    // Calango's own engine is a first-principles DFT code like the rest of
    // this group; that it happens to run in process rather than as a job
    // says nothing about the family it belongs to.
    case CalculatorKind::CalangoDft:
    case CalculatorKind::Siesta:
    case CalculatorKind::OpenMx:
    case CalculatorKind::Fleur:
    case CalculatorKind::NwChem:
    case CalculatorKind::Orca:
        return CalculatorFamily::AbInitio;
    case CalculatorKind::Xtb:
    case CalculatorKind::DftbPlus:
        return CalculatorFamily::SemiEmpirical;
    case CalculatorKind::Mace:
    case CalculatorKind::DeepMd:
    case CalculatorKind::NequIp:
    case CalculatorKind::Allegro:
    case CalculatorKind::ChgNet:
    case CalculatorKind::MatterSim:
    case CalculatorKind::FairChem:
        return CalculatorFamily::MachineLearning;
    case CalculatorKind::EMT:
    case CalculatorKind::LennardJones:
    case CalculatorKind::Asap:
    case CalculatorKind::Lammps:
    case CalculatorKind::Gromacs:
    case CalculatorKind::Amber:
        break;
    }
    return CalculatorFamily::Classical;
}

/// True for the periodic codes that expand in plane waves and therefore take
/// the shared "plane-wave cutoff" row and a Monkhorst-Pack k-grid.
///
/// Deliberately NOT "is it a DFT code". Handing a numeric-orbital or an
/// all-electron APW code a plane-wave cutoff is not a cosmetic mislabel: the
/// number gets mapped onto whatever that code's nearest parameter is (SIESTA's
/// real-space mesh, historically) and raising it to "converge the basis"
/// refines something else entirely while the basis stays exactly as small.
constexpr bool usesPlaneWaveCutoff(CalculatorKind kind)
{
    return kind == CalculatorKind::Gpaw || kind == CalculatorKind::Vasp
        || kind == CalculatorKind::Abinit;
}

/// True for the engines that sample the Brillouin zone with the shared
/// Monkhorst-Pack k-grid row.
constexpr bool usesKpointGrid(CalculatorKind kind)
{
    switch (kind) {
    case CalculatorKind::Gpaw:
    case CalculatorKind::QuantumEspresso:
    case CalculatorKind::Vasp:
    case CalculatorKind::Siesta:
    case CalculatorKind::Abinit:
    case CalculatorKind::FhiAims:
    case CalculatorKind::OpenMx:
    case CalculatorKind::Fleur:
    case CalculatorKind::DftbPlus:
        return true;
    default:
        break;
    }
    return false;
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
///
/// The set GPAW accepts, plus `None`:
/// https://gpaw.readthedocs.io/documentation/smearing.html
///
/// NOT in combo order any more. New methods are appended rather than inserted
/// where they belong in the menu, because the value is what a saved
/// configuration holds — renumbering the original four would silently change
/// the meaning of every one already written. The GUI carries the enum as combo
/// item data instead of relying on the row number, so the two orders are free
/// to differ.
enum class SmearingMethod {
    None,             ///< no broadening — zero-width occupations
    Gaussian,
    FermiDirac,
    MethfesselPaxton,
    MarzariVanderbilt,
    /// Linear tetrahedron BZ integration. Needs a Monkhorst-Pack k-grid and
    /// takes no width — it integrates the Brillouin zone exactly rather than
    /// broadening the occupations.
    TetrahedronMethod,
    /// Tetrahedron integration with the Blöchl curvature correction.
    ImprovedTetrahedronMethod,
    /// Thomas-Fermi occupations for orbital-free DFT. No width.
    OrbitalFree,
    /// Occupation numbers given explicitly, one list per spin channel —
    /// GPAW's {'name': 'fixed', 'numbers': [[...]]}. The way to force a
    /// specific electronic configuration (a core hole, an excited state)
    /// rather than letting the aufbau principle decide.
    FixedOccupations,
};

/// Whether `method` takes a broadening width σ.
///
/// False for the tetrahedron schemes (they integrate the BZ exactly), for
/// orbital-free, and for explicitly fixed occupations. Offering a width there
/// would present a knob that changes nothing — and GPAW rejects the key.
bool smearingUsesWidth(SmearingMethod method);
/// Whether `method` takes the Methfessel-Paxton expansion order N.
bool smearingUsesOrder(SmearingMethod method);
/// Whether `method` needs explicit occupation numbers.
bool smearingUsesFixedOccupations(SmearingMethod method);
/// The `name` GPAW's `occupations` dict expects for `method`.
///
/// Gaussian resolves to "methfessel-paxton" at order 0, which is what Gaussian
/// smearing IS — GPAW has no separate name for it, and the previous code's
/// silent fall-back to Fermi-Dirac gave a genuinely different occupation
/// function than the one the user picked.
std::string gpawSmearingName(SmearingMethod method);

/// Who actually drives a VASP geometry optimization.
///
/// This has to be a choice, and exactly one side has to win, because both
/// halves can relax on their own: VASP relaxes internally when IBRION/NSW say
/// so, and ASE's optimizers relax anything that can return forces. Letting
/// both run means every "force evaluation" ASE asks for is a complete VASP
/// relaxation — the geometry converges, eventually, but the run costs orders
/// of magnitude more than it should and the reported step-by-step trajectory
/// is a sequence of already-relaxed structures rather than a relaxation path.
enum class VaspRelaxDriver {
    /// ASE's optimizer drives; VASP is a static force/energy calculator
    /// (IBRION = -1, NSW = 0).
    ///
    /// The default, because it is the only mode in which the rest of the
    /// application works: geometry constraints, the variable-cell filters, the
    /// live streamed trajectory, the per-step energy/force metrics and the
    /// Geometry Optimization Viewer are all built around ASE taking the steps.
    Ase,
    /// VASP relaxes internally (IBRION / NSW / ISIF / EDIFFG); no ASE
    /// optimizer is created at all.
    ///
    /// Much faster per step — VASP keeps the wavefunction and charge density
    /// between ionic steps instead of restarting from scratch — which is the
    /// whole reason to offer it. The cost is that the ionic steps happen inside
    /// a single ASE call, so they cannot be streamed or constrained from here.
    Vasp,
};

/// VASP PREC. Enum order is the combo order in the VASP settings group.
enum class VaspPrecision { Normal, Accurate, Single };

// ---------------------------------------------------------------------------
// Quantum ESPRESSO
// ---------------------------------------------------------------------------
//
// QE is a plane-wave code with a DUAL grid: the wavefunctions are expanded to
// `ecutwfc` and the charge density to `ecutrho`. That second cutoff is not a
// refinement anyone can skip — for ultrasoft pseudopotentials or PAW the
// density carries augmentation charges that are far harder than the
// wavefunctions, and the default ratio of 4 (right for norm-conserving) leaves
// them badly under-converged. It is a QE-specific decision with no GPAW or
// VASP counterpart, which is why QE gets its own settings group rather than
// borrowing the shared "plane-wave cutoff" row.

/// QE `occupations`. Enum order is the combo order.
enum class QeOccupations {
    Smearing,      ///< metals; needs a smearing function and a width
    Fixed,         ///< insulators with a known gap
    Tetrahedra,    ///< Bloechl-corrected tetrahedra (DOS-quality, no width)
    TetrahedraOpt, ///< the optimized tetrahedron method
};

/// QE `smearing`. Only read when occupations = Smearing.
enum class QeSmearing {
    MarzariVanderbilt, ///< 'cold' smearing — QE's recommended default
    Gaussian,
    MethfesselPaxton,
    FermiDirac,        ///< a physical temperature, not a convergence aid
};

constexpr const char* toString(QeOccupations occupations)
{
    switch (occupations) {
    case QeOccupations::Fixed:         return "fixed";
    case QeOccupations::Tetrahedra:    return "tetrahedra";
    case QeOccupations::TetrahedraOpt: return "tetrahedra_opt";
    case QeOccupations::Smearing:      break;
    }
    return "smearing";
}

constexpr const char* toString(QeSmearing smearing)
{
    switch (smearing) {
    case QeSmearing::Gaussian:         return "gaussian";
    case QeSmearing::MethfesselPaxton: return "methfessel-paxton";
    case QeSmearing::FermiDirac:       return "fermi-dirac";
    case QeSmearing::MarzariVanderbilt: break;
    }
    return "marzari-vanderbilt";
}

/// True when the run needs a smearing function and a width — the tetrahedron
/// methods and fixed occupations take neither, and writing `degauss` alongside
/// them is how a QE input ends up being silently ignored or refused.
constexpr bool qeUsesSmearing(QeOccupations occupations)
{
    return occupations == QeOccupations::Smearing;
}

// ---------------------------------------------------------------------------
// SIESTA
// ---------------------------------------------------------------------------
//
// SIESTA has NO plane-wave cutoff. It is a numerical-atomic-orbital code: the
// basis is a finite set of pseudo-atomic orbitals, and its quality is set by
// how many orbitals per shell (the basis SIZE), how they are generated (the
// basis TYPE) and how far they extend (the ENERGY SHIFT — the confinement
// energy that fixes each orbital's cutoff radius). The only cutoff SIESTA has
// is the MESH cutoff, which discretizes the real-space grid the Hartree and XC
// terms are evaluated on and has nothing to do with a basis-set expansion.
//
// Offering "plane-wave cutoff" for SIESTA was therefore not merely a mislabel:
// it silently mapped one number onto `mesh_cutoff`, so raising it to converge
// "the basis" refined a grid while the basis stayed exactly as small.

/// SIESTA `PAO.BasisType`. Enum order is the combo order.
enum class SiestaBasisType {
    Split,       ///< split-valence (the standard, and SIESTA's default)
    SplitGauss,  ///< split valence with Gaussian tails
    Nodes,       ///< multiple-zeta from the excited-state orbitals
    NoNodes,     ///< nodeless, the original Sankey-type scheme
    Filteret,    ///< filtered basis (needs a filter cutoff)
};

constexpr const char* toString(SiestaBasisType type)
{
    switch (type) {
    case SiestaBasisType::SplitGauss: return "splitgauss";
    case SiestaBasisType::Nodes:      return "nodes";
    case SiestaBasisType::NoNodes:    return "nonodes";
    case SiestaBasisType::Filteret:   return "filteret";
    case SiestaBasisType::Split:      break;
    }
    return "split";
}

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

/// The file each GpawDensityExports flag writes, plus the two densities the
/// other generators produce.
///
/// Named once here rather than spelled as a literal at both ends. The GUI has
/// to map every file back to a display label for the Volumetric Data dock, and
/// when the two copies drifted — the dock looked up "hartree_potential.cube"
/// while the script wrote "potential_hartree.cube" — the Hartree potential
/// silently arrived labelled with its raw file name. A shared symbol makes
/// that particular bug unrepresentable.
namespace densityFiles {
inline constexpr const char* kAllElectron = "density_all_electron.cube";
inline constexpr const char* kPseudo = "density_pseudo.cube";
inline constexpr const char* kSpin = "density_spin.cube";
inline constexpr const char* kHartree = "potential_hartree.cube";
inline constexpr const char* kElf = "elf.cube";
inline constexpr const char* kKineticEnergy = "kinetic_energy_density.cube";
/// The single charge density a plain "export the density" run writes.
inline constexpr const char* kDensity = "density.cube";
/// Charge-density difference, written by the CDD generator.
inline constexpr const char* kChargeDensityDifference = "cdd.cube";
} // namespace densityFiles

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

/// How the thermostat setpoint is swept over a SIMULATED-ANNEALING run.
///
/// All three are written in terms of the run fraction x = step / total steps,
/// and all three are ENDPOINT-EXACT: T(0) is the initial temperature and T(1)
/// the final one, whatever the coefficient. That is deliberate. A schedule
/// that only approaches its target asymptotically ends the run at a
/// temperature nobody asked for, and the discrepancy is invisible in a plot
/// that has been cooling for 50 ps.
///
/// The coefficient k is a CURVATURE, not a rate constant with units: it says
/// how far the ramp bends away from a straight line, and every schedule
/// degenerates to Linear as k -> 0. One knob, one meaning, whichever law is
/// selected.
enum class AnnealingSchedule {
    /// T = T0 + (T1 - T0) x. Constant dT/dt; no coefficient.
    Linear,
    /// T = T1 + (T0 - T1) (e^{-kx} - e^{-k}) / (1 - e^{-k}).
    /// Geometric-style cooling: most of the temperature change happens early,
    /// with a long tail near T1. The usual choice for quenching a melt.
    Exponential,
    /// T = T0 + (T1 - T0) ln(1 + kx) / ln(1 + k).
    /// The slowest of the three near the end — the practical, endpoint-exact
    /// relative of the Geman-Geman ~1/ln(n) schedule, which is the one with
    /// the global-optimum convergence proof and a run length nobody can
    /// afford.
    Logarithmic,
};

/// The thermostat setpoint (K) at run fraction `x` in [0, 1]. Shared by the
/// script generator and the wizard preview so the curve the user is shown and
/// the curve the run follows cannot drift apart.
///
/// `coefficient` at or below this threshold is treated as Linear rather than
/// evaluated: both non-linear laws are 0/0 there.
inline double annealingTemperature(AnnealingSchedule schedule, double startK,
                                   double endK, double coefficient, double x)
{
    x = std::clamp(x, 0.0, 1.0);
    const double linear = startK + (endK - startK) * x;
    constexpr double kFlat = 1.0e-6;
    if (schedule == AnnealingSchedule::Linear || coefficient <= kFlat)
        return linear;

    switch (schedule) {
    case AnnealingSchedule::Exponential: {
        const double decay = std::exp(-coefficient);
        return endK + (startK - endK) * (std::exp(-coefficient * x) - decay)
            / (1.0 - decay);
    }
    case AnnealingSchedule::Logarithmic:
        return startK
            + (endK - startK) * std::log1p(coefficient * x)
            / std::log1p(coefficient);
    case AnnealingSchedule::Linear:
        break;
    }
    return linear;
}

/// The Python expression the generated script evaluates for the setpoint, as a
/// function of the local names `x`, `T_initial`, `T_final` and `anneal_k`.
/// Emitted as ONE expression rather than a branch chain so the script says
/// which law it is running in a form that can be read, edited and pasted into
/// a plot.
inline std::string annealingPythonExpression(AnnealingSchedule schedule)
{
    switch (schedule) {
    case AnnealingSchedule::Exponential:
        return "T_final + (T_initial - T_final) * "
               "(math.exp(-anneal_k * x) - math.exp(-anneal_k)) "
               "/ (1.0 - math.exp(-anneal_k))";
    case AnnealingSchedule::Logarithmic:
        return "T_initial + (T_final - T_initial) * "
               "math.log1p(anneal_k * x) / math.log1p(anneal_k)";
    case AnnealingSchedule::Linear:
        break;
    }
    return "T_initial + (T_final - T_initial) * x";
}

/// Schedule name as it appears in the generated script and in the run report.
inline std::string toString(AnnealingSchedule schedule)
{
    switch (schedule) {
    case AnnealingSchedule::Exponential: return "exponential";
    case AnnealingSchedule::Logarithmic: return "logarithmic";
    case AnnealingSchedule::Linear:      break;
    }
    return "linear";
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
    /// Fermi-Dirac rather than Gaussian, matching the wizard combo's own
    /// default. The two used to disagree, which did not show while every
    /// method was emitted as Fermi-Dirac anyway; now that each one generates
    /// its own occupation function, a wizard that exposes no smearing control
    /// (the Electronic Structure setup, which inherits its SCF) would
    /// otherwise silently switch from Fermi-Dirac to Gaussian.
    SmearingMethod smearing = SmearingMethod::FermiDirac;
    double smearingWidthEv = 0.1;
    /// Methfessel-Paxton expansion order N. 1 rather than GPAW's own 0
    /// default: order 0 IS Gaussian smearing, which has its own entry in the
    /// method list, so a user who explicitly picked Methfessel-Paxton means
    /// N ≥ 1. Read only when smearingUsesOrder(smearing).
    int smearingOrder = 1;
    /// Explicit occupation numbers for SmearingMethod::FixedOccupations — one
    /// inner list per spin channel, as GPAW's `numbers` key expects. Empty
    /// leaves the key off, which GPAW rejects; the wizard therefore refuses to
    /// generate until it is filled.
    std::vector<std::vector<double>> fixedOccupations;

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

    // Simulated annealing — the same integrator, with a moving setpoint.
    //
    // Annealing is a MODE of molecular dynamics rather than a task of its own:
    // it runs one of the thermostatted ensembles above and retargets it every
    // step. Keeping it here (instead of as a TaskKind) is what lets the
    // constraints, the sampling, the streamed trajectory and every calculator
    // setting stay shared with a constant-temperature run.
    bool annealing = false;
    AnnealingSchedule annealingSchedule = AnnealingSchedule::Linear;
    /// Setpoint at the first and last step. Cooling (start > end) is the usual
    /// direction, but heating ramps are legal and are generated the same way.
    /// When annealing is on, the initial Maxwell-Boltzmann velocities are drawn
    /// at annealStartK, not at temperatureK.
    double annealStartK = 1000.0;
    double annealEndK = 300.0;
    /// Curvature of the Exponential / Logarithmic laws; ignored by Linear.
    /// Must stay > 0; both laws are singular at exactly 0 and both tend to
    /// Linear there.
    double annealCoefficient = 3.0;

    // DFT common knobs. `planeWaveCutoffEv` is the plane-wave basis cutoff of
    // GPAW's PW mode and of VASP's ENCUT — the two engines that share one. QE
    // has its own dual-cutoff pair below and SIESTA has no plane-wave cutoff
    // at all, so neither reads this.
    double planeWaveCutoffEv = 500.0;
    int kpts[3] = {7, 7, 7};

    // Quantum ESPRESSO. Cutoffs are in Rydberg, which is what pw.x reads —
    // converting from eV in the UI and back in the generator is one more place
    // for a factor of 13.6 to go missing.
    double qeEcutwfcRy = 60.0;
    /// ecutrho. Zero means "let QE default it to 4 x ecutwfc", which is right
    /// for norm-conserving pseudopotentials and badly wrong for ultrasoft/PAW
    /// (8-12x). Written explicitly whenever non-zero.
    double qeEcutrhoRy = 0.0;
    std::string qeInputDft = "pbe"; ///< input_dft; empty uses the pseudo's own
    /// pseudo_dir. Like VASP's POTCAR root this is a per-INSTALLATION setting
    /// (Preferences → External Files), injected here by the wizard rather than
    /// read from the environment, so the generated script names the library it
    /// was configured against instead of whatever happened to be exported.
    std::string espressoPseudoDir;
    QeOccupations qeOccupations = QeOccupations::Smearing;
    QeSmearing qeSmearing = QeSmearing::MarzariVanderbilt;
    double qeDegaussRy = 0.01;      ///< smearing width, Ry
    double qeConvThrRy = 1.0e-8;    ///< conv_thr, Ry

    // SIESTA. No plane-wave cutoff exists for this engine — see the note at
    // SiestaBasisType.
    SiestaBasisType siestaBasisType = SiestaBasisType::Split;
    std::string siestaBasisSize = "DZP"; ///< SZ | SZP | DZ | DZP | TZP
    /// PAO.EnergyShift (eV): the confinement energy that sets every orbital's
    /// cutoff radius. Smaller means longer-ranged orbitals and a better —
    /// and much more expensive — basis. 0.27 eV (0.02 Ry) is SIESTA's default.
    double siestaEnergyShiftEv = 0.27;
    /// MeshCutoff (eV): the real-space grid the Hartree and XC terms are
    /// integrated on. Not a basis-set parameter.
    double siestaMeshCutoffEv = 300.0;
    std::string siestaXc = "PBE";
    /// SIESTA_PP_PATH, from Preferences → External Files. Same rationale as
    /// the QE and VASP libraries above.
    std::string siestaPseudoDir;

    // MACE machine-learning potential
    MaceModelSource maceSource = MaceModelSource::FoundationMP;
    std::string maceSize = "medium";   ///< "small" | "medium" | "large"
    /// Custom checkpoint (.model / .pt). Used when maceSource is CustomFile;
    /// may also name a downloaded foundation checkpoint
    /// (e.g. "mace-off23-small.model") the user wants to pin.
    std::string maceModelPath;
    std::string maceDevice = "cpu";    ///< "cpu" | "cuda" | "mps"
    MacePrecision macePrecision = MacePrecision::Float64;
    /// MACE-MP-0 only: pass `dispersion=True` to mace.calculators.mace_mp,
    /// enabling the D3(BJ) dispersion head the foundation model ships with.
    /// Ignored by MACE-OFF and custom checkpoints, which take no such flag.
    bool maceDispersion = false;

    // -- Machine-learning interatomic potentials (DeepMD … FAIRChem) --------
    // One shared device selector: every backend below is a PyTorch/TF model
    // that runs on the same hardware, and duplicating the control per engine
    // would let two of them disagree about which GPU a job uses.
    MlipDevice mlipDevice = MlipDevice::Cpu;

    /// DeepMD-kit: frozen graph (`.pb`, or a `.pth` for the PyTorch backend).
    std::string deepmdModelPath;
    /// NequIP / Allegro: a packaged inference model, never a raw training
    /// checkpoint. Which artifact depends on the nequip generation: >= 0.7
    /// compiles one with `nequip-compile` (.nequip.pt2 / .nequip.pth), the
    /// older line deployed TorchScript with `nequip-deploy build` (.pth). The
    /// generated script loads whichever API the installed package exposes.
    std::string nequipModelPath;
    /// Unit names the deployed NequIP/Allegro model was trained in. ASE works
    /// in eV and Å, so the calculator rescales by these — a model trained in
    /// kcal/mol silently reports wrong energies if they are left at eV.
    std::string nequipEnergyUnits = "eV";
    std::string nequipLengthUnits = "Angstrom";

    ChgNetWeights chgnetWeights = ChgNetWeights::V0_3_0;
    /// Retired knob, kept only so saved projects keep deserializing. It used
    /// to drive CHGNetCalculator's `stress_weight`, which is NOT an on/off
    /// switch: it is the GPa -> eV/Å³ conversion factor for the stress CHGNet
    /// always computes. Writing 1.0 reported stresses ~160x too large and
    /// 0.0 zeroed them silently, so the generator no longer emits the kwarg.
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
    VaspRelaxDriver vaspRelaxDriver = VaspRelaxDriver::Ase;
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
    /// LCAO basis-set name. The built-in sz / szp / dz / dzp ship with the
    /// GPAW datasets; any other name is looked up as `<symbol>.<name>.basis`
    /// along GPAW's setup search path, which is what `gpawBasisDir` extends.
    std::string gpawBasis = "dzp";
    /// Directory of CUSTOM LCAO basis files, from Preferences → External
    /// Files. Empty means "only the basis sets that shipped with GPAW".
    ///
    /// Prepended to `gpaw.setup_paths` by the generated script rather than
    /// assigned to it: that list also carries the PAW datasets, and replacing
    /// it would find the basis and lose the setups.
    std::string gpawBasisDir;
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
    /// Drop POINT-GROUP symmetry only, keeping time reversal: emits
    /// `symmetry={"point_group": False, "time_reversal": True}`. Ignored when
    /// `gpawSymmetryOff` is set (that is the stronger request).
    ///
    /// Not the same thing as `symmetry="off"`, which drops time reversal too
    /// and so roughly doubles the k-point count for no gain in a nonmagnetic
    /// system. GPAW's nonlinear-optics module asserts only that the point
    /// group is off (gpaw.nlopt.matrixel.make_nlodata), because the matrix
    /// elements it builds are not invariant under the point-group folding —
    /// so this is the setting that satisfies it at the lowest cost.
    bool gpawPointGroupOff = false;
    /// `nbands=` literal, empty for GPAW's own default.
    ///
    /// A string rather than a number because GPAW's symbolic forms are part of
    /// the parameter: "nao" (every atomic orbital), "110%" (10 % more than the
    /// occupied count). A purely numeric value is emitted unquoted; anything
    /// else is quoted.
    std::string gpawNbands;
    /// `convergence={"bands": N}` — how many bands must be converged, not just
    /// occupied. 0 leaves the key out.
    ///
    /// Negative counts from the top the way GPAW reads them: -10 means "all
    /// but the highest 10". This matters for any response property that sums
    /// over EMPTY states, because the SCF converges occupied bands only and
    /// the unoccupied manifold it leaves behind is unconverged noise.
    int gpawConvergeBands = 0;
    /// Gamma-centered k-point mesh: shift the Monkhorst-Pack grid so it
    /// includes Γ. Off by default.
    ///
    /// Not GPAW-specific, despite living among the GPAW fields for historical
    /// reasons: an even-numbered MP mesh misses Γ in every plane-wave code,
    /// and the offset one is what a hexagonal cell or a Wannier interpolation
    /// wants regardless of who computes it. GPAW emits it as
    /// `kpts={'size': (...), 'gamma': True}`, VASP as the KPOINTS Gamma
    /// centering ASE selects with `gamma=True`.
    bool kptsGammaCentered = false;
    /// Export the charge density to `density.cube` after the SCF (GPAW
    /// single-point only). Off by default; `gpawDensityType` picks pseudo vs
    /// all-electron.
    bool gpawExportDensity = false;
    GpawDensityType gpawDensityType = GpawDensityType::AllElectron;
    /// Per-field export selection. Supersedes the single-choice pair above,
    /// which is kept because saved projects and the headless script path still
    /// carry it; when `gpawDensityExports.any()` is true it wins.
    GpawDensityExports gpawDensityExports;
    /// Convert whichever density/volumetric files THIS run writes into
    /// Calango's compressed HDF5 container (core::VolumetricData::saveHdf5,
    /// see docs/sphinx/source/reference/hdf5_density.md) once it finishes —
    /// GPAW's density_*.cube/elf.cube/potential_hartree.cube/
    /// kinetic_energy_density.cube AND VASP's CHGCAR/AECCAR0/AECCAR2, covered
    /// by the one checkbox regardless of engine, since both are "the density
    /// files this calculator setup page already lets you choose". The
    /// original file is DELETED once its .h5 is written (replace, to save
    /// the disk space the option exists for) — off by default, since a
    /// converted file needs an HDF5-aware reader and every existing workflow
    /// expects a plain .cube/CHGCAR sitting in the job directory.
    bool compressDensityToHdf5 = false;

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

    // -- xTB (semi-empirical tight binding) ---------------------------------
    //
    // Keyword names match xtb-python's XTB.default_parameters exactly
    // (method / accuracy / electronic_temperature / max_iterations), verified
    // against the installed xtb 22.x ASE calculator.
    /// "GFN2-xTB" (default) | "GFN1-xTB" | "GFN-FF". GFN-FF is a force field:
    /// it has no electronic structure, so the two SCC knobs below are not
    /// emitted for it.
    std::string xtbMethod = "GFN2-xTB";
    /// xTB's global accuracy multiplier — LOWER is tighter (it scales the SCC
    /// convergence thresholds and integral cutoffs together). 1.0 is the
    /// method's calibrated default; the useful range is ~1e-4..1e3.
    double xtbAccuracy = 1.0;
    /// Electronic temperature (K) of the tight-binding Fermi smearing. Part
    /// of the GFN parameterization at 300 K rather than a convergence aid, so
    /// the default matches the published methods.
    double xtbElectronicTemperatureK = 300.0;
    int xtbMaxIterations = 250; ///< SCC iteration cap

    // -- DFTB+ (density-functional tight binding) ---------------------------
    /// Directory holding the Slater-Koster .skf tables (mio, 3ob, ...).
    /// Exported as DFTB_PREFIX with a trailing slash — ASE joins file names
    /// onto it verbatim. Like VASP's POTCAR root this is per-installation
    /// state the wizard persists in QSettings, not a per-run choice.
    std::string dftbSlakoDir;
    /// Self-consistent charges. Off is the original non-SCC DFTB — much
    /// faster, but wrong for anything with charge transfer.
    bool dftbScc = true;
    double dftbSccTolerance = 1e-5; ///< SCCTolerance (charge convergence)
    int dftbMaxSccIterations = 100; ///< MaxSCCIterations
    /// Fermi filling temperature (K). 0 keeps DFTB+'s zero-temperature
    /// occupations; emitted as a Filling = Fermi block only when positive,
    /// converted to Hartree in the script because that is the unit DFTB+
    /// reads when no HSD modifier is given.
    double dftbFillingTemperatureK = 0.0;

    // -- GROMACS (classical biomolecular MM) --------------------------------
    //
    // Like LAMMPS, an engine rather than a potential — but with the further
    // constraint that pdb2gmx must be able to TYPE the structure against the
    // chosen force field's residue database. These fields feed the pdb2gmx
    // command line and the generated .mdp parameter file.
    std::string gromacsForceField = "oplsaa"; ///< pdb2gmx -ff
    /// pdb2gmx -water. "none" is legal (no solvent topology).
    std::string gromacsWaterModel = "spc";
    /// The gmx wrapper binary. Every GROMACS tool (pdb2gmx, grompp, mdrun,
    /// energy, traj) is a subcommand of it, so one path configures them all.
    std::string gromacsExecutable = "gmx";
    /// Extra .mdp parameters, one `key = value` per line, applied on top of
    /// the generated defaults. The same escape-hatch rationale as VASP's
    /// extra INCAR tags: no UI covers the full .mdp vocabulary.
    std::string gromacsExtraMdp;

    // -- ORCA (quantum chemistry) ------------------------------------------
    std::string orcaMethod = "B3LYP";   ///< functional / method keyword
    std::string orcaBasis = "def2-SVP"; ///< basis set keyword
    int charge = 0;
    int multiplicity = 1;               ///< 2S+1
    /// "" (gas phase), "CPCM" or "SMD" — with `orcaSolvent` naming the
    /// solvent (water, acetonitrile, ...).
    std::string orcaSolvationModel;
    std::string orcaSolvent = "water";

    // -- ABINIT (plane-wave / PAW DFT) --------------------------------------
    //
    // ASE's Abinit calculator takes `ecut` in eV and converts; the shared
    // `planeWaveCutoffEv` above is therefore the cutoff, exactly as for GPAW's
    // PW mode and VASP's ENCUT, and ABINIT gets no second cutoff field.
    std::string abinitXc = "PBE";
    /// `pps` — WHICH pseudopotential family the run uses, and so which tables
    /// it reads: "fhi" (norm-conserving Fritz-Haber), "paw" (the ATOMPAW /
    /// JTH-style sets), "jth", "pot" (Teter), "hgh", "hgh.k", "tm".
    ///
    /// Not a quality knob with a safe default: the table set installed on a
    /// given machine determines which values work at all, and a `pps` naming
    /// files that are not there fails at the first element ABINIT looks up.
    std::string abinitPps = "fhi";
    /// Directory holding the pseudopotential tables (ASE's `pp_paths`). Like
    /// VASP's POTCAR root this is per-INSTALLATION state; empty leaves ASE to
    /// consult its own configuration file.
    std::string abinitPseudoDir;
    double abinitToldfe = 1.0e-6; ///< SCF total-energy tolerance, Hartree
    int abinitNstep = 100;        ///< SCF iteration cap
    /// Free-form ABINIT input variables appended verbatim, one `key value` per
    /// line ("nbdbuf 4", "diemac 12.0"). ABINIT has several hundred; this is
    /// the escape hatch that stops the wizard from being a ceiling.
    std::string abinitExtra;

    // -- FHI-aims (all-electron, numeric atom-centred orbitals) -------------
    //
    // No plane-wave cutoff exists here. The basis IS the species-defaults
    // TIER: light / tight / really_tight are pre-tabulated, hierarchical basis
    // + grid + accuracy sets, and moving between them is how an aims
    // calculation is converged.
    std::string aimsXc = "pbe";
    /// The species_defaults directory (AIMS_SPECIES_DIR). Per-installation.
    std::string aimsSpeciesDir;
    /// "light" | "intermediate" | "tight" | "really_tight". Joined onto
    /// `aimsSpeciesDir` — the directory holds one subfolder per tier.
    std::string aimsSpeciesTier = "light";
    /// sc_accuracy_etot (eV) — the SCF total-energy convergence criterion.
    double aimsScfAccuracyEv = 1.0e-6;
    /// `relativistic`. "atomic_zora scalar" is the standard choice and is
    /// REQUIRED for anything past the first rows: a non-relativistic aims run
    /// on a 5d element is not merely less accurate, it is wrong. "none" exists
    /// for light-element benchmarks.
    std::string aimsRelativistic = "atomic_zora scalar";
    /// Extra control.in keywords, one per line, appended verbatim.
    std::string aimsExtra;

    // -- NWChem -------------------------------------------------------------
    //
    // NWChem is two codes in one binary, and `theory` picks which. The
    // molecular modules (dft/scf/mp2/ccsd/tce) work in a Gaussian basis and
    // ignore periodicity; the plane-wave ones (pspw/band/paw) are the periodic
    // DFT. Running "dft" on a crystal silently treats it as an isolated
    // cluster, which is the standard way an NWChem input comes out wrong.
    std::string nwchemTheory = "dft";
    std::string nwchemXc = "b3lyp";  ///< only read by theory = dft
    std::string nwchemBasis = "6-31G*"; ///< only read by the molecular modules
    /// Per-process memory, as NWChem's `memory` directive spells it
    /// ("2000 mb", "4 gb"). Empty leaves NWChem's own default.
    std::string nwchemMemory = "2000 mb";
    /// Extra directives, one per line, merged into the generated input.
    std::string nwchemExtra;

    // -- OpenMX (pseudo-atomic-orbital DFT) ---------------------------------
    //
    // Like SIESTA: NO plane-wave basis cutoff. Its "energy cutoff" is the
    // real-space integration grid for the Hartree and XC terms.
    std::string openmxXc = "GGA-PBE"; ///< LDA | LSDA-CA | LSDA-PW | GGA-PBE
    /// DFT_DATA_PATH — the directory holding OpenMX's pseudopotential (VPS)
    /// and pseudo-atomic-orbital (PAO) databases. Per-installation.
    std::string openmxDataPath;
    /// scf.energycutoff (eV in the UI; the script converts to the Ry OpenMX
    /// reads). The REAL-SPACE grid, not a basis cutoff.
    double openmxEnergyCutoffEv = 2721.0; ///< ~200 Ry, OpenMX's usual default
    double openmxScfCriterionEv = 1.0e-4; ///< scf.criterion
    int openmxScfMaxIter = 100;           ///< scf.maxIter
    /// scf.EigenvalueSolver: "Band" for a periodic crystal, "Cluster" for a
    /// molecule, "DC" for the O(N) divide-conquer solver on large cells.
    std::string openmxEigenSolver = "Band";

    // -- FLEUR (full-potential LAPW) ----------------------------------------
    //
    // Through the `ase-fleur` package; ASE's own ase.calculators.fleur is a
    // stub that raises and points at it, so that package IS the ASE
    // integration for modern FLEUR.
    /// K_max (bohr^-1) — FLEUR's interstitial plane-wave cutoff. Its own
    /// convergence parameter, in reciprocal length rather than the
    /// dimensionless RKmax WIEN2k uses.
    double fleurKmax = 4.0;
    std::string fleurXc = "pbe";
    /// The `inpgen` / `fleur` executables' directory. Empty relies on PATH.
    std::string fleurRoot;
    double fleurEnergyConvHtr = 1.0e-5; ///< itmax convergence (Hartree)
    int fleurMaxIterations = 60;

    // -- CP2K (Gaussian and plane waves) ------------------------------------
    //
    // CP2K's cutoff is the PLANE-WAVE GRID cutoff of the GPW auxiliary basis —
    // the grid the density is represented on — NOT a wavefunction cutoff. The
    // wavefunctions are the Gaussian basis set, chosen separately, which is
    // why this engine has both a `cutoff` and a `basis_set`. Treating the
    // cutoff as the basis-set knob is the classic CP2K mistake: it converges
    // the grid while the basis stays exactly as small.
    double cp2kCutoffEv = 5442.0;   ///< ~400 Ry, ASE's own default
    /// REL_CUTOFF — the grid a Gaussian of unit exponent is mapped onto, which
    /// is what actually decides how the multi-grid assigns functions. 60 Ry.
    double cp2kRelCutoffEv = 816.0;
    std::string cp2kBasisSet = "DZVP-MOLOPT-SR-GTH";
    std::string cp2kBasisSetFile = "BASIS_MOLOPT";
    /// GTH pseudopotential. "auto" lets CP2K pick the one matching the
    /// functional, which is right whenever the functional is a standard one.
    std::string cp2kPseudoPotential = "auto";
    std::string cp2kPotentialFile = "POTENTIAL";
    std::string cp2kXc = "PBE";
    int cp2kMaxScf = 50;
    /// The cp2k_shell command ASE talks to. CP2K is driven through a PERSISTENT
    /// process rather than one run per evaluation, which is what makes it fast
    /// for MD — and what makes this a required setting rather than a launch
    /// command like the other file-IO codes.
    std::string cp2kCommand = "cp2k_shell";
    /// Free-form CP2K input sections appended verbatim (`inp`), for the parts
    /// of a 1000-keyword input no dialog can cover.
    std::string cp2kExtraInput;

    // -- Amber (classical biomolecular MM) ----------------------------------
    //
    // An ENGINE with no force field of its own in the UI sense: the physics is
    // entirely inside the prmtop topology, which tleap/antechamber build.
    // Nothing here can type a structure, which is the same constraint GROMACS
    // has and the reason both refuse an untyped inorganic cell.
    /// The sander invocation, ASE's `amber_exe`. The trailing "-O " (overwrite)
    /// is part of ASE's own default and is kept.
    std::string amberExecutable = "sander -O ";
    /// The prmtop topology. REQUIRED — without it there is no force field.
    std::string amberTopologyFile;
    /// The mdin control file. Generated with a single-point / minimization
    /// stanza when left empty, so a first run works without one.
    std::string amberInputFile;
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
