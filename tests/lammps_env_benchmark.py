#!/usr/bin/env python3
"""End-to-end validation of the LAMMPS calculator against a real LAMMPS.

tests/lammps_script_test.py checks that the generated scripts hand ASE the
right arguments, using stubs — it needs no LAMMPS and runs everywhere. This is
the other half: it drives an ACTUAL LAMMPS build through both of ASE's
interfaces and checks the physics that comes back.

The reference is a Lennard-Jones fcc crystal, whose cohesive energy is known in
closed form. For the 12-6 potential on a perfect fcc lattice the energy per
atom is

    E/N = 2 * eps * (A12 * (sigma/r)^12 - A6 * (sigma/r)^6)

with the fcc lattice sums A12 = 12.13188 and A6 = 14.45392 (Kittel), r the
nearest-neighbour distance. That is a number LAMMPS cannot accidentally agree
with: a wrong units style, a wrong type mapping or a mis-parsed pair_coeff all
move it.

What is exercised is the SHIPPED generated script. It is dumped from the C++
generator with `calango_script_test --dump` and then run, with only the pair
style/coefficients and the LAMMPS binary substituted — the same way
raman_ir_mgo_benchmark.py redirects the Raman/IR script's inputs.

Skips cleanly (exit 0) when no LAMMPS is importable.

Usage:  lammps_env_benchmark.py <calango_script_test binary>
"""
import json
import os
import re
import subprocess
import sys
import tempfile

# Argon, the canonical LJ test case.
EPSILON_EV = 0.0103
SIGMA_A = 3.4
# fcc lattice sums for the 12-6 potential (Kittel, Solid State Physics).
A12 = 12.13188
A6 = 14.45392
# Equilibrium nearest-neighbour distance of the LJ fcc crystal,
# r0 = sigma * (2 A12 / A6)^(1/6).
CUTOFF_A = 12.0
# The lattice sums are for an INFINITE lattice; LAMMPS truncates the potential
# at CUTOFF_A and applies no tail correction, which removes attractive energy
# and leaves the crystal under-bound by ~2.4 %. That is not an error to be
# tolerated away — it is a known, closed-form quantity, so it is added back
# analytically below and what remains is held to a tight bound. Measured
# residual after the correction: 0.016 %.
TOLERANCE = 0.005


def _bootstrap_lammps_env() -> None:
    """Re-exec under a Python that has LAMMPS.

    Mirrors the GPAW benchmarks' bootstrap: CTest runs these under the embedded
    interpreter, which deliberately holds only ASE. The LAMMPS engine's own
    environment is the one configured in Preferences → Python & Environments,
    which lands in ~/.calango/settings.json under the "LAMMPS" preset key.
    """
    try:
        import lammps  # noqa: F401
        return
    except Exception:
        pass
    if os.environ.get("_CALANGO_LAMMPS_REEXEC"):
        return
    candidates = []
    try:
        with open(os.path.expanduser("~/.calango/settings.json")) as fh:
            jobs = json.load(fh).get("jobs", {})
        presets = json.loads(jobs.get("environmentPresets", "{}") or "{}")
        if presets.get("LAMMPS"):
            candidates.append(presets["LAMMPS"])
    except Exception:
        pass
    # The conventional env name, as a fallback for a machine where the preset
    # has not been set in the GUI yet.
    candidates.append(os.path.expanduser("~/miniconda3/envs/lammps_env"))
    for env in candidates:
        for python in (os.path.join(env, "bin", "python3"),
                       os.path.join(env, "bin", "python")):
            if os.path.isfile(python) and \
                    os.path.realpath(python) != os.path.realpath(sys.executable):
                os.environ["_CALANGO_LAMMPS_REEXEC"] = "1"
                os.execv(python, [python, os.path.abspath(__file__)] + sys.argv[1:])


def configure(path, pair_style, pair_coeff, command=None):
    """Point a generated script at a pair style that needs no potential file.

    Only the literal argument lines the generator emits are rewritten; every
    other line — the species-order derivation, the calculator construction, the
    task section — is the shipped code.
    """
    source = open(path).read()
    source, n = re.subn(r'"pair_style [^"]*"', f'"pair_style {pair_style}"',
                        source, count=1)
    if n != 1:
        source, n = re.subn(r'"pair_style": "[^"]*"',
                            f'"pair_style": "{pair_style}"', source, count=1)
    assert n == 1, "no pair_style line found in the generated script"

    # Replace the single pair_coeff entry, in whichever of the two shapes this
    # interface uses (a LAMMPS command, or a parameter-dict list entry).
    source, n = re.subn(r'"pair_coeff [^"]*"', f'"pair_coeff {pair_coeff}"',
                        source, count=1)
    if n != 1:
        source, n = re.subn(r'(?m)^        "\* \* [^"]*",$',
                            f'        "{pair_coeff}",', source, count=1)
    assert n == 1, "no pair_coeff line found in the generated script"

    # Drop the potential-file staging: these pair styles read no file.
    source = re.sub(r'(?m)^for _potential in \[.*?\n(?:.*?\n)*?'
                    r'.*?less specific message\.\'\)\n', "", source)
    source = re.sub(r'(?m)^\s*"files": \[\n(?:.*\n)*?\s*\],\n', "", source)
    if command:
        source = re.sub(r'os\.environ\["ASE_LAMMPSRUN_COMMAND"\] = r".*"',
                        f'os.environ["ASE_LAMMPSRUN_COMMAND"] = r"{command}"',
                        source)
    open(path, "w").write(source)


def analytic_energy_per_atom(nearest_neighbour):
    """Cohesive energy per atom of the infinite LJ fcc crystal."""
    ratio = SIGMA_A / nearest_neighbour
    return 2.0 * EPSILON_EV * (A12 * ratio ** 12 - A6 * ratio ** 6)


def tail_correction_per_atom(number_density):
    """Energy the r > rc tail contributes, which a truncated run omits.

    The standard uniform-density (g(r) = 1 beyond rc) LJ correction:

        E_tail/N = (8/3) pi rho eps sigma^3 [ (1/3)(sigma/rc)^9 - (sigma/rc)^3 ]

    Negative, because the missing part of the tail is attractive.
    """
    import numpy as np
    s3 = (SIGMA_A / CUTOFF_A) ** 3
    s9 = (SIGMA_A / CUTOFF_A) ** 9
    return ((8.0 / 3.0) * np.pi * number_density * EPSILON_EV * SIGMA_A ** 3
            * (s9 / 3.0 - s3))


def main():
    if len(sys.argv) < 2:
        print("SKIP: no calango_script_test binary given")
        return 0
    binary = os.path.abspath(sys.argv[1])
    if not os.path.isfile(binary):
        print(f"SKIP: {binary} does not exist")
        return 0
    try:
        import numpy as np
        import lammps  # noqa: F401
        from ase.build import bulk
        from ase.io import read, write
    except Exception as exc:
        print(f"SKIP: no LAMMPS-capable Python ({exc})")
        return 0

    import shutil
    # Look beside the interpreter first: the bootstrap re-execs into the LAMMPS
    # conda env, whose bin/ holds `lmp` but is not on PATH for the outer shell.
    env_bin = os.path.dirname(os.path.realpath(sys.executable))
    lmp_binary = next(
        (os.path.join(env_bin, name) for name in ("lmp", "lmp_serial")
         if os.path.isfile(os.path.join(env_bin, name))), None)
    lmp_binary = lmp_binary or shutil.which("lmp") or shutil.which("lmp_serial")
    workdir = tempfile.mkdtemp(prefix="calango_lammps_env_")
    subprocess.run([binary, "--dump", workdir], check=True,
                   stdout=subprocess.DEVNULL)
    sys.path.insert(0, workdir)

    # Equilibrium fcc argon. `bulk` takes the CUBIC lattice constant, and the
    # nearest-neighbour distance in fcc is a/sqrt(2).
    r0 = SIGMA_A * (2.0 * A12 / A6) ** (1.0 / 6.0)
    lattice_a = r0 * np.sqrt(2.0)
    atoms = bulk("Ar", "fcc", a=lattice_a, cubic=True)
    write(os.path.join(workdir, "structure.extxyz"), atoms)
    infinite = analytic_energy_per_atom(r0)
    tail = tail_correction_per_atom(len(atoms) / atoms.get_volume())
    # What a run truncated at CUTOFF_A should report: the infinite-lattice
    # value MINUS the tail it does not include.
    expected = infinite - tail
    print(f"fcc Ar: a = {lattice_a:.4f} A, nearest neighbour {r0:.4f} A")
    print(f"analytic LJ cohesive energy = {infinite:.6f} eV/atom "
          f"(infinite lattice)")
    print(f"omitted r > {CUTOFF_A} A tail      = {tail:.6f} eV/atom")
    print(f"expected from a truncated run = {expected:.6f} eV/atom")

    failures = 0

    def check(condition, what):
        nonlocal failures
        print(f"  {'ok  ' if condition else 'FAIL'} {what}")
        if not condition:
            failures += 1

    pair_style = f"lj/cut {CUTOFF_A}"
    pair_coeff = f"* * {EPSILON_EV} {SIGMA_A}"

    print("Library interface (LAMMPSlib), single point:")
    script = os.path.join(workdir, "lammps_lib.py")
    configure(script, pair_style, pair_coeff)
    result = subprocess.run([sys.executable, script], cwd=workdir,
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-2000:])
        print(result.stderr[-2000:])
    check(result.returncode == 0, "the generated script runs to completion")
    # The marker the single-point script emits (AseScriptGenerator).
    energies = re.findall(r"CALANGO_RESULT energy_eV=([-0-9.eE+]+)",
                          result.stdout)
    check(bool(energies), "and reports an energy")
    if energies:
        per_atom = float(energies[-1]) / len(atoms)
        error = abs(per_atom - expected) / abs(expected)
        print(f"       E/N = {per_atom:.6f} eV/atom   "
              f"(expected {expected:.6f}, {error:.3%} off)")
        # The decisive check: LAMMPS is running the potential Calango asked
        # for, in the units Calango assumed, with the type mapping Calango
        # derived. Any of those wrong and this number is not close.
        check(error < TOLERANCE,
              f"matches the closed-form fcc LJ cohesive energy to "
              f"{TOLERANCE:.1%} once the truncated tail is accounted for")

    if lmp_binary:
        print(f"Executable interface (lammpsrun via {lmp_binary}):")
        script = os.path.join(workdir, "lammps_run.py")
        configure(script, pair_style, pair_coeff, command=lmp_binary)
        result = subprocess.run([sys.executable, script], cwd=workdir,
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stdout[-1500:])
            print(result.stderr[-1500:])
        check(result.returncode == 0, "the MD script runs to completion")
        # lammps_run.py is dumped as a molecular-dynamics job: reaching the end
        # means LAMMPS supplied forces for every step, which a single-point
        # energy alone would not prove.
        traj = os.path.join(workdir, "md.traj")
        if os.path.exists(traj):
            frames = read(traj, index=":")
            check(len(frames) > 1,
                  f"and produced a trajectory ({len(frames)} frames)")
            moved = np.abs(frames[-1].get_positions()
                           - frames[0].get_positions()).max()
            check(moved > 1e-6,
                  f"whose atoms actually moved (max {moved:.4f} A)")
    else:
        print("Executable interface: SKIP (no lmp binary on PATH)")

    print("\nAll LAMMPS environment checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    _bootstrap_lammps_env()
    sys.exit(main())
