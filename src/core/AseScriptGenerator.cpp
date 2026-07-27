#include "core/AseScriptGenerator.hpp"

#include "core/CalangoLogModule.hpp" // generated from calango_log.py by CMake

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

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
    case CalculatorKind::DeepMd: return "DeepMD-kit";
    case CalculatorKind::NequIp: return "NequIP";
    case CalculatorKind::Allegro: return "Allegro";
    case CalculatorKind::ChgNet: return "CHGNet";
    case CalculatorKind::MatterSim: return "MatterSim";
    case CalculatorKind::FairChem: return "FAIRChem";
    }
    return "?";
}

std::string toString(MlipDevice device)
{
    switch (device) {
    case MlipDevice::Cpu: return "cpu";
    case MlipDevice::Cuda: return "cuda";
    case MlipDevice::Mps: return "mps";
    }
    return "cpu";
}

std::string toString(ChgNetWeights weights)
{
    switch (weights) {
    case ChgNetWeights::V0_3_0: return "0.3.0";
    case ChgNetWeights::Latest: return "latest";
    }
    return "0.3.0";
}

std::string toString(MatterSimModel model)
{
    switch (model) {
    case MatterSimModel::M3: return "MatterSim-v1.0.0-1M.pth";
    case MatterSimModel::M100: return "MatterSim-v1.0.0-5M.pth";
    }
    return "MatterSim-v1.0.0-1M.pth";
}

std::string toString(FairChemModel model)
{
    switch (model) {
    case FairChemModel::EquiformerV2: return "EquiformerV2";
    case FairChemModel::EScn: return "eSCN";
    }
    return "EquiformerV2";
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

/// Frozen degrees of freedom, emitted as an ASE constraint list bound to
/// `atoms` before the optimizer (or its cell filter) ever sees them.
///
/// An index rule becomes a literal list; a REGION rule becomes a comprehension
/// over `atoms.get_positions()` evaluated at run time rather than a baked-in
/// index list. That is the difference that matters when the same script is
/// re-run on a re-cleaved slab or a supercell: bounds still mean "the bottom
/// two layers", a stale index list means "whatever atoms 0..23 happen to be".
///
/// All three directions frozen is FixAtoms; a partial mask is FixCartesian,
/// whose mask entries are true for the coordinates that are HELD.
void emitConstraints(std::ostringstream& out, const CalculatorConfig& c)
{
    // A rule that freezes nothing is a no-op the user disabled in the dialog;
    // dropping it here keeps it out of the script instead of emitting a
    // constraint with an all-false mask.
    std::vector<const GeometryConstraint*> active;
    for (const GeometryConstraint& constraint : c.constraints) {
        if (!constraint.fixesAnyDirection())
            continue;
        if (constraint.selection == GeometryConstraint::Selection::Indices
            && constraint.indices.empty())
            continue;
        active.push_back(&constraint);
    }
    if (active.empty())
        return;

    const char* axisNames = "xyz";
    out << "\n"
           "# --- Geometry constraints ----------------------------------------\n"
           "# Frozen degrees of freedom. FixAtoms holds an atom entirely;\n"
           "# FixCartesian holds only the marked directions (True = held).\n"
           "from ase.constraints import FixAtoms, FixCartesian\n"
           "\n"
           "_constraints = []\n";

    int region = 0;
    for (const GeometryConstraint* constraint : active) {
        std::string selectionExpr;
        if (constraint->selection == GeometryConstraint::Selection::Indices) {
            std::ostringstream list;
            list << "[";
            for (std::size_t i = 0; i < constraint->indices.size(); ++i)
                list << (i ? ", " : "") << constraint->indices[i];
            list << "]";
            selectionExpr = list.str();
        } else {
            const int axis = std::clamp(constraint->axis, 0, 2);
            const char component = axisNames[axis];
            std::ostringstream test;
            if (constraint->hasMin)
                test << "_p[" << axis << "] > " << constraint->minValue;
            if (constraint->hasMin && constraint->hasMax)
                test << " and ";
            if (constraint->hasMax)
                test << "_p[" << axis << "] < " << constraint->maxValue;
            const std::string name = "_region" + std::to_string(region++);
            out << "# Every atom with ";
            if (constraint->hasMin)
                out << component << " > " << constraint->minValue;
            if (constraint->hasMin && constraint->hasMax)
                out << " and ";
            if (constraint->hasMax)
                out << component << " < " << constraint->maxValue;
            if (!constraint->hasMin && !constraint->hasMax)
                out << "any position (unbounded region)";
            out << " Å, re-evaluated against the geometry actually read.\n"
                << name << " = [_i for _i, _p in enumerate(atoms.get_positions())"
                << (test.str().empty() ? "]\n" : "\n         if " + test.str() + "]\n")
                << "print(f\"CALANGO_INFO constrained atoms: {len(" << name
                << ")}\", flush=True)\n";
            selectionExpr = name;
        }

        if (constraint->fixesAllDirections()) {
            out << "_constraints.append(FixAtoms(indices=" << selectionExpr
                << "))\n";
        } else {
            out << "_constraints.append(FixCartesian(" << selectionExpr
                << ", mask=(" << (constraint->fix[0] ? "True" : "False") << ", "
                << (constraint->fix[1] ? "True" : "False") << ", "
                << (constraint->fix[2] ? "True" : "False") << ")))\n";
        }
    }
    out << "atoms.set_constraint(_constraints)\n"
           "\n";
}

/// Wrap whatever calculator was just bound to `atoms.calc` in ASE's DFTD4,
/// adding Grimme's D4 dispersion energy and forces on top of it.
///
/// This runs AFTER the calculator block rather than being folded into each
/// one: every branch above ends by assigning `atoms.calc`, so wrapping the
/// result once covers all of them and cannot fall out of step with a
/// calculator added later.
void emitDispersion(std::ostringstream& out, const CalculatorConfig& c)
{
    if (!c.dispersionD4)
        return;
    // The damping parameters are fitted per functional, so D4 has to be told
    // which one it is correcting. Following the calculator's own xc keeps the
    // two from silently disagreeing.
    std::string method = c.dispersionD4Method;
    if (method.empty())
        method = c.calculator == CalculatorKind::Gpaw ? c.gpawXc : "PBE";
    out << "\n"
           "# --- van der Waals dispersion (Grimme D4) -------------------------\n"
           "# Semilocal functionals carry no long-range correlation, so layered\n"
           "# and molecular systems come out under-bound without this. D4 is\n"
           "# charge-dependent, which is what separates it from D3.\n"
           "# Requires:  pip install dftd4   (or conda install -c conda-forge dftd4)\n"
           "from ase.calculators.dftd4 import DFTD4\n"
           "\n"
        << "atoms.calc = DFTD4(method=\"" << method
        << "\", calc=atoms.calc)\n";
}

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

    // -- Machine-learning interatomic potentials ---------------------------
    // Each block is a plain ASE calculator construction: the user edits the
    // model path in the wizard, and the script stays runnable standalone.

    case CalculatorKind::DeepMd:
        out << "# DeepMD-kit — deep potential from a frozen model graph.\n"
               "# Requires:  pip install deepmd-kit   (TensorFlow or PyTorch backend)\n"
               "from deepmd.calculator import DP\n"
               "\n"
            << "atoms.calc = DP(model=r\"" << c.deepmdModelPath << "\")\n";
        if (c.deepmdModelPath.empty())
            out << "# EDIT ME: no model file was selected — DP() needs a frozen\n"
                   "# .pb (TensorFlow) or .pth (PyTorch) deep-potential graph.\n";
        break;

    case CalculatorKind::NequIp:
    case CalculatorKind::Allegro: {
        const bool allegro = c.calculator == CalculatorKind::Allegro;
        out << (allegro
                    ? "# Allegro — strictly-local equivariant potential (NequIP\n"
                      "# framework), loaded from a deployed TorchScript model.\n"
                    : "# NequIP — E(3)-equivariant message-passing potential,\n"
                      "# loaded from a deployed TorchScript model.\n")
            << "# Requires:  pip install nequip"
            << (allegro ? " mir-allegro\n" : "\n")
            << "# The model must be DEPLOYED (`nequip-deploy build ...`), not a\n"
               "# raw training checkpoint.\n"
               "from nequip.ase import NequIPCalculator\n"
               "\n"
               "atoms.calc = NequIPCalculator.from_deployed_model(\n"
            << "    model_path=r\"" << c.nequipModelPath << "\",\n"
            << "    device=\"" << toString(c.mlipDevice) << "\",\n"
               "    # The deployed model carries its own training units; these\n"
               "    # rescale them to ASE's eV / Angstrom.\n"
            << "    energy_units_to_eV=" << (c.nequipEnergyUnits == "eV" ? "1.0" : "None")
            << ",  # " << c.nequipEnergyUnits << "\n"
            << "    length_units_to_A=" << (c.nequipLengthUnits == "Angstrom" ? "1.0" : "None")
            << ",  # " << c.nequipLengthUnits << "\n"
               ")\n";
        if (c.nequipEnergyUnits != "eV" || c.nequipLengthUnits != "Angstrom")
            out << "# EDIT ME: this model was trained in " << c.nequipEnergyUnits
                << " / " << c.nequipLengthUnits << ". Replace the None values\n"
                   "# above with the numeric conversion factors to eV / Angstrom\n"
                   "# (e.g. 0.0433641 for kcal/mol -> eV); leaving them None makes\n"
                   "# NequIP fall back to the units recorded in the deployed model.\n";
        break;
    }

    case CalculatorKind::ChgNet:
        out << "# CHGNet — universal pretrained potential with magnetic-moment\n"
               "# prediction (crystal Hamiltonian graph network).\n"
               "# Requires:  pip install chgnet\n"
               "from chgnet.model.dynamics import CHGNetCalculator\n"
               "from chgnet.model.model import CHGNet\n"
               "\n"
            << "model = CHGNet.load(model_name=\"" << toString(c.chgnetWeights)
            << "\")\n"
            << "atoms.calc = CHGNetCalculator(model=model, use_device=\""
            << toString(c.mlipDevice) << "\",\n"
            << "                              stress_weight="
            << (c.chgnetStress ? "1.0" : "0.0") << ")\n";
        if (!c.chgnetStress)
            out << "# Stress evaluation disabled: cheaper, but variable-cell\n"
                   "# relaxation and any stress-based analysis will not work.\n";
        break;

    case CalculatorKind::MatterSim:
        out << "# MatterSim — universal interatomic potential across the\n"
               "# periodic table and a wide temperature / pressure range.\n"
               "# Requires:  pip install mattersim\n"
               "from mattersim.forcefield import MatterSimCalculator\n"
               "\n"
            << "atoms.calc = MatterSimCalculator(\n"
            << "    load_path=\"" << toString(c.matterSimModel) << "\",\n"
            << "    device=\"" << toString(c.mlipDevice) << "\",\n"
               ")\n";
        if (c.matterSimThermal)
            out << "# Thermodynamic state this potential is evaluated at:\n"
                << "#   T = " << c.matterSimTemperatureK << " K, P = "
                << c.matterSimPressureGPa << " GPa\n"
                   "# MatterSim's released checkpoints are trained across T/P, so\n"
                   "# the state is carried on the Atoms rather than the calculator.\n"
                << "atoms.info[\"temperature_K\"] = " << c.matterSimTemperatureK
                << "\n"
                << "atoms.info[\"pressure_GPa\"] = " << c.matterSimPressureGPa
                << "\n";
        break;

    case CalculatorKind::FairChem:
        out << "# FAIRChem (formerly Open Catalyst / OCP) — "
            << toString(c.fairChemModel) << " architecture.\n"
               "# Requires:  pip install fairchem-core\n"
               "from fairchem.core import OCPCalculator\n"
               "\n"
               "atoms.calc = OCPCalculator(\n"
            << "    checkpoint_path=r\"" << c.fairChemCheckpointPath << "\",\n"
            << "    cpu=" << (c.mlipDevice == MlipDevice::Cpu ? "True" : "False")
            << ",\n"
               ")\n";
        if (c.fairChemCheckpointPath.empty())
            out << "# EDIT ME: FAIRChem ships no default model — download an\n"
                   "# " << toString(c.fairChemModel)
                << " checkpoint and point checkpoint_path at it.\n";
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

    emitDispersion(out, c);
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
        out << "import numpy as _np\n"
               "import json\n"
               "energy = atoms.get_potential_energy()\n"
               "_forces = _np.asarray(atoms.get_forces(), dtype=float)\n"
               "_fnorms = _np.linalg.norm(_forces, axis=1) if _forces.size "
               "else _np.zeros(0)\n"
               "fmax = float(_fnorms.max()) if _fnorms.size else 0.0\n"
               "_fmax_atom = int(_fnorms.argmax()) if _fnorms.size else -1\n"
               "_calango_log.metric(0, energy=energy, max_force=fmax)\n"
               "print(f\"CALANGO_RESULT energy_eV={energy:.6f}\", flush=True)\n"
               "print(f\"CALANGO_RESULT fmax_eV_per_A={fmax:.6f}\", flush=True)\n"
               "# Fermi level: defined for periodic / smeared DFT; many\n"
               "# molecular or classical calculators do not expose one.\n"
               "try:\n"
               "    _fermi = float(atoms.calc.get_fermi_level())\n"
               "except Exception:\n"
               "    _fermi = None\n"
               "# SCF iteration count, where the backend reports it.\n"
               "try:\n"
               "    _nscf = int(atoms.calc.get_number_of_iterations())\n"
               "except Exception:\n"
               "    _nscf = None\n"
               "# Total magnetic moment (μB) for spin-polarized runs; scalar for\n"
               "# collinear, undefined/vector for others -> None.\n"
               "try:\n"
               "    _magmom = float(atoms.get_magnetic_moment())\n"
               "except Exception:\n"
               "    _magmom = None\n";
        // 1 Hartree = 27.211386245988 eV (CODATA). The convergence targets the
        // wizard collected are echoed into the summary so the viewer can show
        // the tolerance the run was held to.
        out << "_summary = {\n"
               "    \"energy_eV\": float(energy),\n"
               "    \"energy_Hartree\": float(energy) / 27.211386245988,\n"
               "    \"fermi_eV\": _fermi,\n"
               "    \"fmax_eV_per_A\": fmax,\n"
               "    \"fmax_atom\": _fmax_atom,\n"
               "    \"total_magnetic_moment\": _magmom,\n"
               "    \"natoms\": int(len(atoms)),\n"
               "    \"forces_eV_per_A\": [[float(v) for v in row] "
               "for row in _forces],\n"
               "    \"scf\": {\n"
               "        \"completed\": True,\n"
               "        \"iterations\": _nscf,\n"
            << "        \"energy_tol_eV\": " << c.scfEnergyTolEv << ",\n"
            << "        \"max_steps\": " << c.scfMaxSteps << ",\n"
               "    },\n"
               "}\n"
               "with open(\"single_point.json\", \"w\") as _fh:\n"
               "    json.dump(_summary, _fh, indent=2)\n"
               "print(\"CALANGO_RESULT single_point=single_point.json\", "
               "flush=True)\n";
        if (c.calculator == CalculatorKind::Gpaw) {
            // Save the converged charge density so a later Electronic Structure
            // run can load it and evaluate bands/PDOS non-self-consistently
            // (mode="all" writes the density + wavefunctions). This file is what
            // the Electronic Structure wizard's baseline selector looks for.
            out << "atoms.calc.write(\"single_point.gpw\", mode=\"all\")\n"
                   "print(\"CALANGO_RESULT density_file=single_point.gpw\", "
                   "flush=True)\n";
            if (c.gpawExportDensity) {
                // Charge density → a standard Gaussian cube. The new GPAW engine
                // returns a (possibly distributed) array-like; normalize it to a
                // contiguous float64 grid before write_cube.
                const bool ae =
                    c.gpawDensityType == GpawDensityType::AllElectron;
                out << "_density = atoms.calc."
                    << (ae ? "get_all_electron_density(gridrefinement=2)"
                           : "get_pseudo_density()")
                    << "\n"
                       "_density = _np.ascontiguousarray(\n"
                       "    _np.asarray(getattr(_density, \"data\", "
                       "_density), dtype=float))\n"
                       "from ase.io.cube import write_cube\n"
                       "with open(\"density.cube\", \"w\") as _dfh:\n"
                       "    write_cube(_dfh, atoms, data=_density)\n"
                    << "print(\"CALANGO_RESULT density_cube=density.cube "
                    << (ae ? "all_electron" : "pseudo") << "\", flush=True)\n";
            }
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
        // Constraints bind to `atoms` BEFORE the cell filter wraps it: the
        // filter forwards the constrained forces, so the order decides whether
        // the frozen atoms are actually frozen.
        emitConstraints(out, c);
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
               "print(f\"CALANGO_RESULT converged={converged} energy_eV={energy:.6f}\", flush=True)\n"
               "\n"
               "# Machine-readable summary for the Geometry Optimization Viewer.\n"
               "# Written last so its presence means the run reached the end.\n"
               "import json\n"
               "import numpy as _np\n"
               "\n"
               "_forces = atoms.get_forces()\n"
               "_force_norms = _np.linalg.norm(_forces, axis=1)\n"
               "# The energy CHANGE over the relaxation is what says whether the\n"
               "# optimizer actually did anything, so read the first recorded\n"
               "# step back out of the trajectory rather than reporting only the\n"
               "# final value.\n"
               "_initial_energy = None\n"
               "try:\n"
               "    from ase.io import read as _read\n"
               "\n"
               "    _first = _read(\"opt.traj\", index=0)\n"
               "    _initial_energy = float(_first.get_potential_energy())\n"
               "except Exception:\n"
               "    pass  # single-step run, or a calculator that cannot re-evaluate\n"
               "_summary = {\n"
            << "    \"optimizer\": \"" << opt << "\",\n"
            << "    \"fmax_criterion_eV_per_A\": " << c.fmax << ",\n"
               "    \"max_steps\": max_steps,\n"
               "    \"steps\": int(opt.nsteps),\n"
               "    \"converged\": bool(converged),\n"
            << "    \"relax_cell\": " << (c.relaxCell ? "True" : "False") << ",\n"
               "    \"energy_eV\": float(energy),\n"
               "    \"initial_energy_eV\": _initial_energy,\n"
               "    \"energy_change_eV\": (None if _initial_energy is None\n"
               "                         else float(energy) - _initial_energy),\n"
               "    \"fmax_eV_per_A\": float(_force_norms.max()) if len(_force_norms) else 0.0,\n"
               "    \"fmax_atom\": int(_force_norms.argmax()) if len(_force_norms) else -1,\n"
               "    # RMS over ALL 3N force components, the conventional\n"
               "    # definition — not the RMS of the per-atom magnitudes.\n"
               "    \"frms_eV_per_A\": float(_np.sqrt((_forces ** 2).sum() / _forces.size))\n"
               "                     if _forces.size else 0.0,\n"
               "    \"natoms\": len(atoms),\n"
               "    \"formula\": atoms.get_chemical_formula(),\n"
               "    \"trajectory\": \"opt.traj\",\n"
               "    \"forces\": [[float(c) for c in row] for row in _forces],\n"
               "}\n"
               "with open(\"geometry_optimization.json\", \"w\") as f:\n"
               "    json.dump(_summary, f)\n"
               "print(\"CALANGO_INFO wrote geometry_optimization.json\", flush=True)\n";
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
            << "  # record every N steps\n";
        // Constraints go on BEFORE the velocities are drawn: ASE's
        // MaxwellBoltzmannDistribution consults the constraints and leaves the
        // frozen degrees of freedom at zero, so a held substrate starts at rest
        // instead of being given thermal velocities it then has to have
        // projected out every step.
        emitConstraints(out, c);
        out << "MaxwellBoltzmannDistribution(atoms, temperature_K=temperature_K)\n"
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
                   "                        kinetic=ekin, volume=atoms.get_volume(),\n"
                   "                        max_force=fmax_now, pressure=pressure_GPa)\n";
        else
            out << "    # Kinetic energy and volume are logged alongside the\n"
                   "    # potential energy so the MD Viewer can show E_tot =\n"
                   "    # E_pot + E_kin (whose drift is the integrator health\n"
                   "    # check) rather than only the potential term.\n"
                   "    _calango_log.metric(dyn.nsteps, energy=epot, temperature=temp,\n"
                   "                        kinetic=ekin,\n"
                   "                        volume=(atoms.get_volume() if atoms.cell.rank == 3\n"
                   "                                else 0.0),\n"
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
    out << indent << "xc=\"" << c.gpawXc << "\",\n";
    if (c.gpawGammaCentered)
        // Γ-centered Monkhorst-Pack grid: the {'size', 'gamma'} dict form.
        out << indent << "kpts={\"size\": (" << c.kpts[0] << ", " << c.kpts[1]
            << ", " << c.kpts[2] << "), \"gamma\": True},  # Γ-centered\n";
    else
        out << indent << "kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", "
            << c.kpts[2] << "),  # Monkhorst-Pack grid\n";
    // Point-group symmetry reduction of the k-points is on by default; only
    // write "off" when the user turned it off (e.g. to expose the full
    // Brillouin zone for a downstream Wannier localization).
    //
    // Molecular dynamics forces it off regardless. GPAW detects the symmetry
    // group ONCE from the starting geometry and then validates every later set
    // of positions against it; thermal velocities break that group on the first
    // step, and the run dies with "Broken symmetry!" (GPAW 25) or
    // SymmetryBrokenError (GPAW 26). An MD snapshot has no symmetry by
    // construction, so keeping the reduction on is not an option the user
    // should be able to pick — it can only fail.
    //
    // Geometry optimization is deliberately NOT included: relaxation follows
    // symmetric forces and stays inside the group it started in.
    const bool symmetryOff =
        c.gpawSymmetryOff || c.task == TaskKind::MolecularDynamics;
    if (symmetryOff) {
        out << indent
            << "symmetry=\"off\",  # no point-group reduction of the k-points\n";
    } else {
        // Pin the symmetry-detection tolerance to 1e-5 Å.
        //
        // GPAW picks it as `1e-7 if backwards_compatible else 1e-5`, and
        // `backwards_compatible` still defaults to TRUE (gpaw/new/builder.py),
        // so an unqualified run inherits the legacy 1e-7. That is tight enough
        // to be a trap: an atom sitting a few times 1e-7 Å off a symmetry
        // point — ordinary float32-grade numerical residue, five orders of
        // magnitude below anything physical — makes the analyser report
        // operations that do not close under composition, and GPAW aborts the
        // whole run with
        //     SymmetryAnalysisBug: Sorry!  Try using spglib.standardize_cell()
        // The window is narrow (≈1e-7; smaller snaps to the full group, larger
        // correctly drops to the lower one), which is what makes it look
        // random: the same workflow runs fine until one structure lands in it.
        //
        // 1e-5 Å is not a loosening — it is GPAW's own modern default, and is
        // still far below any physically meaningful displacement.
        out << indent
            << "symmetry={\"tolerance\": 1e-5},  # GPAW's modern default; the\n"
            << indent
            << "                               # legacy 1e-7 aborts on ~1e-7 Å\n"
            << indent
            << "                               # coordinate noise\n";
    }
    out << indent << "eigensolver=\"" << toString(c.gpawEigensolver) << "\",\n"
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
    // DFT+U as GPAW's `setups` dictionary. The leading colon keeps the default
    // PAW dataset and appends the correction to it — without it GPAW would look
    // for a differently named dataset instead of correcting the standard one.
    if (c.useHubbardU && !c.hubbardU.empty()) {
        out << indent << "setups={";
        bool first = true;
        for (const HubbardU& hubbard : c.hubbardU) {
            if (hubbard.element.empty())
                continue;
            if (!first)
                out << ", ";
            first = false;
            out << "\"" << hubbard.element << "\": \":"
                << (hubbard.orbital.empty() ? std::string("d") : hubbard.orbital)
                << "," << hubbard.u << (hubbard.scale ? ",1" : "") << "\"";
        }
        out << "},  # DFT+U (element: :shell,U[,scale])\n";
    }
    // Spin: collinear sets spinpol=True; non-collinear is driven by the vector
    // initial moments seeded in the preamble, so no spinpol keyword is written
    // (GPAW infers the spinor treatment from the (N,3) magmoms). `spinPolarized`
    // is honored as a collinear fallback for configs that only set the boolean.
    if (c.spinMode == SpinMode::NonCollinear)
        out << indent << "# non-collinear spin: driven by the vector initial "
                         "magnetic moments\n";
    else if (c.spinMode == SpinMode::Collinear || c.spinPolarized)
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

std::string AseScriptGenerator::densityCubeScript(const std::string& gpwDir,
                                                  bool allElectron)
{
    std::ostringstream out;
    out << "# Charge-density export — generated by Calango\n"
           "import os\n"
           "import glob\n"
           "import numpy as _np\n"
           "from calango_log import CalangoLog\n"
           "_log = CalangoLog()\n"
        << "_base = r\"" << gpwDir << "\"\n"
        << "_gpw = sorted(glob.glob(os.path.join(_base, '*.gpw')))\n"
           "if not _gpw:\n"
           "    raise RuntimeError('No GPAW wavefunction (.gpw) found in ' + "
           "_base +\n"
           "                       '. Re-run the single-point so it saves the "
           "density.')\n"
           "os.environ.setdefault('GPAW_NEW', '1')\n"
           "from gpaw import GPAW\n"
           "calc = GPAW(_gpw[0], txt='gpaw_density.txt')\n"
           "atoms = calc.get_atoms()\n"
           "_log.progress(1, 2)\n"
        << "_density = calc."
        << (allElectron ? "get_all_electron_density(gridrefinement=2)"
                        : "get_pseudo_density()")
        << "\n"
           "_density = _np.ascontiguousarray(\n"
           "    _np.asarray(getattr(_density, 'data', _density), dtype=float))\n"
           "from ase.io.cube import write_cube\n"
           "with open('density.cube', 'w') as _dfh:\n"
           "    write_cube(_dfh, atoms, data=_density)\n"
           "_log.progress(2, 2)\n"
        << "print('CALANGO_RESULT density_cube=density.cube "
        << (allElectron ? "all_electron" : "pseudo") << "', flush=True)\n";
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
        << kJsonLoggerHelper
        << "atoms = read(r\"" << structureFile << "\")\n"
        << "print(f\"CALANGO_INFO natoms={len(atoms)}\", flush=True)\n"
           "\n";
    if (config.spinMode != SpinMode::Unpolarized || config.spinPolarized) {
        // Non-collinear needs vector moments (N,3); a scalar list is applied
        // along +z so a magnitude-only entry still seeds a spinor calculation.
        const bool nc = config.spinMode == SpinMode::NonCollinear;
        out << "# Spin polarization: seed each atom with an initial magnetic\n"
               "# moment so the SCF can find a magnetic solution.\n";
        if (!config.initialMagMomentsCsv.empty()) {
            // Explicit per-atom moments, zero-padded to the atom count so a
            // short list still applies (remaining atoms start non-magnetic).
            out << "_moments = [float(_m) for _m in \""
                << config.initialMagMomentsCsv
                << "\".replace(\",\", \" \").split()]\n"
                   "_moments += [0.0] * (len(atoms) - len(_moments))\n"
                   "_moments = _moments[:len(atoms)]\n";
            if (nc)
                out << "atoms.set_initial_magnetic_moments("
                       "[[0.0, 0.0, _m] for _m in _moments])\n\n";
            else
                out << "atoms.set_initial_magnetic_moments(_moments)\n\n";
        } else if (nc) {
            out << "atoms.set_initial_magnetic_moments([[0.0, 0.0, "
                << config.initialMagMoment << "]] * len(atoms))\n\n";
        } else {
            out << "atoms.set_initial_magnetic_moments([" << config.initialMagMoment
                << "] * len(atoms))\n\n";
        }
    }
    emitCalculator(out, config);
    out << "\n";
    emitTask(out, config);
    out << "\nprint(\"CALANGO_DONE\", flush=True)\n";
    return out.str();
}

} // namespace calango::core
