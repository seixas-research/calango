#include "core/BornChargesScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"
#include "core/PolarizationScriptHelpers.hpp"

#include <sstream>
#include <string>

namespace calango::core {

namespace {

/// The shared tail: acoustic sum rule, then `born_charges.json` in exactly the
/// schema the GPAW path writes.
///
/// One schema for three engines is the point. The Born Charges viewer, the
/// phonon LO-TO block that consumes `born_charges.json`, and every test that
/// reads it are written against ONE file format; an engine that wrote its own
/// dialect would mean each of those growing a second reader, and the second
/// reader is always the one nobody keeps in step.
///
/// Consumes a Python `tensors` array of shape (natoms, 3, 3), a `symbols`
/// list and a float `volume`.
std::string bornJsonTail(bool acousticSumRule)
{
    std::string out =
        "tensors = np.asarray(tensors, dtype=float)\n"
        "raw = tensors.copy()\n"
        "# Sum_k Z*_k = 0 is exact — translating the whole crystal cannot\n"
        "# polarize it — so a non-zero sum measures the run's own convergence\n"
        "# error rather than any physics. It is reported either way, and the\n"
        "# uncorrected tensors are kept alongside so the size of the\n"
        "# correction stays visible instead of being quietly absorbed.\n"
        "_residual = tensors.sum(axis=0)\n"
        "asr_residual = float(np.abs(_residual).max())\n";
    if (acousticSumRule)
        out += "tensors -= _residual / len(tensors)\n"
               "asr_imposed = True\n";
    else
        out += "asr_imposed = False\n";
    out +=
        "\n"
        "results = []\n"
        "for atom_index in range(len(tensors)):\n"
        "    tensor = tensors[atom_index]\n"
        "    eigenvalues = np.linalg.eigvals(\n"
        "        0.5 * (tensor + tensor.T)).real\n"
        "    results.append({\n"
        "        'index': int(atom_index),\n"
        "        'symbol': symbols[atom_index],\n"
        "        'tensor': [[float(v) for v in row] for row in tensor],\n"
        "        'raw_tensor': [[float(v) for v in row]\n"
        "                       for row in raw[atom_index]],\n"
        "        'isotropic': float(np.trace(tensor) / 3.0),\n"
        "        'eigenvalues': sorted(float(v) for v in eigenvalues),\n"
        "    })\n"
        "\n"
        "summary = {\n"
        "    'displacement_A': 0.0,  # DFPT: analytic derivative, no finite step\n"
        "    'acoustic_sum_rule': asr_imposed,\n"
        "    'asr_residual_e': asr_residual,\n"
        "    'volume_A3': float(volume),\n"
        "    'atoms': results,\n"
        "}\n"
        "with open('born_charges.json', 'w') as handle:\n"
        "    json.dump(summary, handle, indent=2)\n"
        "\n"
        "print(f'CALANGO_RESULT born_charges=born_charges.json '\n"
        "      f'atoms={len(results)} asr_residual_e={asr_residual:.4f}',\n"
        "      flush=True)\n";
    return out;
}

/// VASP and Quantum ESPRESSO both compute Z* by DENSITY-FUNCTIONAL
/// PERTURBATION THEORY in a single run, not by displacing atoms.
///
/// That is not merely cheaper than the 6N self-consistent runs the GPAW path
/// needs — it is a different and better quantity: an analytic derivative
/// rather than a central difference, so there is no displacement amplitude to
/// trade off against SCF noise, and no linearity assumption to check. The
/// `displacement_A` field in the output is therefore 0 for these engines,
/// which is what says which route produced the numbers.
std::string bornDfptScript(const BornChargesConfig& cfg)
{
    const bool vasp = cfg.calculator.calculator == CalculatorKind::Vasp;
    std::string out;

    if (vasp) {
        out +=
            "# --- VASP, DFPT (LEPSILON = .TRUE.) ------------------------\n"
            "# One linear-response run gives the full Z* tensor of every ion\n"
            "# and the macroscopic dielectric tensor with it. LPEAD is left\n"
            "# off: it is the finite-field route, and mixing the two is how a\n"
            "# run ends up reporting one of them while the user reads the\n"
            "# other.\n"
            "import re\n"
            "import subprocess\n"
            "from ase.calculators.vasp import Vasp\n"
            "\n";
        out += AseScriptGenerator::vaspPotcarResolutionSnippet(cfg.calculator);
        out +=
            "calc = Vasp(\n"
            "    directory='.',\n"
            "    xc='" + cfg.calculator.vaspXc + "',\n"
            "    encut=" + std::to_string(cfg.calculator.planeWaveCutoffEv) + ",\n"
            "    kpts=(" + std::to_string(cfg.calculator.kpts[0]) + ", "
            + std::to_string(cfg.calculator.kpts[1]) + ", "
            + std::to_string(cfg.calculator.kpts[2]) + "),\n"
            "    ismear=0, sigma=0.05,\n"
            "    # DFPT needs a tightly converged ground state: the response is\n"
            "    # a derivative of it, and derivative noise is the SCF's noise\n"
            "    # amplified.\n"
            "    ediff=1e-8,\n"
            "    lepsilon=True,\n"
            "    lreal=False,   # LREAL=Auto is not supported with LEPSILON\n"
            "    lwave=False, lcharg=False,\n"
            ")\n"
            "atoms.calc = calc\n"
            "atoms.get_potential_energy()\n"
            "\n"
            "outcar = 'OUTCAR'\n"
            "if not os.path.exists(outcar):\n"
            "    raise RuntimeError('VASP produced no OUTCAR in ' + os.getcwd())\n"
            "text = open(outcar, errors='ignore').read()\n"
            "marker = 'BORN EFFECTIVE CHARGES'\n"
            "if marker not in text:\n"
            "    raise RuntimeError(\n"
            "        'No Born charges in OUTCAR. LEPSILON runs need a "
            "SEMICONDUCTOR\\n'\n"
            "        'or insulator: for a metal the macroscopic polarization is "
            "not\\n'\n"
            "        'defined and VASP writes nothing.')\n"
            "block = text[text.index(marker):]\n"
            "tensors = []\n"
            "# Each ion is `ion <n>` followed by three `<axis> xx xy xz` rows.\n"
            "for match in re.finditer(r'ion\\s+\\d+\\s*\\n((?:\\s*[1-3]"
            "(?:\\s+-?\\d+\\.\\d+){3}\\s*\\n){3})', block):\n"
            "    rows = []\n"
            "    for line in match.group(1).strip().splitlines():\n"
            "        rows.append([float(v) for v in line.split()[1:4]])\n"
            "    tensors.append(rows)\n"
            "    if len(tensors) == len(atoms):\n"
            "        break\n"
            "if len(tensors) != len(atoms):\n"
            "    raise RuntimeError('Parsed %d Born tensors for %d atoms'\n"
            "                       % (len(tensors), len(atoms)))\n"
            "\n"
            "# The dielectric tensor comes free with the same run, and the\n"
            "# phonon LO-TO correction needs both.\n"
            "_eps = re.search(r'MACROSCOPIC STATIC DIELECTRIC TENSOR.*?\\n"
            "\\s*-+\\s*\\n((?:.*\\n){3})', text)\n"
            "if _eps:\n"
            "    dielectric = [[float(v) for v in row.split()]\n"
            "                  for row in _eps.group(1).strip().splitlines()]\n"
            "    print('CALANGO_INFO dielectric=' + repr(dielectric), flush=True)\n";
    } else {
        out +=
            "# --- Quantum ESPRESSO, DFPT (ph.x with epsil) --------------\n"
            "# pw.x converges the ground state, then ph.x solves the linear\n"
            "# response at q = 0 with epsil = .true., which is what makes it\n"
            "# report the Born charges and the dielectric tensor rather than\n"
            "# only the dynamical matrix.\n"
            "#\n"
            "# epsil is legal only for an INSULATOR; for a metal the\n"
            "# macroscopic field is screened out and ph.x refuses.\n"
            "import re\n"
            "import subprocess\n"
            "from ase.calculators.espresso import Espresso, EspressoProfile\n"
            "\n"
            "_pseudo_dir = r\"" + cfg.calculator.espressoPseudoDir + "\"\n"
            "_pw = os.environ.get('CALANGO_PW_X', 'pw.x')\n"
            "_ph = os.environ.get('CALANGO_PH_X', 'ph.x')\n"
            "profile = EspressoProfile(command=_pw, pseudo_dir=_pseudo_dir)\n"
            "pseudopotentials = {}  # EDIT ME: one UPF per element\n"
            "atoms.calc = Espresso(\n"
            "    profile=profile,\n"
            "    pseudopotentials=pseudopotentials,\n"
            "    input_data={\n"
            "        'control': {'calculation': 'scf', 'prefix': 'calango',\n"
            "                    'outdir': './qe', 'tprnfor': True},\n"
            "        'system': {'ecutwfc': "
            + std::to_string(cfg.calculator.qeEcutwfcRy) + ",\n"
            "                   'occupations': 'fixed'},\n"
            "        'electrons': {'conv_thr': 1e-12},\n"
            "    },\n"
            "    kpts=(" + std::to_string(cfg.calculator.kpts[0]) + ", "
            + std::to_string(cfg.calculator.kpts[1]) + ", "
            + std::to_string(cfg.calculator.kpts[2]) + "),\n"
            ")\n"
            "atoms.get_potential_energy()\n"
            "\n"
            "with open('ph.in', 'w') as handle:\n"
            "    handle.write('Born charges at Gamma\\n')\n"
            "    handle.write('&INPUTPH\\n')\n"
            "    handle.write(\"  prefix = 'calango'\\n\")\n"
            "    handle.write(\"  outdir = './qe'\\n\")\n"
            "    handle.write(\"  fildyn = 'calango.dyn'\\n\")\n"
            "    handle.write('  epsil = .true.\\n')\n"
            "    handle.write('  tr2_ph = 1.0d-14\\n')\n"
            "    handle.write('/\\n')\n"
            "    handle.write('0.0 0.0 0.0\\n')\n"
            "with open('ph.in') as _in, open('ph.out', 'w') as _out:\n"
            "    _result = subprocess.run([_ph], stdin=_in, stdout=_out,\n"
            "                             stderr=subprocess.STDOUT)\n"
            "if _result.returncode != 0:\n"
            "    raise RuntimeError('ph.x failed (%s); see ph.out. Set "
            "CALANGO_PH_X\\n'\n"
            "                       'to its full path if it is not on PATH.'\n"
            "                       % _ph)\n"
            "\n"
            "text = open('ph.out', errors='ignore').read()\n"
            "marker = 'Effective charges'\n"
            "if marker not in text:\n"
            "    raise RuntimeError(\n"
            "        'ph.x reported no effective charges. epsil = .true. is "
            "only\\n'\n"
            "        'legal for an insulator — for a metal the macroscopic "
            "field is\\n'\n"
            "        'screened out and the quantity is not defined.')\n"
            "# The FIRST block is the one without the acoustic sum rule\n"
            "# applied — ph.x prints both. That is the one wanted here: the\n"
            "# ASR is imposed downstream, and its residual is a convergence\n"
            "# diagnostic that a pre-corrected set would have thrown away.\n"
            "block = text[text.index(marker):]\n"
            "tensors = []\n"
            "# `atom <n> <Symbol> Mean Z*: <value>` — the trailing mean is part\n"
            "# of the line, so the pattern runs to the newline rather than\n"
            "# stopping after the symbol — then three `Ex ( xx xy xz )` rows.\n"
            "for match in re.finditer(\n"
            "        r'atom\\s+\\d+\\s+\\S+[^\\n]*\\n((?:\\s*E[xyz]\\s*\\("
            "(?:\\s+-?\\d+\\.\\d+){3}\\s*\\)\\s*\\n){3})', block):\n"
            "    rows = []\n"
            "    for line in match.group(1).strip().splitlines():\n"
            "        values = re.findall(r'-?\\d+\\.\\d+', line)\n"
            "        rows.append([float(v) for v in values[:3]])\n"
            "    tensors.append(rows)\n"
            "    if len(tensors) == len(atoms):\n"
            "        break\n"
            "if len(tensors) != len(atoms):\n"
            "    raise RuntimeError('Parsed %d Born tensors for %d atoms'\n"
            "                       % (len(tensors), len(atoms)))\n";
    }

    out += "\nsymbols = list(atoms.get_chemical_symbols())\n"
           "volume = atoms.get_volume()\n"
           "\n"
        + bornJsonTail(cfg.acousticSumRule);
    return out;
}

} // namespace

std::string generateBornChargesScript(const BornChargesConfig& config)
{
    // The Berry phase is defined on the UNSYMMETRIZED Brillouin zone, so the
    // symmetry switch is not the user's to make here. Setting it on the config
    // (rather than appending a second `symmetry=` after the shared keyword
    // block) is what keeps the generated call free of a duplicated keyword
    // argument, which Python rejects outright.
    BornChargesConfig cfg = config;
    cfg.calculator.gpawSymmetryOff = true;

    const bool gpaw = cfg.calculator.calculator == CalculatorKind::Gpaw;

    // The header describes the route this script actually takes. The two are
    // genuinely different calculations — a central difference of Berry phases
    // versus an analytic linear response — and a preamble that described
    // finite differences above a DFPT run would misstate both the method and
    // its cost.
    const bool dfpt = config.calculator.calculator == CalculatorKind::Vasp
        || config.calculator.calculator == CalculatorKind::QuantumEspresso;

    std::ostringstream out;
    out << "# Born effective charges Z* — generated by Calango\n"
           "#\n"
           "# Z*_{k,ab} = (Omega / e) * dP_a / du_{k,b}\n"
           "#\n";
    if (dfpt)
        out << "# Evaluated by DENSITY-FUNCTIONAL PERTURBATION THEORY: one\n"
               "# linear-response run returns the analytic derivative for every\n"
               "# ion at once, together with the macroscopic dielectric tensor.\n"
               "#\n"
               "# Cost: one run. And it is the better quantity — an analytic\n"
               "# derivative has no displacement amplitude to trade off against\n"
               "# SCF noise, and no linearity assumption left to check.\n";
    else
        out << "# Evaluated by central finite differences of the BERRY-PHASE\n"
               "# polarization: each atom is displaced by +/- delta along each\n"
               "# Cartesian axis, the SCF is re-converged, and the macroscopic\n"
               "# polarization is read off the Berry phase of the occupied\n"
               "# manifold.\n"
               "#\n"
               "# Cost: 6 SCF runs per atom. That is the honest price of a\n"
               "# finite-difference Z* through GPAW.\n";
    out << "import json\n"
           "import os\n"
           "from pathlib import Path\n"
           "\n"
           "# GPAW's Berry-phase module lives in the new engine.\n"
           "os.environ.setdefault('GPAW_NEW', '1')\n"
           "import numpy as np\n"
           "from ase.io import read\n"
           "from ase.units import Bohr\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble();
    if (cfg.baselinePath.empty())
        out << "atoms = read('structure.extxyz')\n\n";

    const auto engine = cfg.calculator.calculator;
    const bool vasp = engine == CalculatorKind::Vasp;
    const bool espresso = engine == CalculatorKind::QuantumEspresso;

    if (vasp || espresso) {
        out << bornDfptScript(cfg);
        return out.str();
    }

    if (!gpaw) {
        // Refusing up front beats running 6N SCF cycles and failing at the
        // polarization step; the message names the one thing that has to change.
        out << "raise RuntimeError(\n"
               "    'Born effective charges need either the Berry-phase "
               "polarization\\n'\n"
               "    '(GPAW) or a DFPT linear-response run (VASP LEPSILON, "
               "Quantum\\n'\n"
               "    'ESPRESSO ph.x). The selected engine was \""
            << toString(cfg.calculator.calculator) << "\".\\n'\n"
               "    'Re-open the wizard and choose one of those three.')\n";
        return out.str();
    }

    if (!cfg.baselinePath.empty())
        out << "# --- Ground-state baseline ---------------------------------\n"
               "# A completed Single-Point Calculation supplies the converged\n"
               "# GEOMETRY the displacements are taken about, and the calculator\n"
               "# every displaced run is rebuilt from (calc.new()) — so all of\n"
               "# them use exactly the settings the baseline was validated with.\n"
               "#\n"
               "# It does NOT supply a density to evaluate at, the way the\n"
               "# Optics and Electronic Structure baselines do. Z* IS the\n"
               "# response of the charge distribution to a displacement, so each\n"
               "# displaced geometry re-converges its own SCF. There is no\n"
               "# fixed-density shortcut here.\n"
               "from gpaw import GPAW\n"
               "\n"
            << "_baseline = GPAW(r\"" << cfg.baselinePath << "\", txt=None)\n"
            << "atoms = _baseline.get_atoms()\n"
               "\n";

    out << "if not atoms.pbc.all():\n"
           "    raise RuntimeError(\n"
           "        'The Berry-phase polarization is defined for a periodic "
           "crystal.\\n'\n"
           "        'This structure is not periodic in all three directions — "
           "Z* is\\n'\n"
           "        'only meaningful for a bulk insulator. Build a periodic "
           "cell first.')\n"
           "\n"
        << AseScriptGenerator::gpawImports(cfg.calculator)
        << berryPhaseImportShim()
        << "delta = " << cfg.displacement << "        # Angstrom\n";

    out << "indices = ";
    if (cfg.atomIndices.empty()) {
        out << "list(range(len(atoms)))\n";
    } else {
        out << "[";
        for (std::size_t i = 0; i < cfg.atomIndices.size(); ++i)
            out << (i ? ", " : "") << cfg.atomIndices[i];
        out << "]\n";
    }

    out << "cell = atoms.get_cell()\n"
           "volume = atoms.get_volume()\n"
           "reference = atoms.get_positions().copy()\n"
           "total_steps = 6 * len(indices)\n"
           "_calango_event('start', "
           "f'{len(indices)} atom(s), {total_steps} SCF runs')\n"
           "\n"
           "\n"
        << polarizationPhaseCFunction()
        << "def displaced_phase(displaced, tag):\n"
           "    \"\"\"Total polarization phase of one displaced geometry.\"\"\"\n";
    if (cfg.baselinePath.empty()) {
        out << "    calc = GPAW(\n"
            << AseScriptGenerator::gpawKeywordArguments(cfg.calculator, "        ")
            << "        txt=f'gpaw_{tag}.txt',\n"
               "    )\n";
    } else {
        // calc.new() rebuilds the baseline's calculator verbatim, so every
        // displaced SCF is run with the settings that were validated once —
        // rather than a second, hand-copied set that can drift from them.
        //
        // symmetry='off' is forced on top: the Berry phase is an integral over
        // the FULL Brillouin zone, and a symmetry-folded k-point set silently
        // gives the wrong polarization rather than an error.
        out << "    calc = _baseline.new(symmetry='off', "
               "txt=f'gpaw_{tag}.txt')\n";
    }
    out << "    displaced.calc = calc\n"
           "    displaced.get_potential_energy()\n"
           "    return polarization_phase_c(calc, tag)\n"
           "\n"
           "\n"
           "def phase_difference(plus_c, minus_c):\n"
           "    \"\"\"phi(+u) - phi(-u), resolved onto the correct branch.\n"
           "\n"
           "    The Berry phase is only defined MODULO 2*pi — the polarization\n"
           "    quantum — and the two displaced calculations routinely land on\n"
           "    different branches. Subtracting them raw then yields a Z* out by\n"
           "    an integer number of quanta: cubic BN came out at -540 e for\n"
           "    boron instead of +1.9, exactly 6 quanta off, while the nitrogen\n"
           "    (whose phases happened to agree) was already right.\n"
           "\n"
           "    A displacement of delta ~ 0.01 A cannot physically move the\n"
           "    polarization by anything close to a quantum, so the true\n"
           "    difference is the representative in (-pi, pi]; wrapping it there\n"
           "    is what picks the same branch for both.\n"
           "    \"\"\"\n"
           "    return (plus_c - minus_c + np.pi) % (2.0 * np.pi) - np.pi\n"
           "\n"
           "\n"
           "born = np.zeros((len(atoms), 3, 3))\n"
           "step = 0\n"
           "for atom_index in indices:\n"
           "    for axis in range(3):\n"
           "        phases = []\n"
           "        for sign in (+1, -1):\n"
           "            positions = reference.copy()\n"
           "            positions[atom_index, axis] += sign * delta\n"
           "            moved = atoms.copy()\n"
           "            moved.set_positions(positions)\n"
           "            tag = f'{atom_index}_{axis}_{\"p\" if sign > 0 else \"m\"}'\n"
           "            phases.append(displaced_phase(moved, tag))\n"
           "            step += 1\n"
           "            _calango_progress(step, total_steps)\n"
           "        # The branch is resolved on the PHASE, in crystal\n"
           "        # coordinates, before anything is converted to Cartesian.\n"
           "        dphi_c = phase_difference(phases[0], phases[1])\n"
           "        # Z*_{k,ab} = d(Omega P_a) / du_{k,b}. Omega*P is the cell\n"
           "        # dipole sum_c (phi_c / 2pi) a_c (e*A), so dividing by the\n"
           "        # displacement (A) leaves a result in units of e.\n"
           "        born[atom_index, :, axis] = (\n"
           "            (dphi_c / (2.0 * np.pi)) @ np.array(cell) / (2.0 * delta))\n"
           "    _calango_metric(atom_index,\n"
           "                    z_iso=float(np.trace(born[atom_index]) / 3.0))\n"
           "\n"
           "raw = born.copy()\n";

    out << "computed = np.array(indices)\n"
           "# The sum rule is a statement about ALL the atoms in the cell:\n"
           "# translating the whole crystal cannot polarize it, so sum_k Z*_k\n"
           "# must vanish over every k. A partial sum is not a residual and\n"
           "# subtracting it would corrupt the tensors it was meant to correct\n"
           "# — for one Si and one O of alpha-quartz it comes to 1.06 e, which\n"
           "# is the Si+2O neutrality of the formula unit, not an error.\n"
           "complete = len(indices) == len(atoms)\n"
           "residual = born[computed].sum(axis=0)\n"
           "asr_residual = float(np.abs(residual).max())\n";
    if (cfg.acousticSumRule) {
        out << "if complete:\n"
               "    # Whatever the full sum comes out to IS this calculation's\n"
               "    # own convergence error; spreading it evenly over the atoms\n"
               "    # is the standard correction.\n"
               "    born -= residual / len(atoms)\n"
               "    asr_imposed = True\n"
               "else:\n"
               "    asr_imposed = False\n"
               "    print('CALANGO_INFO acoustic sum rule NOT imposed: only '\n"
               "          f'{len(indices)} of {len(atoms)} atoms were computed, '\n"
               "          'so the sum over k is incomplete', flush=True)\n";
    } else {
        out << "asr_imposed = False\n";
    }

    out << "\n"
           "symbols = atoms.get_chemical_symbols()\n"
           "results = []\n"
           "for atom_index in indices:\n"
           "    tensor = born[atom_index]\n"
           "    # The isotropic part is the number usually quoted; the\n"
           "    # eigenvalues say how anisotropic the response really is.\n"
           "    eigenvalues = np.linalg.eigvals(\n"
           "        0.5 * (tensor + tensor.T)).real\n"
           "    results.append({\n"
           "        'index': int(atom_index),\n"
           "        'symbol': symbols[atom_index],\n"
           "        'tensor': [[float(v) for v in row] for row in tensor],\n"
           "        'raw_tensor': [[float(v) for v in row]\n"
           "                       for row in raw[atom_index]],\n"
           "        'isotropic': float(np.trace(tensor) / 3.0),\n"
           "        'eigenvalues': sorted(float(v) for v in eigenvalues),\n"
           "    })\n"
           "\n"
           "summary = {\n"
           "    'displacement_A': delta,\n"
           "    'acoustic_sum_rule': asr_imposed,\n"
           "    'asr_residual_e': asr_residual,\n"
           "    'volume_A3': float(volume),\n"
           "    'atoms': results,\n"
           "}\n"
           "with open('born_charges.json', 'w') as handle:\n"
           "    json.dump(summary, handle, indent=2)\n"
           "\n"
           "print(f'CALANGO_RESULT born_charges=born_charges.json '\n"
           "      f'atoms={len(results)} asr_residual_e={asr_residual:.4f}',\n"
           "      flush=True)\n";
    return out.str();
}

} // namespace calango::core
