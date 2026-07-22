#include "core/AseScriptGenerator.hpp"

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
    "    lines.append(f\"CALANGO_FRAME {len(atoms)}\")\n"
    "    for s, p in zip(atoms.get_chemical_symbols(), atoms.get_positions()):\n"
    "        lines.append(f\"{s} {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}\")\n"
    "    _sys.stdout.write(\"\\n\".join(lines) + \"\\n\")\n"
    "    _sys.stdout.flush()\n"
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

    case CalculatorKind::Mace:
        out << "# MACE machine-learning interatomic potential.\n"
               "# Requires:  pip install mace-torch   (in the interpreter running this job)\n";
        switch (c.maceSource) {
        case MaceModelSource::FoundationMP:
            out << "# The MACE-MP-0 foundation model is downloaded automatically on\n"
                   "# first use and cached under ~/.cache/mace.\n"
                   "from mace.calculators import mace_mp\n"
                   "\n"
                << "atoms.calc = mace_mp(model=\"" << c.maceSize
                << "\", device=\"" << c.maceDevice
                << "\", default_dtype=\"float64\")\n";
            break;
        case MaceModelSource::FoundationOFF:
            out << "# The MACE-OFF foundation model (organic molecules) is downloaded\n"
                   "# automatically on first use and cached under ~/.cache/mace.\n"
                   "from mace.calculators import mace_off\n"
                   "\n"
                << "atoms.calc = mace_off(model=\"" << c.maceSize
                << "\", device=\"" << c.maceDevice
                << "\", default_dtype=\"float64\")\n";
            break;
        case MaceModelSource::CustomFile:
            out << "# User-trained MACE model checkpoint.\n"
                   "from mace.calculators import MACECalculator\n"
                   "\n"
                << "atoms.calc = MACECalculator(model_paths=r\"" << c.maceModelPath
                << "\", device=\"" << c.maceDevice
                << "\", default_dtype=\"float64\")\n";
            break;
        }
        break;

    case CalculatorKind::Gpaw:
        out << "# GPAW DFT — requires the gpaw package and its PAW datasets in the\n"
               "# job environment (e.g. conda install -c conda-forge gpaw).\n"
               "from gpaw import GPAW, PW\n"
               "\n"
               "atoms.calc = GPAW(\n"
            << "    mode=PW(" << c.planeWaveCutoffEv << "),  # eV\n"
               "    xc=\"PBE\",\n"
            << "    kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2] << "),\n"
               "    txt=\"gpaw.out\",\n"
               ")\n";
        break;

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
        if (isDft) {
            // The DFT calculator blocks are user-completed hooks; surface the
            // electronic-convergence targets the dialog collected so they can
            // be wired into the backend (nelm/ediff, electron_maxstep/conv_thr,
            // convergence={'energy': ...}, ...).
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
               "print(f\"CALANGO_RESULT energy_eV={energy:.6f}\", flush=True)\n"
               "print(f\"CALANGO_RESULT fmax_eV_per_A={fmax:.6f}\", flush=True)\n";
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
        out << "opt = " << opt << "("
            << (c.relaxCell ? "_target" : "atoms")
            << ", trajectory=\"opt.traj\", logfile=\"-\")\n"
               "\n"
            << kStreamFrameHelper
            << "def _report():\n"
               "    print(f\"CALANGO_PROGRESS {opt.nsteps} {max_steps}\", flush=True)\n"
               "    energy = atoms.get_potential_energy()\n"
               "    fmax_now = abs(atoms.get_forces()).max()\n"
               "    print(f\"CALANGO_ENERGY {opt.nsteps} {energy:.6f}\", flush=True)\n"
               "    print(f\"CALANGO_FMAX {opt.nsteps} {fmax_now:.6f}\", flush=True)\n"
               "    _stream_frame()\n"
               "\n"
               "_stream_frame()\n"
               "opt.attach(_report)\n"
            << "converged = opt.run(fmax=" << c.fmax << ", steps=max_steps)\n"
               "\n"
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
                   "    trajectory=\"md.traj\",\n"
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
                   "    trajectory=\"md.traj\",\n"
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
                   "    trajectory=\"md.traj\",\n"
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
                   "    trajectory=\"md.traj\",\n"
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
                   "    trajectory=\"md.traj\",\n"
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
                   "    trajectory=\"md.traj\",\n"
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
                   "    trajectory=\"md.traj\",\n"
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
        out << "\n"
            << kStreamFrameHelper
            << "dyn.attach(lambda: _stream_frame() if dyn.nsteps > 0 else None,\n"
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
               "    print(f\"CALANGO_PROGRESS {dyn.nsteps} {md_steps}\", flush=True)\n"
               "    print(f\"CALANGO_ENERGY {dyn.nsteps} {epot:.6f}\", flush=True)\n"
               "    print(f\"CALANGO_TEMP {dyn.nsteps} {temp:.2f}\", flush=True)\n"
               "    print(f\"CALANGO_FMAX {dyn.nsteps} {fmax_now:.6f}\", flush=True)\n";

        if (isConstantPressure(c.ensemble))
            out << "    # Scalar pressure P = -tr(σ)/3 from the full stress tensor\n"
                   "    # (eV/Å³ → GPa); only meaningful with a barostatted cell.\n"
                   "    stress = atoms.get_stress(voigt=True)\n"
                   "    pressure_GPa = -(stress[0] + stress[1] + stress[2]) / 3.0 / units.GPa\n"
                   "    print(f\"CALANGO_PRESSURE {dyn.nsteps} {pressure_GPa:.6f}\", flush=True)\n";

        out << "    print(f\"CALANGO_MD step={dyn.nsteps} epot_eV={epot:.4f} ekin_eV={ekin:.4f}"
               " T_K={temp:.1f}\", flush=True)\n"
               "\n"
               "dyn.attach(_report, interval=sample_interval)\n"
               "dyn.run(md_steps)\n"
               "\n"
               "write(\"md_final.extxyz\", atoms)\n"
               "print(f\"CALANGO_RESULT epot_eV={atoms.get_potential_energy():.6f}\", flush=True)\n";
        break;
    }
}

} // namespace

std::string AseScriptGenerator::calculatorSnippet(const CalculatorConfig& config)
{
    std::ostringstream out;
    emitCalculator(out, config);
    return out.str();
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
