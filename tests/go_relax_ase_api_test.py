"""The GO relaxation must not depend on ase's Optimizer.converged() signature.

THE FAILURE THIS PINS. A GO Grand Canonical MC run on Linux died before a
single Monte Carlo cycle:

    Traceback (most recent call last):
      File ".../proc_1/run.py", line 2288, in <module>
        energy, equilibration_relocations = equilibrate(atoms)
      File ".../proc_1/run.py", line 1931, in equilibrate
        energy_value, converged, taken = run_relax(system, 0,
      File ".../proc_1/run.py", line 1347, in run_relax
        if optimizer.converged():
    TypeError: Optimizer.converged() missing 1 required positional
    argument: 'gradient'

It looked like an ML-potential or a GPU problem -- it was reported against
MACE on CUDA and again against MatterSim -- and it is neither. The engine
loaded, the device was accepted, and the grand-canonical reference
pre-stage completed (its cache file was written). What differs between the
environments where it fails and the ones where it does not is the ase
version: `Optimizer.converged()` used to take no required argument, and on
a newer ase `gradient` is a REQUIRED POSITIONAL one.

HOW THIS TESTS IT. ase here has the older, permissive signature, so the bug
cannot be reproduced by running the script as-is. The newer signature is
therefore imposed on ase at import time -- exactly the shape the traceback
reports -- and the generated script is run under it. Before the fix that
reproduces the TypeError verbatim; after it, the run completes.

Uses EMT, not MACE or MatterSim: the defect is in ase's optimizer API and
has nothing to do with which calculator supplies the forces, so the cheap
one that is always present is the honest choice.

Run directly, or through ctest as `go_relax_ase_api`.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

failures = 0

# The shim: make ase look like the ase the failure was reported on.
SITECUSTOMIZE = '''
import ase.optimize.optimize as _o


def converged(self, gradient):
    """`gradient` REQUIRED and positional -- the newer ase signature."""
    return bool(((gradient ** 2).sum(axis=1) ** 0.5).max() < self.fmax)


_o.Optimizer.converged = converged
'''

# EMT stands in for whatever ML potential the user ran.
EMT_BLOCK = '''def attach_calculator(system):
    atoms = system
    from ase.calculators.emt import EMT
    atoms.calc = EMT()
    return atoms


'''


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1
    return condition


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _find_script_test():
    for cand in sys.argv[1:]:
        if os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    cand = os.path.join(_repo_root(), "build", "calango_script_test")
    return cand if os.access(cand, os.X_OK) else None


def _swap_calculator(text):
    i = text.index("def attach_calculator(system):")
    j = text.index("attach_calculator(atoms)", i)
    return text[:i] + EMT_BLOCK + text[j:]


def _shrink(text):
    for pattern, replacement in (
            (r"^mc_cycles = .*$", "mc_cycles = 2"),
            (r"^optimizer_max_steps = .*$", "optimizer_max_steps = 3"),
            (r"^equilibration_steps = .*$", "equilibration_steps = 2")):
        text = re.sub(pattern, replacement, text, count=1, flags=re.M)
    return text


def main():
    try:
        import ase  # noqa: F401
    except Exception as exc:
        print(f"SKIP go_relax_ase_api: ase is not importable ({exc!r})")
        return 0
    binary = _find_script_test()
    if binary is None:
        print("SKIP go_relax_ase_api: calango_script_test is not built")
        return 0

    tmp = tempfile.mkdtemp(prefix="calango_ase_api_")
    scripts = os.path.join(tmp, "scripts")
    os.makedirs(scripts)
    subprocess.run([binary, "--dump", scripts], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    shim = os.path.join(tmp, "shim")
    os.makedirs(shim)
    with open(os.path.join(shim, "sitecustomize.py"), "w") as fh:
        fh.write(SITECUSTOMIZE)

    # The shim really does reproduce the reported signature.
    probe = subprocess.run(
        [sys.executable, "-c",
         "import sitecustomize, inspect\n"
         "from ase.optimize import BFGS\n"
         "print(inspect.signature(BFGS.converged))\n"
         "try:\n"
         "    BFGS.converged(type('X', (), {'fmax': 0.05})())\n"
         "except TypeError as e:\n"
         "    print('TYPEERROR', e)\n"],
        cwd=shim, env=dict(os.environ, PYTHONPATH=shim),
        capture_output=True, text=True)
    check("(self, gradient)" in probe.stdout,
          f"the shim imposes the reported signature "
          f"({probe.stdout.splitlines()[0] if probe.stdout else probe.stderr[-120:]})")
    check("missing 1 required positional argument: 'gradient'" in probe.stdout,
          "and a no-argument call raises the reported TypeError verbatim")

    print("\nThe generated GO/MC-Opt script under that ase:")
    run = os.path.join(tmp, "run")
    os.makedirs(run)
    from ase.build import graphene
    from ase.io import write
    sheet = graphene(formula="C2", a=2.46, size=(4, 4, 1), vacuum=7.0)
    write(os.path.join(run, "structure.extxyz"), sheet)

    with open(os.path.join(scripts, "graphene_oxide_gcmc.py")) as fh:
        text = _shrink(_swap_calculator(fh.read()))
    script = os.path.join(run, "run.py")
    with open(script, "w") as fh:
        fh.write(text)

    done = subprocess.run(
        [sys.executable, script], cwd=run,
        env=dict(os.environ, PYTHONPATH=shim), capture_output=True, text=True)
    combined = done.stdout + done.stderr
    check("Optimizer.converged() missing" not in combined
          and "converged() missing 1 required" not in combined,
          "does NOT raise the reported TypeError")
    check(done.returncode == 0,
          f"and completes (exit {done.returncode})"
          + ("" if done.returncode == 0 else f": {combined[-800:]}"))
    if done.returncode == 0:
        check("CALANGO_DONE" in combined, "reporting completion")
        log = os.path.join(run, "log.json")
        if os.path.isfile(log):
            entries = json.load(open(log))["log"]
            check(not any(e["level"] == "error" for e in entries),
                  "with no error logged")

    # And the same script on the UNPATCHED ase must behave identically --
    # the fix must not have traded one version dependence for another.
    print("\nThe same script on this machine's own ase:")
    plain = os.path.join(tmp, "plain")
    os.makedirs(plain)
    shutil.copy(os.path.join(run, "structure.extxyz"), plain)
    with open(os.path.join(plain, "run.py"), "w") as fh:
        fh.write(text)
    done2 = subprocess.run([sys.executable, os.path.join(plain, "run.py")],
                           cwd=plain, capture_output=True, text=True)
    check(done2.returncode == 0,
          f"completes there too (exit {done2.returncode})")

    shutil.rmtree(tmp, ignore_errors=True)
    print("\n" + (f"{failures} check(s) FAILED." if failures
                  else "All GO relaxation / ase-API checks passed."))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
