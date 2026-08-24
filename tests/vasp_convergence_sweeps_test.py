"""The two Parameters-Convergence sweeps with VASP, end to end.

WHY A REAL RUN. Three separate defects were reported against the plane-wave
cutoff sweep with VASP (2026-08-24), and only one of them is visible in the
generated text:

  1. no way to choose the XC functional on the setup page,
  2. "POTCAR not found" against a directory that was correct,
  3. a message about HPC execution on a LOCAL run.

(1) and (3) are GUI defects and are covered by dialog_construction. (2) is a
runtime fact about how ASE resolves POTCARs, and the only way to know a fix
works is to let ASE resolve them. This runs both sweeps against a real VASP
binary, in BOTH documented library layouts.

THE LAYOUTS, and why the distinction matters. ASE searches
$VASP_PP_PATH/<family>/<El>/POTCAR, where <family> comes from `xc`
(potpaw_PBE for the PBE-based functionals, the unversioned potpaw for LDA --
verified against ase/calculators/vasp/create_input.py). So:

  * a directory that is the PARENT of potpaw_PBE/ works as-is;
  * a directory that IS the library (element folders directly inside) cannot
    be read by ASE at all, and the generated script builds a symlink shim.

The old resolver tried a fixed family list and validated whichever existed
first, which is why a PBE-only library passed the pre-flight under xc=LDA and
then failed inside ASE looking for `potpaw`.

SELF-SKIPS (exit 0) without VASP, its POTCARs or ase.

Run directly, or through ctest as `vasp_convergence_sweeps`.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ENCUT_SERIES = [200.0, 250.0]
KMESH_SERIES = [1, 2]


def _skip(reason):
    print("SKIP vasp_convergence_sweeps: " + reason)
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


def _library_root():
    root = os.environ.get("CALANGO_VASP_POTCAR", "").strip() \
        or os.environ.get("VASP_PP_PATH", "").strip() \
        or os.path.expanduser("~/Simulations/vasp/POTCARs")
    return root if os.path.isdir(root) else None


def _find_script_test():
    for cand in sys.argv[1:]:
        if os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    cand = os.path.join(_repo_root(), "build", "calango_script_test")
    return cand if os.access(cand, os.X_OK) else None


def _element_dir(root, element):
    """Where `element`'s POTCAR lives under `root`, in either layout."""
    for base in (root,
                 os.path.join(root, "potpaw_PBE"),
                 os.path.join(root, "potpaw")):
        if os.path.isfile(os.path.join(base, element, "POTCAR")):
            return os.path.join(base, element)
    return None


def _make_layouts(tmp, element):
    """A flat library and a family-parent one, both holding `element`."""
    root = _library_root()
    src = _element_dir(root, element) if root else None
    if src is None:
        return None, None
    flat = os.path.join(tmp, "lib_flat")
    shutil.copytree(src, os.path.join(flat, element))
    family = os.path.join(tmp, "lib_family")
    shutil.copytree(src, os.path.join(family, "potpaw_PBE", element))
    return flat, family


def _retarget(text, potcar_dir, series_line):
    subs = [
        # Indented: the calculator snippet is spliced into a helper
        # function, so the leading whitespace has to survive.
        (r'^(\s*)_potcar_root = _cluster_override or r"[^"]*"$',
         r'\g<1>_potcar_root = _cluster_override or r"%s"' % potcar_dir),
        (r'encut=\d+(\.\d+)?', 'encut=250'),
        (r'prec="[A-Za-z]+"', 'prec="Normal"'),
    ] + series_line
    for pattern, replacement in subs:
        text, n = re.subn(pattern, replacement, text, count=1, flags=re.M)
        if n != 1:
            raise RuntimeError("could not retarget %r" % pattern)
    return text


def _run(text, work, vasp):
    from ase.build import bulk
    from ase.io import write
    os.makedirs(work, exist_ok=True)
    write(os.path.join(work, "structure.extxyz"), bulk("Cu", "fcc", a=3.61))
    script = os.path.join(work, "sweep.py")
    with open(script, "w") as fh:
        fh.write(text)
    env = dict(os.environ, ASE_VASP_COMMAND=vasp)
    env.pop("VASP_PP_PATH", None)          # the script must set it itself
    env.pop("CALANGO_VASP_PP_PATH", None)  # ... from the configured path
    return subprocess.run([sys.executable, script], cwd=work, env=env,
                          capture_output=True, text=True)


def main():
    try:
        import ase  # noqa: F401
    except Exception as exc:
        return _skip("ase is not importable (%r)" % (exc,))
    vasp = _find_vasp()
    if vasp is None:
        return _skip("no VASP binary (set CALANGO_VASP_STD)")
    binary = _find_script_test()
    if binary is None:
        return _skip("calango_script_test is not built")

    tmp = tempfile.mkdtemp(prefix="calango_sweeps_")
    flat, family = _make_layouts(tmp, "Cu")
    if flat is None:
        shutil.rmtree(tmp, ignore_errors=True)
        return _skip("no Cu POTCAR in the configured library")

    scripts = os.path.join(tmp, "scripts")
    os.makedirs(scripts)
    subprocess.run([binary, "--dump", scripts], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    failures = 0
    energies = {}

    # --- The cutoff sweep, in both layouts. -----------------------------
    with open(os.path.join(scripts, "cutoff_convergence_vasp.py")) as fh:
        ecut_src = fh.read()
    series = [(r'^CUTOFFS = \[[^\]]*\]$',
               "CUTOFFS = [%s]" % ", ".join("%g" % v for v in ENCUT_SERIES))]
    for label, potcar_dir in (("family-parent", family), ("flat", flat)):
        work = os.path.join(tmp, "ecut_" + label.replace("-", "_"))
        proc = _run(_retarget(ecut_src, potcar_dir, series), work, vasp)
        if proc.returncode != 0:
            failures += fail("the cutoff sweep failed in the %s layout:\n%s"
                             % (label, (proc.stdout + proc.stderr)[-2500:]))
            continue
        with open(os.path.join(work, "cutoff_convergence.json")) as fh:
            data = json.load(fh)
        points = data.get("points", [])
        if len(points) != len(ENCUT_SERIES):
            failures += fail("the cutoff sweep recorded %d of %d points"
                             % (len(points), len(ENCUT_SERIES)))
            continue
        energies[label] = [p["energy_eV"] for p in points]
        shimmed = "shimmed as" in proc.stdout
        print("ok   the cutoff sweep runs in the %s layout (%d points%s)"
              % (label, len(points), ", via the shim" if shimmed else ""))
        if label == "flat" and not shimmed:
            failures += fail("the flat layout resolved without the shim — "
                             "ASE cannot read that layout directly, so "
                             "something else supplied VASP_PP_PATH")
        if label == "family-parent" and shimmed:
            failures += fail("the family-parent layout should NOT need a "
                             "shim; ASE reads it as-is")

    # The two layouts point at the same PAW dataset, so the sweep must give
    # the same numbers. This is what says the shim resolves to the same
    # library rather than to something else that merely exists.
    if len(energies) == 2:
        a, b = energies["family-parent"], energies["flat"]
        worst = max(abs(x - y) for x, y in zip(a, b))
        if worst > 1e-6:
            failures += fail("the two layouts gave different energies "
                             "(worst %.3e eV) — they are not resolving to "
                             "the same POTCAR" % worst)
        else:
            print("ok   both layouts resolve to the SAME POTCAR (identical "
                  "energies to %.0e eV)" % worst)

    # --- The k-points sweep, once. --------------------------------------
    with open(os.path.join(scripts, "kpoints_convergence_vasp.py")) as fh:
        kpts_src = fh.read()
    # MESHES and K_PER_AXIS are parallel lists the generator writes from one
    # source; retargeting one without the other would desync them, and the
    # recorded k_per_axis would describe a mesh that never ran.
    kseries = [(r'^MESHES = \[[^\]]*\]$',
                "MESHES = [%s]" % ", ".join(
                    "(%d, %d, %d)" % (n, n, n) for n in KMESH_SERIES)),
               (r'^K_PER_AXIS = \[[^\]]*\]$',
                "K_PER_AXIS = [%s]" % ", ".join(
                    str(n) for n in KMESH_SERIES))]
    work = os.path.join(tmp, "kpts")
    proc = _run(_retarget(kpts_src, family, kseries), work, vasp)
    if proc.returncode != 0:
        failures += fail("the k-points sweep failed:\n%s"
                         % (proc.stdout + proc.stderr)[-2500:])
    else:
        with open(os.path.join(work, "kpoints_convergence.json")) as fh:
            data = json.load(fh)
        points = data.get("points", [])
        if len(points) != len(KMESH_SERIES):
            failures += fail("the k-points sweep recorded %d of %d meshes"
                             % (len(points), len(KMESH_SERIES)))
        else:
            ks = [p.get("k_per_axis") for p in points]
            if ks != sorted(ks):
                failures += fail("the mesh series is not ascending: %r" % ks)
            elif points[-1].get("k_per_axis") != max(KMESH_SERIES):
                failures += fail("the densest mesh is not the reference")
            else:
                print("ok   the k-points sweep runs and records %d meshes, "
                      "densest last (%r)" % (len(points), ks))

    shutil.rmtree(tmp, ignore_errors=True)
    print("\n%d check(s) FAILED." % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
