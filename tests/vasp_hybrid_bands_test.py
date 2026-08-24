"""Hybrid band structure via KPOINTS_OPT, end to end against a real VASP.

WHY A REAL RUN. ICHARG = 11 is invalid for a hybrid functional -- the VASP
wiki says "the electronic charge density must not be fixed for any hybrid
calculation, i.e., never set ICHARG=11!", because for a hybrid the
Hamiltonian is not a functional of the density alone. So Calango's hybrid
band structure takes the documented alternative: ONE self-consistent hybrid
run on a uniform mesh, carrying the band path in a KPOINTS_OPT file ("an
optional input file to perform an additional one-shot calculation after
self-consistency is reached", VASP >= 6.3.0).

Almost nothing about that is checkable from the generated text. Whether VASP
accepts the tag set, whether it reads KPOINTS_OPT at all, whether it returns
the requested path unchanged, and where it puts the answer are all runtime
facts -- and the last one is not even on the wiki. It was established by
running VASP 6.6.1: the eigenvalues land in vasprun.xml under
<eigenvalues_kpoints_opt>, and NO EIGENVAL_OPT or DOSCAR_OPT text file is
written. ASE has no reader for that element, which is why the generated
script parses it itself.

WHAT IS ASSERTED. Not a stored copy of a previous run:

  1. The run converges and VASP emits no warning banner.
  2. The INCAR carries the hybrid tag set and NO ICHARG.
  3. The k-points VASP returns are exactly the ones the band path asked for,
     in order -- the generated script raises if not, and this checks the
     check.
  4. THE PHYSICS: HSE06 must open Si's direct gap at Gamma well beyond PBE's.
     PBE underestimates it at about 2.5 eV against an experimental ~3.4 eV;
     HSE06 is expected in the low-to-mid 3s. Both are computed here, on the
     same path, same cell, same cutoff, so the comparison is internal and the
     only external number is the experimental gap.

SELF-SKIPS (exit 0) without VASP, its POTCARs or ase.

Run directly, or through ctest as `vasp_hybrid_bands`.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ENCUT = 250.0
KGRID = 2
NPOINTS = 12
PATH = "GX"
# Si has 8 valence electrons per primitive cell -> 4 occupied bands.
OCCUPIED = 4
# Experimental direct gap of Si at Gamma, ~3.4 eV (Landolt-Boernstein). PBE
# is expected to fall far short of it and HSE06 to land near it; the test
# asserts the ORDERING and a generous band, not a specific value.
EXPERIMENT_GAMMA_GAP = 3.4


def _skip(reason):
    print("SKIP vasp_hybrid_bands: " + reason)
    return 0


def fail(message):
    print("FAIL " + message)
    return 1


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _find_vasp():
    for var in ("CALANGO_VASP_STD", "ASE_VASP_COMMAND", "VASP_COMMAND"):
        value = os.environ.get(var, "").strip()
        if value:
            return value
    found = shutil.which("vasp_std")
    if found:
        return found
    guess = os.path.expanduser("~/Codes/vasp/6.6.1/bin/vasp_std")
    return guess if os.access(guess, os.X_OK) else None


def _potcar_root():
    """A VASP_PP_PATH ASE can use, shimming a flat POTCAR/<sym>/ layout."""
    root = os.environ.get("CALANGO_VASP_POTCAR", "").strip() \
        or os.environ.get("VASP_PP_PATH", "").strip() \
        or os.path.expanduser("~/Simulations/vasp/POTCARs")
    if not os.path.isdir(root):
        return None
    for variant in ("potpaw_PBE", "potpaw", "potpaw_LDA"):
        if os.path.isdir(os.path.join(root, variant)):
            return root
    if not os.path.isfile(os.path.join(root, "Si", "POTCAR")):
        return None
    shim = tempfile.mkdtemp(prefix="calango_potcar_shim_")
    for variant in ("potpaw_PBE", "potpaw_LDA", "potpaw"):
        os.symlink(root, os.path.join(shim, variant))
    return shim


def _find_script_test():
    for cand in sys.argv[1:]:
        if os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    cand = os.path.join(_repo_root(), "build", "calango_script_test")
    return cand if os.access(cand, os.X_OK) else None


def _si():
    from ase.build import bulk
    return bulk("Si", "diamond", a=5.43)


def _run_parent(work, vasp, pp_path):
    """The converged semilocal WAVECAR the wiki's step 1 asks for."""
    from ase.calculators.vasp import Vasp
    os.makedirs(work, exist_ok=True)
    os.environ["VASP_PP_PATH"] = pp_path
    Vasp(xc="PBE", encut=ENCUT, kpts=(KGRID,) * 3, gamma=True, prec="Normal",
         ismear=0, sigma=0.05, ediff=1e-5, nelm=40,
         lwave=True, lcharg=True, directory=work).write_input(_si())
    proc = subprocess.run(vasp, shell=True, cwd=work, capture_output=True,
                          text=True)
    with open(os.path.join(work, "vasp.out"), "w") as fh:
        fh.write(proc.stdout)
    if proc.returncode != 0:
        return "the parent SCF exited %d" % proc.returncode
    wavecar = os.path.join(work, "WAVECAR")
    if not os.path.exists(wavecar) or os.path.getsize(wavecar) < 4096:
        return "the parent left no usable WAVECAR"
    return None


def _retarget(text, wavecar_dir):
    subs = [
        (r'^npoints = \d+$', "npoints = %d" % NPOINTS),
        (r'^path_str = .*$', 'path_str = "%s"' % PATH),
        (r'^kgrid = \d+$', "kgrid = %d" % KGRID),
        (r"encut=\d+(\.\d+)?", "encut=%g" % ENCUT),
        (r'prec="[A-Za-z]+"', 'prec="Normal"'),
        (r'else r"[^"]*WAVECAR"', 'else r"%s/WAVECAR"' % wavecar_dir),
    ]
    for pattern, replacement in subs:
        text, n = re.subn(pattern, replacement, text, count=1, flags=re.M)
        if n != 1:
            raise RuntimeError("could not retarget %r" % pattern)
    return text


def _run_script(text, work, vasp, pp_path):
    from ase.io import write
    os.makedirs(work, exist_ok=True)
    write(os.path.join(work, "structure.extxyz"), _si())
    script = os.path.join(work, "bands.py")
    with open(script, "w") as fh:
        fh.write(text)
    env = dict(os.environ, VASP_PP_PATH=pp_path, ASE_VASP_COMMAND=vasp)
    return subprocess.run([sys.executable, script], cwd=work, env=env,
                          capture_output=True, text=True)


def _gamma_gap(bands_json):
    with open(bands_json) as fh:
        data = json.load(fh)
    gamma = data["energies"][0][0]     # spin 0, first k-point = Gamma
    return gamma[OCCUPIED] - gamma[OCCUPIED - 1], data


def main():
    try:
        import ase  # noqa: F401
    except Exception as exc:
        return _skip("ase is not importable (%r)" % (exc,))
    vasp = _find_vasp()
    if vasp is None:
        return _skip("no VASP binary (set CALANGO_VASP_STD)")
    pp_path = _potcar_root()
    if pp_path is None:
        return _skip("no usable POTCAR library (set CALANGO_VASP_POTCAR)")
    binary = _find_script_test()
    if binary is None:
        return _skip("calango_script_test is not built (pass its path as "
                     "argv[1])")

    failures = 0
    tmp = tempfile.mkdtemp(prefix="calango_hybrid_bands_")
    parent = os.path.join(tmp, "parent")
    error = _run_parent(parent, vasp, pp_path)
    if error:
        return fail("%s — the hybrid cannot be tested without it" % error)
    print("ok   the semilocal parent converged and wrote a WAVECAR")

    scripts = os.path.join(tmp, "scripts")
    os.makedirs(scripts)
    subprocess.run([binary, "--dump", scripts], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    with open(os.path.join(scripts, "bands_vasp_hse06.py")) as fh:
        hybrid_text = _retarget(fh.read(), parent)
    run = os.path.join(tmp, "hse06")
    proc = _run_script(hybrid_text, run, vasp, pp_path)
    if proc.returncode != 0:
        return fail("the generated hybrid band script exited %d:\n%s"
                    % (proc.returncode, (proc.stdout + proc.stderr)[-3000:]))
    print("ok   the generated hybrid band script ran VASP to completion")

    with open(os.path.join(run, "vasp.out")) as fh:
        vasp_out = fh.read()
    banners = [l.strip() for l in vasp_out.splitlines() if "W A R N I N G" in l]
    if banners:
        failures += fail("VASP emitted %d warning banner(s): %s"
                         % (len(banners), banners[:3]))
    else:
        print("ok   VASP emitted no warning banner")

    with open(os.path.join(run, "INCAR")) as fh:
        incar = fh.read()
    for tag in ("LHFCALC = .TRUE.", "HFSCREEN = 0.2", "HFRCUT = -1",
                "AEXX = 0.25"):
        if tag not in incar:
            failures += fail("the INCAR is missing %r:\n%s" % (tag, incar))
    if "ICHARG" in incar:
        failures += fail("the INCAR sets ICHARG, which the wiki forbids for "
                         "a hybrid:\n%s" % incar)
    if not failures:
        print("ok   the INCAR carries the hybrid tag set and NO ICHARG")

    if not os.path.exists(os.path.join(run, "KPOINTS_OPT")):
        failures += fail("no KPOINTS_OPT was written")
    else:
        with open(os.path.join(run, "KPOINTS_OPT")) as fh:
            head = fh.read().splitlines()
        if len(head) < 3 or head[2].strip().lower() != "reciprocal":
            failures += fail("KPOINTS_OPT is not an explicit reciprocal "
                             "list: %r" % head[:3])
        elif int(head[1]) != NPOINTS:
            failures += fail("KPOINTS_OPT lists %s points, the path has %d"
                             % (head[1], NPOINTS))
        else:
            print("ok   KPOINTS_OPT carries all %d path points as an "
                  "explicit reciprocal list" % NPOINTS)

    hse_gap, hse_data = _gamma_gap(os.path.join(run, "bands.json"))
    if len(hse_data["energies"][0]) != NPOINTS:
        failures += fail("bands.json has %d k-points, expected %d — the "
                         "generated script's own path check should have "
                         "caught that"
                         % (len(hse_data["energies"][0]), NPOINTS))
    else:
        print("ok   bands.json carries exactly the %d requested k-points, "
              "and the script verified their order against the path"
              % NPOINTS)

    # The semilocal comparison, on the identical path/cell/cutoff.
    with open(os.path.join(scripts, "bands_vasp_pdos.py")) as fh:
        semi_text = fh.read()
    semi_text = re.sub(r'^npoints = \d+$', "npoints = %d" % NPOINTS,
                       semi_text, count=1, flags=re.M)
    semi_text = re.sub(r'^path_str = .*$', 'path_str = "%s"' % PATH,
                       semi_text, count=1, flags=re.M)
    semi_text = re.sub(r'^kgrid = \d+$', "kgrid = %d" % KGRID, semi_text,
                       count=1, flags=re.M)
    semi_text = re.sub(r"encut=\d+(\.\d+)?", "encut=%g" % ENCUT, semi_text,
                       flags=re.M)
    semi_text = re.sub(r'prec="[A-Za-z]+"', 'prec="Normal"', semi_text,
                       flags=re.M)
    semi_text = re.sub(r'else r"[^"]*CHGCAR"',
                       'else r"%s/CHGCAR"' % parent, semi_text, count=1,
                       flags=re.M)
    semi_run = os.path.join(tmp, "pbe")
    semi_proc = _run_script(semi_text, semi_run, vasp, pp_path)
    if semi_proc.returncode != 0:
        failures += fail("the semilocal reference run exited %d:\n%s"
                         % (semi_proc.returncode,
                            (semi_proc.stdout + semi_proc.stderr)[-2000:]))
    else:
        pbe_gap, _ = _gamma_gap(os.path.join(semi_run, "bands.json"))
        print("     Si direct gap at Gamma: PBE %.3f eV, HSE06 %.3f eV "
              "(experiment ~%.1f eV)" % (pbe_gap, hse_gap,
                                         EXPERIMENT_GAMMA_GAP))
        if hse_gap <= pbe_gap:
            failures += fail(
                "HSE06 gave a gap of %.3f eV against PBE's %.3f eV. Exact "
                "exchange OPENS the gap; if it did not, the hybrid tags "
                "reached VASP but the band energies did not come from the "
                "hybrid run." % (hse_gap, pbe_gap))
        elif not 2.5 <= hse_gap <= 4.5:
            failures += fail(
                "HSE06 gave %.3f eV for Si's direct gap at Gamma; the "
                "experimental value is ~%.1f eV and a hybrid is expected "
                "within a few tenths of it."
                % (hse_gap, EXPERIMENT_GAMMA_GAP))
        else:
            print("ok   HSE06 opens Si's Gamma gap from %.3f to %.3f eV, "
                  "toward the experimental ~%.1f eV — the hybrid really is "
                  "what produced these bands"
                  % (pbe_gap, hse_gap, EXPERIMENT_GAMMA_GAP))

    shutil.rmtree(tmp, ignore_errors=True)
    print("\n%d check(s) FAILED." % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
