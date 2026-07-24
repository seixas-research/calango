#include "core/AseScriptGenerator.hpp"

#include "core/CalangoLogModule.hpp" // generated from calango_log.py by CMake

#include <sstream>

namespace calango::core {

std::string toString(CalculatorKind kind)
{
    switch (kind) {
    case CalculatorKind::EMT: return "EMT";
    case CalculatorKind::LennardJones: return "Lennard-Jones";
    case CalculatorKind::QuantumEspresso: return "Quantum ESPRESSO";
    case CalculatorKind::Vasp: return "VASP";
    case CalculatorKind::Mace: return "MACE";
    case CalculatorKind::Gpaw: return "GPAW";
    case CalculatorKind::Siesta: return "SIESTA";
    case CalculatorKind::Orca: return "ORCA";
    case CalculatorKind::Asap: return "ASAP";
    }
    return "?";
}

std::string toString(TaskKind kind)
{
    switch (kind) {
    case TaskKind::SinglePoint: return "Single-point energy";
    case TaskKind::GeometryOptimization: return "Geometry optimization";
    case TaskKind::MolecularDynamics: return "Molecular dynamics";
    }
    return "?";
}

std::string toString(Optimizer optimizer)
{
    switch (optimizer) {
    case Optimizer::BFGS: return "BFGS";
    case Optimizer::LBFGS: return "LBFGS";
    case Optimizer::FIRE: return "FIRE";
    case Optimizer::GPMin: return "GPMin";
    case Optimizer::MDMin: return "MDMin";
    }
    return "BFGS";
}

std::string toString(MacePrecision precision)
{
    return precision == MacePrecision::Float32 ? "float32" : "float64";
}

std::string toString(GpawEigensolver solver)
{
    switch (solver) {
    case GpawEigensolver::Davidson: return "dav";
    case GpawEigensolver::ConjugateGradient: return "cg";
    case GpawEigensolver::RmmDiis: return "rmm-diis";
    case GpawEigensolver::Direct: return "direct";
    }
    return "dav";
}

std::string toString(GpawMixerKind mixer)
{
    switch (mixer) {
    case GpawMixerKind::Mixer: return "Mixer";
    case GpawMixerKind::MixerSum: return "MixerSum";
    case GpawMixerKind::MixerDif: return "MixerDif";
    }
    return "Mixer";
}

std::string toString(SmearingMethod method)
{
    switch (method) {
    case SmearingMethod::None: return "None (fixed occupations)";
    case SmearingMethod::Gaussian: return "Gaussian";
    case SmearingMethod::FermiDirac: return "Fermi-Dirac";
    case SmearingMethod::MethfesselPaxton: return "Methfessel-Paxton";
    }
    return "Gaussian";
}

namespace {

/// Live viewport streaming: one "CALANGO_CELL … / CALANGO_FRAME n /
/// n atom lines" block per call, parsed by JobRunner into a trajectory
/// frame. Built as a single write + flush so blocks arrive atomically.
constexpr const char* kStreamFrameHelper =
    "import sys as _sys\n"
    "\n"
    "def _stream_frame():\n"
    "    lines = []\n"
    "    if atoms.pbc.any():\n"
    "        cell = atoms.cell[:]\n"
    "        lines.append(\"CALANGO_CELL \" + \" \".join(\n"
    "            f\"{v:.8f}\" for row in cell for v in row))\n"
    "    # Per-atom vectors travel with the frame so the viewport's Vector\n"
    "    # overlay works DURING the run, not only after reloading the saved\n"
    "    # trajectory. Both are already computed at this point (the\n"
    "    # integrator just used them), so reading them costs nothing; the\n"
    "    # try/except covers calculators that cannot supply forces.\n"
    "    try:\n"
    "        _f = atoms.get_forces()\n"
    "    except Exception:\n"
    "        _f = None\n"
    "    try:\n"
    "        _v = atoms.get_velocities()\n"
    "    except Exception:\n"
    "        _v = None\n"
    "    # 'FV' marks the extended 10-column atom lines; a bare count keeps\n"
    "    # the original positions-only form for readers that predate this.\n"
    "    _extended = _f is not None and _v is not None\n"
    "    lines.append(f\"CALANGO_FRAME {len(atoms)}\" + (\" FV\" if _extended else \"\"))\n"
    "    _sym = atoms.get_chemical_symbols()\n"
    "    _pos = atoms.get_positions()\n"
    "    for _i in range(len(atoms)):\n"
    "        s, p = _sym[_i], _pos[_i]\n"
    "        row = f\"{s} {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}\"\n"
    "        if _extended:\n"
    "            row += (f\" {_f[_i][0]:.6e} {_f[_i][1]:.6e} {_f[_i][2]:.6e}\"\n"
    "                    f\" {_v[_i][0]:.6e} {_v[_i][1]:.6e} {_v[_i][2]:.6e}\")\n"
    "        lines.append(row)\n"
    "    _sys.stdout.write(\"\\n\".join(lines) + \"\\n\")\n"
    "    _sys.stdout.flush()\n"
    "\n";

/// Structured logging preamble, emitted once near the top of every generated
/// script. The logger itself lives in calango_log.py (staged beside run.py by
/// MainWindow::stageJob and beside an exported script by the wizards' Export
/// action), so the ~55 lines of class definition no longer get pasted into
/// every generated script. Constructing CalangoLog also installs the warning
/// routing (Python warnings from ASE, PyTorch, SciPy, GPAW … go to
/// warnings.log instead of stdout, keeping the Results "Log" tab readable).
///
/// The instance keeps its historical `_calango_log` name so every existing
/// call site (`_calango_log.metric(...)`) is unaffected.
constexpr const char* kJsonLoggerHelper =
    "# Structured job logging. calango_log.py is staged next to this script;\n"
    "# it writes metrics.json / log.json (read live by Calango's Results\n"
    "# panel) and routes Python warnings to warnings.log.\n"
    "from calango_log import CalangoLog\n"
    "\n"
    "_calango_log = CalangoLog()\n"
    "\n";

void emitCalculator(std::ostringstream& out, const CalculatorConfig& c)
{
    switch (c.calculator) {
    case CalculatorKind::EMT:
        out << "from ase.calculators.emt import EMT\n"
               "\n"
               "atoms.calc = EMT()\n";
        break;

    case CalculatorKind::LennardJones:
        out << "from ase.calculators.lj import LennardJones\n"
               "\n"
               "atoms.calc = LennardJones()\n";
        break;

    case CalculatorKind::Asap:
        out << "# ASAP — fast C++ EMT / OpenKIM potentials (pip install asap3).\n"
               "from asap3 import EMT\n"
               "\n"
               "atoms.calc = EMT()\n";
        break;

    case CalculatorKind::QuantumEspresso:
        out << "# Quantum ESPRESSO via ASE — requires pw.x and pseudopotentials.\n"
               "# EDIT the profile command and pseudopotential map before running.\n"
               "from ase.calculators.espresso import Espresso, EspressoProfile\n"
               "\n"
               "profile = EspressoProfile(\n"
               "    command=\"mpirun -np 4 pw.x\",          # EDIT ME\n"
               "    pseudo_dir=\"/path/to/pseudopotentials\", # EDIT ME\n"
               ")\n"
               "pseudopotentials = {\n"
               "    # \"Si\": \"Si.pbe-n-rrkjus_psl.1.0.0.UPF\",  # EDIT ME: one entry per element\n"
               "}\n"
               "atoms.calc = Espresso(\n"
               "    profile=profile,\n"
               "    pseudopotentials=pseudopotentials,\n"
               "    input_data={\n"
               "        \"control\": {\"calculation\": \"scf\"},\n"
            << "        \"system\": {\"ecutwfc\": " << c.planeWaveCutoffEv / 13.605693 << "},  # Ry\n"
               "    },\n"
            << "    kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2] << "),\n"
               ")\n";
        break;

    case CalculatorKind::Mace: {
        // A foundation entry point (mace_mp / mace_off) accepts either a size
        // keyword or a path in `model`; passing an explicit checkpoint pins
        // the weights so a run is reproducible even if the cached foundation
        // model is later updated upstream.
        const std::string precision = toString(c.macePrecision);
        const bool pinnedFile = !c.maceModelPath.empty();
        out << "# MACE machine-learning interatomic potential.\n"
               "# Requires:  pip install mace-torch   (in the interpreter running this job)\n";
        if (c.macePrecision == MacePrecision::Float32)
            out << "# float32: ~2x lighter/faster than float64 (especially on GPU),\n"
                   "# at the cost of ~1e-4 eV/A noise in the forces — tighten fmax\n"
                   "# accordingly, or switch to float64 for vibrational work.\n";
        switch (c.maceSource) {
        case MaceModelSource::FoundationMP:
            if (pinnedFile)
                out << "# MACE-MP foundation family, pinned to a local checkpoint.\n";
            else
                out << "# The MACE-MP-0 foundation model is downloaded automatically on\n"
                       "# first use and cached under ~/.cache/mace.\n";
            out << "from mace.calculators import mace_mp\n"
                   "\n"
                << "atoms.calc = mace_mp(model="
                << (pinnedFile ? "r\"" + c.maceModelPath + "\"" : "\"" + c.maceSize + "\"")
                << ", device=\"" << c.maceDevice
                << "\", default_dtype=\"" << precision << "\")\n";
            break;
        case MaceModelSource::FoundationOFF:
            if (pinnedFile)
                out << "# MACE-OFF foundation family, pinned to a local checkpoint.\n";
            else
                out << "# The MACE-OFF foundation model (organic molecules) is downloaded\n"
                       "# automatically on first use and cached under ~/.cache/mace.\n";
            out << "from mace.calculators import mace_off\n"
                   "\n"
                << "atoms.calc = mace_off(model="
                << (pinnedFile ? "r\"" + c.maceModelPath + "\"" : "\"" + c.maceSize + "\"")
                << ", device=\"" << c.maceDevice
                << "\", default_dtype=\"" << precision << "\")\n";
            break;
        case MaceModelSource::CustomFile:
            out << "# User-trained MACE model checkpoint.\n"
                   "from mace.calculators import MACECalculator\n"
                   "\n"
                << "atoms.calc = MACECalculator(model_paths=r\"" << c.maceModelPath
                << "\", device=\"" << c.maceDevice
                << "\", default_dtype=\"" << precision << "\")\n";
            break;
        }
        break;
    }

    case CalculatorKind::Gpaw: {
        out << "# GPAW DFT — requires the gpaw package and its PAW datasets in the\n"
               "# job environment (e.g. conda install -c conda-forge gpaw).\n"
            << AseScriptGenerator::gpawImports(c) << "\n"
               "atoms.calc = GPAW(\n"
            << AseScriptGenerator::gpawKeywordArguments(c, "    ")
            << "    txt=\"gpaw.out\",\n"
               ")\n";
        break;
    }

    case CalculatorKind::Siesta:
        out << "# SIESTA — requires the siesta binary and pseudopotentials\n"
               "# (.psf/.psml) in the job environment. EDIT the two settings below.\n"
               "import os\n"
               "os.environ.setdefault(\"ASE_SIESTA_COMMAND\",\n"
               "                      \"siesta < PREFIX.fdf > PREFIX.out\")  # EDIT ME\n"
               "os.environ.setdefault(\"SIESTA_PP_PATH\", \"/path/to/pseudos\")  # EDIT ME\n"
               "from ase.calculators.siesta import Siesta\n"
               "\n"
               "atoms.calc = Siesta(\n"
               "    label=\"calango\",\n"
               "    xc=\"PBE\",\n"
               "    basis_set=\"DZP\",\n"
            << "    mesh_cutoff=" << c.planeWaveCutoffEv << ",  # eV\n"
            << "    kpts=[" << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2] << "],\n"
               ")\n";
        break;

    case CalculatorKind::Orca: {
        // Simple-input line: method, basis and (optionally) implicit
        // solvation. SMD goes through the %cpcm block instead.
        std::string simpleInput = c.orcaMethod + " " + c.orcaBasis;
        if (c.orcaSolvationModel == "CPCM")
            simpleInput += " CPCM(" + c.orcaSolvent + ")";
        out << "# ORCA quantum chemistry — requires the ORCA binaries\n"
               "# (https://orcaforum.kofo.mpg.de). ASE writes the .inp input\n"
               "# file into the job directory and parses the .out results.\n"
               "from ase.calculators.orca import ORCA, OrcaProfile\n"
               "\n"
               "profile = OrcaProfile(command=\"/path/to/orca\")  # EDIT ME\n"
               "\n"
               "atoms.calc = ORCA(\n"
               "    profile=profile,\n"
               "    directory=\".\",\n"
            << "    charge=" << c.charge << ",\n"
            << "    mult=" << c.multiplicity << ",\n"
            << "    orcasimpleinput=\"" << simpleInput << "\",\n"
               "    orcablocks=\"\"\"%pal nprocs 1 end";
        if (c.orcaSolvationModel == "SMD")
            out << "\n%cpcm smd true SMDsolvent \\\"" << c.orcaSolvent
                << "\\\" end";
        out << "\"\"\",\n"
               ")\n";
        break;
    }

    case CalculatorKind::Vasp:
        out << "# VASP via ASE — requires the VASP_PP_PATH and ASE_VASP_COMMAND\n"
               "# environment variables (see the ASE VASP calculator docs).\n"
               "from ase.calculators.vasp import Vasp\n"
               "\n"
               "atoms.calc = Vasp(\n"
               "    xc=\"PBE\",\n"
            << "    encut=" << c.planeWaveCutoffEv << ",\n"
            << "    kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2] << "),\n"
               "    directory=\".\",\n"
               ")\n";
        break;
    }
}

void emitTask(std::ostringstream& out, const CalculatorConfig& c)
{
    const bool isDft = c.calculator == CalculatorKind::QuantumEspresso
        || c.calculator == CalculatorKind::Vasp
        || c.calculator == CalculatorKind::Gpaw
        || c.calculator == CalculatorKind::Siesta;

    switch (c.task) {
    case TaskKind::SinglePoint:
        // GPAW is fully parameterized above (mode, xc, eigensolver, mixer,
        // convergence dict, maxiter), so it needs no hand-off comment.
        if (isDft && c.calculator != CalculatorKind::Gpaw) {
            // The other DFT calculator blocks are user-completed hooks;
            // surface the electronic-convergence targets the wizard collected
            // so they can be wired into the backend (nelm/ediff,
            // electron_maxstep/conv_thr, ...).
            out << "# Electronic convergence targets (apply in the calculator block above):\n"
                << "#   max SCF iterations : " << c.scfMaxSteps << "\n"
                << "#   energy tolerance   : " << c.scfEnergyTolEv << " eV\n"
                << "#   spin polarization  : "
                << (c.spinPolarized ? "on" : "off") << "\n"
                << "#   smearing           : " << toString(c.smearing)
                << " (width " << c.smearingWidthEv << " eV)\n";
        }
        out << "energy = atoms.get_potential_energy()\n"
               "fmax = abs(atoms.get_forces()).max()\n"
               "_calango_log.metric(0, energy=energy, max_force=fmax)\n"
               "print(f\"CALANGO_RESULT energy_eV={energy:.6f}\", flush=True)\n"
               "print(f\"CALANGO_RESULT fmax_eV_per_A={fmax:.6f}\", flush=True)\n";
        if (c.calculator == CalculatorKind::Gpaw) {
            // Save the converged charge density so a later Electronic Structure
            // run can load it and evaluate bands/PDOS non-self-consistently
            // (mode="all" writes the density + wavefunctions). This file is what
            // the Electronic Structure wizard's baseline selector looks for.
            out << "atoms.calc.write(\"single_point.gpw\", mode=\"all\")\n"
                   "print(\"CALANGO_RESULT density_file=single_point.gpw\", "
                   "flush=True)\n";
        }
        break;

    case TaskKind::GeometryOptimization: {
        const std::string opt = toString(c.optimizer);
        out << "from ase.optimize import " << opt << "\n";
        if (c.relaxCell) {
            const char* filter = c.cellFilter == CellFilter::UnitCell
                ? "UnitCellFilter"
                : "FrechetCellFilter";
            out << "try:\n"
                << "    from ase.filters import " << filter << " as _CellFilter\n"
                   "except ImportError:  # ASE < 3.23\n"
                   "    from ase.constraints import UnitCellFilter as _CellFilter\n";
        }
        out << "\n"
            << "max_steps = " << c.maxSteps << "\n";
        if (c.relaxCell) {
            out << "# Variable-cell relaxation: relax atomic positions AND the\n"
                   "# unit cell.\n";
            if (c.cellCustomMask) {
                // Voigt-order mask [xx, yy, zz, yz, xz, xy]: 1 = relax.
                out << "_target = _CellFilter(atoms, mask=[";
                for (int i = 0; i < 6; ++i)
                    out << (c.cellMask[i] ? "1" : "0") << (i < 5 ? ", " : "");
                out << "])\n";
            } else {
                out << "_target = _CellFilter(atoms, hydrostatic_strain="
                    << (c.cellHydrostatic ? "True" : "False") << ")\n";
            }
        }
        // The trajectory is written by an observer rather than the
        // optimizer's own `trajectory=` argument: that writer always records
        // step 0, the raw input geometry, whose forces are not yet the
        // relaxation's own and which carries no velocities at all. Scrubbing
        // onto it showed an empty vector overlay between frames that had one.
        // Every recorded frame now comes from an evaluated step.
        out << "opt = " << opt << "("
            << (c.relaxCell ? "_target" : "atoms")
            << ", logfile=\"-\")\n"
               "\n"
               "from ase.io.trajectory import Trajectory\n"
               "\n"
               "_opt_traj = Trajectory(\"opt.traj\", \"w\", atoms)\n"
               "\n"
            << kStreamFrameHelper
            << "def _report():\n"
               "    if opt.nsteps == 0:\n"
               "        return  # skip the unevaluated input geometry\n"
               "    _calango_log.progress(opt.nsteps, max_steps)\n"
               "    energy = atoms.get_potential_energy()\n"
               "    fmax_now = abs(atoms.get_forces()).max()\n"
               "    _calango_log.metric(opt.nsteps, energy=energy, max_force=fmax_now)\n"
               "    _opt_traj.write()\n"
               "    _stream_frame()\n"
               "\n"
               "opt.attach(_report)\n"
            << "converged = opt.run(fmax=" << c.fmax << ", steps=max_steps)\n"
               "\n"
               "_opt_traj.close()\n"
               "write(\"optimized.extxyz\", atoms)\n"
               "energy = atoms.get_potential_energy()\n"
               "print(f\"CALANGO_RESULT converged={converged} energy_eV={energy:.6f}\", flush=True)\n";
        break;
    }

    case TaskKind::MolecularDynamics:
        out << "from ase import units\n"
               "from ase.md.velocitydistribution import (MaxwellBoltzmannDistribution,\n"
               "                                         Stationary, ZeroRotation)\n"
               "\n"
            << "temperature_K = " << c.temperatureK << "\n"
            << "md_steps = " << c.mdSteps << "\n"
            << "sample_interval = "
            << (c.mdSampleInterval > 0 ? std::to_string(c.mdSampleInterval)
                                       : std::string("max(1, md_steps // 400)"))
            << "  # record every N steps\n"
               "\n"
               "MaxwellBoltzmannDistribution(atoms, temperature_K=temperature_K)\n"
               "# Remove the net center-of-mass momentum the random velocities\n"
               "# carry, so the system does not drift as a whole during MD.\n"
               "Stationary(atoms)\n"
               "if not atoms.pbc.any():\n"
               "    # Isolated system: also remove net angular momentum.\n"
               "    ZeroRotation(atoms)\n";

        switch (c.ensemble) {
        case MdEnsemble::VelocityVerletNVE:
            out << "from ase.md.verlet import VelocityVerlet\n"
                   "\n"
                   "dyn = VelocityVerlet(\n"
                   "    atoms,\n"
                << "    timestep=" << c.timestepFs << " * units.fs,\n"
    
                   ")\n";
            break;
        case MdEnsemble::LangevinNVT:
            out << "from ase.md.langevin import Langevin\n"
                   "\n"
                   "dyn = Langevin(\n"
                   "    atoms,\n"
                << "    timestep=" << c.timestepFs << " * units.fs,\n"
                   "    temperature_K=temperature_K,\n"
                << "    friction=" << c.frictionPerFs << " / units.fs,\n"
    
                   ")\n";
            break;
        case MdEnsemble::AndersenNVT:
            out << "from ase.md.andersen import Andersen\n"
                   "\n"
                   "dyn = Andersen(\n"
                   "    atoms,\n"
                << "    timestep=" << c.timestepFs << " * units.fs,\n"
                   "    temperature_K=temperature_K,\n"
                << "    andersen_prob=" << c.andersenProb << ",\n"
    
                   ")\n";
            break;
        case MdEnsemble::BerendsenNVT:
            out << "from ase.md.nvtberendsen import NVTBerendsen\n"
                   "\n"
                   "dyn = NVTBerendsen(\n"
                   "    atoms,\n"
                << "    timestep=" << c.timestepFs << " * units.fs,\n"
                   "    temperature_K=temperature_K,\n"
                << "    taut=" << c.tautFs << " * units.fs,\n"
    
                   ")\n";
            break;
        case MdEnsemble::NoseHooverChainNVT:
            out << "from ase.md.nose_hoover_chain import NoseHooverChainNVT\n"
                   "\n"
                   "dyn = NoseHooverChainNVT(\n"
                   "    atoms,\n"
                << "    timestep=" << c.timestepFs << " * units.fs,\n"
                   "    temperature_K=temperature_K,\n"
                << "    tdamp=" << c.tautFs << " * units.fs,\n"
    
                   ")\n";
            break;
        case MdEnsemble::BerendsenNPT:
            out << "from ase.md.nptberendsen import NPTBerendsen\n"
                   "\n"
                   "# EDIT ME: compressibility_au below is water-like; use your\n"
                   "# material's isothermal compressibility for meaningful cell\n"
                   "# dynamics.\n"
                   "dyn = NPTBerendsen(\n"
                   "    atoms,\n"
                << "    timestep=" << c.timestepFs << " * units.fs,\n"
                   "    temperature_K=temperature_K,\n"
                << "    taut=" << c.tautFs << " * units.fs,\n"
                << "    pressure_au=" << c.pressureGPa << " * units.GPa,\n"
                << "    taup=" << c.taupFs << " * units.fs,\n"
                   "    compressibility_au=4.57e-5 / units.bar,\n"
    
                   ")\n";
            break;
        case MdEnsemble::MelchionnaNPT:
            out << "from ase.md.npt import NPT\n"
                   "\n"
                   "# Nosé-Hoover thermostat + Parrinello-Rahman barostat\n"
                   "# (Melchionna). Requires an upper-triangular cell.\n"
                   "# EDIT ME: pfactor = ptime² · bulk modulus — 100 GPa below is\n"
                   "# a solid-like placeholder.\n"
                   "atoms.set_cell(atoms.cell.standard_form()[0], scale_atoms=True)\n"
                   "dyn = NPT(\n"
                   "    atoms,\n"
                << "    timestep=" << c.timestepFs << " * units.fs,\n"
                   "    temperature_K=temperature_K,\n"
                << "    externalstress=" << c.pressureGPa << " * units.GPa,\n"
                << "    ttime=" << c.tautFs << " * units.fs,\n"
                << "    pfactor=(" << c.taupFs << " * units.fs) ** 2 * 100 * units.GPa,\n"
    
                   ")\n";
            break;
        }

        // Live viewport trajectory: stream a frame every few MD steps
        // (capped at ~400 streamed frames per run). The t=0 frame is already
        // seeded on the C++ side from the starting structure, so the observer
        // must NOT emit its own t=0 frame — ASE calls attached observers once
        // at nsteps==0 (before the first step) and again after every step, so
        // we guard on dyn.nsteps > 0. An N-step run then yields exactly N+1
        // frames (seed at t=0 plus one per integrated step).
        // md.traj is written by an observer instead of the integrator's
        // `trajectory=` argument: that writer always records step 0 — the raw
        // input geometry, before any force evaluation or thermalization —
        // which is exactly the frame that showed an empty vector overlay when
        // scrubbing.
        out << "\n"
               "from ase.io.trajectory import Trajectory\n"
               "\n"
               "_md_traj = Trajectory(\"md.traj\", \"w\", atoms)\n"
               "dyn.attach(lambda: _md_traj.write() if dyn.nsteps > 0 else None,\n"
               "           interval=sample_interval)\n"
               "\n"
            << kStreamFrameHelper
            << "dyn.attach(lambda: _stream_frame() if dyn.nsteps > 0 else None,\n"
               "           interval=sample_interval)\n"
               "\n"
               "# Extended-XYZ trajectory alongside md.traj. ASE's binary .traj\n"
               "# already stores forces and momenta, but .extxyz is the format\n"
               "# that is portable, diff-able and readable by other tools — and\n"
               "# it is what carries the per-atom vectors the viewport's Vector\n"
               "# overlay draws. Forces and velocities are written explicitly on\n"
               "# every dump so they are present in every frame, not only where\n"
               "# a calculator happened to leave results attached.\n"
               "from ase.calculators.singlepoint import SinglePointCalculator\n"
               "\n"
               "_md_xyz = \"md.extxyz\"\n"
               "open(_md_xyz, \"w\").close()  # truncate any previous run\n"
               "\n"
               "def _dump_extxyz():\n"
               "    snapshot = atoms.copy()\n"
               "    snapshot.set_velocities(atoms.get_velocities())\n"
               "    try:\n"
               "        # A SinglePointCalculator is how ASE round-trips computed\n"
               "        # properties through extended XYZ; assigning to\n"
               "        # snapshot.arrays would not survive the writer.\n"
               "        snapshot.calc = SinglePointCalculator(\n"
               "            snapshot, energy=atoms.get_potential_energy(),\n"
               "            forces=atoms.get_forces())\n"
               "    except Exception as error:\n"
               "        _calango_log.event(\"warning\",\n"
               "                           f\"no forces for the extxyz dump: {error}\")\n"
               "    write(_md_xyz, snapshot, format=\"extxyz\", append=True)\n"
               "\n"
               "# No t = 0 dump: the raw input geometry has no evaluated\n"
               "# forces of its own and no thermalized velocities yet, so it\n"
               "# would be the one frame in the trajectory without a vector\n"
               "# overlay. Recording starts at the first integrated step.\n"
               "dyn.attach(lambda: _dump_extxyz() if dyn.nsteps > 0 else None,\n"
               "           interval=sample_interval)\n";

        if (isConstantTemperature(c.ensemble))
            out << "\n"
                   "# Thermostat target — drives the dashed reference line in the\n"
                   "# Temperature tab (omitted for NVE).\n"
                   "print(f\"CALANGO_TARGET_TEMP {temperature_K}\", flush=True)\n";

        if (isConstantPressure(c.ensemble))
            out << "\n"
                   "# Barostat target — drives the dashed reference line in the\n"
                   "# Pressure tab (omitted for constant-volume ensembles).\n"
                << "print(f\"CALANGO_TARGET_PRESSURE " << c.pressureGPa
                << "\", flush=True)\n";

        out << "\n"
               "def _report():\n"
               "    epot = atoms.get_potential_energy()\n"
               "    ekin = atoms.get_kinetic_energy()\n"
               "    temp = atoms.get_temperature()\n"
               "    fmax_now = abs(atoms.get_forces()).max()\n"
               "    _calango_log.progress(dyn.nsteps, md_steps)\n";

        if (isConstantPressure(c.ensemble))
            out << "    # Scalar pressure P = -tr(σ)/3 from the full stress tensor\n"
                   "    # (eV/Å³ → GPa); only meaningful with a barostatted cell.\n"
                   "    stress = atoms.get_stress(voigt=True)\n"
                   "    pressure_GPa = -(stress[0] + stress[1] + stress[2]) / 3.0 / units.GPa\n"
                   "    _calango_log.metric(dyn.nsteps, energy=epot, temperature=temp,\n"
                   "                        max_force=fmax_now, pressure=pressure_GPa)\n";
        else
            out << "    _calango_log.metric(dyn.nsteps, energy=epot, temperature=temp,\n"
                   "                        max_force=fmax_now)\n";

        out << "    print(f\"CALANGO_MD step={dyn.nsteps} epot_eV={epot:.4f} ekin_eV={ekin:.4f}"
               " T_K={temp:.1f}\", flush=True)\n"
               "\n"
               "dyn.attach(_report, interval=sample_interval)\n"
               "dyn.run(md_steps)\n"
               "\n"
               "_md_traj.close()\n"
               "write(\"md_final.extxyz\", atoms)\n"
               "print(f\"CALANGO_RESULT epot_eV={atoms.get_potential_energy():.6f}\", flush=True)\n";
        break;
    }
}

} // namespace

std::string AseScriptGenerator::gpawImports(const CalculatorConfig& c)
{
    // Only what the chosen mode needs, so an unavailable symbol never breaks
    // an unrelated run.
    std::string line = "from gpaw import GPAW";
    if (c.gpawMode == GpawMode::PlaneWave)
        line += ", PW";
    line += ", " + toString(c.gpawMixer);
    return line + "\n";
}

std::string AseScriptGenerator::gpawKeywordArguments(const CalculatorConfig& c,
                                                     const std::string& indent)
{
    std::ostringstream out;
    switch (c.gpawMode) {
    case GpawMode::PlaneWave:
        out << indent << "mode=PW(" << c.planeWaveCutoffEv
            << "),  # plane-wave cutoff, eV\n";
        break;
    case GpawMode::FiniteDifference:
        // In FD mode the real-space grid spacing h replaces the cutoff.
        out << indent << "mode=\"fd\",\n"
            << indent << "h=" << c.gpawGridSpacing
            << ",  # real-space grid spacing, Ang\n";
        break;
    case GpawMode::Lcao:
        out << indent << "mode=\"lcao\",\n"
            << indent << "basis=\"" << c.gpawBasis << "\",\n";
        break;
    }
    out << indent << "xc=\"" << c.gpawXc << "\",\n"
        << indent << "kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", "
        << c.kpts[2] << "),  # Monkhorst-Pack grid\n"
        << indent << "eigensolver=\"" << toString(c.gpawEigensolver) << "\",\n"
        // Mixer(beta, nmaxold, weight) — GPAW's positional signature.
        << indent << "mixer=" << toString(c.gpawMixer) << "(" << c.gpawMixerBeta
        << ", " << c.gpawMixerNmaxold << ", " << c.gpawMixerWeight << "),\n"
        << indent << "convergence={\n"
        << indent << "    \"energy\": " << c.scfEnergyTolEv
        << ",       # eV/electron\n"
        << indent << "    \"eigenstates\": " << c.gpawConvEigenstates
        << ",  # eV^2/electron\n"
        << indent << "    \"density\": " << c.gpawConvDensity
        << ",      # electrons/valence electron\n"
        << indent << "},\n"
        << indent << "maxiter=" << c.scfMaxSteps << ",\n";
    if (c.spinPolarized)
        out << indent << "spinpol=True,\n";
    if (c.smearing != SmearingMethod::None) {
        // GPAW exposes only Fermi-Dirac / Marzari-Vanderbilt broadening; the
        // Gaussian and Methfessel-Paxton choices in the shared smearing combo
        // have no GPAW equivalent, so they map onto Fermi-Dirac and say so.
        out << indent << "occupations={\"name\": \"fermi-dirac\", \"width\": "
            << c.smearingWidthEv << "},\n";
        if (c.smearing != SmearingMethod::FermiDirac)
            out << indent << "# (" << toString(c.smearing)
                << " has no GPAW equivalent — using Fermi-Dirac at the same "
                   "width.)\n";
    } else {
        out << indent
            << "occupations={\"name\": \"fermi-dirac\", \"width\": 0.0},\n";
    }
    return out.str();
}

std::string AseScriptGenerator::calculatorSnippet(const CalculatorConfig& config)
{
    std::ostringstream out;
    emitCalculator(out, config);
    return out.str();
}

std::string AseScriptGenerator::jsonLoggerPreamble()
{
    return kJsonLoggerHelper;
}

std::string AseScriptGenerator::loggerModuleSource()
{
    return generated::kCalangoLogModule;
}

const char* AseScriptGenerator::loggerModuleFileName()
{
    return "calango_log.py";
}

std::string AseScriptGenerator::generate(const CalculatorConfig& config,
                                         const std::string& structureFile)
{
    std::ostringstream out;
    out << "#!/usr/bin/env python3\n"
           "# Generated by Calango " << CALANGO_VERSION << " — "
        << toString(config.task) << " with " << toString(config.calculator) << ".\n"
           "# This is a plain ASE script: edit it freely or run it standalone.\n"
           "\n"
           "from ase.io import read, write\n"
           "\n"
        << kJsonLoggerHelper
        << "atoms = read(r\"" << structureFile << "\")\n"
        << "print(f\"CALANGO_INFO natoms={len(atoms)}\", flush=True)\n"
           "\n";
    if (config.spinPolarized) {
        out << "# Spin polarization: seed every atom with an initial magnetic\n"
               "# moment so the SCF can find a magnetic solution.\n"
            << "atoms.set_initial_magnetic_moments([" << config.initialMagMoment
            << "] * len(atoms))\n\n";
    }
    emitCalculator(out, config);
    out << "\n";
    emitTask(out, config);
    out << "\nprint(\"CALANGO_DONE\", flush=True)\n";
    return out.str();
}

} // namespace calango::core
