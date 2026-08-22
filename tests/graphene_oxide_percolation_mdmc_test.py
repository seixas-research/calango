#!/usr/bin/env python3
"""Ring/percolation analysis against a REAL MDMC trajectory, end to end.

tests/GrapheneOxidePercolationTest.cpp validates the ring-finding and
percolation ALGORITHM against closed forms (Euler's formula on a torus, the
nanoflake ring-count formula, a hand-edited one-direction stripe) — none of
it against a trajectory an actual simulation produced. This test closes that
gap the way graphene_oxide_mdmc_antiposition_test.py closes the analogous
one for hydroxyl antiposition: run the GENERATED MDMC script for real (plain
Lennard-Jones, no MACE/GPAW/xTB needed — ships with ASE itself), then feed
the resulting trajectory to the native calango-ring-percolation-analyze CLI
(core::analyzeRingPercolationTrajectory(), the same function the Analysis
dialog's trajectory scope calls) and check two CLOSED-FORM invariants that
hold regardless of which specific moves the sampler happened to accept:

  * **ring count is exactly N/2 on every frame** (Euler's formula on a
    torus, N = 200 framework carbons here) — MDMC relocates existing
    functional groups, it never adds or removes a carbon or a C-C bond, so
    the carbon lattice's own topology cannot change frame to frame even as
    which rings are "intact" does.
  * **sp2 carbon fraction is exactly constant on every frame** — the total
    functionalized-carbon count is conserved for the same reason (12
    epoxide groups relocate; the count of 12 never changes).

Both would break under a topology bug MUCH more specific fixtures (a single
static structure, or a hand-built one-off) could miss — this is a moving
target the analysis has to get right on every single frame of a real run.

Self-skips (exit 0) when no interpreter has `ase` — set CALANGO_ASE_PYTHON,
or the test looks through the Conda environments the same way the
application does.

Usage:  graphene_oxide_percolation_mdmc_test.py <calango_script_test> \\
            <calango-ring-percolation-analyze>
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

N_EPOXIDES = 12

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def candidate_interpreters():
    """Interpreters that might have ase, in the order the app would try."""
    explicit = os.environ.get("CALANGO_ASE_PYTHON")
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


def find_ase_python():
    for exe in candidate_interpreters():
        try:
            done = subprocess.run(
                [exe, "-c",
                 "import ase.io, ase.build, ase.neighborlist, "
                 "ase.calculators.lj; print('yes')"],
                capture_output=True, timeout=120)
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0 and b"yes" in done.stdout:
            return exe
    return None


# Builds a 10x10 periodic graphene supercell (200 framework carbons, same
# size graphene_oxide_mdmc_antiposition_test.py uses) and hand-places
# N_EPOXIDES bridging oxygens at well-separated bonds — same geometric
# convention (1.44 A projected C-O reach) as that fixture, minus the
# antiposition-pair bookkeeping this test has no use for.
BUILD_STRUCTURE = f"""
import numpy as np
from ase import Atoms
from ase.build import graphene
from ase.io import write
from ase.neighborlist import NeighborList, natural_cutoffs

atoms = graphene(vacuum=15.0) * (10, 10, 1)
positions = atoms.get_positions()
n = len(atoms)

cutoffs = [1.2 * r for r in natural_cutoffs(atoms)]
nl = NeighborList(cutoffs, skin=0.0, self_interaction=False, bothways=True)
nl.update(atoms)
bonds = sorted({{tuple(sorted((i, int(j))))
               for i in range(n) for j in nl.get_neighbors(i)[0]}})

rng = np.random.default_rng(1)
chosen = []
used = set()
for i, j in bonds:
    neigh_i = set(int(x) for x in nl.get_neighbors(i)[0])
    neigh_j = set(int(x) for x in nl.get_neighbors(j)[0])
    footprint = neigh_i | neigh_j | {{i, j}}
    if footprint & used:
        continue
    chosen.append((i, j))
    used |= footprint
    if len(chosen) == {N_EPOXIDES}:
        break
assert len(chosen) == {N_EPOXIDES}, (
    f"fixture too small: only found {{len(chosen)}} well-separated bonds")

normal = np.array([0.0, 0.0, 1.0])
extra_symbols = []
extra_positions = []
for i, j in chosen:
    mid = 0.5 * (positions[i] + positions[j])
    half = 0.5 * np.linalg.norm(positions[i] - positions[j])
    height = max(1.44 ** 2 - half ** 2, 0.25) ** 0.5
    extra_symbols.append("O")
    extra_positions.append(mid + height * normal)

combined = atoms + Atoms(extra_symbols, positions=extra_positions)
write(r"__OUTPUT__", combined)
print(f"CARBONS={{n}} EPOXIDES={{len(chosen)}} ATOMS={{len(combined)}}")
"""


def run_ase(python, source, *args, timeout=300):
    return subprocess.run([python, "-c", source, *args],
                          capture_output=True, text=True, timeout=timeout)


def run_script(python, script, job):
    shutil.copy(script, job / "run.py")
    return subprocess.run([python, "run.py"], cwd=job, capture_output=True,
                          text=True, timeout=600)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(
            "usage: graphene_oxide_percolation_mdmc_test.py "
            "<calango_script_test> <calango-ring-percolation-analyze>")
    scriptTestBinary = sys.argv[1]
    analyzeBinary = sys.argv[2]

    python = find_ase_python()
    if python is None:
        print("no interpreter with ase found - skipping the ring/percolation "
              "MDMC trajectory test\n(set CALANGO_ASE_PYTHON, or install ase "
              "in a Conda environment this machine can find)")
        return 0
    print(f"ase interpreter: {python}\n")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        scripts = tmp / "scripts"
        scripts.mkdir()
        subprocess.run([scriptTestBinary, "--dump", str(scripts)], check=True,
                       stdout=subprocess.DEVNULL, timeout=300)

        structure = tmp / "structure.extxyz"
        build = run_ase(python,
                        BUILD_STRUCTURE.replace("__OUTPUT__", str(structure)))
        check(build.returncode == 0 and structure.is_file()
                  and "EPOXIDES=" in build.stdout,
              f"fixture: a 10x10 graphene sheet with {N_EPOXIDES} scattered "
              f"epoxides built "
              f"({build.stdout.strip().splitlines()[0] if build.stdout else build.stderr.strip()[-200:]})")
        if build.returncode != 0 or "EPOXIDES=" not in build.stdout:
            print(f"\n{failures} check(s) FAILED.")
            return 1
        expected_rings = int(build.stdout.split("CARBONS=")[1].split()[0]) // 2
        expected_sp2_fraction = (200 - 2 * N_EPOXIDES) / 200

        print("Running the generated MDMC script (plain Lennard-Jones, "
              "no MD burst) for real:")
        job = tmp / "job"
        job.mkdir()
        shutil.copy(structure, job / "structure.extxyz")
        done = run_script(python, scripts / "graphene_oxide_mdmc_off.py", job)
        check(done.returncode == 0,
              f"the MDMC run completed (exit {done.returncode}): "
              f"{done.stderr.strip()[-300:] if done.returncode else 'ok'}")
        check("CALANGO_DONE" in done.stdout, "and reported completion")

        summary_path = job / "mdmc_summary.json"
        trajectory_path = job / "accepted_structures.extxyz"
        if done.returncode == 0 and summary_path.is_file():
            summary = json.loads(summary_path.read_text())
            check(summary.get("accepted", 0) > 0,
                  f"at least one move was actually accepted "
                  f"({summary.get('accepted')} of {summary.get('cycles')} "
                  f"cycles) — otherwise the trajectory would just be the "
                  f"starting frame repeated")

        if not (done.returncode == 0 and trajectory_path.is_file()):
            check(False, "a trajectory was written to analyze")
            print(f"\n{failures} check(s) FAILED.")
            return 1

        print("\nAnalyzing every frame with calango-ring-percolation-analyze:")
        analyzed = subprocess.run([analyzeBinary, str(trajectory_path)],
                                  capture_output=True, text=True, timeout=120)
        check(analyzed.returncode == 0,
              f"the CLI ran successfully: {analyzed.stderr.strip()[-300:]}")
        if analyzed.returncode != 0:
            print(f"\n{failures} check(s) FAILED.")
            return 1
        frames = json.loads(analyzed.stdout)
        check(len(frames) > 1,
              f"the trajectory has more than the starting frame "
              f"({len(frames)} frames)")

        check(all(f["rings"] == expected_rings for f in frames),
              f"ring count is exactly N/2 = {expected_rings} on EVERY frame "
              f"(Euler's formula on a torus) — MDMC relocates functional "
              f"groups, it never touches the carbon lattice itself "
              f"({sorted(set(f['rings'] for f in frames))} count(s) seen)")
        check(all(abs(f["sp2_fraction"] - expected_sp2_fraction) < 1e-9
                 for f in frames),
              f"sp2 carbon fraction is exactly {expected_sp2_fraction:.4f} "
              f"on EVERY frame — the functionalized-carbon COUNT is "
              f"conserved by relocation, only which carbons are affected "
              f"changes "
              f"({sorted(set(round(f['sp2_fraction'], 6) for f in frames))} "
              f"value(s) seen)")
        check(all(f["intact_rings"] <= f["rings"] for f in frames),
              "intact-ring count never exceeds the total ring count, on "
              "any frame")
        check(all(0.0 <= f["intact_fraction"] <= 1.0 for f in frames),
              "intact-ring fraction stays in [0, 1] on every frame")

        distinct_intact = sorted(set(f["intact_rings"] for f in frames))
        print(f"  info intact-ring counts seen across the trajectory: "
              f"{distinct_intact}")
        percolating_frames = sum(
            1 for f in frames
            if f["percolates_a"] or f["percolates_b"] or f["percolates_c"])
        print(f"  info {percolating_frames}/{len(frames)} frames percolate "
              f"at least one axis")

    print(failures == 0 and "\nAll ring-percolation MDMC checks passed."
          or f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
