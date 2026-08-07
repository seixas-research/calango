#!/usr/bin/env python3
"""xTB calculator integration: run the GENERATED script for real.

Compiling the script only proves it parses. This runs it — xTB is fast enough
that a water molecule finishes in milliseconds — and checks that an energy and
a force array come back through the whole path the application uses: the C++
generator writes run.py, ASE reads structure.extxyz, xtb-python evaluates it,
and the results land in single_point.json and on the CALANGO_RESULT lines the
job console parses.

Three things are pinned, each of which was found broken:

  * The METHOD reaches the calculator. GFN2, GFN1 and GFN-FF are three
    different parameterizations and must give three different energies; a
    generator that dropped `method=` would silently run GFN2 for all of them
    and nothing downstream would notice.

  * A PERIODIC structure is refused, cleanly. xtb-python evaluates isolated
    systems only: GFN1/GFN2 raise CalculationFailed on a cell, and GFN-FF
    SEGFAULTS on a 2D one — verified against xtb-python 22.1, where periodic
    graphene killed the interpreter with SIGSEGV/SIGABRT and no traceback.
    From the job console that is indistinguishable from a machine fault, so
    the script has to refuse before calling the library.

  * Forces are real. A calculator returning zeros for every atom would pass
    any "did it run" check and be useless.

Self-skips when no interpreter with xtb is available. Note that this is the
NORMAL case for the project's own venv: xtb-python is conda-forge only (there
is no PyPI wheel — `pip install xtb` builds from source and needs MKL) and its
newest build is py313, so a py314 embedded interpreter cannot host it. Set
CALANGO_XTB_PYTHON to an interpreter that has it, or the test looks through
the Conda environments the same way the application does.
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

METHODS = {
    "GFN2-xTB": "xtb_gfn2_xtb.py",
    "GFN1-xTB": "xtb_gfn1_xtb.py",
    "GFN-FF": "xtb_gfn_ff.py",
}

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def candidate_interpreters():
    """Interpreters that might have xtb, in the order the app would try."""
    explicit = os.environ.get("CALANGO_XTB_PYTHON")
    if explicit:
        yield explicit
    yield sys.executable
    # The Conda environments CondaEnvs::discover() would list.
    home = pathlib.Path.home()
    roots = [home / "miniconda3" / "envs", home / "anaconda3" / "envs",
             home / "miniforge3" / "envs", home / "mambaforge" / "envs",
             pathlib.Path("/opt/miniconda3/envs"),
             pathlib.Path("/opt/anaconda3/envs")]
    prefix = os.environ.get("CONDA_PREFIX")
    if prefix:
        roots.insert(0, pathlib.Path(prefix).parent)
    for root in roots:
        if not root.is_dir():
            continue
        for env in sorted(root.iterdir()):
            exe = env / "bin" / "python"
            if exe.is_file():
                yield str(exe)


def find_xtb_python():
    for exe in candidate_interpreters():
        try:
            done = subprocess.run(
                [exe, "-c", "import xtb.ase.calculator, ase; print('yes')"],
                capture_output=True, timeout=120)
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0 and b"yes" in done.stdout:
            return exe
    return None


def write_structure(python, path, kind):
    """H2O, a finite benzene-like carbon flake, or 2D-periodic graphene."""
    builders = {
        "h2o": "a = molecule('H2O'); a.center(vacuum=5.0); a.pbc = False",
        "flake": "a = molecule('C6H6'); a.center(vacuum=6.0); a.pbc = False",
        "graphene": "from ase.build import graphene; a = graphene(vacuum=6.0)",
    }
    subprocess.run(
        [python, "-c",
         "from ase.build import molecule\nfrom ase.io import write\n"
         f"{builders[kind]}\nwrite(r'{path}', a)"],
        check=True, capture_output=True, timeout=300)


def run_script(python, script, job):
    shutil.copy(script, job / "run.py")
    return subprocess.run([python, "run.py"], cwd=job, capture_output=True,
                          text=True, timeout=900)


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: xtb_integration_test.py <calango_script_test>")
    binary = sys.argv[1]

    python = find_xtb_python()
    if python is None:
        print("no interpreter with xtb-python found - skipping xTB integration "
              "test\n(conda install -c conda-forge xtb-python, or set "
              "CALANGO_XTB_PYTHON)")
        return 0
    print(f"xtb interpreter: {python}\n")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        scripts = tmp / "scripts"
        scripts.mkdir()
        subprocess.run([binary, "--dump", str(scripts)], check=True,
                       stdout=subprocess.DEVNULL, timeout=300)

        print("Single point on H2O:")
        energies = {}
        for method, name in METHODS.items():
            job = tmp / f"job_{name}"
            job.mkdir()
            write_structure(python, job / "structure.extxyz", "h2o")
            done = run_script(python, scripts / name, job)
            if done.returncode != 0:
                check(False, f"{method} ran (exit {done.returncode}): "
                             f"{done.stderr.strip()[-200:]}")
                continue

            result = job / "single_point.json"
            if not result.is_file():
                check(False, f"{method} wrote single_point.json")
                continue
            data = json.loads(result.read_text())
            energy = data.get("energy_eV")
            forces = data.get("forces_eV_per_A") or []
            fmax = data.get("fmax_eV_per_A")

            ok = (isinstance(energy, (int, float)) and energy < 0.0
                  and len(forces) == 3 and all(len(f) == 3 for f in forces)
                  and isinstance(fmax, (int, float)) and fmax > 0.0)
            check(ok, f"{method}: E = {energy!r} eV, {len(forces)} force "
                      f"vectors, fmax = {fmax!r} eV/A")
            # A calculator wired up but not evaluating would hand back zeros
            # for every component, which passes every structural check above.
            check(any(abs(c) > 1e-6 for f in forces for c in f),
                  f"{method}: the forces are not identically zero")
            check("CALANGO_DONE" in done.stdout,
                  f"{method}: the run reported completion to the console")
            if isinstance(energy, (int, float)):
                energies[method] = energy

        # Three parameterizations, three different answers. Equal energies mean
        # `method=` never reached the calculator.
        print("\nThe selected method actually reaches the calculator:")
        distinct = len({round(e, 6) for e in energies.values()})
        check(distinct == len(energies) and len(energies) >= 2,
              f"GFN2 / GFN1 / GFN-FF give {distinct} distinct energies "
              f"({', '.join(f'{m}={e:.3f}' for m, e in energies.items())})")

        print("\nA carbon flake (the sp2 case, 12 atoms):")
        job = tmp / "job_flake"
        job.mkdir()
        write_structure(python, job / "structure.extxyz", "flake")
        done = run_script(python, scripts / METHODS["GFN2-xTB"], job)
        ok = done.returncode == 0 and (job / "single_point.json").is_file()
        if ok:
            data = json.loads((job / "single_point.json").read_text())
            check(data.get("natoms") == 12 and data.get("energy_eV", 0) < 0.0,
                  f"GFN2 on a 12-atom flake: E = "
                  f"{data.get('energy_eV'):.3f} eV")
        else:
            check(False, f"GFN2 on a flake ran (exit {done.returncode})")

        print("\nA periodic cell is refused rather than crashing:")
        for method, name in METHODS.items():
            job = tmp / f"pbc_{name}"
            job.mkdir()
            write_structure(python, job / "structure.extxyz", "graphene")
            done = run_script(python, scripts / name, job)
            # The distinction that matters: a REFUSAL (non-zero exit with the
            # explanation on stderr) rather than a signal. Python reports a
            # fatal signal as a negative return code.
            refused = (done.returncode > 0
                       and "periodic" in (done.stdout + done.stderr).lower())
            check(refused,
                  f"{method}: refused with a reason, exit {done.returncode}")
            check(done.returncode >= 0,
                  f"{method}: did not die from a signal "
                  f"({'signal ' + str(-done.returncode) if done.returncode < 0 else 'clean'})")

    print("\n" + ("All xTB integration checks passed." if failures == 0
                  else f"{failures} check(s) FAILED."))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
