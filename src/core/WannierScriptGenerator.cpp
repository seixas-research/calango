#include "core/WannierScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <sstream>
#include <string>

namespace calango::core {

namespace {

/// Ground-state acquisition. Both branches leave `calc` (a calculator holding
/// the Bloch wavefunctions) and `atoms` defined, and log progress 1..2.
///
/// The localization runs through ASE's Wannier, which needs the full
/// (unsymmetrized) Brillouin zone, so the fresh GPAW SCF forces `symmetry='off'`
/// and writes `wannier.gpw`. The GPAW knobs come from the wizard's Calculator
/// Settings via the shared helpers. A non-GPAW engine still runs its own ground
/// state (its Conda env is bound per engine from Preferences); the localization
/// below is guarded and raises a clear error if that calculator can not feed
/// ase.dft.wannier.
std::string groundState(const WannierConfig& cfg)
{
    std::ostringstream out;

    if (!cfg.baselineDir.empty()) {
        out << "_base = r\"" << cfg.baselineDir << "\"\n"
               // A clearer refusal than the bare .gpw-glob miss below, when
               // the baseline says outright which engine produced it: fails
               // in milliseconds, before the .gpw search even runs. Absent
               // for a baseline staged before this check existed (or one
               // reached without going through the wizard's own pre-flight
               // check in WannierWizard::refreshBaselineSummary) —
               // json.load then simply finds nothing and this block is a
               // no-op, falling through to the .gpw glob below.
               "try:\n"
               "    with open(os.path.join(_base, 'calculator.json')) as _cf:\n"
               "        _prov = json.load(_cf)\n"
               "    _engine = _prov.get('engine', '')\n"
               "    if _engine and _engine.upper() != 'GPAW':\n"
               "        raise RuntimeError(\n"
               "            'Wannier Functions cannot be driven from a ' "
               "+ _engine + ' '\n"
               "            'baseline: ase.dft.wannier.Wannier needs the '\n"
               "            'calculator method get_wannier_localization_matrix(), '\n"
               "            'which only GPAW implements. Re-run the Single-Point '\n"
               "            'Calculation with GPAW, or run this node with no '\n"
               "            'baseline selected for a fresh GPAW SCF.')\n"
               "except (OSError, ValueError):\n"
               "    pass\n"
               "_gpw = sorted(glob.glob(os.path.join(_base, '*.gpw')))\n"
               "if not _gpw:\n"
               "    raise RuntimeError('No GPAW wavefunction (.gpw) found in '\n"
               "                       + _base + '. The MLWF localization needs '\n"
               "                       'the Bloch wavefunctions — re-run the '\n"
               "                       'single-point with '\n"
               "                       \"calc.write('single_point.gpw', \"\n"
               "                       \"mode='all').\")\n"
               "from gpaw import GPAW\n"
               "# Absolute, and recorded in wannier.json below. This run reads\n"
               "# the wavefunctions from ANOTHER job's directory and writes no\n"
               "# .gpw of its own, so a later Wannier interpolation that looked\n"
               "# for one beside wannier.json found nothing and died.\n"
               "_gpw_path = os.path.abspath(_gpw[0])\n"
               "calc = GPAW(_gpw_path, txt='gpaw_wannier.txt')\n"
               "atoms = calc.get_atoms()\n"
               "_calango_progress(1, 3)\n";
        return out.str();
    }

    out << "atoms = read('structure.extxyz')\n"
           "_gpw_path = None\n"
           "_calango_progress(1, 3)\n";

    if (cfg.calculator.calculator == CalculatorKind::Gpaw) {
        // ASE's Wannier requires the full (unsymmetrized) Brillouin zone, so
        // force symmetry="off" through the shared GPAW keyword emitter (a
        // single source of truth — no hand-written duplicate keyword).
        CalculatorConfig scf = cfg.calculator;
        scf.gpawSymmetryOff = true;
        out << AseScriptGenerator::gpawImports(scf)
            << "# symmetry=\"off\" — ASE's Wannier needs the full "
               "(unsymmetrized)\n"
               "# Brillouin zone; it raises otherwise.\n"
               "calc = GPAW(\n"
            << AseScriptGenerator::gpawKeywordArguments(scf, "    ")
            << "    txt='gpaw.txt',\n"
               ")\n"
               "atoms.calc = calc\n"
               "atoms.get_potential_energy()\n"
               "calc.write('wannier.gpw', mode='all')\n"
               "_gpw_path = os.path.abspath('wannier.gpw')\n"
               "_calango_progress(2, 3)\n";
    } else {
        // True pre-flight: raised BEFORE calculatorSnippet() below runs
        // anything, so a non-GPAW engine fails in milliseconds rather than
        // after a (possibly VASP-expensive) SCF that ase.dft.wannier could
        // never have used anyway — verified against the installed ASE
        // source (ase/dft/wannier.py's new_Z() calls
        // calc.get_wannier_localization_matrix() unconditionally, a
        // GPAW-only method). WannierWizard::calculatorAllowed() already
        // keeps this UNREACHABLE from the wizard's own "no baseline"
        // path (which fixes the engine at GPAW), but a hand-edited or
        // programmatically built WannierConfig is not bound by that.
        out << "raise RuntimeError(\n"
               "    'Wannier Functions cannot run a fresh SCF with "
            << toString(cfg.calculator.calculator)
            << ": '\n"
               "    'ase.dft.wannier.Wannier needs the calculator method '\n"
               "    'get_wannier_localization_matrix(), which only GPAW "
               "implements.')\n";
    }
    return out.str();
}

/// VASP's OWN native Wannier90 interface, entirely independent of
/// ase.dft.wannier and its GPAW-only get_wannier_localization_matrix(). Two
/// INCAR tags drive it (verified against the VASP wiki this session, not
/// assumed): LWANNIER90 switches the interface on; LWANNIER90_RUN makes VASP
/// run the wannier90 LIBRARY IT IS LINKED AGAINST to completion (disentangle
/// + wannierise), rather than only writing .amn/.mmn/.eig for an external
/// wannier90.x to finish — so this needs no wannier90 BINARY, matching the
/// "reads files VASP's own interface writes" rule as literally as possible:
/// with LWANNIER90_RUN the intermediate files are not even written, per the
/// same source.
///
/// The wannier90.win Calango pre-writes carries ONLY num_wann, write_hr and
/// a projections block — deliberately NOT mp_grid/kpoints/unit_cell_cart/
/// atoms_cart. VASP fills those in from its OWN actual KPOINTS/POSCAR
/// whenever a pre-existing win file omits them (same source), and "the
/// program will not check whether this [k-points] block agrees with the k
/// points used in the VASP calculation" — so writing them by hand risks a
/// SILENT mismatch the interface itself does not catch, while leaving them
/// out makes correctness a property of the SAME Vasp(kpts=...) call already
/// used for the calculation, not a second, independently-typed copy of it.
///
/// Disentanglement window: only FixedStatesMode::FromWannierCount (no
/// frozen window) is mapped. wannier90's window keywords (dis_froz_min/max)
/// are referenced to the FERMI level by a convention this session did not
/// verify to the same standard as the rest of this function, and an
/// unverified numeric window is worse than none — EnergyWindow/BandCount
/// fall back to no window with a logged warning rather than risk a silently
/// wrong one. See FUTURE.md.
std::string generateVaspWannier90Script(const WannierConfig& cfg)
{
    std::ostringstream out;
    out << "# Wannier Functions (VASP's own Wannier90 library) — generated "
           "by Calango\n"
           "import json\n"
           "import os\n"
           "import shutil\n"
           "import numpy as np\n"
           "from ase.io import read\n"
           "from ase.io.wannier90 import read_wout_all\n"
           "from ase.calculators.vasp import Vasp\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble()
        << "atoms = read('structure.extxyz')\n"
        << AseScriptGenerator::vaspPotcarResolutionSnippet(
               cfg.calculator.vaspPotcarPath)
        << "_calango_progress(1, 4)\n"
           "\n";

    const CalculatorConfig& c = cfg.calculator;
    const std::string kpts = std::to_string(c.kpts[0]) + ", "
        + std::to_string(c.kpts[1]) + ", " + std::to_string(c.kpts[2]);
    // PREC must match whatever wrote the CHGCAR this reads (the baseline's
    // own SCF, or -- the no-baseline branch below -- this script's OWN
    // `scf` object): a mismatched FFT grid is a hard VASP error at the
    // ICHARG=11 `bands` pass below, same class of bug as
    // ElectronicScriptGenerator.cpp's own fix (Task 3/4, 2026-08-22).
    const std::string vaspPrec = AseScriptGenerator::vaspPrecString(c.vaspPrec);

    if (!cfg.baselineDir.empty()) {
        out << "# Reuse the baseline's converged charge density (CHGCAR).\n"
               "# Unlike a restart from saved WAVEFUNCTIONS (the GPAW path's\n"
               "# .gpw), CHGCAR carries no k-point-specific data, so the\n"
               "# baseline's OWN symmetry setting does not matter here — this\n"
               "# node's own Wannier90 pass below sets isym=0 on ITSELF.\n"
            << "_base = r\"" << cfg.baselineDir << "\"\n"
               "if not os.path.exists(_base):\n"
               "    raise RuntimeError(\n"
               "        'The baseline charge density is gone: ' + _base + '\\n'\n"
               "        'The Wannier90 run needs it — re-run the Single-Point '\n"
               "        'Calculation that produced it.')\n"
               "_baseline_chgcar = os.path.join(_base, 'CHGCAR')\n"
               "if not os.path.exists(_baseline_chgcar):\n"
               "    raise RuntimeError(\n"
               "        'No CHGCAR found in ' + _base + '. The Wannier90 run "
               "needs '\n"
               "        'the converged charge density — re-run the "
               "Single-Point '\n"
               "        'Calculation with LCHARG = .TRUE. (the default).')\n"
               "if os.path.abspath(_baseline_chgcar) != "
               "os.path.abspath('CHGCAR'):\n"
               "    shutil.copyfile(_baseline_chgcar, 'CHGCAR')\n"
               "_calango_progress(2, 4)\n"
               "\n";
    } else {
        out << "# No baseline: run the SCF here first, writing its own "
               "CHGCAR.\n"
            << "scf = Vasp(xc=\"" << c.vaspXc << "\", encut="
            << c.planeWaveCutoffEv << ",\n"
               "           kpts=(" << kpts
            << "), ismear=0, sigma=0.05,\n"
            << "           prec=\"" << vaspPrec << "\",\n"
               "           lcharg=True, directory=\".\")\n"
               "atoms.calc = scf\n"
               "atoms.get_potential_energy()\n"
               "_calango_progress(2, 4)\n"
               "\n";
    }

    out << "# The win file VASP reads before running its Wannier90 library —\n"
           "# see this function's own doc comment for why mp_grid/kpoints/\n"
           "# unit_cell_cart/atoms_cart are deliberately absent.\n"
           "with open('wannier90.win', 'w') as _fh:\n"
        << "    _fh.write('num_wann = " << cfg.nWannier << "\\n')\n";
    if (cfg.fixedMode != WannierConfig::FixedStatesMode::FromWannierCount)
        out << "    # A frozen-window request (EnergyWindow/BandCount) was "
               "made,\n"
               "    # but this path does not map it yet — see this "
               "function's\n"
               "    # own doc comment. Proceeding with NO frozen window "
               "rather\n"
               "    # than an unverified one.\n"
               "    pass\n"
               "_calango_event('warning', 'The requested disentanglement "
               "window is '\n"
               "               'not applied on the VASP/Wannier90 path yet "
               "— running '\n"
               "               'with no frozen window instead. See "
               "FUTURE.md.')\n";
    out << "    _fh.write('write_hr = .true.\\n')\n"
           "    _fh.write('begin projections\\nrandom\\nend projections\\n')\n"
           "\n"
           "# The Wannier90 pass itself: isym=0 for the full (unsymmetrized)\n"
           "# Brillouin zone the localization needs; LWANNIER90_RUN makes "
           "VASP\n"
           "# run wannier90's disentangle+wannierise INTERNALLY rather than\n"
           "# only writing .amn/.mmn/.eig for an external binary.\n"
        << "bands = Vasp(xc=\"" << c.vaspXc << "\", encut="
        << c.planeWaveCutoffEv << ", icharg=11,\n"
           "             ismear=0, sigma=0.05, isym=0,\n"
        << "             prec=\"" << vaspPrec << "\",\n"
           "             lwannier90=True, lwannier90_run=True,\n"
           "             directory=\".\", kpts=(" << kpts << "))\n"
           "atoms.calc = bands\n"
           "atoms.get_potential_energy()\n"
           "_calango_progress(3, 4)\n"
           "\n"
           "if not os.path.exists('wannier90.wout'):\n"
           "    raise RuntimeError(\n"
           "        'VASP finished but wrote no wannier90.wout — the "
           "Wannier90 '\n"
           "        'library run did not complete. Check OUTCAR for the "
           "actual '\n"
           "        'error.')\n"
           "with open('wannier90.wout') as _fh:\n"
           "    _wout = read_wout_all(_fh)\n"
           "centers = np.asarray(_wout['centers'], dtype=float)\n"
           "spreads = [float(s) for s in np.asarray(_wout['spreads'], "
           "dtype=float)]\n"
           "if centers.shape[0] == 0:\n"
           "    raise RuntimeError(\n"
           "        \"wannier90.wout has no 'Final State' — the Wannier90 "
           "run \"\n"
           "        'inside VASP did not converge, or was never reached. '\n"
           "        'Check OUTCAR and wannier90.wout for the actual "
           "error.')\n"
           "total_spread = float(np.nansum(spreads)) if spreads else "
           "float('nan')\n"
           "_calango_event('info', 'centres: %d, total spread %.3f A^2' "
           "% (len(spreads), total_spread))\n"
           "\n"
           "hr_file = None\n"
           "if os.path.exists('wannier90_hr.dat'):\n"
           "    shutil.copyfile('wannier90_hr.dat', 'wannier_hr.dat')\n"
           "    hr_file = 'wannier_hr.dat'\n"
           "    _calango_event('info', 'H(R) written by VASP\\'s own "
           "Wannier90 library')\n"
           "else:\n"
           "    _calango_event('warning',\n"
           "                   'VASP wrote no wannier90_hr.dat — H(R) will "
           "be '\n"
           "                   'unavailable to Boltzmann Transport / Berry "
           "Phase / cRPA.')\n"
           "\n"
           // The Fermi level travels with the run: every downstream consumer
           // of H(R) (interpolation, Fermi surface, topology) needs a zero of
           // energy, and once the .gpw is out of the picture there is nowhere
           // else to get it from. VASP's own value, as ASE reads it out of
           // OUTCAR/vasprun.xml.
           "try:\n"
           "    efermi = float(atoms.calc.get_fermi_level())\n"
           "except Exception as _exc:  # no calculator state, or an older ASE\n"
           "    efermi = None\n"
           "    _calango_event('warning',\n"
           "                   'Fermi level unavailable (%s) — downstream '\n"
           "                   'interpolation will assume 0 eV.' % _exc)\n"
           "else:\n"
           "    _calango_event('info', 'Fermi level: %.4f eV' % efermi)\n"
           "\n"
        << "result = {\n"
           "    'efermi': efermi,\n"
           "    'total_spread': total_spread,\n"
           "    'functional_value': None,  # not computed on the VASP/\n"
           "                                # Wannier90 path; None -> JSON\n"
           "                                # null, never a bare NaN token\n"
           "    'gpw': None,\n"
        << "    'nwannier': " << cfg.nWannier << ",\n"
           "    'projection': 'random',\n"
           "    'centers': [[float(v) for v in row] for row in centers],\n"
           "    'spreads': spreads,\n"
           "    'cubes': [],\n"
           "    'hr': hr_file,\n"
           "    'cell': [[float(v) for v in row]\n"
           "             for row in np.asarray(atoms.cell, dtype=float)],\n"
           "    'engine': 'VASP',\n"
           "}\n"
           "with open('wannier.json', 'w') as _fh:\n"
           "    json.dump(result, _fh, indent=2)\n"
           "print('CALANGO_RESULT wannier=wannier.json', flush=True)\n"
           "_calango_progress(4, 4)\n";
    return out.str();
}

} // namespace

std::string generateWannierScript(const WannierConfig& cfg)
{
    // VASP is a COMPLETELY DIFFERENT path — its own native Wannier90
    // library, not ase.dft.wannier at all — so it bypasses everything
    // below entirely rather than threading a second calculator branch
    // through GPAW-oriented code. See generateVaspWannier90Script()'s own
    // doc comment.
    if (cfg.calculator.calculator == CalculatorKind::Vasp)
        return generateVaspWannier90Script(cfg);

    const std::string preamble =
        std::string(
            "# Wannier Functions — generated by "
            "Calango\n"
            "import json\n"
            "import os\n"
            "import glob\n"
            "import numpy as np\n"
            "from ase.io import read\n"
            "\n")
        + AseScriptGenerator::jsonLoggerPreamble();

    const int nWannier = cfg.nWannier > 0 ? cfg.nWannier : 4;

    // Map the (untranslated) projection key onto ASE's `initialwannier` arg.
    // The atomic sets fall back to 'orbitals', from which ASE derives the
    // projections; 'bloch' / 'random' pass straight through.
    const std::string init = (cfg.initialWannier == "bloch"
                              || cfg.initialWannier == "random")
                                 ? cfg.initialWannier
                                 : std::string("orbitals");

    const int maxIter = cfg.maxIterations > 0 ? cfg.maxIterations : 50;

    // How the fixed (frozen) part of the Hilbert space is chosen.
    //
    // ASE takes `fixedenergy` OR `fixedstates` and raises RuntimeError('You can
    // not set both fixedenergy and fixedstates') when handed both, so they are
    // emitted as two variables of which AT MOST ONE is ever non-None. Both None
    // is the documented default: ASE then fixes exactly `nwannier` states per
    // k-point with no extra degrees of freedom.
    std::ostringstream fixedVars;
    fixedVars << "# The fixed (frozen) part of the Hilbert space. ASE accepts "
                 "one of\n"
                 "# these or neither — passing both raises.\n";
    switch (cfg.fixedMode) {
    case WannierConfig::FixedStatesMode::EnergyWindow:
        // The reference level is ASE's, not ours: the CONDUCTION BAND MINIMUM
        // for a gapped system, the Fermi level for a metal (choose_states() in
        // ase/dft/wannier.py). Spelled out because the number on its own reads
        // as "above E_F" and is not.
        fixedVars << "# fixedenergy is measured from the CONDUCTION BAND "
                     "MINIMUM when the\n"
                     "# system has a gap (> 0.01 eV) and the value is >= 0.01 "
                     "eV; from the\n"
                     "# Fermi level otherwise. This is ASE's rule, not a "
                     "Calango choice.\n"
                  << "_fixedenergy = " << cfg.energyWindowEv << "\n"
                  << "_fixedstates = None\n";
        break;
    case WannierConfig::FixedStatesMode::BandCount:
        fixedVars << "# An explicit band count, applied at every k-point.\n"
                  << "_fixedenergy = None\n"
                  << "_fixedstates = "
                  << (cfg.fixedStates > 0 ? cfg.fixedStates : nWannier)
                  << "\n";
        break;
    case WannierConfig::FixedStatesMode::FromWannierCount:
        fixedVars << "# Neither: ASE fixes exactly nwannier states per "
                     "k-point.\n"
                     "_fixedenergy = None\n"
                     "_fixedstates = None\n";
        break;
    }

    // Marzari-Vanderbilt localization via ASE. Guard the import so a missing
    // ase.dft.wannier surfaces a clear error rather than a bare ImportError.
    std::ostringstream computeStream;
    computeStream
        << "try:\n"
           "    from ase.dft.wannier import Wannier\n"
           "except Exception as _e:\n"
           "    raise RuntimeError('Wannier Functions '\n"
           "                       'require ase.dft.wannier, which is not '\n"
           "                       'available in this ASE install: ' + repr(_e))\n"
        << "nwannier = " << nWannier << "\n"
        << fixedVars.str()
        // Pre-flight, and it has to live HERE rather than in the wizard.
        //
        // With no frozen window ASE fixes exactly `nwannier` states per
        // k-point, so nwannier can never exceed the band count the parent
        // ground state actually converged. Asking for more dies inside
        // rotation_from_projection with
        //     could not broadcast input array from shape (N,W) into shape (W,W)
        // which names neither the Wannier count nor the band count.
        //
        // The wizard cannot catch this: GPAW chooses nbands itself unless it
        // was set explicitly (9 for a one-atom Cu cell), so the number does
        // not exist anywhere until the .gpw is open. This is the first point
        // that knows both, and it runs before any expensive work.
        << "_nbands = int(calc.get_number_of_bands())\n"
           "_calango_event('info', 'ground state has %d bands; %d Wannier "
           "functions requested' % (_nbands, nwannier))\n"
           "if nwannier > _nbands:\n"
           "    raise RuntimeError(\n"
           "        'Requested %d Wannier functions, but the ground state in "
           "%s has only '\n"
           "        '%d bands. With no frozen window ASE fixes exactly one "
           "state per '\n"
           "        'Wannier function, and it cannot fix more states than "
           "exist — it '\n"
           "        'fails inside the rotation setup with a shape error "
           "naming neither '\n"
           "        'number. Lower the Wannier count to %d or fewer, or "
           "re-run the '\n"
           "        'Single-Point Calculation with more bands.'\n"
           "        % (nwannier, os.path.basename(_gpw_path), _nbands, "
           "_nbands))\n"
        << "# initialwannier seeds the trial projections (atomic orbitals give\n"
           "# good initial overlaps + center estimates).\n"
           "#\n"
           "# ASE derives the extra degrees of freedom as\n"
           "#     edf_k = nwannier - fixedstates_k\n"
           "# and never checks the sign. Fixing more states than there are\n"
           "# Wannier functions makes it negative, and the run then dies inside\n"
           "# the rotation setup with a shape error naming neither number — so\n"
           "# the construction is wrapped to say which two disagree.\n"
           "try:\n"
        << "    wan = Wannier(nwannier=nwannier, calc=calc,\n"
           "                  fixedenergy=_fixedenergy, "
           "fixedstates=_fixedstates,\n"
        << "                  initialwannier='" << init << "')\n"
           "except Exception as _e:\n"
           "    _fixed = None\n"
           "    try:\n"
           "        # Best effort: ASE's own state chooser, so the numbers "
           "reported\n"
           "        # are the ones it actually computed. Guarded because it is\n"
           "        # module-internal and may move between ASE versions.\n"
           "        from ase.dft.wannier import choose_states, get_calcdata\n"
           "        _cd = get_calcdata(calc)\n"
           "        _fs, _ = choose_states(_cd, _fixedenergy, _fixedstates,\n"
           "                               len(_cd.kpt_kc), nwannier,\n"
           "                               lambda *a, **k: None, 0)\n"
           "        _fixed = int(max(_fs))\n"
           "    except Exception:\n"
           "        pass\n"
           "    if _fixed is not None and _fixed > nwannier:\n"
           "        raise RuntimeError(\n"
           "            'The frozen window fixes %d states at some k-point, "
           "but only '\n"
           "            '%d Wannier functions were requested — ASE needs at "
           "least as '\n"
           "            'many Wannier functions as fixed states. Raise the "
           "Wannier '\n"
           "            'count to %d or more, or narrow the window.'\n"
           "            % (_fixed, nwannier, _fixed)) from _e\n"
           "    raise\n"
           "_calango_event('info', 'fixed states per k-point: %s, extra "
           "degrees of '\n"
           "                       'freedom: %s' % (wan.fixedstates_k, "
           "wan.edf_k))\n"
        << "# Iterative Marzari-Vanderbilt minimization (Omega = Omega_I +\n"
           "# Omega_tilde_D). Repeat localize() until the spread functional\n"
           "# stops decreasing (early exit), capped at the requested maximum.\n"
           "_prev = None\n"
        << "for _it in range(" << maxIter << "):\n"
        << "    wan.localize(step=0.25, tolerance=1e-6)\n"
           "    _val = float(wan.get_functional_value())\n"
           "    if _prev is not None and abs(_val - _prev) < 1e-6:\n"
           "        break\n"
           "    _prev = _val\n"
           "_calango_progress(3, 3)\n";

    // Evaluation. Centres are in Å; per-orbital spreads use get_spreads() (Å²)
    // with get_radii()² as a fallback; every optional API is guarded so version
    // drift degrades gracefully.
    const std::string evaluate =
        "centers = np.asarray(wan.get_centers(), dtype=float)\n"
        "try:\n"
        "    spreads = [float(s) for s in np.asarray(wan.get_spreads(),\n"
        "                                            dtype=float)]\n"
        "except Exception:\n"
        "    try:\n"
        "        _radii = np.asarray(wan.get_radii(), dtype=float)\n"
        "        spreads = [float(r * r) for r in _radii]\n"
        "    except Exception:\n"
        "        spreads = [float('nan')] * nwannier\n"
        "total_spread = float(np.nansum(spreads))\n"
        // The total is a SUM, so it is dominated by whichever function is
        // most diffuse — and on a d-band metal that is the single s-like one,
        // not a uniformly bad wannierization. Measured on FCC Cu: five d
        // functions at 0.32-0.54 A^2 (the literature scale) beside one s
        // function at 4.49, and a frozen window pulls that one to 2.67 while
        // barely moving the d manifold. Reported so a high total can be read
        // as the one thing it usually is, instead of being taken for a broken
        // spread functional.
        "_finite = [s for s in spreads if s == s]\n"
        "if _finite:\n"
        "    _worst = max(_finite)\n"
        "    _rest = sorted(_finite)[:-1]\n"
        "    _median = _rest[len(_rest) // 2] if _rest else _worst\n"
        "    _calango_event('info', 'spreads: total %.3f A^2 over %d "
        "functions; most diffuse %.3f, median of the rest %.3f'\n"
        "                   % (total_spread, len(_finite), _worst, _median))\n"
        "    if _rest and _worst > 4.0 * _median and _fixedenergy is None "
        "and _fixedstates is None:\n"
        "        _calango_event('warning', 'One Wannier function is %.1fx more "
        "diffuse than the rest, which is what dominates the total. This run "
        "used no frozen window; setting one (Disentanglement) typically halves "
        "that function without moving the others.' % (_worst / _median))\n"
        "# Localization functional Ω = Ω_I + Ω_D̃ (the minimized value); it is\n"
        "# the trial-projection overlap metric surfaced in the MLWF viewer.\n"
        "#\n"
        "# None on failure, not float('nan'): json.dump writes a bare NaN\n"
        "# token for that by default, which is not valid JSON (RFC 8259) and\n"
        "# Qt's strict QJsonDocument parser rejects it outright — the whole\n"
        "# file would fail to load, not just this one field. The MLWF viewer\n"
        "# (MlwfViewer.cpp) already treats a missing/null value the same way\n"
        "# it would have treated a NaN: no functional line shown.\n"
        "try:\n"
        "    functional_value = float(wan.get_functional_value())\n"
        "except Exception:\n"
        "    functional_value = None\n"
        "cubes = []\n"
        "for _i in range(nwannier):\n"
        "    _fn = 'wannier_%d.cube' % _i\n"
        "    wan.write_cube(_i, _fn)\n"
        "    cubes.append(_fn)\n"
        // The real-space Hamiltonian. Everything needed for it is already in
        // hand at this point — the localization is converged and `wan` holds
        // it — and without it the run's output is a picture of the orbitals
        // with the physics thrown away: Boltzmann Transport, Berry Phase and
        // cRPA all consume H(R) and nothing else, and had no way to reach one
        // that Calango produced.
        //
        // Written in wannier90's _hr.dat layout because that is the format
        // those three already read, through Calango's own parser. Emitting a
        // text table is not a dependency on wannier90 any more than reading
        // one is; it is the interchange format for this object.
        "hr_file = 'wannier_hr.dat'\n"
        "try:\n"
        // H(R) is a Fourier sum over the k-mesh, so it is periodic in R with
        // the mesh's own period: the Born-von Karman cell centred on the
        // origin is the complete, non-redundant set. Listing more R vectors
        // would repeat blocks, listing fewer would truncate the interaction.
        "    _n1, _n2, _n3 = (int(_n) for _n in wan.kptgrid)\n"
        "    def _span(_n):\n"
        "        return range(-(_n // 2), -(_n // 2) + _n)\n"
        "    _rs = [(_i, _j, _k) for _i in _span(_n1)\n"
        "           for _j in _span(_n2) for _k in _span(_n3)]\n"
        "    with open(hr_file, 'w') as _fh:\n"
        "        _fh.write(' Wannier Hamiltonian H(R) written by Calango\\n')\n"
        "        _fh.write('%12d\\n' % nwannier)\n"
        "        _fh.write('%12d\\n' % len(_rs))\n"
        // No Wigner-Seitz folding is applied, so every point has weight 1. The
        // reader DIVIDES by these, so writing them wrong rescales every
        // hopping silently.
        "        for _o in range(0, len(_rs), 15):\n"
        "            _fh.write(''.join('%5d' % 1 for _ in _rs[_o:_o + 15])"
        " + '\\n')\n"
        // wannier90 column order: the row index m varies fastest.
        "        for (_r1, _r2, _r3) in _rs:\n"
        "            _h = np.asarray(wan.get_hopping([_r1, _r2, _r3]),\n"
        "                            dtype=complex)\n"
        "            for _n in range(nwannier):\n"
        "                for _m in range(nwannier):\n"
        "                    _fh.write('%5d%5d%5d%5d%5d%22.12f%22.12f\\n'\n"
        "                              % (_r1, _r2, _r3, _m + 1, _n + 1,\n"
        "                                 _h[_m, _n].real, _h[_m, _n].imag))\n"
        // Hermiticity is the cheap check that the blocks came out in the right
        // gauge: H(-R) must be H(R) conjugate-transposed. A failure here means
        // the file is wrong in a way no consumer would notice.
        "    _worst = 0.0\n"
        "    for (_r1, _r2, _r3) in _rs[:20]:\n"
        "        _a = np.asarray(wan.get_hopping([_r1, _r2, _r3]))\n"
        "        _b = np.asarray(wan.get_hopping([-_r1, -_r2, -_r3]))\n"
        "        _worst = max(_worst, float(np.max(np.abs(_a - _b.conj().T))))\n"
        "    if _worst > 1e-6:\n"
        "        _calango_event('warning',\n"
        "                       'H(R) is not Hermitian to 1e-6 (worst %.2e) - "
        "transport and cRPA results from it are suspect' % _worst)\n"
        "    else:\n"
        "        _calango_event('info',\n"
        "                       'H(R): %d blocks on a %dx%dx%d mesh, Hermitian "
        "to %.1e' % (len(_rs), _n1, _n2, _n3, _worst))\n"
        "except Exception as _exc:\n"
        // Not fatal: the centres, spreads and cubes are the run's primary
        // output and are already on disk. Losing H(R) costs the three
        // downstream modules, not this run.
        "    hr_file = None\n"
        "    _calango_event('warning',\n"
        "                   'could not write the Wannier Hamiltonian: %r' "
        "% (_exc,))\n";

    // Write wannier.json and emit the result marker the controller watches for.
    // `projection` records the trial-orbital seed so the Wannier band
    // interpolation can rebuild the same localization from the saved .gpw.
    std::ostringstream tail;
    // Recorded for the same reason the VASP path records it: H(R) is a
    // hopping table with no zero of energy attached, and every consumer of it
    // needs one. GPAW consumers can also reopen the .gpw for it, so a failure
    // here is a warning rather than an error.
    tail << "try:\n"
            "    efermi = float(calc.get_fermi_level())\n"
            "except Exception as _exc:\n"
            "    efermi = None\n"
            "    _calango_event('warning',\n"
            "                   'Fermi level unavailable (%s).' % _exc)\n"
            "result = {\n"
            "    'efermi': efermi,\n"
            "    'total_spread': total_spread,\n"
            "    'functional_value': functional_value,\n"
            // The interpolation re-runs the SAME localization from the SAME
            // wavefunctions, so both have to be recorded rather than guessed.
            // `gpw` may point outside this directory (a run started from a
            // single-point baseline never writes one of its own), and
            // `nwannier` used to be recovered as len(centers) — which is only
            // incidentally right.
            "    'gpw': _gpw_path,\n"
            "    'nwannier': int(nwannier),\n"
         << "    'projection': '" << init << "',\n"
         << "    'centers': [[float(v) for v in row] for row in centers],\n"
            "    'spreads': [float(s) for s in spreads],\n"
            "    'cubes': cubes,\n"
            // The real-space Hamiltonian, or null when it could not be
            // written. Recorded rather than assumed by name so a consumer can
            // tell "this run has no H(R)" from "look for it under the usual
            // name and hope".
            "    'hr': hr_file,\n"
            // The cell H(R)'s integer R vectors are expressed in. A hopping
            // table is meaningless without it — every consumer needs it to
            // turn R into a distance and dH/dk into a velocity — and reading
            // it back out of the .gpw would mean starting GPAW again.
            "    'cell': [[float(v) for v in row]\n"
            "             for row in np.asarray(atoms.cell, dtype=float)],\n"
            "}\n"
            "json.dump(result, open('wannier.json', 'w'), indent=2)\n"
            "print('CALANGO_RESULT wannier=wannier.json', flush=True)\n";

    return preamble + groundState(cfg) + computeStream.str() + evaluate
        + tail.str();
}

std::string wannierHrInterpolatorPreamble()
{
    // Kept as a raw string: it is ordinary Python with no interpolated C++
    // values, and the escaping tax of the "line"\n" style buys nothing here.
    return R"PY(
class _HrHamiltonian:
    """H(k) from a wannier90 `_hr.dat`, with ase.dft.wannier's interface.

    Exposes get_hamiltonian_kpoint(kpt) so a consumer written against
    ase.dft.wannier.Wannier works unchanged on a VASP-sourced localization.
    """

    def __init__(self, path):
        with open(path) as _fh:
            _lines = [_l for _l in _fh.read().splitlines()]
        # Line 0 is a free-form comment (wannier90 writes a timestamp).
        self.comment = _lines[0].strip()
        self.num_wann = int(_lines[1].split()[0])
        self.nrpts = int(_lines[2].split()[0])
        # Degeneracies: nrpts integers, 15 per line. They count the
        # Wigner-Seitz images a given R stands for; dividing them out is
        # not optional - skipping it misweights every zone-boundary hopping.
        _deg = []
        _i = 3
        while len(_deg) < self.nrpts:
            _deg.extend(int(_t) for _t in _lines[_i].split())
            _i += 1
        _deg = np.asarray(_deg[:self.nrpts], dtype=float)
        _want = self.num_wann * self.num_wann * self.nrpts
        _rows = np.asarray(
            [[float(_t) for _t in _l.split()]
             for _l in _lines[_i:_i + _want] if _l.strip()],
            dtype=float)
        if _rows.shape[0] != _want:
            raise RuntimeError(
                '%s is truncated: expected %d hopping rows for '
                'num_wann=%d, nrpts=%d, found %d.'
                % (path, _want, self.num_wann, self.nrpts, _rows.shape[0]))
        # Row order in the file is R slowest, then n, then m fastest - so a
        # (nrpts, n, m) reshape, transposed to the (m, n) convention
        # ase.dft.wannier's get_hopping() returns.
        _h = _rows.reshape(self.nrpts, self.num_wann, self.num_wann, 7)
        self.r_vectors = _rows[::self.num_wann * self.num_wann, 0:3].astype(int)
        _hop = _h[..., 5] + 1j * _h[..., 6]
        self.hoppings = np.transpose(_hop, (0, 2, 1)) / _deg[:, None, None]

    def get_hamiltonian_kpoint(self, kpt):
        """H(k) = sum_R e^{2 pi i k.R} H(R) / deg(R), Hermitized."""
        _phase = np.exp(2j * np.pi * (self.r_vectors @ np.asarray(kpt, float)))
        _hk = np.tensordot(_phase, self.hoppings, axes=(0, 0))
        # Numerically symmetric rather than merely nearly so: the eigenvalues
        # feed a band structure, and eigh on a slightly non-Hermitian matrix
        # silently reads only one triangle.
        return 0.5 * (_hk + _hk.conj().T)


def _hr_hermiticity_report(wan):
    """Warn if H(R) and H(-R) are not conjugate transposes of one another."""
    _index = {tuple(int(_v) for _v in _r): _i
              for _i, _r in enumerate(wan.r_vectors)}
    _worst = 0.0
    for _r, _i in list(_index.items())[:64]:
        _j = _index.get(tuple(-_v for _v in _r))
        if _j is None:
            continue
        _worst = max(_worst, float(np.max(np.abs(
            wan.hoppings[_i] - wan.hoppings[_j].conj().T))))
    return _worst
)PY";
}
std::string wannierHrSetupBlock(const std::string& module, int progressStage,
                                int progressTotal)
{
    std::ostringstream out;
    out << R"PY(
# --- Where does H(k) come from? ------------------------------------------
# Two sources, and the choice is made by what the run actually left behind
# rather than by engine name. A GPAW-sourced run reopens its .gpw and
# rebuilds the ase.dft.wannier localization. A VASP-sourced one has no
# restartable wavefunction at all (engine='VASP', gpw=None -- see
# WannierScriptGenerator.cpp's generateVaspWannier90Script), but VASP's own
# linked Wannier90 library already wrote the real-space Hamiltonian to
# wannier90_hr.dat, which carries exactly the same information: H(R), and
# therefore H(k) at any k. Reading that file is what turned this module
# from "not implemented for VASP" into an engine-agnostic one.
_hr = _meta.get('hr')
if _hr and not os.path.isabs(_hr):
    _hr = os.path.join(_base, _hr)
_use_hr = bool(_hr and os.path.exists(_hr)) and not _meta.get('gpw')
if _meta.get('engine') == 'VASP' and not _use_hr:
    raise RuntimeError(
        'The MLWF run in ' + _base + ' used VASP\'s own Wannier90 library, so '
)PY";
    out << "        '" << module
        << " reads its real-space Hamiltonian '\n";
    out << R"PY(        '(wannier_hr.dat) rather than restarting a wavefunction file -- '
        + ('but that file is not in the run directory. '
           if _meta.get('hr') else
           'but that run recorded none: VASP wrote no wannier90_hr.dat, which '
           'means LWANNIER90 produced no Hamiltonian. ')
        + 'Re-run the MLWF calculation and check OUTCAR / wannier90.wout.')

if _use_hr:
    from ase import Atoms
    wan = _HrHamiltonian(_hr)
    nwannier = wan.num_wann
    _cell = np.asarray(_meta.get('cell'), dtype=float)
    if _cell.shape != (3, 3):
        raise RuntimeError(
            'wannier.json in ' + _base + ' records no 3x3 cell. H(R) is a '
            'table indexed by INTEGER lattice vectors and is meaningless '
            'without the lattice they index -- re-run the MLWF calculation.')
    # No positions: nothing downstream needs them, and inventing atoms to
    # fill a cell would be a lie about what the run contained. The cell is
    # what the band path and the reciprocal mesh are built from.
    atoms = Atoms(cell=_cell, pbc=True)
    # Without a zero of energy every band and every Fermi surface is offset
    # by an unknown constant, so a missing E_F is said out loud rather than
    # quietly defaulted -- 0 eV looks like a converged reference.
    efermi = _meta.get('efermi')
    if efermi is None:
        efermi = 0.0
        _calango_event('warning',
                       'The MLWF run recorded no Fermi level; energies are '
                       'referenced to 0 eV, not to E_F.')
    else:
        efermi = float(efermi)
    # H(-R) must be H(R) conjugate-transposed. A failure means the file is
    # wrong in a way no consumer downstream would notice on its own.
    _worst = _hr_hermiticity_report(wan)
    if _worst > 1e-6:
        _calango_event('warning',
                       'H(R) is not Hermitian to 1e-6 (worst %.2e) -- the '
                       'interpolated result is suspect.' % _worst)
    _calango_event('info',
                   'H(R): %d Wannier functions, %d R-vectors, from %s'
                   % (wan.num_wann, wan.nrpts, os.path.basename(_hr)))
)PY";
    // Nothing to localize on this route -- H(R) IS the converged answer --
    // so the arm reaches the GPAW arm's post-localization stage at once.
    out << "    _calango_progress(" << progressStage << ", " << progressTotal
        << ")\n"
           "else:\n";
    return out.str();
}
std::string generateWannierInterpolationScript(
    const std::string& mlwfDir, const WannierInterpolationConfig& cfg)
{
    const int bandPoints = cfg.bandPoints > 1 ? cfg.bandPoints : 200;
    const int kx = cfg.kmesh[0] > 0 ? cfg.kmesh[0] : 8;
    const int ky = cfg.kmesh[1] > 0 ? cfg.kmesh[1] : 8;
    const int kz = cfg.kmesh[2] > 0 ? cfg.kmesh[2] : 8;
    const double width = cfg.pdosWidth > 0.0 ? cfg.pdosWidth : 0.1;

    std::ostringstream out;
    out << "# Wannier-interpolated bands + PDOS — generated by Calango\n"
           "import json\n"
           "import os\n"
           "import glob\n"
           "import numpy as np\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble()
        << wannierHrInterpolatorPreamble()
        << "_base = r\"" << mlwfDir << "\"\n"
           "\n"
           "# --- Locate the MLWF run's inputs ---------------------------------\n"
           "# wannier.json is read FIRST, because it is what says where the\n"
           "# wavefunctions are. An MLWF started from a single-point baseline\n"
           "# reads that baseline's .gpw and writes none of its own, so globbing\n"
           "# this directory for one finds nothing — which is exactly how this\n"
           "# step used to fail, with an error that named the wrong directory.\n"
           "_mj = os.path.join(_base, 'wannier.json')\n"
           "if not os.path.exists(_mj):\n"
           "    raise RuntimeError(\n"
           "        'No wannier.json in ' + _base + '. The interpolation rebuilds "
           "the '\n"
           "        'same localization the MLWF run produced, so it needs that "
           "run\\'s '\n"
           "        'summary — point this at a COMPLETED MLWF job.')\n"
           "_meta = json.load(open(_mj))\n"
           "\n";

    // The engine dispatch, shared verbatim with the Fermi-surface and
    // topological-invariant modules — all three need exactly H(k) and got it
    // three identical ways.
    out << wannierHrSetupBlock("band interpolation", 2, 4);

    // The GPAW arm, unchanged, collected into its own stream so it can be
    // re-emitted one level in under that `else:`. Indenting ~110 lines of
    // Python by hand inside C++ string literals is exactly how a block like
    // this gets silently broken.
    std::ostringstream gpaw;
    gpaw << "# Resolution order: the path the MLWF run recorded, then any .gpw\n"
           "# sitting in its directory (which is where a fresh-SCF MLWF puts\n"
           "# wannier.gpw).\n"
           "_gpw_path = _meta.get('gpw')\n"
           "if not (_gpw_path and os.path.exists(_gpw_path)):\n"
           "    _found = sorted(glob.glob(os.path.join(_base, '*.gpw')))\n"
           "    _gpw_path = _found[0] if _found else None\n"
           "if not _gpw_path:\n"
           "    raise RuntimeError(\n"
           "        'The MLWF run in ' + _base + ' left no GPAW wavefunction "
           "(.gpw) '\n"
           "        'this interpolation can restart from, and recorded no path "
           "to one. '\n"
           "        + ('The .gpw it used (' + str(_meta.get('gpw')) + ') no "
           "longer exists. '\n"
           "           if _meta.get('gpw') else\n"
           "           'That run predates Calango recording the path. ')\n"
           "        + 'Re-run the MLWF calculation: it now records where its '\n"
           "          'wavefunctions live, whether it computed them itself or "
           "read '\n"
           "          'them from a single-point baseline.')\n"
           "\n"
           "from gpaw import GPAW\n"
           "calc = GPAW(_gpw_path, txt='gpaw_wannier_interp.txt')\n"
           "atoms = calc.get_atoms()\n"
           "\n"
           "# ase.dft.wannier needs the FULL Brillouin zone. A .gpw written by a\n"
           "# symmetry-reduced run carries only the irreducible wedge, and the\n"
           "# failure surfaces deep inside Wannier as an index error that names\n"
           "# nothing the user can act on. Comparing the two k-point counts is\n"
           "# version-robust and says what to do about it.\n"
           "if len(calc.get_ibz_k_points()) < len(calc.get_bz_k_points()):\n"
           "    raise RuntimeError(\n"
           "        'The wavefunctions in ' + _gpw_path + ' were written by a '\n"
           "        'symmetry-reduced run (' + str(len(calc.get_ibz_k_points()))\n"
           "        + ' of ' + str(len(calc.get_bz_k_points())) + ' k-points "
           "kept). '\n"
           "        'Wannier localization needs the full Brillouin zone — re-run "
           "the '\n"
           "        'baseline single-point with \"Symmetry: off\", or let the "
           "MLWF '\n"
           "        'wizard run its own SCF (which forces it).')\n"
           "\n"
           "try:\n"
           "    efermi = float(calc.get_fermi_level())\n"
           "except Exception:\n"
           "    efermi = 0.0\n"
           "_calango_progress(1, 4)\n"
           "# Prefer the recorded count; len(centers) is only incidentally the\n"
           "# same number, and silently interpolating a different manifold than\n"
           "# the one that was localized is worse than failing.\n"
           "nwannier = int(_meta.get('nwannier') or len(_meta.get('centers', "
           "[])) or 0)\n"
           "if nwannier <= 0:\n"
           "    raise RuntimeError('wannier.json in ' + _base + ' records "
           "neither a '\n"
           "                       'Wannier-function count nor any centers — the "
           "MLWF '\n"
           "                       'run did not complete.')\n"
           "projection = _meta.get('projection', 'orbitals')\n"
           "from ase.dft.wannier import Wannier\n"
           "\n"
           "# --- Disentanglement windows -------------------------------------\n"
           "#\n"
           "# INNER (frozen) window  -> fixedenergy : states below it are\n"
           "#     reproduced exactly by the Wannier manifold.\n"
           "# OUTER window           -> nbands     : the Bloch states the\n"
           "#     manifold may be drawn from at all. ASE documents nbands as\n"
           "#     \"bands to include in localization\", which is precisely what\n"
           "#     an outer window selects, so the cutoff is turned into a band\n"
           "#     count here rather than written down and ignored.\n";

    // INNER (frozen) window → fixedenergy.
    if (cfg.useInnerWindow) {
        gpaw << "# NOTE: ASE measures fixedenergy from the CONDUCTION BAND "
               "MINIMUM when\n"
               "# the system has a gap (> 0.01 eV) and the value is >= 0.01 "
               "eV; from the\n"
               "# Fermi level otherwise.\n"
            << "_fixedenergy = " << cfg.innerWindowEv << "\n";
    } else {
        gpaw << "_fixedenergy = None\n";
    }

    // OUTER window → nbands, resolved from the eigenvalues at run time.
    if (cfg.useOuterWindow) {
        gpaw << "_outer = " << cfg.outerWindowEv << "  # eV above E_F\n"
            << R"PY(# Count the bands that stay below the outer cutoff. The MAXIMUM over
# k-points is taken, not the minimum: nbands is a single number for the
# whole calculation, and truncating to the smallest k-point's count would
# silently drop states that are inside the window everywhere else.
_eps_kn = np.asarray([calc.get_eigenvalues(kpt=_k, spin=0)
                      for _k in range(len(calc.get_ibz_k_points()))])
_nbands = int(max(int(np.searchsorted(np.sort(_row), efermi + _outer))
                  for _row in _eps_kn))
# nbands must still span the manifold being built, and cannot exceed what
# the calculator actually holds.
_nbands = max(_nbands, nwannier)
_nbands = min(_nbands, _eps_kn.shape[1])
_calango_event('info',
               'outer window %.3f eV above E_F -> nbands=%d of %d'
               % (_outer, _nbands, _eps_kn.shape[1]))
)PY";
    } else {
        gpaw << "_nbands = None  # every band the calculator holds\n";
    }
    gpaw << "wan = Wannier(nwannier=nwannier, calc=calc, "
           "initialwannier=projection,\n"
           "              fixedenergy=_fixedenergy, nbands=_nbands)\n";
    gpaw << "_prev = None\n"
           "for _it in range(50):\n"
           "    wan.localize(step=0.25, tolerance=1e-6)\n"
           "    _val = float(wan.get_functional_value())\n"
           "    if _prev is not None and abs(_val - _prev) < 1e-6:\n"
           "        break\n"
           "    _prev = _val\n"
           "_calango_progress(2, 4)\n";

    // wannierHrSetupBlock() above ended on a bare `else:`; this is its arm.
    // From here down nothing knows which engine ran.
    AseScriptGenerator::emitIndented(out, gpaw.str(), "    ");


    // --- Band structure along the requested path --------------------------
    if (!cfg.kpath.empty()) {
        out << "path = atoms.cell.bandpath(path='" << cfg.kpath
            << "', npoints=" << bandPoints << ")\n";
    } else {
        out << "path = atoms.cell.bandpath(npoints=" << bandPoints << ")\n";
    }
    out << "band_energies = []\n"
           "for _kpt in path.kpts:\n"
           "    _H = wan.get_hamiltonian_kpoint(np.asarray(_kpt, dtype=float))\n"
           "    _eigs = np.linalg.eigvalsh(np.asarray(_H))\n"
           "    band_energies.append(np.sort(_eigs.real))\n"
           "band_energies = np.asarray(band_energies)[None, :, :]\n"
           "from ase.spectrum.band_structure import BandStructure\n"
           "bs = BandStructure(path=path, energies=band_energies, "
           "reference=efermi)\n"
           "x, special_x, special_labels = bs.path.get_linear_kpoint_axis()\n"
           "bands = {\n"
           "    \"x\": list(map(float, x)),\n"
           "    \"special_x\": list(map(float, special_x)),\n"
           "    \"special_labels\": list(special_labels),\n"
           "    \"efermi\": float(efermi),\n"
           "    \"energies\": [[list(map(float, kpt)) for kpt in spin]\n"
           "                 for spin in bs.energies],\n"
           "}\n"
           "with open('bands.json', 'w') as _fh:\n"
           "    json.dump(bands, _fh)\n"
           "_calango_progress(3, 4)\n";

    // --- Wannier-projected PDOS on a dense mesh ---------------------------
    out << "# PDOS from the Wannier H(k) on a Monkhorst-Pack mesh; the squared\n"
           "# eigenvector amplitudes project the DOS onto each Wannier "
           "function.\n"
           "from ase.dft.kpoints import monkhorst_pack\n"
        << "_mp = monkhorst_pack((" << kx << ", " << ky << ", " << kz << "))\n"
        << "_all_eigs = []\n"
           "_all_w = []\n"
           "for _kpt in _mp:\n"
           "    _Hk = np.asarray(wan.get_hamiltonian_kpoint("
           "np.asarray(_kpt, dtype=float)))\n"
           "    _w, _v = np.linalg.eigh(_Hk)\n"
           "    _all_eigs.append(_w.real)\n"
           "    _all_w.append(np.abs(_v) ** 2)  # weights[wannier, band]\n"
           "_all_eigs = np.asarray(_all_eigs)\n"
           "_nk, _nb = _all_eigs.shape\n"
        << "_width = " << width << "\n"
           "_emin = float(_all_eigs.min()) - 5.0 * _width\n"
           "_emax = float(_all_eigs.max()) + 5.0 * _width\n"
           "_egrid = np.linspace(_emin, _emax, 600)\n"
           "_proj = np.zeros((_nb, _egrid.size))\n"
           "_norm = 1.0 / (_width * np.sqrt(2.0 * np.pi)) / _nk\n"
           "for _ik in range(_nk):\n"
           "    _wk = _all_w[_ik]\n"
           "    for _ib in range(_nb):\n"
           "        _g = np.exp(-0.5 * ((_egrid - _all_eigs[_ik, _ib]) "
           "/ _width) ** 2) * _norm\n"
           "        _proj += np.outer(_wk[:, _ib], _g)\n"
           "pdos = {\n"
           "    \"energies\": [float(e) for e in _egrid],\n"
           "    \"efermi\": float(efermi),\n"
           "    \"projections\": {\n"
           "        (\"Wannier %d\" % _iw): [float(x) for x in _proj[_iw]]\n"
           "        for _iw in range(_nb)\n"
           "    },\n"
           "}\n"
           "with open('pdos.json', 'w') as _fh:\n"
           "    json.dump(pdos, _fh)\n"
           "_calango_progress(4, 4)\n"
           "print('CALANGO_RESULT bands=bands.json pdos=pdos.json', "
           "flush=True)\n";
    return out.str();
}

} // namespace calango::core
