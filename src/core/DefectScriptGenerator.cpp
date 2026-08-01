#include "core/DefectScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace calango::core {

namespace {

/// Python literal for a path, or None when empty.
std::string pathLiteral(const std::string& path)
{
    return path.empty() ? std::string("None") : "r\"" + path + "\"";
}

/// What VASP and Quantum ESPRESSO supply in place of GPAW's `.gpw` restarts.
///
/// The formation-energy arithmetic, the lower envelope and the
/// transition-level search are engine-independent — they consume three
/// numbers per charge state and nothing else. Only three things have to be
/// obtained per engine:
///
///   E_tot(q)   the total energy of the charged supercell
///   E_VBM      the host's valence band maximum, the zero E_F is measured from
///   V(r)       the electrostatic potential, for the FNV alignment
///
/// so this emits `_read_energy()`, `_read_band_edges()` and `_run_charged()`
/// against the selected code, and the rest of the script is shared verbatim.
///
/// THE CORRECTION IS DELEGATED, DELIBERATELY. GPAW ships
/// `gpaw.defects.charged_defect_corrections`; VASP and QE do not, and a
/// hand-rolled FNV that has never been checked against a reference is worse
/// than no correction at all — it is a plausible number of the right
/// magnitude and the wrong value, applied silently to every point of the
/// diagram. So pymatgen's implementation is used where it is available, and
/// where it is not the run falls back to UNCORRECTED energies and says so
/// loudly. Uncorrected is a legitimate mode (it is what a supercell
/// convergence study needs); a wrong correction is not.
std::string defectExternalReaders(const DefectConfig& cfg)
{
    const bool vasp = cfg.calculator.calculator == CalculatorKind::Vasp;
    const auto& k = cfg.calculator.kpts;
    std::string out = "import os\nimport glob\nimport subprocess\n";

    if (vasp) {
        out +=
            "from ase.io import read as _ase_read\n"
            "from ase.calculators.vasp import Vasp\n"
            "\n";
        if (!cfg.calculator.vaspPotcarPath.empty())
            out += "os.environ['VASP_PP_PATH'] = r\""
                + cfg.calculator.vaspPotcarPath + "\"\n";
        out +=
            "\n"
            "def _read_energy(directory):\n"
            "    \"\"\"Total energy from vasprun.xml — the parsed, structured\n"
            "    output, rather than a regex over OUTCAR's last free energy.\n"
            "    \"\"\"\n"
            "    frame = _ase_read(os.path.join(directory, 'vasprun.xml'),\n"
            "                      index=-1)\n"
            "    return frame, float(frame.get_potential_energy())\n"
            "\n"
            "\n"
            "def _read_band_edges(directory):\n"
            "    \"\"\"VBM and CBM of the host, from the eigenvalues.\n"
            "\n"
            "    The diagram's Fermi axis is measured FROM THE VBM, not from\n"
            "    the Fermi level: E_F is the variable, and the VBM is the fixed\n"
            "    zero the convention places it against.\n"
            "    \"\"\"\n"
            "    frame = _ase_read(os.path.join(directory, 'vasprun.xml'),\n"
            "                      index=-1)\n"
            "    calc = frame.calc\n"
            "    fermi = float(calc.get_fermi_level())\n"
            "    values = []\n"
            "    for spin in range(calc.get_number_of_spins()):\n"
            "        for kpt in range(len(calc.get_ibz_k_points())):\n"
            "            values.extend(calc.get_eigenvalues(kpt=kpt, spin=spin))\n"
            "    values = np.asarray(values, dtype=float)\n"
            "    below = values[values <= fermi]\n"
            "    above = values[values > fermi]\n"
            "    if not len(below) or not len(above):\n"
            "        raise RuntimeError('No band gap in the host reference.')\n"
            "    return float(below.max()), float(above.min())\n"
            "\n"
            "\n"
            "def _run_charged(atoms, q, tag):\n"
            "    \"\"\"One fixed-geometry SCF at charge q.\n"
            "\n"
            "    VASP spells the charge as NELECT — the absolute electron\n"
            "    count — so a q = +1 defect is one electron FEWER than the\n"
            "    neutral cell. Getting that sign backwards inverts the whole\n"
            "    diagram, which is why it is written once, here.\n"
            "    \"\"\"\n"
            "    workdir = 'defect_q%s' % tag\n"
            "    os.makedirs(workdir, exist_ok=True)\n"
            "    zval = _nelect_per_atom()\n"
            "    neutral_electrons = sum(zval.get(s, 0.0)\n"
            "                            for s in atoms.get_chemical_symbols())\n"
            "    if not zval:\n"
            "        raise RuntimeError(\n"
            "            'Could not read ZVAL from the host OUTCAR, so NELECT '\n"
            "            'cannot be set and the charge state would silently be '\n"
            "            'ignored — every line of the diagram would be the '\n"
            "            'neutral one. Check that the host run left an OUTCAR '\n"
            "            'in ' + str(PRISTINE) + '.')\n"
            "    sub = atoms.copy()\n"
            "    sub.calc = Vasp(\n"
            "        directory=workdir,\n"
            "        xc='" + cfg.calculator.vaspXc + "',\n"
            "        encut=" + std::to_string(cfg.calculator.planeWaveCutoffEv)
            + ",\n"
            "        kpts=(" + std::to_string(k[0]) + ", " + std::to_string(k[1])
            + ", " + std::to_string(k[2]) + "),\n"
            "        ismear=0, sigma=0.05, ispin=2,\n"
            "        nelect=(neutral_electrons - float(q))\n"
            "               if neutral_electrons else None,\n"
            "        # The FNV alignment needs the electrostatic potential on\n"
            "        # the grid, not just the energy.\n"
            "        lvhar=True, lcharg=True,\n"
            "        nsw=0,  # fixed geometry: every charge state at the same one\n"
            "    )\n"
            "    energy = float(sub.get_potential_energy())\n"
            "    return sub, energy, workdir\n"
            "\n"
            "\n"
            "# NELECT is the ABSOLUTE electron count, so the neutral total has\n"
            "# to come from the POTCARs. Read from the host run's OUTCAR, where\n"
            "# VASP prints ZVAL per species — and read LAZILY, on first use:\n"
            "# PRISTINE is defined further down this file, so reading it at\n"
            "# import time would be a NameError that no syntax check catches.\n"
            "_NELECT_CACHE = {}\n"
            "\n"
            "\n"
            "def _nelect_per_atom():\n"
            "    if _NELECT_CACHE:\n"
            "        return _NELECT_CACHE\n"
            "    import re as _re\n"
            "    try:\n"
            "        text = open(os.path.join(PRISTINE, 'OUTCAR'),\n"
            "                    errors='ignore').read()\n"
            "    except OSError as exc:\n"
            "        print('CALANGO_WARN could not open the host OUTCAR (%r)'\n"
            "              % (exc,), flush=True)\n"
            "        return _NELECT_CACHE\n"
            "    species = _re.findall(r'VRHFIN\\s*=\\s*([A-Za-z]+)', text)\n"
            "    zvals = _re.findall(r'ZVAL\\s*=\\s*([0-9.]+)', text)\n"
            "    for symbol, zval in zip(species, zvals):\n"
            "        _NELECT_CACHE[symbol] = float(zval)\n"
            "    return _NELECT_CACHE\n";
    } else {
        out +=
            "from ase.io import read as _ase_read\n"
            "from ase.io.espresso import read_espresso_out\n"
            "from ase.calculators.espresso import Espresso, EspressoProfile\n"
            "\n"
            "_pw = os.environ.get('CALANGO_PW_X', 'pw.x')\n"
            "_pseudo_dir = r\"" + cfg.calculator.espressoPseudoDir + "\"\n"
            "pseudopotentials = {}  # EDIT ME: one UPF per element\n"
            "\n"
            "\n"
            "def _latest_output(directory):\n"
            "    outs = sorted(glob.glob(os.path.join(directory, '*.pwo'))\n"
            "                  + glob.glob(os.path.join(directory, '*.out')))\n"
            "    if not outs:\n"
            "        raise RuntimeError('No pw.x output in ' + directory)\n"
            "    return outs[-1]\n"
            "\n"
            "\n"
            "def _read_energy(directory):\n"
            "    with open(_latest_output(directory)) as handle:\n"
            "        frame = list(read_espresso_out(handle, index=slice(None)))[-1]\n"
            "    return frame, float(frame.get_potential_energy())\n"
            "\n"
            "\n"
            "def _read_band_edges(directory):\n"
            "    \"\"\"VBM and CBM from pw.x's printed eigenvalues.\"\"\"\n"
            "    with open(_latest_output(directory)) as handle:\n"
            "        frame = list(read_espresso_out(handle, index=slice(None)))[-1]\n"
            "    calc = frame.calc\n"
            "    fermi = float(calc.get_fermi_level())\n"
            "    values = []\n"
            "    for spin in range(calc.get_number_of_spins()):\n"
            "        for kpt in range(len(calc.get_ibz_k_points())):\n"
            "            values.extend(calc.get_eigenvalues(kpt=kpt, spin=spin))\n"
            "    values = np.asarray(values, dtype=float)\n"
            "    below = values[values <= fermi]\n"
            "    above = values[values > fermi]\n"
            "    if not len(below) or not len(above):\n"
            "        raise RuntimeError('No band gap in the host reference.')\n"
            "    return float(below.max()), float(above.min())\n"
            "\n"
            "\n"
            "def _run_charged(atoms, q, tag):\n"
            "    \"\"\"One fixed-geometry SCF at charge q.\n"
            "\n"
            "    QE's `tot_charge` follows the physical convention directly: a\n"
            "    positive value removes electrons, so tot_charge = q.\n"
            "    \"\"\"\n"
            "    workdir = os.path.abspath('defect_q%s' % tag)\n"
            "    os.makedirs(workdir, exist_ok=True)\n"
            "    sub = atoms.copy()\n"
            "    sub.calc = Espresso(\n"
            "        profile=EspressoProfile(command=_pw,\n"
            "                                pseudo_dir=_pseudo_dir),\n"
            "        directory=workdir,\n"
            "        pseudopotentials=pseudopotentials,\n"
            "        input_data={\n"
            "            'control': {'calculation': 'scf', 'prefix': tag,\n"
            "                        'outdir': workdir},\n"
            "            'system': {'ecutwfc': "
            + std::to_string(cfg.calculator.qeEcutwfcRy) + ",\n"
            "                       'tot_charge': float(q),\n"
            "                       'occupations': 'smearing',\n"
            "                       'smearing': 'gaussian', 'degauss': 0.01,\n"
            "                       'nspin': 2,\n"
            "                       'starting_magnetization': 0.0,\n"
            "                       # A charged periodic cell needs the\n"
            "                       # compensating background QE adds by\n"
            "                       # default; assume_isolated would change the\n"
            "                       # reference the FNV correction is defined\n"
            "                       # against.\n"
            "                       },\n"
            "            'electrons': {'conv_thr': 1e-8},\n"
            "        },\n"
            "        kpts=(" + std::to_string(k[0]) + ", " + std::to_string(k[1])
            + ", " + std::to_string(k[2]) + "),\n"
            "    )\n"
            "    energy = float(sub.get_potential_energy())\n"
            "    return sub, energy, workdir\n";
    }
    return out;
}

} // namespace

std::string generateDefectScript(const DefectConfig& cfg)
{
    // q = 0 is the reference every other charge state is measured against and
    // the line the diagram is anchored on, so it is present whatever the
    // caller asked for. Sorted and de-duplicated: the diagram's envelope and
    // the transition-level search both walk the list in order.
    std::set<int> chargeSet(cfg.charges.begin(), cfg.charges.end());
    chargeSet.insert(0);

    // See defectExternalReaders() below for what VASP and Quantum ESPRESSO
    // supply in place of the .gpw restarts.
    std::ostringstream out;
    out << "# Charged-defect formation energies (FNV) — generated by Calango\n"
           "#\n"
           "#   E_f[X^q](E_F) = E_tot[X^q] - E_tot[host] - sum_i n_i mu_i\n"
           "#                   + q (E_VBM + E_F) + E_corr(q)\n"
           "#\n"
           "# One fixed-geometry SCF per charge state, then the\n"
           "# Freysoldt-Neugebauer-Van de Walle correction for the spurious\n"
           "# interaction between the periodic images of the charge and its\n"
           "# neutralizing background.\n"
           "import json\n"
           "\n"
           "import numpy as np\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble();

    const auto engine = cfg.calculator.calculator;
    const bool vasp = engine == CalculatorKind::Vasp;
    const bool espresso = engine == CalculatorKind::QuantumEspresso;
    if (vasp || espresso)
        out << defectExternalReaders(cfg);
    else
        out << "from gpaw import GPAW\n";
    out << "\n"
           "# --- Inherited inputs ---------------------------------------------\n"
        << "PRISTINE = " << pathLiteral(cfg.pristinePath) << "\n"
        << "NEUTRAL = " << pathLiteral(cfg.neutralDefectPath) << "\n"
        << "EPSILON = " << cfg.dielectricConstant << "\n"
        << "DEFECT_INDEX = " << cfg.defectIndex << "\n"
        << "RC = " << cfg.modelChargeRadius << "        # Angstrom\n"
        << "MODEL_ECUT = " << cfg.modelCutoffEv << "    # eV\n"
        << "RAVG = " << cfg.averagingRadius << "        # Angstrom\n"
        << "APPLY_FNV = " << (cfg.applyFnvCorrection ? "True" : "False") << "\n"
        << "FERMI_POINTS = " << std::max(2, cfg.fermiPoints) << "\n"
        << "CHARGES = [";
    bool first = true;
    for (const int q : chargeSet) {
        out << (first ? "" : ", ") << q;
        first = false;
    }
    out << "]\n";

    // Species / chemical potentials as a plain list of records, so the script
    // reads exactly what the wizard collected and the JSON can echo it back.
    out << "SPECIES = [";
    for (std::size_t i = 0; i < cfg.species.size(); ++i) {
        const DefectSpecies& s = cfg.species[i];
        out << (i ? ",\n           " : "") << "{'symbol': '" << s.symbol
            << "', 'count': " << s.count << ", 'mu_eV': "
            << s.chemicalPotentialEv << "}";
    }
    out << "]\n\n";

    if (vasp || espresso) {
        // The engine-specific half: host reference, neutral defect, one SCF
        // per charge state, and the correction. Everything after it — the
        // formation energies, the lower envelope, the transition levels and
        // the JSON — is shared with the GPAW path verbatim, because none of it
        // knows or needs to know which code produced the three numbers.
        out << R"PY(
# --- Host reference ------------------------------------------------------
atoms_host, E_host = _read_energy(PRISTINE)
E_VBM, E_CBM = _read_band_edges(PRISTINE)
E_GAP = float(E_CBM - E_VBM)

if E_GAP <= 1e-3:
    raise RuntimeError(
        f'The host reference has essentially no band gap (E_gap = {E_GAP:.4f} '
        'eV). Charged-defect formation energies are only defined against a '
        'gap: in a metal the Fermi level is pinned and there is no interval '
        'for the transition levels to lie in.')

print(f'CALANGO_INFO host E_VBM={E_VBM:.4f} eV E_CBM={E_CBM:.4f} eV '
      f'E_gap={E_GAP:.4f} eV', flush=True)
_calango_progress(1, len(CHARGES) + 2)

# --- Neutral defect ------------------------------------------------------
atoms_defect, E_neutral = _read_energy(NEUTRAL)

if len(atoms_host) == len(atoms_defect) and not SPECIES:
    print('CALANGO_WARN the defect and host cells hold the same number of '
          'atoms and no species exchange was declared. If this is a '
          'substitution, declare both the removed and the added species — '
          'otherwise the chemical-potential term is missing and every '
          'formation energy is shifted by a constant.', flush=True)

# Every charge state runs at the NEUTRAL defect's geometry. Re-relaxing per
# charge would be a different (and more expensive) study; changing anything
# else would make the energy differences meaningless, since they are
# differences of total energies from separate SCFs.
results = {}
for step, q in enumerate(CHARGES):
    if q == 0:
        E_q = E_neutral
        workdir_q = NEUTRAL
    else:
        _, E_q, workdir_q = _run_charged(atoms_defect, q, f'{q:+d}')
    results[q] = {'E_tot_eV': E_q, 'directory': workdir_q,
                  'correction_eV': 0.0, 'correction_terms': {}}
    print(f'CALANGO_INFO charge q={q:+d} E_tot={E_q:.6f} eV', flush=True)
    _calango_progress(step + 2, len(CHARGES) + 2)

# --- FNV corrections -----------------------------------------------------
# DELEGATED, deliberately. GPAW ships its own implementation; these engines do
# not, and a hand-rolled Freysoldt correction that has never been checked
# against a reference is worse than none — it is a plausible number of the
# right magnitude and the wrong value, applied silently to every point of the
# diagram. pymatgen's implementation is used where it is installed; where it
# is not, the run reports UNCORRECTED energies and says so.
if APPLY_FNV:
    try:
        from pymatgen.analysis.defects.corrections.freysoldt import (
            get_freysoldt_correction)
        from pymatgen.io.vasp.outputs import Locpot
    except ImportError as _exc:
        print('CALANGO_WARN pymatgen is not available (%r), so no FNV '
              'correction was applied. Install pymatgen (with '
              'pymatgen-analysis-defects) in the job environment, or run a '
              'supercell convergence study on the uncorrected energies '
              'instead.' % (_exc,), flush=True)
        APPLY_FNV = False
    else:
        _host_locpot = os.path.join(PRISTINE, 'LOCPOT')
        if not os.path.exists(_host_locpot):
            print('CALANGO_WARN no LOCPOT in the host run, so the FNV '
                  'potential alignment cannot be measured. Re-run the host '
                  'with LVHAR = .TRUE.; energies below are UNCORRECTED.',
                  flush=True)
            APPLY_FNV = False
        else:
            _bulk_locpot = Locpot.from_file(_host_locpot)
            for q in CHARGES:
                if q == 0:
                    continue
                _defect_locpot_path = os.path.join(results[q]['directory'],
                                                   'LOCPOT')
                if not os.path.exists(_defect_locpot_path):
                    print(f'CALANGO_WARN no LOCPOT for q={q:+d}; left '
                          'uncorrected.', flush=True)
                    continue
                _summary = get_freysoldt_correction(
                    q=float(q), dielectric=EPSILON,
                    defect_locpot=Locpot.from_file(_defect_locpot_path),
                    bulk_locpot=_bulk_locpot)
                results[q]['correction_eV'] = float(_summary.correction_energy)
                results[q]['correction_terms'] = {
                    key: float(value)
                    for key, value in _summary.metadata.items()
                    if isinstance(value, (int, float))
                }
                print(f'CALANGO_INFO FNV q={q:+d} '
                      f'E_corr={results[q]["correction_eV"]:.4f} eV', flush=True)
if not APPLY_FNV:
    print('CALANGO_WARN FNV correction disabled — the formation energies '
          'below still carry the spurious periodic-image interaction, which '
          'grows as q^2 and decays only as 1/L. Use this for a supercell '
          'convergence study, not for a number to quote.', flush=True)
)PY";
    } else {
        out << R"PY(
# --- Host reference ------------------------------------------------------
host = GPAW(PRISTINE, txt=None)
atoms_host = host.get_atoms()
E_host = float(host.get_potential_energy())

# The Fermi level of the diagram is measured FROM THE VALENCE BAND MAXIMUM of
# the host, not from its Fermi level: E_F is a variable here (the electron
# chemical potential of the sample), and the VBM is the fixed zero the
# convention places it against.
try:
    from ase.dft.bandgap import bandgap
    gap, vbm_k, cbm_k = bandgap(host, output=None)
    eigs = np.array([host.get_eigenvalues(kpt=k, spin=s)
                     for s in range(host.get_number_of_spins())
                     for k in range(len(host.get_ibz_k_points()))])
    occ = host.get_fermi_level()
    E_VBM = float(eigs[eigs <= occ].max())
    E_CBM = float(eigs[eigs > occ].min())
    E_GAP = float(E_CBM - E_VBM)
except Exception as exc:
    raise RuntimeError(
        'Could not determine the host band edges from ' + str(PRISTINE)
        + f': {exc!r}. A charged-defect diagram is drawn across the band gap, '
          'so the host must be an insulator or semiconductor with a resolved '
          'gap — a metallic reference has no gap to place the levels in.')

if E_GAP <= 1e-3:
    raise RuntimeError(
        f'The host reference has essentially no band gap (E_gap = {E_GAP:.4f} '
        'eV). Charged-defect formation energies are only defined against a '
        'gap: in a metal the Fermi level is pinned and there is no interval '
        'for the transition levels to lie in.')

print(f'CALANGO_INFO host E_VBM={E_VBM:.4f} eV E_CBM={E_CBM:.4f} eV '
      f'E_gap={E_GAP:.4f} eV', flush=True)
_calango_progress(1, len(CHARGES) + 2)

# --- Neutral defect ------------------------------------------------------
neutral = GPAW(NEUTRAL, txt=None)
atoms_defect = neutral.get_atoms()

if len(atoms_host) == len(atoms_defect) and not SPECIES:
    print('CALANGO_WARN the defect and host cells hold the same number of '
          'atoms and no species exchange was declared. If this is a '
          'substitution, declare both the removed and the added species — '
          'otherwise the chemical-potential term is missing and every '
          'formation energy is shifted by a constant.', flush=True)

# Every charge state is run at the NEUTRAL defect's geometry with the
# NEUTRAL defect's parameters. Re-relaxing per charge would be a different
# (and more expensive) study; changing any other parameter would make the
# energy differences meaningless, since they are differences of total
# energies from separate SCFs.
base_params = dict(neutral.parameters)
base_params.pop('charge', None)
base_params.pop('txt', None)

results = {}
for step, q in enumerate(CHARGES):
    if q == 0:
        E_q = float(neutral.get_potential_energy())
        calc_q = neutral
        gpw_q = NEUTRAL
    else:
        atoms_q = atoms_defect.copy()
        calc_q = GPAW(charge=float(q), txt=f'gpaw_defect_q{q}.txt',
                      **base_params)
        atoms_q.calc = calc_q
        E_q = float(atoms_q.get_potential_energy())
        # mode='all' because the FNV alignment needs the electrostatic
        # potential on the grid, not just the energy.
        gpw_q = f'defect_q{q}.gpw'
        calc_q.write(gpw_q, mode='all')
    results[q] = {'E_tot_eV': E_q, 'gpw': gpw_q, 'correction_eV': 0.0,
                  'correction_terms': {}}
    print(f'CALANGO_INFO charge q={q:+d} E_tot={E_q:.6f} eV', flush=True)
    _calango_progress(step + 2, len(CHARGES) + 2)

# --- FNV corrections -----------------------------------------------------
# q = 0 needs none: there is no net charge, so no spurious image interaction
# and no potential to align.
if APPLY_FNV:
    from gpaw.defects import charged_defect_corrections
    for q in CHARGES:
        if q == 0:
            continue
        calc_q = GPAW(results[q]['gpw'], txt=None)
        corr = charged_defect_corrections(
            calc_pristine=host, calc_defect=calc_q,
            defect_index=DEFECT_INDEX, charge=float(q), epsilon=EPSILON,
            ecut=MODEL_ECUT, rc=RC, ravg=RAVG)
        E_corr = float(corr.calculate_correction())
        results[q]['correction_eV'] = E_corr
        results[q]['correction_terms'] = {
            'isolated_eV': float(corr.calculate_isolated_correction()
                                 * 27.211386245988),
            'periodic_eV': float(corr.calculate_periodic_correction()
                                 * 27.211386245988),
            'alignment_eV': float(corr.calculate_potential_alignment()
                                  * 27.211386245988),
        }
        print(f'CALANGO_INFO charge q={q:+d} E_corr={E_corr:+.4f} eV',
              flush=True)
else:
    print('CALANGO_WARN FNV correction disabled — the formation energies '
          'below still carry the spurious periodic-image interaction, which '
          'grows as q^2 and decays only as 1/L. Use this for a supercell '
          'convergence study, not for a number to quote.', flush=True)
)PY";
    }

    // --- Shared tail --------------------------------------------------------
    // The formation energies, the lower envelope, the transition levels and
    // the JSON. None of it knows which code produced E_tot(q), E_VBM and
    // E_corr(q) — which is exactly why adding an engine is a matter of
    // supplying those three and nothing else.
    out << R"PY(
# --- Formation energies --------------------------------------------------
# The chemical-potential term. `count` is how many atoms the DEFECT has that
# the host does not, so a vacancy (count = -1) ADDS mu back.
mu_term = sum(s['count'] * s['mu_eV'] for s in SPECIES)

fermi = np.linspace(0.0, E_GAP, FERMI_POINTS)
lines = {}
for q in CHARGES:
    r = results[q]
    intercept = (r['E_tot_eV'] - E_host - mu_term + q * E_VBM
                 + r['correction_eV'])
    # E_f is linear in E_F with slope q — that is the whole content of the
    # diagram, and why the charge state of a line can be read off its slope.
    lines[q] = intercept + q * fermi
    r['formation_energy_at_VBM_eV'] = float(intercept)

stack = np.array([lines[q] for q in CHARGES])
envelope = stack.min(axis=0)
stable = [CHARGES[i] for i in stack.argmin(axis=0)]

# --- Transition levels ---------------------------------------------------
# eps(q/q') is where the two lines cross:
#   E_f[q](eps) = E_f[q'](eps)  =>  eps = (I_q' - I_q) / (q - q')
# Only crossings that lie ON the lower envelope are thermodynamic transition
# levels; the others are between two states neither of which is ever the
# ground state, and reporting them as levels would be wrong.
transitions = []
for i in range(1, len(stable)):
    if stable[i] == stable[i - 1]:
        continue
    q_from, q_to = stable[i - 1], stable[i]
    I_from = results[q_from]['formation_energy_at_VBM_eV']
    I_to = results[q_to]['formation_energy_at_VBM_eV']
    if q_from == q_to:
        continue
    eps = (I_to - I_from) / (q_from - q_to)
    transitions.append({
        'from_charge': int(q_from),
        'to_charge': int(q_to),
        'level_eV_above_VBM': float(eps),
        'level_eV_below_CBM': float(E_GAP - eps),
        'formation_energy_eV': float(I_from + q_from * eps),
    })
    # "+0" is not how a charge state is written; defect notation is (+/0).
    _lbl = lambda q: ('+' * q if q > 0 else '-' * -q) or '0'
    print(f'CALANGO_INFO transition ({_lbl(q_from)}/{_lbl(q_to)}) at E_F = '
          f'{eps:.4f} eV above VBM', flush=True)

if not transitions:
    print('CALANGO_INFO no transition level inside the gap — one charge '
          'state is the ground state across the whole Fermi-level range.',
          flush=True)

summary = {
    'host': {
        'E_tot_eV': E_host,
        'E_VBM_eV': E_VBM,
        'E_CBM_eV': E_CBM,
        'E_gap_eV': E_GAP,
        'natoms': int(len(atoms_host)),
    },
    'defect': {
        'formula': atoms_defect.get_chemical_formula(),
        'natoms': int(len(atoms_defect)),
        'defect_index': int(DEFECT_INDEX),
    },
    'fnv': {
        'applied': bool(APPLY_FNV),
        'epsilon': float(EPSILON),
        'model_charge_radius_A': float(RC),
        'model_cutoff_eV': float(MODEL_ECUT),
        'averaging_radius_A': float(RAVG),
    },
    'species': SPECIES,
    'chemical_potential_term_eV': float(mu_term),
    'charges': [
        {
            'charge': int(q),
            'E_tot_eV': results[q]['E_tot_eV'],
            'correction_eV': results[q]['correction_eV'],
            'correction_terms': results[q]['correction_terms'],
            'formation_energy_at_VBM_eV':
                results[q]['formation_energy_at_VBM_eV'],
            'formation_energy_eV': [float(v) for v in lines[q]],
        }
        for q in CHARGES
    ],
    'fermi_level_eV': [float(v) for v in fermi],
    'envelope_eV': [float(v) for v in envelope],
    'stable_charge': [int(v) for v in stable],
    'transitions': transitions,
}
with open('charged_defects.json', 'w') as handle:
    json.dump(summary, handle, indent=2)

_calango_progress(len(CHARGES) + 2, len(CHARGES) + 2)
print(f'CALANGO_RESULT charged_defects=charged_defects.json '
      f'charges={len(CHARGES)} transitions={len(transitions)} '
      f'gap={E_GAP:.3f}', flush=True)
print('CALANGO_DONE', flush=True)
)PY";
    return out.str();
}

} // namespace calango::core
