#include "core/AseScriptGenerator.hpp"

#include "core/EngineCalculatorBlocks.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace calango::core {

std::string toString(CalculatorKind kind)
{
    switch (kind) {
    case CalculatorKind::CalangoDft: return "Calango DFT";
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
    case CalculatorKind::Lammps: return "LAMMPS";
    case CalculatorKind::Xtb: return "xTB";
    case CalculatorKind::DftbPlus: return "DFTB+";
    case CalculatorKind::Gromacs: return "GROMACS";
    case CalculatorKind::Abinit: return "ABINIT";
    case CalculatorKind::FhiAims: return "FHI-aims";
    case CalculatorKind::NwChem: return "NWChem";
    case CalculatorKind::OpenMx: return "OpenMX";
    case CalculatorKind::Fleur: return "FLEUR";
    case CalculatorKind::Cp2k: return "CP2K";
    case CalculatorKind::Amber: return "Amber";
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
    // GPAW's registry has no "direct". The direct LCAO solver is registered
    // under the name "lcao" (gpaw/old/eigensolvers/__init__.py maps
    // 'lcao' -> DirectLCAO); "direct" raised KeyError in every mode.
    case GpawEigensolver::Direct: return "lcao";
    }
    return "dav";
}

/// The eigensolver name for a (mode, solver) pair — which is not the same as
/// the name for the solver alone.
///
/// GPAW couples the two with an assertion, not a preference:
///
///     assert isinstance(eigensolver, DirectLCAO) == (mode.name == 'lcao') or
///            isinstance(eigensolver, LCAOETDM)  == (mode.name == 'lcao')
///
/// so in LCAO mode the ONLY admissible solvers are DirectLCAO ("lcao") and
/// LCAOETDM ("etdm-lcao"); Davidson, CG and RMM-DIIS all trip it. They are
/// grid/plane-wave solvers — they iterate wavefunctions on a real-space or
/// plane-wave representation, and an LCAO calculation has neither: its
/// variational parameters are the expansion coefficients, and the "solver"
/// there is a direct generalized-eigenproblem diagonalization.
///
/// Verified against gpaw 26.7.1b1 across all three modes.
///
/// Coerced here rather than refused. The user picked a MODE and a solver in
/// two different places, and only one combination of the two is wrong in a way
/// that surfaces as an AssertionError several hundred lines into the run; the
/// script says what it did instead.
std::string eigensolverFor(const CalculatorConfig& c, bool* coerced)
{
    const bool lcao = c.gpawMode == GpawMode::Lcao;
    const std::string requested = toString(c.gpawEigensolver);
    const bool requestedIsLcao = requested == "lcao";
    if (coerced)
        *coerced = (lcao != requestedIsLcao);
    if (lcao)
        return "lcao";
    // The converse also holds: DirectLCAO in a grid/plane-wave run has no
    // basis to diagonalize over. Davidson is GPAW's own default there.
    return requestedIsLcao ? "dav" : requested;
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
    case SmearingMethod::None: return "None (zero-width occupations)";
    case SmearingMethod::Gaussian: return "Gaussian";
    case SmearingMethod::FermiDirac: return "Fermi-Dirac";
    case SmearingMethod::MethfesselPaxton: return "Methfessel-Paxton";
    case SmearingMethod::MarzariVanderbilt: return "Marzari-Vanderbilt";
    case SmearingMethod::TetrahedronMethod: return "Tetrahedron method";
    case SmearingMethod::ImprovedTetrahedronMethod:
        return "Improved tetrahedron method";
    case SmearingMethod::OrbitalFree: return "Orbital-free";
    case SmearingMethod::FixedOccupations: return "Fixed occupations";
    }
    return "Fermi-Dirac";
}

bool smearingUsesWidth(SmearingMethod method)
{
    switch (method) {
    case SmearingMethod::Gaussian:
    case SmearingMethod::FermiDirac:
    case SmearingMethod::MethfesselPaxton:
    case SmearingMethod::MarzariVanderbilt:
        return true;
    case SmearingMethod::None:
    case SmearingMethod::TetrahedronMethod:
    case SmearingMethod::ImprovedTetrahedronMethod:
    case SmearingMethod::OrbitalFree:
    case SmearingMethod::FixedOccupations:
        break;
    }
    return false;
}

bool smearingUsesOrder(SmearingMethod method)
{
    return method == SmearingMethod::MethfesselPaxton;
}

bool smearingUsesFixedOccupations(SmearingMethod method)
{
    return method == SmearingMethod::FixedOccupations;
}

std::string gpawSmearingName(SmearingMethod method)
{
    switch (method) {
    // Gaussian smearing is the zeroth-order Methfessel-Paxton expansion —
    // that is its definition, not an approximation of it — and GPAW has no
    // separate name for it. Emitting it as such is what makes the menu entry
    // mean what it says; the previous code sent every non-Fermi-Dirac choice
    // through as Fermi-Dirac, which is a different occupation function.
    case SmearingMethod::Gaussian:
    case SmearingMethod::MethfesselPaxton: return "methfessel-paxton";
    case SmearingMethod::FermiDirac: return "fermi-dirac";
    case SmearingMethod::MarzariVanderbilt: return "marzari-vanderbilt";
    case SmearingMethod::TetrahedronMethod: return "tetrahedron-method";
    case SmearingMethod::ImprovedTetrahedronMethod:
        return "improved-tetrahedron-method";
    case SmearingMethod::OrbitalFree: return "orbital-free";
    case SmearingMethod::FixedOccupations: return "fixed";
    // "None" is not a GPAW name: it is Fermi-Dirac at zero width, which is
    // how GPAW itself spells "do not smear".
    case SmearingMethod::None: break;
    }
    return "fermi-dirac";
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
/// script.
///
/// Deliberately EMBEDDED rather than imported. It used to be
/// `from calango_log import CalangoLog`, with the module staged beside the
/// script — which meant a generated script was not a script but a two-file
/// bundle, and copying just the .py to a cluster produced an ImportError on
/// line 5 for a run that had nothing to do with logging. The whole promise of
/// this generator is a file you can scp to an HPC box and run under stock ASE;
/// a private helper import broke that promise for the sake of ~40 lines.
///
/// So it is a plain dict and three functions over the standard library: no
/// class, no Calango import, nothing the receiving machine has to have. The
/// on-disk contract is unchanged — metrics.json (polled live by the Results
/// panel for the metric plots and the progress bar), log.json (the Log tab)
/// and warnings.log (Python warnings from ASE, PyTorch, SciPy, GPAW …, kept
/// out of stdout so the Log tab stays readable).
constexpr const char* kJsonLoggerHelper =
    "# --- Structured job logging (standard library only) -------------------\n"
    "#\n"
    "# Writes metrics.json and log.json into the working directory and routes\n"
    "# Python warnings to warnings.log. Calango's Results panel polls those\n"
    "# files, but nothing here needs Calango: this block is self-contained so\n"
    "# the script runs unchanged wherever ASE and the calculator are present.\n"
    "# Delete it and the calls to _calango_progress / _calango_metric /\n"
    "# _calango_event below if you want the script silent.\n"
    "import json as _json\n"
    "import logging as _logging\n"
    "import os as _os\n"
    "import threading as _threading\n"
    "import warnings as _warnings\n"
    "\n"
    "_calango_lock = _threading.Lock()\n"
    "_calango_metrics = {\"metrics\": []}\n"
    "_calango_events = {\"log\": []}\n"
    "\n"
    "_calango_warnings = _logging.getLogger(\"py.warnings\")\n"
    "_calango_warnings.handlers = [_logging.FileHandler(\"warnings.log\",\n"
    "                                                   mode=\"w\")]\n"
    "_calango_warnings.propagate = False\n"
    "_logging.captureWarnings(True)\n"
    "# \"default\", not \"ignore\": every distinct warning is recorded once per\n"
    "# location, which is what makes warnings.log useful for diagnosis.\n"
    "_warnings.simplefilter(\"default\")\n"
    "\n"
    "\n"
    "def _calango_write(path, data):\n"
    "    \"\"\"Replace `path` atomically — a reader must never see half a file.\"\"\"\n"
    "    with open(path + \".tmp\", \"w\") as handle:\n"
    "        _json.dump(data, handle)\n"
    "    _os.replace(path + \".tmp\", path)\n"
    "\n"
    "\n"
    "def _calango_metric(step, **fields):\n"
    "    \"\"\"Record one sample. None fields are skipped, not written as null.\"\"\"\n"
    "    entry = {\"step\": int(step)}\n"
    "    entry.update({k: float(v) for k, v in fields.items() if v is not None})\n"
    "    with _calango_lock:\n"
    "        _calango_metrics[\"metrics\"].append(entry)\n"
    "        _calango_write(\"metrics.json\", _calango_metrics)\n"
    "\n"
    "\n"
    "def _calango_progress(step, total):\n"
    "    \"\"\"Update the completion fraction driving the progress bar.\"\"\"\n"
    "    step, total = int(step), int(total)\n"
    "    with _calango_lock:\n"
    "        _calango_metrics[\"progress\"] = {\n"
    "            \"step\": step,\n"
    "            \"total\": total,\n"
    "            \"percent\": (100.0 * step / total) if total > 0 else 0.0,\n"
    "        }\n"
    "        _calango_write(\"metrics.json\", _calango_metrics)\n"
    "\n"
    "\n"
    "def _calango_event(level, message):\n"
    "    \"\"\"Append a log event (level is free-form: info/warning/error).\"\"\"\n"
    "    with _calango_lock:\n"
    "        _calango_events[\"log\"].append(\n"
    "            {\"level\": str(level), \"message\": str(message)})\n"
    "        _calango_write(\"log.json\", _calango_events)\n"
    "\n"
    "# --- end of logging block ---------------------------------------------\n"
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
           "# The DFTD4 calculator comes from the dftd4 package itself and is\n"
           "# coupled to the electronic-structure calculator through ASE's\n"
           "# SumCalculator: each computes independently, energies and forces\n"
           "# add. (DFTD4 is not a wrapper — passing it another calculator is\n"
           "# not supported.)\n"
           "# Requires:  pip install dftd4   (or conda install -c conda-forge dftd4)\n"
           "from ase.calculators.mixing import SumCalculator\n"
           "from dftd4.ase import DFTD4\n"
           "\n"
        << "atoms.calc = SumCalculator([DFTD4(method=\"" << method
        << "\"), atoms.calc])\n";
}

/// Python list literal of strings, or `[]`.
std::string pythonStringList(const std::vector<std::string>& values,
                             const std::string& indent)
{
    if (values.empty())
        return "[]";
    std::ostringstream out;
    out << "[\n";
    for (const std::string& value : values)
        out << indent << "    r\"" << value << "\",\n";
    out << indent << "]";
    return out.str();
}

/// LAMMPS through ASE, in whichever of the two interfaces the user selected.
///
/// The two are genuinely different objects with different vocabularies —
/// LAMMPSlib takes a list of LAMMPS COMMANDS, lammpsrun takes a parameter DICT
/// it renders into an input deck — so they get separate blocks rather than one
/// parameterized template that would obscure both.
/// True when this run's ionic steps belong to VASP rather than to an ASE
/// optimizer. The single predicate both halves of the script consult, so they
/// cannot disagree about who is relaxing.
bool vaspDrivesRelaxation(const CalculatorConfig& c)
{
    return c.calculator == CalculatorKind::Vasp
        && c.task == TaskKind::GeometryOptimization
        && c.vaspRelaxDriver == VaspRelaxDriver::Vasp;
}

/// VASP through ASE, with the INCAR tags the wizard collects.
///
/// Two things here are not obvious and are the reason this is not a one-liner.
///
/// POTCARs. ASE resolves them as `$VASP_PP_PATH/potpaw_PBE/<El>/POTCAR` and
/// gives no way to override that subdirectory name. Plenty of real
/// installations (including the one this was written against) keep the element
/// folders directly under their POTCAR directory, with no `potpaw_PBE` level at
/// all. Rather than telling the user their layout is wrong, the script detects
/// it and builds a tiny symlink shim so ASE finds what is already there.
///
/// ISMEAR. VASP encodes the smearing METHOD and the Methfessel-Paxton ORDER in
/// one integer, and the useful values are not a simple enumeration: -1 is
/// Fermi-Dirac, 0 is Gaussian, N > 0 is Nth-order MP, -5 is the tetrahedron
/// method. The mapping is spelled out below rather than left to the reader.
void emitVasp(std::ostringstream& out, const CalculatorConfig& c)
{
    const auto pyBool = [](bool value) { return value ? "True" : "False"; };
    const auto prec = [&] {
        switch (c.vaspPrec) {
        case VaspPrecision::Normal: return "Normal";
        case VaspPrecision::Single: return "Single";
        case VaspPrecision::Accurate: break;
        }
        return "Accurate";
    }();
    const auto algo = [&] {
        switch (c.vaspAlgo) {
        case VaspAlgo::Fast: return "Fast";
        case VaspAlgo::VeryFast: return "VeryFast";
        case VaspAlgo::All: return "All";
        case VaspAlgo::Damped: return "Damped";
        case VaspAlgo::Normal: break;
        }
        return "Normal";
    }();
    // ISMEAR / SIGMA. A metal wants MP or Gaussian; an insulator wants the
    // tetrahedron method, which VASP will refuse for a Gamma-only mesh — so a
    // "None" selection maps to a very small Gaussian rather than to -5, which
    // would fail on exactly the small test cells people try first.
    int ismear = 0;
    double sigma = c.smearingWidthEv;
    switch (c.smearing) {
    case SmearingMethod::None:
        ismear = 0;
        sigma = 0.01;
        break;
    case SmearingMethod::Gaussian:
        ismear = 0;
        break;
    case SmearingMethod::FermiDirac:
        ismear = -1;
        break;
    case SmearingMethod::MethfesselPaxton:
        // ISMEAR is literally the MP order, so the order spin box maps
        // straight onto it. Clamped at 1 because order 0 is Gaussian, which
        // the user would have selected by name if that is what they wanted.
        ismear = std::max(1, c.smearingOrder);
        break;
    case SmearingMethod::TetrahedronMethod:
        // -4 is the plain linear tetrahedron method, -5 adds Blöchl's
        // curvature correction — the same distinction GPAW draws between its
        // tetrahedron and improved-tetrahedron schemes. Both need a
        // Γ-centred mesh; VASP refuses them for a single k-point.
        ismear = -4;
        break;
    case SmearingMethod::ImprovedTetrahedronMethod:
        ismear = -5;
        break;
    case SmearingMethod::MarzariVanderbilt:
    case SmearingMethod::OrbitalFree:
    case SmearingMethod::FixedOccupations:
        // No VASP analogue: Marzari-Vanderbilt cold smearing is not one of
        // VASP's ISMEAR schemes, orbital-free DFT is not what VASP does at
        // all, and explicitly fixed occupations would mean FERWE/FERDO, which
        // is a different input entirely. A narrow Gaussian is the closest
        // thing that still runs, and the generated INCAR block says so.
        ismear = 0;
        sigma = smearingUsesWidth(c.smearing) ? c.smearingWidthEv : 0.05;
        break;
    }

    out << "# VASP through ASE. The run needs a VASP binary reachable through\n"
           "# ASE_VASP_COMMAND (or ASE's `command`), and the PAW datasets\n"
           "# below.\n"
           "import os\n"
           "from ase.calculators.vasp import Vasp\n"
           "\n";

    if (!c.vaspPotcarPath.empty()) {
        out << "# PAW datasets. ASE looks for $VASP_PP_PATH/potpaw_PBE/<El>/POTCAR;\n"
               "# an installation that keeps the element folders directly under\n"
               "# the POTCAR directory gets a symlink shim so it resolves anyway.\n"
            << "_potcar_root = r\"" << c.vaspPotcarPath
            << "\"\n"
               "if not os.path.isdir(_potcar_root):\n"
               "    raise RuntimeError(\n"
               "        f'POTCAR directory not found: {_potcar_root}\\n'\n"
               "        'Set it in the calculator settings (VASP -> POTCAR "
               "directory).')\n"
               "if not any(os.path.isdir(os.path.join(_potcar_root, _d))\n"
               "           for _d in ('potpaw_PBE', 'potpaw', 'potpaw_LDA')):\n"
               "    _shim = os.path.abspath('_potcar_shim')\n"
               "    os.makedirs(_shim, exist_ok=True)\n"
               "    for _name in ('potpaw_PBE', 'potpaw_LDA', 'potpaw'):\n"
               "        _link = os.path.join(_shim, _name)\n"
               "        if not os.path.exists(_link):\n"
               "            os.symlink(_potcar_root, _link)\n"
               "    print(f'CALANGO_INFO flat POTCAR layout — shimmed via "
               "{_shim}',\n"
               "          flush=True)\n"
               "    _potcar_root = _shim\n"
               "os.environ['VASP_PP_PATH'] = _potcar_root\n"
               "\n";
    } else {
        out << "# No POTCAR directory configured — ASE falls back to whatever\n"
               "# VASP_PP_PATH the environment already carries.\n\n";
    }

    out << "atoms.calc = Vasp(\n"
        << "    directory=\".\",\n"
        << "    xc=\"" << c.vaspXc << "\",\n"
        << "    prec=\"" << prec << "\",\n"
        << "    encut=" << c.planeWaveCutoffEv << ",\n"
        << "    kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2]
        << "),\n"
        // ASE's VASP calculator writes a Gamma-centered KPOINTS block when
        // `gamma=True` and a Monkhorst-Pack one otherwise. Worth being
        // explicit about rather than leaving to the default: an even mesh
        // without this misses Γ entirely, which a hexagonal cell and any
        // downstream band or Wannier step both care about.
        << "    gamma=" << pyBool(c.kptsGammaCentered) << ",  # "
        << (c.kptsGammaCentered ? "Γ-centered" : "Monkhorst-Pack") << "\n"
        << "    # Electronic minimization\n"
        << "    algo=\"" << algo << "\",\n"
        << "    nelm=" << c.vaspNelm << ",\n"
        << "    ediff=" << c.vaspEdiff << ",\n"
        << "    ismear=" << ismear << ",  # " << toString(c.smearing) << "\n"
        << "    sigma=" << sigma << ",\n";
    // Say so in the script when the selected method had to be approximated,
    // rather than letting a run silently use a different occupation scheme
    // than the one named in the wizard.
    switch (c.smearing) {
    case SmearingMethod::MarzariVanderbilt:
    case SmearingMethod::OrbitalFree:
    case SmearingMethod::FixedOccupations:
        out << "    # NOTE: " << toString(c.smearing)
            << " has no VASP ISMEAR equivalent; a narrow Gaussian is used\n"
               "    #       instead. Select GPAW to run this scheme as chosen.\n";
        break;
    default:
        break;
    }
    out
        << "    lreal=" << (c.vaspLreal == "False" ? std::string("False")
                                                   : "\"" + c.vaspLreal + "\"")
        << ",\n";

    // ISPIN / MAGMOM. The moments themselves ride on `atoms` (set from the
    // structure's initial_magmoms further up), and ASE writes them into MAGMOM
    // for us — so only the switch is emitted here.
    if (c.spinMode != SpinMode::Unpolarized || c.spinPolarized) {
        out << "    # Spin. MAGMOM comes from the structure's initial moments,\n"
               "    # which ASE writes out of `atoms` for us.\n"
               "    ispin=2,\n";
        if (c.spinMode == SpinMode::NonCollinear)
            out << "    lsorbit=True,\n";
    } else {
        out << "    ispin=1,\n";
    }

    // Ionic steps are emitted ONLY when VASP is the one taking them. Writing
    // IBRION/NSW while an ASE optimizer is also running would make every ASE
    // force call a complete VASP relaxation — see VaspRelaxDriver.
    if (vaspDrivesRelaxation(c)) {
        out << "    # Ionic relaxation, driven by VASP itself. No ASE optimizer\n"
               "    # is created for this run; these tags ARE the relaxation.\n"
            << "    ibrion=" << c.vaspIbrion << ",\n"
            // A variable-cell relaxation needs ISIF >= 3; the wizard's own
            // "relax the cell" toggle is the authority on that, so it raises
            // the floor rather than letting a stale 2 quietly pin the lattice.
            << "    isif=" << (c.relaxCell ? std::max(3, c.vaspIsif)
                                           : c.vaspIsif)
            << ",\n"
            << "    nsw=" << std::max(1, c.maxSteps) << ",\n"
            << "    ediffg=" << c.vaspEdiffg << ",\n";
    } else if (c.task == TaskKind::GeometryOptimization) {
        out << "    # Static force/energy calculator: ASE's optimizer takes the\n"
               "    # ionic steps, so VASP must not also relax. With IBRION/NSW\n"
               "    # set here, every force evaluation ASE requested would run a\n"
               "    # full VASP relaxation of its own.\n"
               "    ibrion=-1,\n"
               "    nsw=0,\n";
    } else {
        out << "    # Single point: no ionic steps.\n"
               "    ibrion=-1,\n"
               "    nsw=0,\n";
    }

    out << "    # Output\n"
        << "    lwave=" << pyBool(c.vaspLwave) << ",\n"
        << "    lcharg=" << pyBool(c.vaspLcharg) << ",\n";
    if (c.vaspLaechg)
        out << "    laechg=True,\n";
    if (c.vaspLorbit)
        out << "    lorbit=11,\n";
    if (c.vaspNcore > 0)
        out << "    ncore=" << c.vaspNcore << ",\n";
    if (c.vaspKpar > 0)
        out << "    kpar=" << c.vaspKpar << ",\n";
    out << ")\n";

    if (!c.vaspExtraIncar.empty()) {
        // Free-form tags, applied through ASE's own setter so they land in the
        // INCAR with the right formatting instead of being pasted as text.
        out << "\n# Extra INCAR tags from the calculator settings.\n"
               "for _line in \"\"\"" << c.vaspExtraIncar
            << "\"\"\".splitlines():\n"
               "    _line = _line.split('#')[0].strip()\n"
               "    if not _line or '=' not in _line:\n"
               "        continue\n"
               "    _tag, _value = (_part.strip() for _part in _line.split('=', 1))\n"
               "    atoms.calc.set(**{_tag.lower(): _value})\n";
    }
    out << "\n";
}

void emitLammps(std::ostringstream& out, const CalculatorConfig& c)
{
    const bool library = c.lammpsInterface == LammpsInterface::Library;

    out << "# --- LAMMPS "
           "-------------------------------------------------------\n"
           "import os\n"
           "#\n"
           "# LAMMPS is an ENGINE, not a force field: what is computed below is\n"
           "# decided entirely by the pair style and its coefficients. Nothing\n"
           "# here validates them — a pair_coeff that does not match the style,\n"
           "# or a potential file for the wrong elements, is a physics error\n"
           "# LAMMPS will happily run.\n"
           "#\n"
           "# Units MUST be 'metal' (eV, Angstrom, ps). ASE assumes eV/Angstrom\n"
           "# throughout, and any other units style returns numbers in the wrong\n"
           "# scale rather than an error, so the check below is a hard stop.\n";

    if (c.lammpsUnits != "metal") {
        out << "raise RuntimeError(\n"
               "    \"LAMMPS units style '" << c.lammpsUnits << "' cannot be used "
               "through ASE.\\n\"\n"
               "    \"ASE works in eV and Angstrom, which is LAMMPS's 'metal' "
               "style; every\\n\"\n"
               "    \"other style would return energies and forces in a "
               "different scale\\n\"\n"
               "    \"with nothing to flag it. Re-open the wizard and select "
               "'metal'.\")\n";
        return;
    }

    // specorder is resolved from the STRUCTURE at run time rather than baked in
    // by the wizard. LAMMPS addresses species by integer type, and the mapping
    // from type to element is positional: get it wrong and the run silently
    // computes a different compound. Deriving it from the atoms object that is
    // actually loaded is the only way it cannot disagree with the geometry.
    out << "#\n"
           "# LAMMPS addresses species by integer TYPE, and the type -> element\n"
           "# mapping is positional. It is derived here from the structure that\n"
           "# was actually loaded rather than fixed in the wizard, because a\n"
           "# hard-coded order that disagrees with the geometry does not fail —\n"
           "# it computes a different compound. Order the pair_coeff entries to\n"
           "# match `species` as printed below.\n"
           "species = sorted(set(atoms.get_chemical_symbols()))\n"
           "print(f'CALANGO_INFO LAMMPS species order: "
           "{\" \".join(species)}', flush=True)\n"
           "\n";

    if (library) {
        out << "# LAMMPSlib: in-process, through the LAMMPS Python module. No\n"
               "# file I/O per evaluation, so this is the interface to use for\n"
               "# MD and relaxation.\n"
               "# Requires a shared-library LAMMPS build with its Python module:\n"
               "#   conda install -c conda-forge lammps\n"
               "from ase.calculators.lammpslib import LAMMPSlib\n"
               "\n"
               "lammps_commands = [\n"
            << "    \"pair_style " << c.lammpsPairStyle << "\",\n";
        for (const std::string& coeff : c.lammpsPairCoeff)
            out << "    \"pair_coeff " << coeff << "\",\n";
        for (const std::string& extra : c.lammpsExtraCommands)
            out << "    \"" << extra << "\",\n";
        out << "]\n"
               "\n"
               "atoms.calc = LAMMPSlib(\n"
               "    lmpcmds=lammps_commands,\n"
               "    atom_types={symbol: index + 1\n"
               "                for index, symbol in enumerate(species)},\n"
            << "    log_file="
            << (c.lammpsKeepLog ? "\"lammps.log\"" : "None") << ",\n"
               "    keep_alive=True,   # reuse one LAMMPS instance across steps\n"
               ")\n";
        if (!c.lammpsPotentialFiles.empty()) {
            out << "\n"
                   "# The library interface reads potential files relative to "
                   "the PROCESS's\n"
                   "# working directory, not the script's — absolute paths are "
                   "the safe form.\n"
                   "for _potential in "
                << pythonStringList(c.lammpsPotentialFiles, "")
                << ":\n"
                   "    if not os.path.isfile(_potential):\n"
                   "        raise RuntimeError(\n"
                   "            f'LAMMPS potential file not found: "
                   "{_potential}\\n'\n"
                   "            'The pair_coeff line above names it, so the run "
                   "would fail\\n'\n"
                   "            'inside LAMMPS with a less specific message.')\n";
        }
        return;
    }

    out << "# lammpsrun: spawns the `lmp` binary once per evaluation and\n"
           "# exchanges data files. Works with any LAMMPS build, including a\n"
           "# plain distro package, at the cost of process startup and file I/O\n"
           "# on every force call — noticeable in MD, irrelevant for a\n"
           "# single-point.\n"
           "from ase.calculators.lammpsrun import LAMMPS\n"
           "\n"
           "lammps_parameters = {\n"
        << "    \"units\": \"" << c.lammpsUnits << "\",\n"
        << "    \"atom_style\": \"" << c.lammpsAtomStyle << "\",\n"
        << "    \"pair_style\": \"" << c.lammpsPairStyle << "\",\n"
           "    \"pair_coeff\": [\n";
    for (const std::string& coeff : c.lammpsPairCoeff)
        out << "        \"" << coeff << "\",\n";
    out << "    ],\n";
    if (!c.lammpsPotentialFiles.empty()) {
        // `files` is what makes lammpsrun copy the potential into the scratch
        // directory it runs in; without it the pair_coeff path is resolved
        // against that temporary directory and is not found.
        out << "    # Copied into lammpsrun's scratch directory, which is where\n"
               "    # the pair_coeff paths above are resolved.\n"
               "    \"files\": "
            << pythonStringList(c.lammpsPotentialFiles, "    ") << ",\n";
    }
    for (const std::string& extra : c.lammpsExtraCommands)
        out << "    # extra command (add to the deck by hand): " << extra << "\n";
    out << "}\n"
           "\n";
    if (!c.lammpsCommand.empty()) {
        out << "# The `lmp` binary. ASE reads this from the environment, so it\n"
               "# is set here rather than passed to the constructor.\n"
            << "os.environ[\"ASE_LAMMPSRUN_COMMAND\"] = r\"" << c.lammpsCommand
            << "\"\n"
               "\n";
    } else {
        out << "# No binary configured: ASE falls back to $ASE_LAMMPSRUN_COMMAND\n"
               "# and then to `lmp` on $PATH.\n";
    }
    out << "atoms.calc = LAMMPS(\n"
           "    specorder=species,\n"
           "    **lammps_parameters,\n"
        << "    keep_tmp_files=" << (c.lammpsKeepLog ? "True" : "False")
        << ",\n"
           ")\n";
}

/// xTB through the `xtb` Python package's in-process ASE calculator.
///
/// Keyword names verified against xtb-python's own XTB.default_parameters
/// (method / accuracy / electronic_temperature / max_iterations) — they are
/// spelled exactly as the documented API, no aliases.
void emitXtb(std::ostringstream& out, const CalculatorConfig& c)
{
    // GFN-FF is the force-field member of the family: no electrons, so the
    // two SCC knobs would be inert kwargs and are withheld rather than
    // emitted as settings that change nothing.
    const bool gfnff = c.xtbMethod == "GFN-FF";
    out << "# xTB — semi-empirical tight binding (" << c.xtbMethod
        << ").\n"
           "# Fast and parameterized across most of the periodic table: a\n"
           "# screening / pre-relaxation method for molecules and molecular\n"
           "# crystals, not a DFT replacement.\n"
           "# xTB runs IN-PROCESS: there is no binary to launch and no\n"
           "# $PATH lookup, so it has to be installed in the interpreter this\n"
           "# script runs under. The package is `xtb` on PyPI and\n"
           "# `xtb-python` on conda-forge, and both import as `xtb`.\n"
           "try:\n"
           "    from xtb.ase.calculator import XTB\n"
           "except ImportError as exc:\n"
           "    raise SystemExit(\n"
           "        \"xTB is not installed in this Python environment, so the \"\n"
           "        \"GFN calculator cannot be created.\\n\"\n"
           "        \"    pip install xtb\\n\"\n"
           "        \"    conda install -c conda-forge xtb-python\\n\"\n"
           "        f\"(import error: {exc})\")\n"
           "\n"
           "# --- Isolated systems only -------------------------------------\n"
           "# Checked BEFORE the calculator is built, because the failure it\n"
           "# prevents is not an exception. xtb-python's in-process API\n"
           "# handles a periodic cell as follows, all three verified against\n"
           "# xtb-python 22.1:\n"
           "#\n"
           "#   GFN2-xTB / GFN1-xTB, any pbc -> CalculationFailed, \"xtb could\n"
           "#       not evaluate input\". These parameterizations are molecular;\n"
           "#       the API has no lattice summation to give them.\n"
           "#   GFN-FF, 2D-periodic          -> SIGSEGV or SIGABRT inside the\n"
           "#       force-field setup. The process dies with no traceback and\n"
           "#       no result file, which from the job console is\n"
           "#       indistinguishable from a machine problem.\n"
           "#   GFN-FF, 3D-periodic          -> XTBException, the virial is\n"
           "#       missing from the results ASE asks for.\n"
           "#\n"
           "# So the cell is refused here, with the reason, rather than passed\n"
           "# to a library that will crash on it. xTB is a molecular method in\n"
           "# this binding; a periodic system wants a different engine.\n"
           "if any(atoms.pbc):\n"
           "    raise SystemExit(\n"
           "        \"xTB cannot evaluate this structure: it is periodic \"\n"
           "        f\"(pbc={tuple(bool(p) for p in atoms.pbc)}), and the \"\n"
           "        \"in-process xtb-python API supports isolated systems \"\n"
           "        \"only.\\n\\n\"\n"
           "        \"  * Molecule or cluster: clear the periodic boundary \"\n"
           "        \"conditions on the structure and re-run.\\n\"\n"
           "        \"  * Genuinely periodic solid: use GPAW, Quantum \"\n"
           "        \"ESPRESSO, SIESTA or a machine-learned potential — all \"\n"
           "        \"of which take a cell.\\n\\n\"\n"
           "        \"This is refused rather than attempted because GFN-FF \"\n"
           "        \"does not raise on a 2D cell, it segfaults.\")\n"
           "\n"
           "atoms.calc = XTB(\n"
        << "    method=\"" << c.xtbMethod << "\",\n"
        << "    accuracy=" << c.xtbAccuracy
        << ",  # LOWER is tighter — one multiplier over the SCC thresholds\n";
    if (gfnff) {
        out << "    # GFN-FF is a force field: there is no self-consistent\n"
               "    # charge cycle, so an electronic temperature and an SCC\n"
               "    # iteration cap describe nothing and are left off.\n";
    } else {
        out << "    electronic_temperature=" << c.xtbElectronicTemperatureK
            << ",  # K — part of the GFN parameterization (300 K), not a\n"
               "    # convergence aid\n"
            << "    max_iterations=" << c.xtbMaxIterations << ",\n";
    }
    out << ")\n";
}

/// DFTB+ through ASE's file-IO calculator.
///
/// Two contracts here come from reading ase/calculators/dftb.py rather than
/// the docs. The binary: ASE takes $DFTB_COMMAND and appends its own
/// " > PREFIX.out" (then falls back to the [dftb] config section, then to
/// literally "dftb+ > PREFIX.out"), so the launch command must not carry a
/// redirection of its own. The tables: `slako_dir` falls back to
/// $DFTB_PREFIX, onto which ASE joins "<El>-<El>.skf" verbatim — the trailing
/// slash is part of the value.
void emitDftb(std::ostringstream& out, const CalculatorConfig& c)
{
    const bool fermi = c.dftbFillingTemperatureK > 0.0;
    out << "# DFTB+ through ASE — needs the dftb+ binary and a Slater-Koster\n"
           "# parameter set (e.g. mio-1-1, 3ob-3-1 from https://dftb.org).\n"
           "# The pairwise .skf tables ARE the parameterization: element\n"
           "# coverage is decided by the chosen set, and a missing pair fails\n"
           "# at run time with DFTB+'s own message.\n"
           "# ASE renders the Hamiltonian_* keywords below into dftb_in.hsd\n"
           "# and launches $DFTB_COMMAND, appending '> PREFIX.out' itself.\n"
           "import os\n"
           "from ase.calculators.dftb import Dftb\n";
    if (fermi)
        out << "from ase.units import Hartree, kB\n";
    out << "\n";
    if (!c.dftbSlakoDir.empty()) {
        std::string slako = c.dftbSlakoDir;
        if (slako.back() != '/')
            slako += '/';
        out << "# Slater-Koster tables. ASE joins '<El>-<El>.skf' onto this\n"
               "# verbatim, so the trailing slash is part of the value.\n"
               "os.environ.setdefault(\"DFTB_PREFIX\", r\""
            << slako << "\")\n\n";
    } else {
        out << "# EDIT ME: no Slater-Koster directory is configured. Point\n"
               "# DFTB_PREFIX at the .skf set (keep the trailing slash) or the\n"
               "# run stops at the first table it cannot open.\n"
               "os.environ.setdefault(\"DFTB_PREFIX\", r\"/path/to/slako/\")\n\n";
    }
    out << "atoms.calc = Dftb(\n"
        << "    kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2]
        << "),\n";
    if (c.dftbScc) {
        out << "    Hamiltonian_SCC=\"Yes\",\n"
            << "    Hamiltonian_SCCTolerance=" << c.dftbSccTolerance << ",\n"
            << "    Hamiltonian_MaxSCCIterations=" << c.dftbMaxSccIterations
            << ",\n";
    } else {
        // Tolerance and iteration cap describe a cycle that does not run, so
        // they are withheld with SCC rather than written as inert keys.
        out << "    # Non-SCC DFTB: one shot, no charge self-consistency —\n"
               "    # fast, and wrong wherever charge transfer matters.\n"
               "    Hamiltonian_SCC=\"No\",\n";
    }
    if (fermi) {
        // DFTB+ reads Temperature in its default energy unit (Hartree) when
        // no HSD [K] modifier is present, and ASE's keyword scheme cannot
        // write the modifier — so the K -> Hartree conversion happens here,
        // in the open, instead of a bare number nobody can audit.
        out << "    Hamiltonian_Filling_=\"Fermi\",\n"
            << "    Hamiltonian_Filling_Temperature="
            << c.dftbFillingTemperatureK
            << " * kB / Hartree,  # K -> Hartree (DFTB+'s default unit)\n";
    }
    out << ")\n";
    if (c.spinPolarized)
        out << "# NOTE: a spin-polarized DFTB+ run needs the\n"
               "# Hamiltonian_SpinPolarisation_* block plus per-element spin\n"
               "# constants, which depend on the parameter set — add them here\n"
               "# by hand (see the DFTB+ manual).\n";
}

/// GROMACS through ASE, which is unlike every other calculator here: ASE
/// shells out to the `gmx` binary per evaluation (pdb2gmx / grompp / mdrun /
/// energy / traj) and parses the numbers back out of .xvg text files.
///
/// Two hard constraints, both read out of ase/calculators/gromacs.py:
///
/// Topology. Nothing is typed automatically — pdb2gmx must recognize every
/// residue against the chosen force field's database. That is what makes
/// this a (bio)molecular engine, and why a bare inorganic crystal cannot run.
///
/// Tasks. Gromacs.calculate() ignores the Atoms object ASE hands it: it
/// reruns mdrun on the .g96/.tpr already on disk, and only its own update()
/// method ever rewrites those files. An ASE optimizer or MD loop would
/// therefore evaluate the starting geometry forever while appearing to run —
/// so anything but a single point is refused outright rather than generated
/// subtly wrong. Relaxation belongs to GROMACS itself (mdp integrator).
void emitGromacs(std::ostringstream& out, const CalculatorConfig& c)
{
    out << "# --- GROMACS ------------------------------------------------------\n"
           "#\n"
           "# GROMACS is an ENGINE for (bio)molecular force fields, driven\n"
           "# through the `gmx` binary: ASE writes the .mdp parameter file,\n"
           "# shells out to pdb2gmx/grompp/mdrun per evaluation and parses\n"
           "# energies and forces back from text output.\n"
           "#\n"
           "# The force field must be able to TYPE this structure: pdb2gmx\n"
           "# builds the topology from its residue database, so proteins,\n"
           "# water and known ligands work — a bare inorganic crystal has no\n"
           "# residue entry and will not run.\n";
    if (c.task != TaskKind::SinglePoint) {
        out << "raise RuntimeError(\n"
               "    \"GROMACS through ASE supports single-point evaluation "
               "only.\\n\"\n"
               "    \"ASE's Gromacs calculator reruns mdrun on the files "
               "already on\\n\"\n"
               "    \"disk and never rewrites them with the positions ASE "
               "moved, so an\\n\"\n"
               "    \"optimizer or MD loop would evaluate the starting "
               "geometry forever.\\n\"\n"
               "    \"Relax or run dynamics inside GROMACS itself (.mdp "
               "integrator /\\n\"\n"
               "    \"nsteps), or pick another engine for ASE-driven "
               "tasks.\")\n";
        return;
    }
    out << "from ase.calculators.gromacs import Gromacs\n"
           "\n"
           "# .mdp parameters. integrator=md with nsteps=0 makes mdrun a true\n"
           "# single point — the calculator's own default (a 10000-step cg\n"
           "# minimization) would silently relax before reporting.\n"
           "_mdp = {\n"
           "    \"integrator\": \"md\",\n"
           "    \"nsteps\": \"0\",\n";
    // Free-form `key = value` lines, one .mdp entry each — the same escape
    // hatch as VASP's extra INCAR tags, parsed here so the dict stays valid
    // Python whatever the user typed around the '='.
    {
        std::istringstream extra(c.gromacsExtraMdp);
        std::string line;
        const auto trim = [](std::string text) {
            const auto begin = text.find_first_not_of(" \t\r");
            const auto end = text.find_last_not_of(" \t\r");
            return begin == std::string::npos
                ? std::string()
                : text.substr(begin, end - begin + 1);
        };
        while (std::getline(extra, line)) {
            const std::string entry = trim(line.substr(0, line.find('#')));
            const auto equals = entry.find('=');
            if (entry.empty() || equals == std::string::npos)
                continue;
            const std::string key = trim(entry.substr(0, equals));
            const std::string value = trim(entry.substr(equals + 1));
            if (!key.empty())
                out << "    \"" << key << "\": \"" << value << "\",\n";
        }
    }
    out << "}\n"
           "\n"
           "calc = Gromacs(\n"
           "    label=\"gromacs\",\n"
        << "    force_field=\"" << c.gromacsForceField << "\",\n"
        << "    water_model=\"" << c.gromacsWaterModel << "\",\n";
    if (!c.gromacsExecutable.empty())
        out << "    command=r\"" << c.gromacsExecutable << "\",\n";
    else
        out << "    # No gmx path configured: ASE falls back to\n"
               "    # $ASE_GROMACS_COMMAND.\n";
    out << "    clean=True,\n"
           "    **_mdp,\n"
           ")\n"
           "\n"
           "# Written AFTER the constructor: clean=True sweeps gromacs.??? on\n"
           "# construction, which would delete a pdb exported first.\n"
           "write(\"gromacs.pdb\", atoms)\n"
           "\n"
           "# The input pipeline is explicit — the calculator does not run it\n"
           "# for you. Each tool logs to gromacs.<tool>.log, which is where a\n"
           "# 'residue not found in residue topology database' lands when the\n"
           "# force field cannot type this structure.\n"
           "calc.atoms = atoms\n"
           "calc.generate_topology_and_g96file()  # pdb2gmx -> gromacs.top / .g96\n"
           "calc.write_input()                    # -> gromacs.mdp\n"
           "calc.generate_gromacs_run_file()      # grompp  -> gromacs.tpr\n"
           "atoms.calc = calc\n";
}

void emitCalculator(std::ostringstream& out, const CalculatorConfig& c)
{
    switch (c.calculator) {
    case CalculatorKind::CalangoDft:
        // There is no ASE calculator to build. Calango's own engine runs in
        // process, so a run on it never reaches a generated script — the
        // caller dispatches to calango::dft::CalangoDFTEngine instead. Emitting
        // a comment rather than nothing so a script that somehow got here says
        // why it cannot run, instead of producing an `atoms` with no
        // calculator and failing at get_potential_energy().
        out << "raise RuntimeError(\n"
               "    \"The Calango DFT engine runs inside the application, \"\n"
               "    \"not as a generated script. Reaching this line means a \"\n"
               "    \"run was routed to the script generator instead of to \"\n"
               "    \"the built-in engine.\")\n";
        break;
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
               "    pseudo_dir=\"" << (c.espressoPseudoDir.empty()
                                          ? "/path/to/pseudopotentials"
                                          : c.espressoPseudoDir)
            << "\",\n"
               ")\n"
               "pseudopotentials = {\n"
               "    # \"Si\": \"Si.pbe-n-rrkjus_psl.1.0.0.UPF\",  # EDIT ME: one entry per element\n"
               "}\n"
               "\n"
               "# QE uses a DUAL grid: ecutwfc expands the wavefunctions,\n"
               "# ecutrho the charge density. Leaving ecutrho at its 4 x ecutwfc\n"
               "# default is right for norm-conserving pseudopotentials and\n"
               "# badly under-converged for ultrasoft/PAW, whose augmentation\n"
               "# charges want 8-12x.\n"
               "_system = {\n"
            << "    \"ecutwfc\": " << c.qeEcutwfcRy << ",  # Ry\n";
        if (c.qeEcutrhoRy > 0.0)
            out << "    \"ecutrho\": " << c.qeEcutrhoRy << ",  # Ry\n";
        if (!c.qeInputDft.empty())
            out << "    \"input_dft\": \"" << c.qeInputDft << "\",\n";
        out << "    \"occupations\": \"" << toString(c.qeOccupations) << "\",\n";
        if (qeUsesSmearing(c.qeOccupations))
            out << "    \"smearing\": \"" << toString(c.qeSmearing) << "\",\n"
                << "    \"degauss\": " << c.qeDegaussRy << ",  # Ry\n";
        if (c.spinPolarized)
            out << "    \"nspin\": 2,\n"
                   "    \"starting_magnetization\": 0.5,  # EDIT ME per species\n";
        out << "}\n"
               "\n"
               "atoms.calc = Espresso(\n"
               "    profile=profile,\n"
               "    pseudopotentials=pseudopotentials,\n"
               "    input_data={\n"
               "        \"control\": {\"calculation\": \"scf\", \"tprnfor\": True,\n"
               "                    \"tstress\": True},\n"
               "        \"system\": _system,\n"
            << "        \"electrons\": {\"conv_thr\": " << c.qeConvThrRy << "},  # Ry\n"
               "    },\n"
            << "    kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2]
            << "),\n";
        if (c.kptsGammaCentered)
            // QE offsets the automatic mesh by koffset; (0,0,0) IS the
            // Gamma-centred grid, and the unshifted mesh of an even count
            // misses Gamma entirely.
            out << "    koffset=(0, 0, 0),  # Gamma-centered\n";
        out << ")\n";
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
            if (c.maceDispersion)
                out << "# dispersion=True: adds the D3(BJ) van der Waals "
                       "correction mace_mp ships\n"
                       "# (needs the torch-dftd package in the job "
                       "environment).\n";
            out << "from mace.calculators import mace_mp\n"
                   "\n"
                << "atoms.calc = mace_mp(model="
                << (pinnedFile ? "r\"" + c.maceModelPath + "\"" : "\"" + c.maceSize + "\"")
                << ", device=\"" << c.maceDevice
                << "\", default_dtype=\"" << precision
                << "\", dispersion=" << (c.maceDispersion ? "True" : "False")
                << ")\n";
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
               "# (.psf/.psml) in the job environment.\n"
               "#\n"
               "# SIESTA has NO plane-wave cutoff. Its basis is a finite set of\n"
               "# numerical atomic orbitals, and its quality is set by the basis\n"
               "# SIZE (orbitals per shell), the basis TYPE (how they are\n"
               "# generated) and PAO.EnergyShift (the confinement energy that\n"
               "# fixes how far each orbital reaches — smaller means longer\n"
               "# ranged, better and more expensive). MeshCutoff below is a\n"
               "# different quantity entirely: the real-space grid the Hartree\n"
               "# and XC terms are integrated on.\n"
               "import os\n"
               "\n"
               "# Both of these are normally already in the environment: the\n"
               "# application exports the run command it resolved (the Conda\n"
               "# environment's own siesta, launched by that environment's MPI)\n"
               "# and the pseudopotential library from Preferences before the\n"
               "# job starts. setdefault is what lets that win, and what makes\n"
               "# this script still runnable on its own from a shell that\n"
               "# exports them itself.\n"
               "os.environ.setdefault(\"ASE_SIESTA_COMMAND\",\n"
               "                      \"siesta < PREFIX.fdf > PREFIX.out\")\n";
        if (c.siestaPseudoDir.empty()) {
            // No path is invented here. A fabricated SIESTA_PP_PATH does not
            // fail as a missing setting — it fails much later as "cannot find
            // the pseudopotential for element X", which reads like a broken
            // library rather than one that was never configured.
            out << "if not os.environ.get(\"SIESTA_PP_PATH\"):\n"
                   "    raise SystemExit(\n"
                   "        \"SIESTA_PP_PATH is not set: no pseudopotential \"\n"
                   "        \"library is configured. Set one in Preferences -> \"\n"
                   "        \"External Files, or export SIESTA_PP_PATH before \"\n"
                   "        \"running this script. SIESTA reads its .psf/.psml \"\n"
                   "        \"files from there and cannot start without it.\")\n";
        } else {
            out << "os.environ.setdefault(\"SIESTA_PP_PATH\", r\""
                << c.siestaPseudoDir << "\")\n";
        }
        out << "\n"
               "from ase.calculators.siesta import Siesta\n"
               "\n"
               "atoms.calc = Siesta(\n"
               "    label=\"calango\",\n"
            << "    xc=\"" << c.siestaXc << "\",\n"
            << "    basis_set=\"" << c.siestaBasisSize << "\",\n"
            << "    energy_shift=" << c.siestaEnergyShiftEv << ",  # eV\n"
            << "    mesh_cutoff=" << c.siestaMeshCutoffEv << ",  # eV\n"
            << "    kpts=[" << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2] << "],\n"
            << "    fdf_arguments={\n"
            << "        \"PAO.BasisType\": \"" << toString(c.siestaBasisType) << "\",\n"
            << "        \"SpinPolarized\": " << (c.spinPolarized ? "True" : "False")
            << ",\n"
               "    },\n"
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
                      "# framework), loaded from a packaged inference model.\n"
                    : "# NequIP — E(3)-equivariant message-passing potential,\n"
                      "# loaded from a packaged inference model.\n")
            << "# Requires:  pip install nequip"
            << (allegro ? " nequip-allegro   (formerly mir-allegro)\n" : "\n")
            << "# The model must be packaged for inference, never a raw training\n"
               "# checkpoint — but WHICH package depends on the nequip\n"
               "# generation: >= 0.7 compiles one with `nequip-compile`\n"
               "# (.nequip.pt2 / .nequip.pth) and dropped the old loader, while\n"
               "# <= 0.6 deployed TorchScript with `nequip-deploy build` (.pth).\n"
               "# Both entry points are bound below so the script runs on\n"
               "# either generation instead of dying at load on one of them.\n"
               "try:  # nequip >= 0.7 moved the calculator out of nequip.ase\n"
               "    from nequip.integrations.ase import NequIPCalculator\n"
               "except ImportError:\n"
               "    from nequip.ase import NequIPCalculator\n"
               "\n"
               "# The model carries its own training units; these rescale them\n"
               "# to ASE's eV / Angstrom.\n"
               "_nequip_units = dict(\n"
            << "    energy_units_to_eV="
            << (c.nequipEnergyUnits == "eV" ? "1.0" : "None") << ",  # "
            << c.nequipEnergyUnits << "\n"
            << "    length_units_to_A="
            << (c.nequipLengthUnits == "Angstrom" ? "1.0" : "None") << ",  # "
            << c.nequipLengthUnits << "\n"
               ")\n"
               "if hasattr(NequIPCalculator, \"from_compiled_model\"):\n"
               "    atoms.calc = NequIPCalculator.from_compiled_model(\n"
            << "        r\"" << c.nequipModelPath << "\",\n"
            << "        device=\"" << toString(c.mlipDevice) << "\",\n"
               "        **_nequip_units,\n"
               "    )\n"
               "else:  # the deployed-TorchScript loader of nequip <= 0.6\n"
               "    atoms.calc = NequIPCalculator.from_deployed_model(\n"
            << "        model_path=r\"" << c.nequipModelPath << "\",\n"
            << "        device=\"" << toString(c.mlipDevice) << "\",\n"
               "        **_nequip_units,\n"
               "    )\n";
        if (c.nequipEnergyUnits != "eV" || c.nequipLengthUnits != "Angstrom")
            out << "# EDIT ME: this model was trained in " << c.nequipEnergyUnits
                << " / " << c.nequipLengthUnits << ". Replace the None values\n"
                   "# above with the numeric conversion factors to eV / Angstrom\n"
                   "# (e.g. 0.0433641 for kcal/mol -> eV) — the run fails until\n"
                   "# they are numbers, which beats a silent factor of 1.0.\n";
        break;
    }

    case CalculatorKind::ChgNet:
        out << "# CHGNet — universal pretrained potential with magnetic-moment\n"
               "# prediction (crystal Hamiltonian graph network).\n"
               "# Requires:  pip install chgnet\n"
               "from chgnet.model.dynamics import CHGNetCalculator\n"
               "from chgnet.model.model import CHGNet\n"
               "\n";
        if (c.chgnetWeights == ChgNetWeights::Latest)
            // CHGNet.load() knows only version strings ("0.3.0", "0.2.0",
            // "r2scan") — model_name="latest" raises ValueError. "Track the
            // installed release" is spelled by omitting the argument.
            out << "# No model_name: the installed chgnet release's own default\n"
                   "# checkpoint.\n"
                   "model = CHGNet.load()\n";
        else
            out << "model = CHGNet.load(model_name=\""
                << toString(c.chgnetWeights) << "\")\n";
        // stress_weight is NOT an on/off switch: it is the GPa -> eV/Å³
        // conversion for the stress CHGNet always computes. Overriding it to
        // 1.0 reported stresses ~160x too large (in GPa), and 0.0 zeroed
        // them silently — so the kwarg is left at CHGNet's own default.
        out << "atoms.calc = CHGNetCalculator(model=model, use_device=\""
            << toString(c.mlipDevice) << "\")\n";
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
            // Metadata only: MatterSim's v1 checkpoints were TRAINED across
            // T/P but take no thermodynamic-state input at inference —
            // neither the calculator nor Potential.from_checkpoint reads
            // these keys. Recording them documents the intended state for
            // downstream tooling without implying the model consumed it.
            out << "# Requested thermodynamic state, recorded as metadata:\n"
                << "#   T = " << c.matterSimTemperatureK << " K, P = "
                << c.matterSimPressureGPa << " GPa\n"
                   "# MatterSim v1 takes no T/P input at inference (it was\n"
                   "# trained across that range); these keys only annotate the\n"
                   "# run for downstream tooling.\n"
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

    case CalculatorKind::Lammps:
        emitLammps(out, c);
        break;

    case CalculatorKind::Xtb:
        emitXtb(out, c);
        break;

    case CalculatorKind::DftbPlus:
        emitDftb(out, c);
        break;

    case CalculatorKind::Gromacs:
        emitGromacs(out, c);
        break;

    case CalculatorKind::Vasp:
        emitVasp(out, c);
        break;

    // The engines added later live in core/EngineCalculatorBlocks.cpp — one
    // self-contained block each, kept out of this file's 2600 lines.
    case CalculatorKind::Abinit:
        EngineBlocks::emitAbinit(out, c);
        break;

    case CalculatorKind::FhiAims:
        EngineBlocks::emitAims(out, c);
        break;

    case CalculatorKind::NwChem:
        EngineBlocks::emitNwChem(out, c);
        break;

    case CalculatorKind::OpenMx:
        EngineBlocks::emitOpenMx(out, c);
        break;

    case CalculatorKind::Fleur:
        EngineBlocks::emitFleur(out, c);
        break;

    case CalculatorKind::Cp2k:
        EngineBlocks::emitCp2k(out, c);
        break;

    case CalculatorKind::Amber:
        EngineBlocks::emitAmber(out, c);
        break;
    }

    emitDispersion(out, c);
}

/// Post-SCF volumetric exports, one Gaussian cube per selected field.
///
/// Every field is a grid over the SAME converged calculation, so they share one
/// writer: the only per-field differences are which GPAW call produces the
/// array and what the file is named. The API for two of them is not obvious and
/// was established against a live GPAW 26.x rather than from the docs:
///
///   ELF  gpaw.elf.elf_from_dft_calculation(calc)  — the module-level function;
///        the older `ELF` class it replaced no longer exists in this line.
///   tau  density.update_ked(dft.ibzwfs) then density.taut_sR — the kinetic
///        energy density is not exposed on the ASE calculator at all.
///
/// Both are wrapped so an unsupported build skips that field with a message
/// instead of losing the whole run's exports after the SCF has already been
/// paid for.
std::string gpawDensityExportBlock(const CalculatorConfig& c)
{
    GpawDensityExports fields = c.gpawDensityExports;
    if (!fields.any()) {
        // A config from a saved project (or the headless path) carries only the
        // old single choice; honour it so those keep working unchanged.
        if (c.gpawDensityType == GpawDensityType::AllElectron)
            fields.allElectron = true;
        else
            fields.pseudo = true;
    }

    std::ostringstream out;
    out << "\n"
           "# --- Volumetric exports ------------------------------------------\n"
           "from ase.io.cube import write_cube\n"
           "\n"
           "\n"
           "def _calango_write_cube(name, data, label):\n"
           "    \"\"\"One field to <name>.cube, normalized to a contiguous grid.\n"
           "\n"
           "    The new GPAW engine hands back a (possibly distributed)\n"
           "    array-like rather than a plain ndarray, so `.data` is unwrapped\n"
           "    and the result made contiguous before write_cube touches it.\n"
           "    \"\"\"\n"
           "    grid = _np.ascontiguousarray(\n"
           "        _np.asarray(getattr(data, 'data', data), dtype=float))\n"
           "    with open(name, 'w') as handle:\n"
           "        write_cube(handle, atoms, data=grid)\n"
           "    print(f'CALANGO_RESULT density_cube={name} {label}', flush=True)\n"
           "\n"
           "\n"
           "def _calango_export(name, label, produce):\n"
           "    \"\"\"Guarded export: one unsupported field must not cost the\n"
           "    others, which are already paid for by the converged SCF.\"\"\"\n"
           "    try:\n"
           "        _calango_write_cube(name, produce(), label)\n"
           "    except Exception as exc:\n"
           "        print(f'CALANGO_INFO {label} export unavailable: {exc!r}',\n"
           "              flush=True)\n"
           "\n"
           "_calc = atoms.calc\n";

    if (fields.allElectron) {
        out << "_calango_export('" << densityFiles::kAllElectron
            << "', 'all_electron',\n"
               "                lambda: _calc.get_all_electron_density("
               "gridrefinement=2))\n";
    }
    if (fields.pseudo) {
        out << "_calango_export('" << densityFiles::kPseudo << "', 'pseudo',\n"
               "                lambda: _calc.get_pseudo_density())\n";
    }
    if (fields.spin) {
        // n(up) - n(down). In a spin-restricted run the two are the same array
        // and the difference is identically zero, so say so rather than writing
        // a cube of noise the user would try to interpret.
        out << "def _calango_spin_density():\n"
               "    up = _np.asarray(getattr(_calc.get_pseudo_density(spin=0),\n"
               "                             'data', "
               "_calc.get_pseudo_density(spin=0)), dtype=float)\n"
               "    down = _np.asarray(getattr(_calc.get_pseudo_density(spin=1),\n"
               "                               'data', "
               "_calc.get_pseudo_density(spin=1)), dtype=float)\n"
               "    if _np.abs(up - down).max() < 1e-12:\n"
               "        raise RuntimeError(\n"
               "            'the spin density is identically zero — this run is "
               "spin-restricted; '\n"
               "            'set Spin polarization to Collinear to get a "
               "meaningful field')\n"
               "    return up - down\n"
               "\n"
               "_calango_export('"
            << densityFiles::kSpin << "', 'spin', _calango_spin_density)\n";
    }
    if (fields.hartree) {
        out << "_calango_export('" << densityFiles::kHartree << "', 'hartree',\n"
               "                lambda: _calc.get_electrostatic_potential())\n";
    }
    if (fields.elf) {
        out << "def _calango_elf():\n"
               "    # Module-level function in the current GPAW line; the older\n"
               "    # `ELF` class is kept as a fallback for 24.x and earlier.\n"
               "    try:\n"
               "        from gpaw.elf import elf_from_dft_calculation\n"
               "        return elf_from_dft_calculation(_calc)\n"
               "    except ImportError:\n"
               "        from gpaw.elf import ELF\n"
               "        handle = ELF(_calc)\n"
               "        handle.update()\n"
               "        return handle.get_electronic_localization_function()\n"
               "\n"
               "_calango_export('"
            << densityFiles::kElf << "', 'elf', _calango_elf)\n";
    }
    if (fields.kineticEnergy) {
        out << "def _calango_kinetic_energy_density():\n"
               "    # Not exposed on the ASE calculator: it lives on the DFT\n"
               "    # object's density, and only after update_ked() has been\n"
               "    # asked to build it from the wavefunctions.\n"
               "    dft = getattr(_calc, 'dft', _calc)\n"
               "    density = dft.density\n"
               "    density.update_ked(dft.ibzwfs)\n"
               "    return _np.asarray(density.taut_sR.data).sum(axis=0)\n"
               "\n"
               "_calango_export('"
            << densityFiles::kKineticEnergy << "', 'kinetic_energy',\n"
               "                _calango_kinetic_energy_density)\n";
    }
    return out.str();
}

/// The task body for a relaxation VASP runs by itself.
///
/// One `get_potential_energy()` call does the whole relaxation: VASP walks the
/// ionic steps internally and hands back the final geometry. There is no ASE
/// optimizer here at all, which is the entire point — see VaspRelaxDriver.
///
/// The outputs are deliberately the same files an ASE-driven relaxation
/// produces (opt.traj, optimized.extxyz, geometry_optimization.json), so the
/// Geometry Optimization Viewer and the trajectory tab work identically
/// whichever driver ran. The per-step data comes out of VASP's own OUTCAR
/// rather than from an optimizer callback, since the steps happened inside the
/// single call above.
void emitVaspInternalRelaxation(std::ostringstream& out,
                                const CalculatorConfig& c)
{
    out << "# Relaxation driven by VASP (IBRION / NSW / ISIF / EDIFFG above).\n"
           "#\n"
           "# ASE creates no optimizer here. The one call below runs every\n"
           "# ionic step inside VASP, which keeps the wavefunction and charge\n"
           "# density between them — the reason to use this mode. The cost is\n"
           "# that the steps are not visible until it returns.\n"
           "import json\n"
           "import numpy as _np\n"
           "from ase.io import read as _read\n"
           "from ase.io.trajectory import Trajectory\n"
           "\n"
        << "max_steps = " << c.maxSteps
        << "\n"
           "_calango_progress(0, max_steps)\n"
           "print('CALANGO_INFO VASP is driving the relaxation; '\n"
           "      'ionic steps are reported when it returns', flush=True)\n"
           "\n"
           "_e0 = atoms.get_potential_energy()\n"
           "\n"
           "# VASP has rewritten the geometry in place on `atoms`; CONTCAR is\n"
           "# the same structure and is read back only when ASE did not update\n"
           "# the object (older interface versions).\n"
           "import os\n"
           "if os.path.exists('CONTCAR') and os.path.getsize('CONTCAR') > 0:\n"
           "    try:\n"
           "        _final = _read('CONTCAR')\n"
           "        atoms.set_positions(_final.get_positions())\n"
           "        atoms.set_cell(_final.get_cell())\n"
           "    except Exception as _error:\n"
           "        print(f'CALANGO_WARN could not re-read CONTCAR: {_error}',\n"
           "              flush=True)\n"
           "\n"
           "# The ionic path, recovered from OUTCAR so the viewer and the\n"
           "# trajectory tab get the same frames an ASE-driven run would give.\n"
           "_frames = []\n"
           "try:\n"
           "    _frames = _read('OUTCAR', index=':')\n"
           "except Exception as _error:\n"
           "    print(f'CALANGO_WARN could not read the ionic path from OUTCAR: '\n"
           "          f'{_error}', flush=True)\n"
           "if _frames:\n"
           "    _traj = Trajectory('opt.traj', 'w')\n"
           "    for _step, _frame in enumerate(_frames):\n"
           "        _traj.write(_frame)\n"
           "        try:\n"
           "            _energy = float(_frame.get_potential_energy())\n"
           "            _fmax = float(_np.linalg.norm(_frame.get_forces(),\n"
           "                                          axis=1).max())\n"
           "            _calango_metric(_step, energy=_energy,\n"
           "                            max_force=_fmax)\n"
           "        except Exception:\n"
           "            pass  # a frame without a calculator attached\n"
           "    _traj.close()\n"
           "    _calango_progress(len(_frames), max(max_steps, len(_frames)))\n"
           "\n"
           "energy = atoms.get_potential_energy()\n"
           "_forces = _np.asarray(atoms.get_forces(), dtype=float)\n"
           "_force_norms = _np.linalg.norm(_forces, axis=1)\n"
           "fmax_final = float(_force_norms.max()) if _force_norms.size else 0.0\n"
        << "converged = bool(fmax_final <= " << c.fmax
        << ")\n"
           "write('optimized.extxyz', atoms)\n"
           "print(f'CALANGO_RESULT converged={converged} "
           "energy_eV={energy:.6f}', flush=True)\n"
           "\n"
           "_energy_first = None\n"
           "if _frames:\n"
           "    try:\n"
           "        _energy_first = float(_frames[0].get_potential_energy())\n"
           "    except Exception:\n"
           "        _energy_first = None\n"
           "_summary = {\n"
           "    'driver': 'vasp',\n"
           "    'converged': converged,\n"
           "    'steps': len(_frames),\n"
        << "    'fmax_target_eV_per_A': " << c.fmax
        << ",\n"
           "    'fmax_final_eV_per_A': fmax_final,\n"
           "    'energy_eV': float(energy),\n"
           "    'energy_initial_eV': _energy_first,\n"
           "    'energy_change_eV': (float(energy) - _energy_first)\n"
           "    if _energy_first is not None else None,\n"
           "    'natoms': len(atoms),\n"
           "    'forces_eV_per_A': _forces.tolist(),\n"
           "}\n"
           "with open('geometry_optimization.json', 'w') as _fh:\n"
           "    json.dump(_summary, _fh, indent=2)\n"
           "print('CALANGO_RESULT geometry_optimization="
           "geometry_optimization.json', flush=True)\n";
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
                << "#   smearing           : " << toString(c.smearing);
            // Only report the parameters the method actually reads — a width
            // printed beside "Tetrahedron method" reads as a setting that was
            // applied, when nothing consumes it.
            if (smearingUsesWidth(c.smearing))
                out << " (width " << c.smearingWidthEv << " eV)";
            if (smearingUsesOrder(c.smearing))
                out << " (order " << c.smearingOrder << ")";
            out << "\n";
        }
        out << "import numpy as _np\n"
               "import json\n"
               "energy = atoms.get_potential_energy()\n"
               "_forces = _np.asarray(atoms.get_forces(), dtype=float)\n"
               "_fnorms = _np.linalg.norm(_forces, axis=1) if _forces.size "
               "else _np.zeros(0)\n"
               "fmax = float(_fnorms.max()) if _fnorms.size else 0.0\n"
               "_fmax_atom = int(_fnorms.argmax()) if _fnorms.size else -1\n"
               "_calango_metric(0, energy=energy, max_force=fmax)\n"
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
               "    _magmom = None\n"
               "# Per-atom moments, which is what a magnetic structure is\n"
               "# actually about: the total is 0 for any antiferromagnet, so\n"
               "# reporting only the total said \"not magnetic\" about exactly\n"
               "# the cases worth looking at. Collinear gives (N,); a\n"
               "# non-collinear run gives (N, 3) and is kept as such.\n"
               "try:\n"
               "    _magmoms = _np.asarray(atoms.get_magnetic_moments(),\n"
               "                           dtype=float).tolist()\n"
               "except Exception:\n"
               "    _magmoms = None\n";
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
               "    \"magnetic_moments\": _magmoms,\n"
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
               "flush=True)\n"
               "# The same geometry with the CONVERGED per-atom results\n"
               "# attached. ase.io.extxyz writes the calculator's results as\n"
               "# columns (forces:R:3, magmoms:R:1), which is how the computed\n"
               "# moments reach the viewport's vector overlay — a single point\n"
               "# writes no trajectory, so without this file the overlay had\n"
               "# nothing but the input guess to draw.\n"
               "write(\"single_point.extxyz\", atoms)\n"
               "print(\"CALANGO_RESULT structure=single_point.extxyz\", "
               "flush=True)\n";
        if (c.calculator == CalculatorKind::Gpaw) {
            // Save the converged charge density so a later Electronic Structure
            // run can load it and evaluate bands/PDOS non-self-consistently
            // (mode="all" writes the density + wavefunctions). This file is what
            // the Electronic Structure wizard's baseline selector looks for.
            out << "atoms.calc.write(\"single_point.gpw\", mode=\"all\")\n"
                   "print(\"CALANGO_RESULT density_file=single_point.gpw\", "
                   "flush=True)\n";
            if (c.gpawExportDensity)
                out << gpawDensityExportBlock(c);
        }
        break;

    case TaskKind::GeometryOptimization: {
        if (vaspDrivesRelaxation(c)) {
            emitVaspInternalRelaxation(out, c);
            break;
        }
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
               "    _calango_progress(opt.nsteps, max_steps)\n"
               "    energy = atoms.get_potential_energy()\n"
               "    fmax_now = abs(atoms.get_forces()).max()\n"
               "    _calango_metric(opt.nsteps, energy=energy, max_force=fmax_now)\n"
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

    case TaskKind::MolecularDynamics: {
        // Annealing is a thermostat schedule, so it needs a thermostat. NVE
        // has none; the wizard does not offer the combination, and if one
        // arrives anyway (a project file written before this existed, a
        // headless caller) the run degrades to plain NVE rather than
        // pretending to anneal.
        const bool annealing = c.annealing && isConstantTemperature(c.ensemble);
        // Velocities are drawn at the setpoint the run STARTS from. Seeding a
        // 1000 K quench with 300 K velocities would spend the first
        // picosecond letting the thermostat pump the system up to where the
        // schedule already claims it is.
        const double seedTemperature = annealing ? c.annealStartK : c.temperatureK;

        out << "from ase import units\n"
               "from ase.md.velocitydistribution import (MaxwellBoltzmannDistribution,\n"
               "                                         Stationary, ZeroRotation)\n"
               "\n";
        if (annealing)
            out << "import math\n"
                   "\n"
                   "# ---- Simulated annealing --------------------------------\n"
                   "# One ordinary thermostatted MD run whose SETPOINT moves.\n"
                   "# The integrator, the constraints, the sampling and the\n"
                   "# trajectory are exactly those of a constant-temperature\n"
                   "# run; only the target is retargeted, every step.\n"
                << "T_initial = " << c.annealStartK << "  # K, at step 0\n"
                << "T_final = " << c.annealEndK << "  # K, at the last step\n"
                << "anneal_k = " << std::max(c.annealCoefficient, 1.0e-3)
                << "  # ramp curvature (-> 0 is a straight line)\n"
                   "\n";
        out << "temperature_K = " << seedTemperature
            << (annealing ? "  # starting setpoint; the schedule moves it\n"
                          : "\n")
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

        if (annealing) {
            out << "\n"
                   "def _anneal_target(step):\n"
                   "    \"\"\"Thermostat setpoint (K) at MD step `step`.\n"
                   "\n"
                   "    x runs 0 -> 1 over the run. The law is endpoint-exact:\n"
                   "    T(0) == T_initial and T(md_steps) == T_final whatever\n"
                   "    anneal_k is, so the run finishes at the temperature that\n"
                   "    was asked for rather than near it.\n"
                   "    \"\"\"\n"
                   "    x = min(1.0, max(0.0, step / md_steps)) if md_steps > 0 else 1.0\n"
                << "    return "
                << annealingPythonExpression(c.annealingSchedule) << "\n"
                   "\n"
                   "\n"
                   "def _set_target_temperature(value):\n"
                   "    \"\"\"Retarget the running thermostat.\n"
                   "\n"
                   "    ASE has no single spelling for this. Most integrators\n"
                   "    expose set_temperature(); the Nose-Hoover chain keeps its\n"
                   "    target inside a thermostat object whose fictitious masses\n"
                   "    are PROPORTIONAL TO kT, so the masses have to move with it\n"
                   "    — leaving them behind gives a chain tuned for a\n"
                   "    temperature it is no longer aiming at, which shows up as a\n"
                   "    thermostat that lags further behind the ramp the further\n"
                   "    the run gets from where it started.\n"
                   "    \"\"\"\n"
                   "    setter = getattr(dyn, \"set_temperature\", None)\n"
                   "    if setter is not None:\n"
                   "        setter(temperature_K=value)\n"
                   "        return\n"
                   "    thermostat = getattr(dyn, \"_thermostat\", None)\n"
                   "    if thermostat is not None and hasattr(thermostat, \"_kT\"):\n"
                   "        previous = thermostat._kT\n"
                   "        thermostat._kT = units.kB * value\n"
                   "        if previous > 0 and hasattr(thermostat, \"_Q\"):\n"
                   "            thermostat._Q *= thermostat._kT / previous\n"
                   "        for attribute, new in ((\"_kT\", thermostat._kT),\n"
                   "                               (\"_temperature_K\", value)):\n"
                   "            if hasattr(dyn, attribute):\n"
                   "                setattr(dyn, attribute, new)\n"
                   "        return\n"
                   "    _calango_event(\"warning\",\n"
                   "                   \"this integrator cannot be retargeted; the \"\n"
                   "                   \"run is at a fixed temperature, not annealed\")\n"
                   "\n"
                   "\n"
                   "# Retargeted EVERY step, not every sample_interval: a setpoint\n"
                   "# that jumps in sampling-sized increments is a staircase, and\n"
                   "# every riser is a thermal shock the thermostat then has to\n"
                   "# absorb — visible as sawtooth ringing on the temperature\n"
                   "# trace that has nothing to do with the physics.\n"
                   "dyn.attach(lambda: _set_target_temperature(_anneal_target(dyn.nsteps)),\n"
                   "           interval=1)\n"
                   "_set_target_temperature(_anneal_target(0))\n";
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
               "        _calango_event(\"warning\",\n"
               "                       f\"no forces for the extxyz dump: {error}\")\n"
               "    write(_md_xyz, snapshot, format=\"extxyz\", append=True)\n"
               "\n"
               "# No t = 0 dump: the raw input geometry has no evaluated\n"
               "# forces of its own and no thermalized velocities yet, so it\n"
               "# would be the one frame in the trajectory without a vector\n"
               "# overlay. Recording starts at the first integrated step.\n"
               "dyn.attach(lambda: _dump_extxyz() if dyn.nsteps > 0 else None,\n"
               "           interval=sample_interval)\n";

        if (annealing)
            // Deliberately NOT CALANGO_TARGET_TEMP: that marker draws ONE
            // dashed reference line, and a horizontal line through a ramp is
            // a claim about the run that is wrong at every step but two. The
            // moving setpoint is logged per sample instead (below), which is
            // what the viewer plots against the measured temperature.
            out << "\n"
                << "print(f\"CALANGO_INFO annealing {T_initial:g} K -> {T_final:g} K"
                << " over {md_steps} steps (" << toString(c.annealingSchedule)
                << " schedule)\", flush=True)\n";
        else if (isConstantTemperature(c.ensemble))
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
               "    _calango_progress(dyn.nsteps, md_steps)\n";

        // The setpoint the thermostat is aiming at when the sample is taken.
        // Logged as its own series so the MD Viewer can draw the ramp under
        // the measured temperature: without it, "is the system following the
        // schedule?" — the only question an annealing run is asking — cannot
        // be answered from the plot at all.
        const char* const targetField =
            annealing ? "                    target_temperature=_anneal_target(dyn.nsteps),\n"
                      : "";

        if (isConstantPressure(c.ensemble))
            out << "    # Scalar pressure P = -tr(σ)/3 from the full stress tensor\n"
                   "    # (eV/Å³ → GPa); only meaningful with a barostatted cell.\n"
                   "    stress = atoms.get_stress(voigt=True)\n"
                   "    pressure_GPa = -(stress[0] + stress[1] + stress[2]) / 3.0 / units.GPa\n"
                   "    _calango_metric(dyn.nsteps, energy=epot, temperature=temp,\n"
                << targetField
                << "                    kinetic=ekin, volume=atoms.get_volume(),\n"
                   "                    max_force=fmax_now, pressure=pressure_GPa)\n";
        else
            out << "    # Kinetic energy and volume are logged alongside the\n"
                   "    # potential energy so the MD Viewer can show E_tot =\n"
                   "    # E_pot + E_kin (whose drift is the integrator health\n"
                   "    # check) rather than only the potential term.\n"
                   "    _calango_metric(dyn.nsteps, energy=epot, temperature=temp,\n"
                << targetField
                << "                    kinetic=ekin,\n"
                   "                    volume=(atoms.get_volume() if atoms.cell.rank == 3\n"
                   "                            else 0.0),\n"
                   "                    max_force=fmax_now)\n";

        out << "    print(f\"CALANGO_MD step={dyn.nsteps} epot_eV={epot:.4f} ekin_eV={ekin:.4f}"
               " T_K={temp:.1f}";
        if (annealing)
            out << " T_target_K={_anneal_target(dyn.nsteps):.1f}";
        out << "\", flush=True)\n"
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
    line += "\n";

    // Custom LCAO basis sets (Preferences -> External Files). Emitted here
    // because this is the one place every GPAW-using generator goes through,
    // and because the search path has to be extended BEFORE any calculator is
    // constructed — GPAW resolves the basis when it builds its setups.
    //
    // INSERTED at the front of setup_paths, never assigned over it: the same
    // list is how GPAW finds its PAW datasets, so replacing it would resolve
    // basis="my-tz2p" and then fail to find the setup for hydrogen. The
    // application also exports GPAW_SETUP_PATH for the job, which GPAW itself
    // prepends; doing it again here is what keeps an exported script working
    // when it is run by hand.
    if (c.gpawMode == GpawMode::Lcao && !c.gpawBasisDir.empty()) {
        std::ostringstream out;
        out << line
            << "\n"
               "# Custom LCAO basis sets: GPAW looks for files named\n"
               "# <symbol>.<name>.basis along gpaw.setup_paths, so a basis it\n"
               "# did not ship with is only findable once its directory is on\n"
               "# that list.\n"
               "import os as _basis_os\n"
               "import gpaw as _gpaw\n"
               "\n"
            << "_basis_dir = r\"" << c.gpawBasisDir << "\"\n"
               "if _basis_dir and _basis_dir not in "
               "[str(_p) for _p in _gpaw.setup_paths]:\n"
               "    _gpaw.setup_paths.insert(0, _basis_dir)\n"
               "if not _basis_os.path.isdir(_basis_dir):\n"
               "    print(f\"CALANGO_WARN the custom LCAO basis directory \"\n"
               "          f\"{_basis_dir!r} does not exist; only the basis \"\n"
               "          f\"sets shipped with GPAW are available\", flush=True)\n";
        return out.str();
    }
    return line;
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
    // The van der Waals functionals carry a non-local correlation kernel that
    // GPAW evaluates through libvdwxc — selected with the {'name', 'backend'}
    // dict rather than the plain string (which would fall back to the old
    // slow FFT implementation, or fail outright for VV10/rVV10). Needs a GPAW
    // build compiled against libvdwxc.
    static const char* const kLibvdwxcFunctionals[] = {
        "vdW-DF",     "vdW-DF2",    "vdW-DF-cx", "optPBE-vdW",
        "optB88-vdW", "BEEF-vdW",   "VV10",      "rVV10",
    };
    const bool vdwXc =
        std::any_of(std::begin(kLibvdwxcFunctionals),
                    std::end(kLibvdwxcFunctionals),
                    [&c](const char* name) { return c.gpawXc == name; });
    if (vdwXc)
        out << indent << "xc={\"name\": \"" << c.gpawXc
            << "\", \"backend\": \"libvdwxc\"},  # non-local vdW functional\n";
    else
        out << indent << "xc=\"" << c.gpawXc << "\",\n";
    if (c.kptsGammaCentered)
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
    } else if (c.gpawPointGroupOff) {
        // Point group off, time reversal KEPT. The weaker of the two requests,
        // and the right one for the response codes: they need the unfolded
        // point group, but a nonmagnetic system still satisfies E(k) = E(-k),
        // so dropping time reversal as well would double the k-points for
        // nothing.
        out << indent
            << "symmetry={\"point_group\": False, \"time_reversal\": True},\n"
            << indent
            << "                       # unfolded point group, time reversal\n"
            << indent
            << "                       # kept (nonmagnetic)\n";
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
    if (!c.gpawNbands.empty()) {
        // Quoted unless it is a plain integer: GPAW takes both nbands=200 and
        // the symbolic nbands="nao" / nbands="110%", and quoting a number
        // would be a type error rather than a harmless extra.
        const bool numeric =
            c.gpawNbands.find_first_not_of("+-0123456789") == std::string::npos;
        out << indent << "nbands=" << (numeric ? "" : "\"") << c.gpawNbands
            << (numeric ? "" : "\"") << ",\n";
    }
    bool eigensolverCoerced = false;
    const std::string eigensolver = eigensolverFor(c, &eigensolverCoerced);
    if (eigensolverCoerced)
        // Said in the script, because the script is what the user reads back
        // when the number looks wrong. A silent substitution of the SCF
        // solver is not something to discover from a diff against another run.
        out << indent << "# Eigensolver set to \"" << eigensolver
            << "\" for this mode: GPAW admits only the direct LCAO solver\n"
            << indent << "# (\"lcao\"/\"etdm-lcao\") in LCAO mode and only the "
               "iterative grid/plane-wave\n"
            << indent << "# solvers (dav/cg/rmm-diis) outside it — the pairing "
               "is an assertion, not a\n"
            << indent << "# preference.\n";
    out << indent << "eigensolver=\"" << eigensolver << "\",\n"
        // Mixer(beta, nmaxold, weight) — GPAW's positional signature.
        << indent << "mixer=" << toString(c.gpawMixer) << "(" << c.gpawMixerBeta
        << ", " << c.gpawMixerNmaxold << ", " << c.gpawMixerWeight << "),\n"
        << indent << "convergence={\n"
        << indent << "    \"energy\": " << c.scfEnergyTolEv
        << ",       # eV/electron\n"
        << indent << "    \"eigenstates\": " << c.gpawConvEigenstates
        << ",  # eV^2/electron\n"
        << indent << "    \"density\": " << c.gpawConvDensity
        << ",      # electrons/valence electron\n";
    if (c.gpawConvergeBands != 0)
        out << indent << "    \"bands\": " << c.gpawConvergeBands
            << ",  # converge EMPTY states too (negative counts from the top)\n";
    out << indent << "},\n"
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
    // Occupations. Each method gets its own GPAW `name` and only the keys that
    // name accepts — a width on a tetrahedron scheme or an order on Fermi-Dirac
    // is not a harmless extra, GPAW raises on it.
    // https://gpaw.readthedocs.io/documentation/smearing.html
    if (c.smearing == SmearingMethod::None) {
        // GPAW's own way of spelling "do not smear".
        out << indent
            << "occupations={\"name\": \"fermi-dirac\", \"width\": 0.0},\n";
    } else {
        out << indent << "occupations={\"name\": \"" << gpawSmearingName(c.smearing)
            << "\"";
        if (smearingUsesWidth(c.smearing))
            out << ", \"width\": " << c.smearingWidthEv;
        if (smearingUsesOrder(c.smearing))
            out << ", \"order\": " << c.smearingOrder;
        else if (c.smearing == SmearingMethod::Gaussian)
            // Written out rather than left to GPAW's default: order 0 is what
            // makes this Gaussian rather than some other MP expansion, and a
            // change to that default would otherwise silently alter the run.
            out << ", \"order\": 0";
        if (smearingUsesFixedOccupations(c.smearing)) {
            out << ", \"numbers\": [";
            for (std::size_t s = 0; s < c.fixedOccupations.size(); ++s) {
                out << (s ? ", [" : "[");
                const auto& channel = c.fixedOccupations[s];
                for (std::size_t n = 0; n < channel.size(); ++n)
                    out << (n ? ", " : "") << channel[n];
                out << "]";
            }
            out << "]";
        }
        out << "},";
        if (c.smearing == SmearingMethod::Gaussian)
            out << "  # Gaussian == Methfessel-Paxton at order 0";
        // An empty `numbers` is not something the generator can paper over —
        // there is no configuration to guess. Marked loudly here so the
        // script-review stage shows the gap rather than the run failing later
        // with a message that does not name the cause.
        if (smearingUsesFixedOccupations(c.smearing) && c.fixedOccupations.empty())
            out << "  # <- EMPTY: enter one occupation number per band in the "
                   "setup dialog";
        out << "\n";
        if (c.smearing == SmearingMethod::TetrahedronMethod
            || c.smearing == SmearingMethod::ImprovedTetrahedronMethod)
            out << indent
                << "# The tetrahedron methods integrate the BZ over a "
                   "Monkhorst-Pack grid;\n"
                << indent
                << "# they take no width and will fail on a Gamma-only "
                   "sampling.\n";
    }
    return out.str();
}

std::string AseScriptGenerator::calculatorSnippet(const CalculatorConfig& config)
{
    std::ostringstream out;
    emitCalculator(out, config);
    return out.str();
}

std::string AseScriptGenerator::streamFrameHelper()
{
    return kStreamFrameHelper;
}

std::string AseScriptGenerator::jsonLoggerPreamble()
{
    return kJsonLoggerHelper;
}

std::string AseScriptGenerator::densityCubeScript(const std::string& gpwDir,
                                                  bool allElectron)
{
    std::ostringstream out;
    out << "# Charge-density export — generated by Calango\n"
           "import os\n"
           "import glob\n"
           "import numpy as _np\n"
           "\n"
        << kJsonLoggerHelper
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
           "_calango_progress(1, 2)\n"
        << "_density = calc."
        << (allElectron ? "get_all_electron_density(gridrefinement=2)"
                        : "get_pseudo_density()")
        << "\n"
           "_density = _np.ascontiguousarray(\n"
           "    _np.asarray(getattr(_density, 'data', _density), dtype=float))\n"
           "from ase.io.cube import write_cube\n"
        << "with open('" << densityFiles::kDensity << "', 'w') as _dfh:\n"
           "    write_cube(_dfh, atoms, data=_density)\n"
           "_calango_progress(2, 2)\n"
        << "print('CALANGO_RESULT density_cube=" << densityFiles::kDensity
        << " " << (allElectron ? "all_electron" : "pseudo") << "', flush=True)\n";
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
        // The normal path: the moments are a property of the STRUCTURE and
        // were set per atom in Edit Structure, so they arrive in the
        // geometry file's `initial_magmoms` column and are already on
        // `atoms`. Only a structure that carries none gets the uniform
        // fallback — overwriting real per-atom moments with one number was
        // how a carefully-set antiferromagnetic seed became a ferromagnetic
        // one without a word.
        // The test is whether the structure CARRIES the column, not whether
        // the values in it are non-zero. Those are different questions, and
        // conflating them silently overwrote a deliberate all-zero seed with
        // the uniform fallback: a user who set every moment to 0 in Edit
        // Structure got 1 uB per atom instead, and no way to ask for the
        // non-magnetic starting point of a spin-polarized run.
        //
        // atoms.has() is exactly this distinction — ase.io.extxyz round-trips
        // an all-zero `initial_magmoms` column faithfully, while a structure
        // that never had one reports no array at all.
        out << "import numpy as _np\n"
               "_seeded = _np.asarray(atoms.get_initial_magnetic_moments(),\n"
               "                      dtype=float)\n"
               "if atoms.has('initial_magmoms'):\n"
               "    print(f\"CALANGO_INFO initial magnetic moments from the \"\n"
               "          f\"structure: {_np.round(_seeded, 3).tolist()}\",\n"
               "          flush=True)\n"
               "    if _np.abs(_seeded).max(initial=0.0) <= 1e-12:\n"
               "        # Deliberate, and honored: an all-zero seed is the\n"
               "        # non-magnetic starting point. It is still worth a note,\n"
               "        # because a spin-polarized SCF started there usually\n"
               "        # stays there — the symmetric solution is a stationary\n"
               "        # point, so this finds a magnetic ground state only if\n"
               "        # something else breaks the symmetry.\n"
               "        print(\"CALANGO_INFO every initial magnetic moment is \"\n"
               "              \"zero — this is the non-magnetic seed, used as \"\n"
               "              \"set. A spin-polarized SCF started from it \"\n"
               "              \"normally converges back to the non-magnetic \"\n"
               "              \"solution.\", flush=True)\n"
               "else:\n"
               "    # No column at all: nothing was ever set. A spin-polarized\n"
               "    # run seeded with all-zero moments converges straight back\n"
               "    # to the non-magnetic solution, which looks like \"this\n"
               "    # system is not magnetic\" but is really \"nobody asked\".\n";
        if (nc)
            out << "    atoms.set_initial_magnetic_moments(\n"
                   "        [[0.0, 0.0, " << config.initialMagMoment
                << "]] * len(atoms))\n";
        else
            out << "    atoms.set_initial_magnetic_moments(\n"
                   "        [" << config.initialMagMoment
                << "] * len(atoms))\n";
        out << "    print(\"CALANGO_WARN the structure carries no initial \"\n"
               "          \"magnetic moments — seeding every atom with "
            << config.initialMagMoment << " uB. \"\n"
               "          \"Set them per atom in Edit Structure for anything \"\n"
               "          \"but a ferromagnetic guess.\", flush=True)\n\n";
    }
    emitCalculator(out, config);
    out << "\n";
    emitTask(out, config);
    out << "\nprint(\"CALANGO_DONE\", flush=True)\n";
    return out.str();
}

} // namespace calango::core
