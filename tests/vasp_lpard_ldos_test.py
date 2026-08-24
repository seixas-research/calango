"""LDOS via VASP's LPARD, end to end against a real VASP binary.

WHAT THIS PROVES, and why it is worth a real run. The VASP route of the LDOS
module (LdosScriptGenerator.cpp's generateVaspLdosScript) does not sum
anything itself: it hands the selection to VASP's own post-processing pass
(`LPARD = .TRUE.`, which the VASP wiki calls "a postprocessing step that
requires a pre-converged calculation" in which "no electronic (or ionic)
minimization is performed"), and then converts the resulting PARCHG into the
`.cube` the Volumetric Data dock reads. Every part of that is invisible to a
golden-script text assertion: whether VASP accepts the INCAR at all, whether
NBMOD/EINT select what they are documented to select, and — the one that
actually bit during development — whether the PARCHG-to-cube conversion has
the units right.

THE CLOSED FORM. Select EVERY occupied band (an absolute window running from
far below the valence floor up to E_F, NBMOD = -2) and the partial charge
density IS the valence pseudo-density, whose integral over the cell is the
valence electron count: exactly 8 e for two-atom diamond Si with the standard
PAW_PBE Si potential (4 valence electrons each). Nothing about that number
comes from a previous run of this code.

That check found a real bug: the conversion divided the ASE-returned grid by
the cell volume, having copied CddScriptGenerator.cpp's comment claiming
VaspChargeDensity "returns the density already multiplied by the cell
volume". ASE's own `VaspChargeDensity._read_chg` does `chg /= volume` before
returning, so the values are ALREADY e/Ang^3 and the second division was off
by the cell volume — a factor of 40 for this cell. The integral came out
0.1999 instead of 8.

SELF-SKIPS (exit 0) when VASP, its POTCARs or ase are unavailable, which is
every environment except a licensed one — the same convention the GPAW
benchmarks here follow.

Run directly, or through ctest as `vasp_lpard_ldos`.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

# Two Si atoms, 4 valence electrons each in PAW_PBE Si. The whole point of
# the check: an external, exact number.
VALENCE_ELECTRONS = 8.0
ENCUT = 250.0
KPTS = (2, 2, 2)


def _skip(reason):
    print("SKIP vasp_lpard_ldos: " + reason)
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
    """A VASP_PP_PATH ASE can use, shimming a flat POTCAR/<sym>/ layout.

    Mirrors AseScriptGenerator::vaspPotcarResolutionSnippet's own shim: ASE
    looks under VASP_PP_PATH/potpaw_PBE/<sym>/POTCAR, and this machine (like
    many) keeps a flat <sym>/POTCAR tree instead.
    """
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


def _run_parent(work, vasp, pp_path):
    """The pre-converged calculation LPARD requires: Si, LWAVE = .TRUE."""
    from ase.build import bulk
    from ase.calculators.vasp import Vasp

    os.makedirs(work, exist_ok=True)
    atoms = bulk("Si", "diamond", a=5.43)
    # write_input() resolves POTCARs in THIS process, from os.environ - not
    # from the env dict handed to the subprocess below - so the shimmed path
    # has to be exported here rather than only passed along.
    os.environ["VASP_PP_PATH"] = pp_path
    env = dict(os.environ)
    Vasp(xc="PBE", encut=ENCUT, kpts=KPTS, gamma=True, prec="Normal",
         ismear=0, sigma=0.05, ediff=1e-5, nelm=40,
         # THE tag this whole route depends on. Without it VASP leaves an
         # empty WAVECAR and the LPARD pass has no orbitals to read.
         lwave=True, lcharg=True, directory=work).write_input(atoms)
    proc = subprocess.run(vasp, shell=True, cwd=work, env=env,
                          capture_output=True, text=True)
    with open(os.path.join(work, "vasp.out"), "w") as fh:
        fh.write(proc.stdout)
    if proc.returncode != 0:
        return None, "the parent SCF exited %d" % proc.returncode
    # VASP spells its banner with spaces between the letters; a plain grep
    # for "warning" finds nothing in a run that emitted several.
    banners = [line.strip() for line in proc.stdout.splitlines()
               if "W A R N I N G" in line]
    if banners:
        print("     note: the parent SCF emitted %d VASP warning banner(s): %s"
              % (len(banners), banners[:2]))
    wavecar = os.path.join(work, "WAVECAR")
    if not os.path.exists(wavecar) or os.path.getsize(wavecar) < 4096:
        return None, "the parent left no usable WAVECAR"
    efermi = None
    with open(os.path.join(work, "OUTCAR"), errors="replace") as fh:
        for line in fh:
            if "E-fermi" in line:
                efermi = float(line.split(":")[1].split()[0])
    return efermi, None


def _generated_script(binary, out_dir):
    subprocess.run([binary, "--dump", out_dir], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with open(os.path.join(out_dir, "ldos_vasp_lpard.py")) as fh:
        return fh.read()


def _retarget(text, base, efermi):
    """Point the generated script at this fixture.

    Only the values the wizard itself fills in from the parent's provenance
    are substituted — the base directory, the parent's basis, and the window.
    Everything that is the module's own logic is left exactly as generated.
    """
    subs = [
        (r'^_base = r"[^"]*"$', '_base = r"%s"' % base),
        (r"encut=\d+(\.\d+)?", "encut=%g" % ENCUT),
        (r'prec="[A-Za-z]+"', 'prec="Normal"'),
        (r"kpts=\(\d+, \d+, \d+\), gamma=(True|False)",
         "kpts=(%d, %d, %d), gamma=True" % KPTS),
        # NBMOD = -2: an ABSOLUTE window, running from far below the valence
        # floor to E_F, so every occupied band is selected and the integral
        # below has a closed form.
        (r"nbmod=-3", "nbmod=-2"),
        (r"eint=\[[^\]]*\]", "eint=[-100.0, %.4f]" % efermi),
    ]
    for pattern, replacement in subs:
        text, n = re.subn(pattern, replacement, text, count=1, flags=re.M)
        if n != 1:
            raise RuntimeError("could not retarget %r in the generated script"
                               % pattern)
    return text


def main():
    try:
        import ase  # noqa: F401
    except Exception as exc:
        return _skip("ase is not importable (%r)" % (exc,))

    vasp = _find_vasp()
    if vasp is None:
        return _skip("no VASP binary (set CALANGO_VASP_STD or put vasp_std "
                     "on PATH)")
    pp_path = _potcar_root()
    if pp_path is None:
        return _skip("no usable POTCAR library (set CALANGO_VASP_POTCAR)")
    binary = _find_script_test()
    if binary is None:
        return _skip("calango_script_test is not built (pass its path as "
                     "argv[1])")

    failures = 0
    tmp = tempfile.mkdtemp(prefix="calango_lpard_")
    parent = os.path.join(tmp, "parent")

    efermi, error = _run_parent(parent, vasp, pp_path)
    if error:
        return fail("%s — the LPARD pass cannot be tested without it" % error)
    print("ok   the parent SCF converged and wrote a WAVECAR "
          "(E_F = %.4f eV)" % efermi)

    scripts = os.path.join(tmp, "scripts")
    os.makedirs(scripts)
    text = _retarget(_generated_script(binary, scripts), parent, efermi)

    run = os.path.join(tmp, "ldos")
    os.makedirs(run)
    from ase.build import bulk
    from ase.io import write
    write(os.path.join(run, "structure.extxyz"), bulk("Si", "diamond", a=5.43))
    script = os.path.join(run, "ldos.py")
    with open(script, "w") as fh:
        fh.write(text)
    env = dict(os.environ, VASP_PP_PATH=pp_path, ASE_VASP_COMMAND=vasp)
    proc = subprocess.run([sys.executable, script], cwd=run, env=env,
                          capture_output=True, text=True)
    if proc.returncode != 0:
        return fail("the generated LPARD script exited %d:\n%s"
                    % (proc.returncode,
                       (proc.stdout + proc.stderr)[-3000:]))
    print("ok   the generated LPARD script ran VASP to completion")

    # The INCAR the module actually produced — read back rather than
    # assumed, since ASE is what wrote it.
    with open(os.path.join(run, "INCAR")) as fh:
        incar = fh.read()
    for tag in ("LPARD = .TRUE.", "NBMOD = -2", "ISTART = 1"):
        if tag not in incar:
            failures += fail("the INCAR is missing %r:\n%s" % (tag, incar))
    if "EINT" not in incar:
        failures += fail("the INCAR carries no EINT window:\n%s" % incar)
    if not failures:
        print("ok   the INCAR carries LPARD / NBMOD / EINT / ISTART as "
              "documented")

    if not os.path.exists(os.path.join(run, "PARCHG")):
        return failures + fail("VASP wrote no PARCHG")
    print("ok   VASP wrote a PARCHG")

    with open(os.path.join(run, "ldos.json")) as fh:
        summary = json.load(fh)
    outputs = summary.get("outputs") or []
    if len(outputs) != 1:
        failures += fail("ldos.json lists %d outputs, expected 1 summed "
                         "PARCHG" % len(outputs))
    else:
        integral = outputs[0]["integral_e"]
        if abs(integral - VALENCE_ELECTRONS) > 1e-3:
            failures += fail(
                "the all-valence PARCHG integrates to %.5f e; two Si atoms "
                "carry exactly %.1f valence electrons. A discrepancy of "
                "about the cell volume means the density was normalized "
                "twice." % (integral, VALENCE_ELECTRONS))
        else:
            print("ok   the all-valence PARCHG integrates to %.5f e — the "
                  "valence electron count of two Si atoms, to %.0e"
                  % (integral, abs(integral - VALENCE_ELECTRONS)))
        cube = os.path.join(run, outputs[0]["cube"])
        if not os.path.exists(cube):
            failures += fail("no .cube was written for the volumetric dock")
        else:
            print("ok   ... and reached the volumetric pipeline as %s"
                  % os.path.basename(cube))

    if summary.get("engine") != "VASP" or summary.get("nstates") is not None:
        failures += fail("ldos.json's schema drifted: engine=%r nstates=%r "
                         "(VASP reports no per-state list, and `None` is how "
                         "that differs from 'none selected')"
                         % (summary.get("engine"), summary.get("nstates")))
    else:
        print("ok   ldos.json marks the run VASP and reports nstates=null "
              "rather than 0")

    shutil.rmtree(tmp, ignore_errors=True)
    print("\n%d check(s) FAILED." % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
