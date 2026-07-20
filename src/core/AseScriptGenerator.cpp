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

namespace {

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
    switch (c.task) {
    case TaskKind::SinglePoint:
        out << "energy = atoms.get_potential_energy()\n"
               "fmax = abs(atoms.get_forces()).max()\n"
               "print(f\"CALANGO_RESULT energy_eV={energy:.6f}\", flush=True)\n"
               "print(f\"CALANGO_RESULT fmax_eV_per_A={fmax:.6f}\", flush=True)\n";
        break;

    case TaskKind::GeometryOptimization:
        out << "from ase.optimize import BFGS\n"
               "\n"
            << "max_steps = " << c.maxSteps << "\n"
               "opt = BFGS(atoms, trajectory=\"opt.traj\", logfile=\"-\")\n"
               "\n"
               "def _report():\n"
               "    print(f\"CALANGO_PROGRESS {opt.nsteps} {max_steps}\", flush=True)\n"
               "    energy = atoms.get_potential_energy()\n"
               "    print(f\"CALANGO_ENERGY {opt.nsteps} {energy:.6f}\", flush=True)\n"
               "\n"
               "opt.attach(_report)\n"
            << "converged = opt.run(fmax=" << c.fmax << ", steps=max_steps)\n"
               "\n"
               "write(\"optimized.extxyz\", atoms)\n"
               "energy = atoms.get_potential_energy()\n"
               "print(f\"CALANGO_RESULT converged={converged} energy_eV={energy:.6f}\", flush=True)\n";
        break;

    case TaskKind::MolecularDynamics:
        out << "from ase import units\n"
               "from ase.md.velocitydistribution import MaxwellBoltzmannDistribution\n"
               "\n"
            << "temperature_K = " << c.temperatureK << "\n"
            << "md_steps = " << c.mdSteps << "\n"
               "\n"
               "MaxwellBoltzmannDistribution(atoms, temperature_K=temperature_K)\n";
        if (c.ensemble == MdEnsemble::LangevinNVT) {
            out << "from ase.md.langevin import Langevin\n"
                   "\n"
                   "dyn = Langevin(\n"
                   "    atoms,\n"
                << "    timestep=" << c.timestepFs << " * units.fs,\n"
                   "    temperature_K=temperature_K,\n"
                   "    friction=0.02,\n"
                   "    trajectory=\"md.traj\",\n"
                   ")\n";
        } else {
            out << "from ase.md.verlet import VelocityVerlet\n"
                   "\n"
                   "dyn = VelocityVerlet(\n"
                   "    atoms,\n"
                << "    timestep=" << c.timestepFs << " * units.fs,\n"
                   "    trajectory=\"md.traj\",\n"
                   ")\n";
        }
        out << "\n"
               "def _report():\n"
               "    epot = atoms.get_potential_energy()\n"
               "    ekin = atoms.get_kinetic_energy()\n"
               "    print(f\"CALANGO_PROGRESS {dyn.nsteps} {md_steps}\", flush=True)\n"
               "    print(f\"CALANGO_ENERGY {dyn.nsteps} {epot:.6f}\", flush=True)\n"
               "    print(f\"CALANGO_MD step={dyn.nsteps} epot_eV={epot:.4f} ekin_eV={ekin:.4f}\","
               " flush=True)\n"
               "\n"
               "dyn.attach(_report, interval=10)\n"
               "dyn.run(md_steps)\n"
               "\n"
               "write(\"md_final.extxyz\", atoms)\n"
               "print(f\"CALANGO_RESULT epot_eV={atoms.get_potential_energy():.6f}\", flush=True)\n";
        break;
    }
}

} // namespace

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
    emitCalculator(out, config);
    out << "\n";
    emitTask(out, config);
    out << "\nprint(\"CALANGO_DONE\", flush=True)\n";
    return out.str();
}

} // namespace calango::core
