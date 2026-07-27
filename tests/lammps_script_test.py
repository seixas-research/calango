#!/usr/bin/env python3
"""LAMMPS calculator: run the generated scripts against a recording stub.

Grepping the generated text proves the right words were written; it does not
prove ASE would accept them. A misspelled keyword (`atom_type` for
`atom_types`, `lmpcmd` for `lmpcmds`) reads perfectly well and fails only when
LAMMPS is actually driven — which needs a LAMMPS build, so it is exactly the
kind of defect that reaches a user untested.

So this EXECUTES each generated script with ASE's two LAMMPS calculator
classes replaced by stubs that record their keyword arguments and return canned
energies and forces. Everything else runs for real: the structure is read, the
species order is derived from it, the task section drives the calculator. What
comes back is the precise argument set the shipped script would have handed
LAMMPS.

The stub deliberately does NOT accept arbitrary keywords — it mirrors ASE's own
signature — so a keyword ASE would reject is rejected here too.

Skips cleanly (exit 0) without ASE.

Usage:  lammps_script_test.py <calango_script_test binary>
"""
import os
import runpy
import subprocess
import sys
import tempfile

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


class Recorder:
    """Captures the kwargs the generated script constructs the calculator with."""

    def __init__(self):
        self.calls = []

    def make(self, accepted):
        recorder = self

        class Stub:
            # ASE reads `name` off the calculator when a trajectory frame is
            # written, so an MD script exercises it even though no force call
            # does.
            name = "lammps"

            # ASE calculators are duck-typed through these three; nothing in
            # the generated task section needs more than energy and forces.
            def __init__(self, **kwargs):
                unexpected = set(kwargs) - accepted
                if unexpected:
                    raise TypeError(
                        f"{sorted(unexpected)} is not accepted by this ASE "
                        f"calculator (accepted: {sorted(accepted)})")
                recorder.calls.append(kwargs)
                self.kwargs = kwargs
                self.results = {}

            def get_potential_energy(self, atoms=None, **_):
                return -1.234

            def get_forces(self, atoms=None, **_):
                import numpy as np
                return np.zeros((len(atoms), 3)) if atoms is not None else None

            def get_stress(self, atoms=None, **_):
                import numpy as np
                return np.zeros(6)

            def get_property(self, name, atoms=None, allow_calculation=True):
                if name == "energy":
                    return self.get_potential_energy(atoms)
                if name == "forces":
                    return self.get_forces(atoms)
                if name == "stress":
                    return self.get_stress(atoms)
                return None

            def calculate(self, *args, **kwargs):
                return None

            def reset(self):
                return None

            def check_state(self, atoms, tol=1e-15):
                return []

            def todict(self):
                return dict(self.kwargs)

        return Stub


def run_script(path, workdir):
    """Execute one generated script with both LAMMPS classes stubbed out."""
    import ase.calculators.lammpslib as lammpslib
    import ase.calculators.lammpsrun as lammpsrun

    recorder = Recorder()
    # The accepted sets mirror ASE's own: LAMMPSlib takes lmpcmds/atom_types/…
    # and lammpsrun takes its parameter names plus `specorder`. Keeping them
    # explicit is what makes a typo fail here.
    lib_accepted = set(lammpslib.LAMMPSlib.default_parameters) | {
        "lmpcmds", "log_file", "keep_alive", "atom_types"}
    run_accepted = set(lammpsrun.LAMMPS.default_parameters) | {
        "label", "specorder", "keep_tmp_files", "files", "parameters"}

    original = (lammpslib.LAMMPSlib, lammpsrun.LAMMPS)
    lammpslib.LAMMPSlib = recorder.make(lib_accepted)
    lammpsrun.LAMMPS = recorder.make(run_accepted)
    cwd = os.getcwd()
    os.chdir(workdir)
    try:
        runpy.run_path(path, run_name="__main__")
    finally:
        os.chdir(cwd)
        lammpslib.LAMMPSlib, lammpsrun.LAMMPS = original
    return recorder.calls


def main():
    if len(sys.argv) < 2:
        print("SKIP: no calango_script_test binary given")
        return 0
    binary = os.path.abspath(sys.argv[1])
    if not os.path.isfile(binary):
        print(f"SKIP: {binary} does not exist")
        return 0
    try:
        from ase.build import bulk
        from ase.io import write
        import ase.calculators.lammpslib  # noqa: F401
        import ase.calculators.lammpsrun  # noqa: F401
    except Exception as exc:
        print(f"SKIP: ASE LAMMPS calculators not importable ({exc})")
        return 0

    workdir = tempfile.mkdtemp(prefix="calango_lammps_")
    subprocess.run([binary, "--dump", workdir], check=True,
                   stdout=subprocess.DEVNULL)
    # A two-element cell, so the derived species order is a real mapping rather
    # than the trivial one-element case that would hide an ordering bug.
    atoms = bulk("Cu", "fcc", a=3.6, cubic=True)
    atoms.symbols[0] = "Al"
    write(os.path.join(workdir, "structure.extxyz"), atoms)
    # The fixture names a potential inside the dump directory; create it so the
    # generated guard takes its found branch here, and delete it later to
    # exercise the missing branch.
    potential = os.path.join(workdir, "Cu_u3.eam.alloy")
    open(potential, "w").write("# stub EAM table\n")
    # The generated scripts import the staged logger module from the cwd.
    sys.path.insert(0, workdir)

    print("Library interface (LAMMPSlib):")
    calls = run_script(os.path.join(workdir, "lammps_lib.py"), workdir)
    check(len(calls) == 1, "constructs exactly one calculator")
    if calls:
        kwargs = calls[0]
        check("lmpcmds" in kwargs, "passes lmpcmds")
        commands = kwargs.get("lmpcmds", [])
        check("pair_style eam/alloy" in commands, "the pair style is a command")
        check("pair_coeff * * Cu_u3.eam.alloy Cu" in commands,
              "the pair coefficients are a command")
        check("neighbor 2.0 bin" in commands, "extra commands are appended")
        # Species order is derived from the structure; Al sorts before Cu, so
        # this is the mapping LAMMPS would actually be given.
        check(kwargs.get("atom_types") == {"Al": 1, "Cu": 2},
              f"atom_types maps the sorted species to 1-based LAMMPS types "
              f"(got {kwargs.get('atom_types')})")
        check(kwargs.get("log_file") == "lammps.log", "the log is kept")

    print("Executable interface (lammpsrun):")
    calls = run_script(os.path.join(workdir, "lammps_run.py"), workdir)
    check(len(calls) == 1, "constructs exactly one calculator")
    if calls:
        kwargs = calls[0]
        check(kwargs.get("units") == "metal", "units are metal")
        check(kwargs.get("specorder") == ["Al", "Cu"],
              f"specorder is the derived species order "
              f"(got {kwargs.get('specorder')})")
        check(kwargs.get("pair_style") == "eam/alloy", "pair style is passed")
        check(kwargs.get("pair_coeff") == ["* * Cu_u3.eam.alloy Cu"],
              "pair coefficients are passed as a list")
        check(kwargs.get("files") == [potential], "potential files are staged")
        check(os.environ.get("ASE_LAMMPSRUN_COMMAND") == "/usr/bin/lmp_serial",
              "the configured binary is exported to the environment")

    print("Several pair_coeff lines:")
    calls = run_script(os.path.join(workdir, "lammps_multi.py"), workdir)
    if calls:
        commands = calls[0].get("lmpcmds", [])
        pair_coeffs = [c for c in commands if c.startswith("pair_coeff")]
        check(len(pair_coeffs) == 3,
              f"all three pair_coeff lines survive (got {len(pair_coeffs)})")
        check(pair_coeffs[0] == "pair_coeff 1 1 0.0103 3.4",
              "and keep their order, which LAMMPS is sensitive to")

    print("A missing potential file is refused:")
    os.remove(potential)
    try:
        run_script(os.path.join(workdir, "lammps_lib.py"), workdir)
        check(False, "a pair_coeff naming an absent potential must abort")
    except RuntimeError as exc:
        # Without this guard the run reaches LAMMPS and dies there with a much
        # less specific message, after the job has already been queued.
        check("not found" in str(exc), "the refusal names the missing file")

    print("Non-metal units are refused:")
    try:
        run_script(os.path.join(workdir, "lammps_bad_units.py"), workdir)
        check(False, "a 'real' units style must abort the run")
    except RuntimeError as exc:
        check("metal" in str(exc),
              "the refusal names the units style that is required")

    print("\nAll LAMMPS script checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
