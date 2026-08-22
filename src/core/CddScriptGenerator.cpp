#include "core/CddScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <sstream>

namespace calango::core {

namespace {

/// The VASP and Quantum ESPRESSO fragment machinery.
///
/// The physics is identical to the GPAW path and the arithmetic downstream is
/// literally the same code; only "how do I get one density" changes. What does
/// NOT come free with these two engines is the guarantee GPAW's restart gives:
/// there is no file to read the parent's calculator back out of, so every
/// fragment run is a re-specification, and the FFT grid has to be pinned by
/// hand. Both codes choose their grid from the cutoff AND the cell contents,
/// so a fragment with fewer atoms can silently land on a different one — and
/// two densities on different grids cannot be subtracted at all.
std::string cddExternalEngine(const CddRunConfig& c)
{
    const bool vasp = c.calculator.calculator == CalculatorKind::Vasp;
    const auto& k = c.calculator.kpts;
    std::string out;

    if (vasp) {
        out +=
            "import subprocess\n"
            "from ase.calculators.vasp import Vasp, VaspChargeDensity\n"
            "\n";
        out +=
            "\n"
            "def _read_vasp_density(directory):\n"
            "    \"\"\"CHGCAR, or AECCAR0 + AECCAR2 for the all-electron form.\n"
            "\n"
            "    VaspChargeDensity returns the density already multiplied by\n"
            "    the cell volume (VASP's own file convention), so it is divided\n"
            "    out here to give e/Ang^3 — the units every term downstream,\n"
            "    and the cube writer, assume.\n"
            "    \"\"\"\n";
        if (c.allElectron)
            out +=
                "    core = os.path.join(directory, 'AECCAR0')\n"
                "    val = os.path.join(directory, 'AECCAR2')\n"
                "    if not (os.path.exists(core) and os.path.exists(val)):\n"
                "        raise RuntimeError(\n"
                "            'All-electron CDD needs AECCAR0 and AECCAR2 in '\n"
                "            + directory + ' — re-run with LAECHG = .TRUE., or\\n'\n"
                "            'switch the wizard to the pseudodensity.')\n"
                "    _c = VaspChargeDensity(core)\n"
                "    _v = VaspChargeDensity(val)\n"
                "    frame = _v.atoms[-1]\n"
                "    grid = (np.asarray(_c.chg[-1], dtype=float)\n"
                "            + np.asarray(_v.chg[-1], dtype=float))\n";
        else
            out +=
                "    chg = os.path.join(directory, 'CHGCAR')\n"
                "    if not os.path.exists(chg):\n"
                "        raise RuntimeError('No CHGCAR in ' + directory\n"
                "                           + ' — re-run with LCHARG = .TRUE.')\n"
                "    _d = VaspChargeDensity(chg)\n"
                "    frame = _d.atoms[-1]\n"
                "    grid = np.asarray(_d.chg[-1], dtype=float)\n";
        out +=
            "    return frame, np.ascontiguousarray(grid, dtype=float)\n"
            "\n"
            "\n"
            "atoms, rho_ab = _read_vasp_density(baseline_dir)\n"
        + AseScriptGenerator::vaspPotcarResolutionSnippet(
              c.calculator.vaspPotcarPath)
        +
            "_ngx, _ngy, _ngz = rho_ab.shape\n"
            "print('CALANGO_INFO parent grid %dx%dx%d' % rho_ab.shape,\n"
            "      flush=True)\n"
            "\n"
            "\n"
            "def fragment_density(indices, tag):\n"
            "    \"\"\"One fragment, in the parent's cell and on its grid.\"\"\"\n"
            "    sub = atoms.copy()\n"
            "    del sub[[i for i in range(len(sub)) if i not in set(indices)]]\n"
            "    sub.set_cell(atoms.get_cell())\n"
            "    sub.set_pbc(atoms.get_pbc())\n"
            "    workdir = 'cdd_' + tag\n"
            "    os.makedirs(workdir, exist_ok=True)\n"
            "    sub.calc = Vasp(\n"
            "        directory=workdir,\n"
            "        xc='" + c.calculator.vaspXc + "',\n"
            "        encut=" + std::to_string(c.calculator.planeWaveCutoffEv) + ",\n"
            "        kpts=(" + std::to_string(k[0]) + ", " + std::to_string(k[1])
            + ", " + std::to_string(k[2]) + "),\n"
            "        ismear=0, sigma=0.05,\n"
            "        # Cutting a closed shell in half leaves open-shell\n"
            "        # fragments; spin polarization is what lets them be a\n"
            "        # state rather than a failure to converge.\n"
            "        ispin=2,\n"
            "        lcharg=True,\n"
            + (c.allElectron ? "        laechg=True,\n" : "")
            + "        # THE grid, pinned to the parent's. VASP picks NGXF from\n"
            "        # the cutoff and the cell CONTENTS, so a fragment with\n"
            "        # fewer atoms can land on a different one — and a\n"
            "        # difference of two grids is not a difference at all.\n"
            "        ngxf=_ngx, ngyf=_ngy, ngzf=_ngz,\n"
            "    )\n"
            "    sub.get_potential_energy()\n"
            "    frame, grid = _read_vasp_density(workdir)\n"
            "    return sub, grid, 'VASP spin-polarized fragment'\n";
    } else {
        out +=
            "import subprocess\n"
            "from ase.io.cube import read_cube\n"
            "from ase.calculators.espresso import Espresso, EspressoProfile\n"
            "\n"
            "_pw = os.environ.get('CALANGO_PW_X', 'pw.x')\n"
            "_pp = os.environ.get('CALANGO_PP_X', 'pp.x')\n"
            "_pseudo_dir = r\"" + c.calculator.espressoPseudoDir + "\"\n"
            "pseudopotentials = {}  # EDIT ME: one UPF per element\n"
            "\n"
            "\n"
            "def _export_cube(outdir, prefix, target, grid=None):\n"
            "    \"\"\"pp.x plot_num = 0 — QE keeps the density in a binary file\n"
            "    inside .save with no public reader, so this is the only\n"
            "    supported route to a grid.\n"
            "\n"
            "    `grid`, when given, pins nr1/nr2/nr3 so a fragment is sampled\n"
            "    exactly where the parent was.\n"
            "    \"\"\"\n"
            "    with open(target + '.in', 'w') as handle:\n"
            "        handle.write('&INPUTPP\\n')\n"
            "        handle.write(\"  prefix = '%s'\\n\" % prefix)\n"
            "        handle.write(\"  outdir = '%s'\\n\" % outdir)\n"
            "        handle.write('  plot_num = 0\\n')\n"
            "        handle.write('/\\n&PLOT\\n')\n"
            "        handle.write('  iflag = 3\\n')\n"
            "        handle.write('  output_format = 6\\n')\n"
            "        if grid is not None:\n"
            "            handle.write('  nx = %d\\n  ny = %d\\n  nz = %d\\n'\n"
            "                         % tuple(grid))\n"
            "        handle.write(\"  fileout = '%s'\\n\" % target)\n"
            "        handle.write('/\\n')\n"
            "    with open(target + '.in') as _in:\n"
            "        result = subprocess.run([_pp], stdin=_in,\n"
            "                                capture_output=True, text=True)\n"
            "    if result.returncode != 0 or not os.path.exists(target):\n"
            "        raise RuntimeError('pp.x failed to export %s\\n%s'\n"
            "                           % (target, result.stderr[-2000:]))\n"
            "    with open(target) as handle:\n"
            "        data = read_cube(handle)\n"
            "    return data['atoms'], np.ascontiguousarray(data['data'],\n"
            "                                               dtype=float)\n"
            "\n"
            "\n"
            "_saves = sorted(glob.glob(os.path.join(baseline_dir, '*.save')))\n"
            "if not _saves:\n"
            "    raise RuntimeError('No Quantum ESPRESSO .save directory in '\n"
            "                       + baseline_dir)\n"
            "_prefix = os.path.basename(_saves[0])[:-len('.save')]\n"
            "atoms, rho_ab = _export_cube(baseline_dir, _prefix, 'cdd_parent.cube')\n"
            "_grid = rho_ab.shape\n"
            "print('CALANGO_INFO parent grid %dx%dx%d' % _grid, flush=True)\n"
            "\n"
            "\n"
            "def fragment_density(indices, tag):\n"
            "    \"\"\"One fragment, in the parent's cell and on its grid.\"\"\"\n"
            "    sub = atoms.copy()\n"
            "    del sub[[i for i in range(len(sub)) if i not in set(indices)]]\n"
            "    sub.set_cell(atoms.get_cell())\n"
            "    sub.set_pbc(atoms.get_pbc())\n"
            "    workdir = os.path.abspath('cdd_' + tag)\n"
            "    os.makedirs(workdir, exist_ok=True)\n"
            "    profile = EspressoProfile(command=_pw, pseudo_dir=_pseudo_dir)\n"
            "    system = {\n"
            "        'ecutwfc': " + std::to_string(c.calculator.qeEcutwfcRy) + ",\n"
            "        'ecutrho': "
            + std::to_string(c.calculator.qeEcutrhoRy > 0.0
                                 ? c.calculator.qeEcutrhoRy
                                 : 4.0 * c.calculator.qeEcutwfcRy)
            + ",\n"
            "        'occupations': 'smearing',\n"
            "        'smearing': 'gaussian',\n"
            "        'degauss': 0.01,\n"
            "        # An open-shell fragment of a closed-shell whole needs\n"
            "        # somewhere for the unpaired electron to go.\n"
            "        'nspin': 2,\n"
            "        'starting_magnetization': 0.1,\n"
            "    }\n"
            "    # The FFT grid, taken from the PARENT at run time rather than\n"
            "    # baked in when this script was written.\n"
            "    system['nr1'], system['nr2'], system['nr3'] = _grid\n"
            "    sub.calc = Espresso(\n"
            "        profile=profile,\n"
            "        directory=workdir,\n"
            "        pseudopotentials=pseudopotentials,\n"
            "        input_data={\n"
            "            'control': {'calculation': 'scf', 'prefix': tag,\n"
            "                        'outdir': workdir},\n"
            "            'system': system,\n"
            "            'electrons': {'conv_thr': 1e-8},\n"
            "        },\n"
            "        kpts=(" + std::to_string(k[0]) + ", " + std::to_string(k[1])
            + ", " + std::to_string(k[2]) + "),\n"
            "    )\n"
            "    sub.get_potential_energy()\n"
            "    _, grid = _export_cube(workdir, tag,\n"
            "                           os.path.join(workdir, tag + '.cube'),\n"
            "                           _grid)\n"
            "    return sub, grid, 'QE spin-polarized fragment'\n";
    }
    return out;
}

} // namespace

std::string CddScriptGenerator::generate(const CddRunConfig& c)
{
    const bool gpaw = c.calculator.calculator == CalculatorKind::Gpaw
        || c.calculator.calculator == CalculatorKind::EMT; // EMT = unset default
    const bool vasp = c.calculator.calculator == CalculatorKind::Vasp;
    const bool espresso =
        c.calculator.calculator == CalculatorKind::QuantumEspresso;

    std::ostringstream out;
    out << "#!/usr/bin/env python3\n"
           "# Generated by Calango " << CALANGO_VERSION
        << " — charge density difference.\n"
           "#\n"
           "#     delta_rho = rho(A+B) - rho(A) - rho(B)\n"
           "#\n";
    if (gpaw)
        out << "# The parent calculation is restarted from its .gpw and its "
               "exact\n"
               "# parameters are reused for both fragments, so the three "
               "densities\n"
               "# differ only in which atoms are present.\n";
    else
        out << "# The parent density is read from the completed run; each "
               "fragment\n"
               "# is re-run with the same settings, in the same cell, and — the "
               "part\n"
               "# that actually decides whether the subtraction means anything "
               "— on\n"
               "# the same FFT grid, pinned explicitly to the parent's.\n";
    out << "# Nothing is relaxed: the difference is defined at one geometry.\n"
           "# This is a plain ASE script: edit it freely or run it standalone.\n"
           "\n"
           "import glob\n"
           "import json\n"
           "import os\n"
           "\n"
           "import numpy as np\n"
           "from ase.io.cube import write_cube\n"
           "from ase.units import Bohr\n"
           "\n"
        << "baseline_dir = r\"" << c.baselineDir
        << "\"\n"
           "output_cube = r\""
        << c.outputCube
        << "\"\n"
           "results_path = r\""
        << c.resultsJson
        << "\"\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble();

    if (vasp || espresso)
        // The external engines define `atoms`, `rho_ab` and
        // `fragment_density()` themselves; everything after the partition
        // block is shared with GPAW verbatim.
        out << cddExternalEngine(c);
    else
        out << "_gpw = sorted(glob.glob(os.path.join(baseline_dir, '*.gpw')))\n"
               "if not _gpw:\n"
               "    raise RuntimeError(\n"
               "        'No GPAW wavefunction (.gpw) in ' + baseline_dir + '. The "
               "charge\\n'\n"
               "        'density difference restarts from the parent "
               "single-point, so that\\n'\n"
               "        'run has to have saved one.')\n"
               "\n"
               "os.environ.setdefault('GPAW_NEW', '1')\n"
               "from gpaw import GPAW\n"
               "\n"
               "print('CALANGO_INFO restarting from ' + "
               "os.path.basename(_gpw[0]),\n"
               "      flush=True)\n"
               "parent = GPAW(_gpw[0], txt='cdd_parent.txt')\n"
               "atoms = parent.get_atoms()\n"
               "\n"
               "# The parent's own parameters, not a re-derivation of them. The "
               "new\n"
               "# GPAW engine exposes them through todict(); the legacy "
               "Parameters\n"
               "# object is a plain dict subclass, hence the fallback.\n"
               "_p = parent.parameters\n"
               "try:\n"
               "    params = dict(_p.todict())\n"
               "except AttributeError:\n"
               "    params = dict(_p)\n"
               "# `txt` is a log destination, not physics: each fragment gets "
               "its own.\n"
               "params.pop('txt', None)\n"
               "print('CALANGO_INFO parameters=' + json.dumps(params, "
               "default=str),\n"
               "      flush=True)\n"
               "\n";

    // The partition. Emitted as a literal so the script is reproducible on its
    // own — someone reading run.py can see exactly which atoms were in which
    // fragment without consulting the GUI that launched it.
    out << "subsystem_b = [";
    for (std::size_t i = 0; i < c.subsystemB.size(); ++i)
        out << (i ? ", " : "") << c.subsystemB[i];
    out << "]\n"
           "subsystem_a = [i for i in range(len(atoms)) if i not in "
           "set(subsystem_b)]\n"
           "if not subsystem_a or not subsystem_b:\n"
           "    raise RuntimeError(\n"
           "        'Both subsystems must contain at least one atom; the "
           "difference\\n'\n"
           "        'of a system with nothing is the system itself.')\n"
           "print(f'CALANGO_INFO subsystem_A={subsystem_a} "
           "subsystem_B={subsystem_b}',\n"
           "      flush=True)\n"
           "\n";

    // GPAW defines its own density reader and fragment driver here; the
    // external engines already emitted theirs above, so this whole block is
    // skipped for them and everything after it is shared verbatim.
    if (!gpaw)
        out << "\n";
    else
        out << "\n"
           "def density_of(calc):\n"
           "    \"\"\"One density, as a plain contiguous array.\"\"\"\n"
        << "    grid = calc."
        << (c.allElectron ? "get_all_electron_density(gridrefinement=2)"
                          : "get_pseudo_density()")
        << "\n"
           "    return np.ascontiguousarray(\n"
           "        np.asarray(getattr(grid, 'data', grid), dtype=float))\n"
           "\n"
           "\n"
           "def fragment_density(indices, tag):\n"
           "    \"\"\"SCF a subset of the atoms in the parent's own cell.\"\"\"\n"
           "    sub = atoms.copy()\n"
           "    del sub[[i for i in range(len(sub)) if i not in "
           "set(indices)]]\n"
           "    # Same cell, same boundary conditions, same grid. Two densities\n"
           "    # on different grids cannot be subtracted, and two in different\n"
           "    # cells would differ by their electrostatics rather than by "
           "their\n"
           "    # bonding.\n"
           "    sub.set_cell(atoms.get_cell())\n"
           "    sub.set_pbc(atoms.get_pbc())\n"
           "\n"
           "    # Cutting a closed-shell system in half leaves open-shell\n"
           "    # fragments, and an isolated atom with a partly-filled "
           "degenerate\n"
           "    # p shell will oscillate forever under the settings that "
           "converged\n"
           "    # the molecule in one pass. So the parent's parameters are "
           "tried\n"
           "    # first and only widened if they fail:\n"
           "    #\n"
           "    #   1. exactly what the parent used;\n"
           "    #   2. plus occupation smearing — a numerical aid that lets the\n"
           "    #      degenerate shell fill fractionally, leaving the spin\n"
           "    #      treatment (and so the meaning of the difference) alone;\n"
           "    #   3. plus spin polarization, which IS a different electronic\n"
           "    #      state and is therefore the last resort, not the first.\n"
           "    smearing = {'name': 'fermi-dirac', 'width': 0.05}\n"
           "    ladder = [('parent parameters', {}),\n"
           "              ('smearing 0.05 eV', {'occupations': smearing})]\n"
           "    spin_extra = {'occupations': smearing, 'spinpol': True}\n"
           "    if len(sub) == 1:\n"
           "        # Hund's rule is defined for an atom, so it is only "
           "offered\n"
           "        # for one.\n"
           "        spin_extra['hund'] = True\n"
           "    ladder.append(('spin-polarized', spin_extra))\n"
           "\n"
           "    failures = []\n"
           "    for label, extra in ladder:\n"
           "        kwargs = dict(params)\n"
           "        kwargs.update(extra)\n"
           "        trial = sub.copy()\n"
           "        try:\n"
           "            trial.calc = GPAW(txt=f'cdd_{tag}.txt', **kwargs)\n"
           "            # Single point only — never relaxed. Delta rho is "
           "defined\n"
           "            # at the parent's geometry.\n"
           "            trial.get_potential_energy()\n"
           "        except Exception as error:\n"
           "            failures.append(f'{label}: {error!r}')\n"
           "            print(f'CALANGO_WARN subsystem {tag.upper()} did not "
           "converge '\n"
           "                  f'with {label}', flush=True)\n"
           "            continue\n"
           "        if extra:\n"
           "            print(f'CALANGO_WARN subsystem {tag.upper()} needed "
           "{label} — '\n"
           "                  f'it is an open-shell fragment of a converged "
           "whole',\n"
           "                  flush=True)\n"
           "        return trial, density_of(trial.calc), label\n"
           "    raise RuntimeError(\n"
           "        f'Subsystem {tag.upper()} would not converge. Tried:\\n  '\n"
           "        + '\\n  '.join(failures))\n"
           "\n";

    out << "\n"
           "_calango_progress(0, 3)\n";
    // rho_ab already exists for the external engines — they read it straight
    // out of the completed run's files rather than re-deriving it.
    if (gpaw)
        out << "rho_ab = density_of(parent)\n";
    out << "_calango_progress(1, 3)\n"
           "atoms_a, rho_a, how_a = fragment_density(subsystem_a, 'a')\n"
           "_calango_progress(2, 3)\n"
           "atoms_b, rho_b, how_b = fragment_density(subsystem_b, 'b')\n"
           "_calango_progress(3, 3)\n"
           "\n"
           "if not (rho_ab.shape == rho_a.shape == rho_b.shape):\n"
           "    raise RuntimeError(\n"
           "        f'Grids disagree: A+B {rho_ab.shape}, A {rho_a.shape}, '\n"
           "        f'B {rho_b.shape}. The difference is only defined "
           "pointwise, so\\n'\n"
           "        'all three have to be sampled identically.')\n"
           "\n"
           "delta = rho_ab - rho_a - rho_b\n"
           "\n"
           "with open(output_cube, 'w') as handle:\n"
           "    # Written against the FULL structure: the difference belongs to\n"
           "    # the combined system, and an isosurface over half the atoms "
           "would\n"
           "    # be unreadable.\n"
           "    write_cube(handle, atoms, data=delta)\n"
           "print(f'CALANGO_RESULT density_cube={output_cube} cdd', flush=True)\n"
           "\n"
           "# Integrated statistics. The volume element comes from the cell and\n"
           "# the grid shape, so it is right for a refined all-electron grid as\n"
           "# well as for the pseudodensity's own.\n"
           "#\n"
           "# ASE hands write_cube a density in e/Ang^3 but writes cube files in\n"
           "# Bohr, so the integral is taken here in the array's own units.\n"
           "cell_volume = float(abs(np.linalg.det(np.asarray(atoms.get_cell()))))\n"
           "dv = cell_volume / float(delta.size)\n"
           "moved = float(np.abs(delta).sum() * dv) / 2.0\n"
           "net = float(delta.sum() * dv)\n"
           "summary = {\n"
           "    'all_electron': " << (c.allElectron ? "True" : "False")
        << ",\n"
           "    'subsystem_a': subsystem_a,\n"
           "    'subsystem_b': subsystem_b,\n"
           "    'formula': atoms.get_chemical_formula(),\n"
           "    'formula_a': atoms_a.get_chemical_formula(),\n"
           "    'formula_b': atoms_b.get_chemical_formula(),\n"
           "    'converged_a': how_a,\n"
           "    'converged_b': how_b,\n"
           "    'grid': list(delta.shape),\n"
           "    'charge_transferred': moved,\n"
           "    'net_charge': net,\n"
           "    'delta_min': float(delta.min()),\n"
           "    'delta_max': float(delta.max()),\n"
           "    'cube': output_cube,\n"
           "}\n"
           "with open(results_path, 'w') as handle:\n"
           "    json.dump(summary, handle, indent=2)\n"
           "\n"
           "# The net integral is a check, not a result: A and B together hold\n"
           "# exactly the electrons A+B does, so it must come out at zero. A\n"
           "# value that does not is a grid or convergence problem, and saying "
           "so\n"
           "# here is cheaper than discovering it from a nonsensical "
           "isosurface.\n"
           "print(f'CALANGO_SUMMARY moved={moved:.4f} e  net={net:+.2e} e  '\n"
           "      f'range=[{delta.min():.4e}, {delta.max():.4e}]', flush=True)\n"
           "if abs(net) > 1e-2:\n"
           "    print(f'CALANGO_WARN the fragments do not account for the whole "
           "'\n"
           "          f'electron count (net {net:+.3e} e) — check convergence',\n"
           "          flush=True)\n"
           "_calango_event('done', f'charge density difference written to "
           "{output_cube}')\n"
           "print(f'CALANGO_INFO wrote {results_path}', flush=True)\n";
    return out.str();
}

} // namespace calango::core
