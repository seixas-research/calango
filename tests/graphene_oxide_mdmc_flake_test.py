#!/usr/bin/env python3
"""Edge-group moves on a nanoflake conserve the rim: run the GENERATED
MDMC script for real on a coronene flake.

An edge carbon of a flake carries either a functional group (carbonyl,
carboxyl) or a terminating hydrogen — never both, never neither. Before
this test existed, GrapheneOxideMdmcScriptGenerator's edge move rebuilt
the group at its new host ON TOP of that host's hydrogen (an oxygen 0.14 Å
from an H) and left the vacated carbon with a dangling bond: every edge
move was a fused atom pair and a radical, whatever the energy said. The
move now swaps the terminating hydrogen to the vacated carbon, so the
rim's hydrogen inventory is conserved exactly like the groups are.

Independently re-derived from raw geometry on EVERY accepted frame:

  * every edge carbon carries exactly one substituent (an H or a group
    atom) — no dangling carbon, no doubly substituted one;
  * the number of rim hydrogens never changes;
  * no two atoms anywhere are closer than the O-H bond (0.98 Å) — the
    direct signature of a group placed on top of a hydrogen.

Runs on plain ASE Lennard-Jones with the dynamics off (the antiposition
dump: mdStepsPerCycle = 0, no equilibration), like
graphene_oxide_mdmc_antiposition_test.py, and self-skips the same way.

Usage:  graphene_oxide_mdmc_flake_test.py <calango_script_test>
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

N_EDGE = 12      # coronene C24H12: 12 rim carbons
N_RIM_H = 9      # 12 minus two carbonyls and one carboxyl

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def candidate_interpreters():
    explicit = os.environ.get("CALANGO_ASE_PYTHON")
    if explicit:
        yield explicit
    yield sys.executable
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
                 "import ase.io, ase.neighborlist, ase.calculators.lj; "
                 "print('yes')"],
                capture_output=True, timeout=120)
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0 and b"yes" in done.stdout:
            return exe
    return None


# Coronene (the 24 honeycomb sites nearest a hexagon centre: 12 inner
# three-coordinated carbons, 12 rim two-coordinated ones), terminated the
# way GrapheneOxideBuilder terminates a flake: each rim carbon gets an
# in-plane H at 1.09 Å along the outward direction, except two that get a
# carbonyl O (1.23 Å) and one that gets a -COOH (the builder's own
# geometry). One antiposition hydroxyl pair on two bonded inner carbons
# gives the sampler a basal move kind as well.
BUILD_STRUCTURE = """
import numpy as np
from ase import Atoms
from ase.io import write
from ase.neighborlist import NeighborList, natural_cutoffs

a1 = np.array([2.46, 0.0, 0.0])
a2 = np.array([1.23, 2.1304, 0.0])
basis = [np.array([0.0, 0.0, 0.0]), (a1 + a2) / 3.0]
centre = 2.0 * (a1 + a2) / 3.0
pts = np.array([i * a1 + j * a2 + b
                for i in range(-4, 5) for j in range(-4, 5) for b in basis])
order = np.argsort(np.linalg.norm(pts - centre, axis=1))
carbons = Atoms("C" * 24, positions=pts[order[:24]])
carbons.center(vacuum=10.0)
nl = NeighborList([1.2 * r for r in natural_cutoffs(carbons)], skin=0.0,
                  self_interaction=False, bothways=True)
nl.update(carbons)
pos = carbons.get_positions()
coord = [len(nl.get_neighbors(i)[0]) for i in range(24)]
rim = [i for i in range(24) if coord[i] == 2]
assert len(rim) == 12 and min(coord) == 2, coord

def outward(i):
    v = pos[i] - pos[nl.get_neighbors(i)[0]].mean(axis=0)
    v[2] = 0.0
    return v / np.linalg.norm(v)

def rotate(v, angle):
    c, s = np.cos(angle), np.sin(angle)
    return np.array([c * v[0] - s * v[1], s * v[0] + c * v[1], 0.0])

symbols = list(carbons.get_chemical_symbols())
positions = list(pos)
for k, i in enumerate(rim):
    u = outward(i)
    if k in (0, 4):
        symbols.append("O")
        positions.append(pos[i] + 1.23 * u)
    elif k == 8:
        cc = pos[i] + 1.48 * u
        single = cc + 1.34 * rotate(u, np.pi / 3.0)
        symbols += ["C", "O", "O", "H"]
        positions += [cc, cc + 1.21 * rotate(u, -np.pi / 3.0), single,
                      single + 0.98 * rotate(u, -14.0 * np.pi / 180.0)]
    else:
        symbols.append("H")
        positions.append(pos[i] + 1.09 * u)
inner = [i for i in range(24) if coord[i] == 3]
host_a = inner[0]
host_b = next(j for j in nl.get_neighbors(host_a)[0] if coord[j] == 3)
for host, sign in ((host_a, 1.0), (host_b, -1.0)):
    o = pos[host] + sign * 1.48 * np.array([0.0, 0.0, 1.0])
    symbols += ["O", "H"]
    positions += [o, o + np.array([0.92, 0.0, sign * 0.32])]
flake = Atoms(symbols, positions=positions, cell=carbons.cell, pbc=False)
write(r"__OUTPUT__", flake)
print(f"ATOMS={len(flake)}")
"""

VERIFY_TRAJECTORY = r"""
import json
import sys
import numpy as np
from ase.io import read
from ase.neighborlist import NeighborList, natural_cutoffs

def analyze(atoms):
    nl = NeighborList([1.2 * r for r in natural_cutoffs(atoms)], skin=0.0,
                      self_interaction=False, bothways=True)
    nl.update(atoms)
    symbols = atoms.get_chemical_symbols()
    graph = [set(int(j) for j in nl.get_neighbors(i)[0])
             for i in range(len(atoms))]
    framework = {i for i in range(len(atoms)) if symbols[i] == "C"
                 and len([j for j in graph[i] if symbols[j] == "C"]) >= 2}
    rim = [i for i in framework
           if len([j for j in graph[i] if j in framework]) < 3]
    substituents = [len([j for j in graph[i] if j not in framework])
                    for i in rim]
    distances = atoms.get_all_distances()
    np.fill_diagonal(distances, 9.0)
    return {
        "n_rim": len(rim),
        "rim_all_single": all(n == 1 for n in substituents),
        "rim_h": sum(1 for i in rim for j in graph[i] if symbols[j] == "H"),
        "min_distance": float(distances.min()),
    }

frames = read(sys.argv[1], index=":")
print(json.dumps([analyze(f) for f in frames]))
"""


def run_py(python, source, *args, timeout=300):
    return subprocess.run([python, "-c", source, *args],
                          capture_output=True, text=True, timeout=timeout)


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: graphene_oxide_mdmc_flake_test.py "
                         "<calango_script_test>")
    binary = sys.argv[1]
    python = find_ase_python()
    if python is None:
        print("no interpreter with ase found - skipping the MDMC flake test")
        return 0
    print(f"ase interpreter: {python}\n")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        scripts = tmp / "scripts"
        scripts.mkdir()
        subprocess.run([binary, "--dump", str(scripts)], check=True,
                       stdout=subprocess.DEVNULL, timeout=300)
        job = tmp / "job"
        job.mkdir()
        build = run_py(python, BUILD_STRUCTURE.replace(
            "__OUTPUT__", str(job / "structure.extxyz")))
        check(build.returncode == 0 and "ATOMS=" in build.stdout,
              f"fixture: a coronene flake with two carbonyls, a carboxyl, "
              f"nine rim hydrogens and one hydroxyl pair "
              f"({build.stdout.strip() if build.returncode == 0 else build.stderr.strip()[-300:]})")
        if build.returncode != 0:
            return 1
        shutil.copy(scripts / "graphene_oxide_mdmc_antiposition.py",
                    job / "run.py")
        done = subprocess.run([python, "run.py"], cwd=job,
                              capture_output=True, text=True, timeout=600)
        check(done.returncode == 0,
              f"the MDMC run completed (exit {done.returncode})"
              + (f": {done.stderr.strip()[-300:]}" if done.returncode else ""))
        summary_path = job / "mdmc_summary.json"
        trajectory = job / "accepted_structures.extxyz"
        if done.returncode != 0 or not summary_path.is_file():
            return 1
        summary = json.loads(summary_path.read_text())
        by_kind = summary["acceptance_by_kind"]
        check(by_kind["carbonyl"]["accepted"] > 0
              and by_kind["carboxyl"]["accepted"] > 0,
              f"edge moves were actually accepted "
              f"({by_kind['carbonyl']['accepted']} carbonyl, "
              f"{by_kind['carboxyl']['accepted']} carboxyl) - otherwise "
              f"nothing below is exercised")
        check(summary["rejected_topology"] == 0,
              "and none was rejected for broken chemistry (the dynamics is "
              "off, so only the placement itself could break it)")
        result = run_py(python, VERIFY_TRAJECTORY, str(trajectory))
        check(result.returncode == 0,
              "the trajectory could be re-analyzed independently"
              + (f": {result.stderr.strip()[-300:]}"
                 if result.returncode else ""))
        if result.returncode == 0:
            frames = json.loads(result.stdout)
            check(len(frames) > 1, f"{len(frames)} frames")
            check(all(f["n_rim"] == N_EDGE and f["rim_all_single"]
                      for f in frames),
                  f"every one of the {N_EDGE} rim carbons carries exactly "
                  f"one substituent on every frame - no dangling carbon, "
                  f"none doubly substituted")
            check(all(f["rim_h"] == N_RIM_H for f in frames),
                  f"the rim hydrogen count is {N_RIM_H} on every frame "
                  f"({sorted(set(f['rim_h'] for f in frames))} seen)")
            closest = min(f["min_distance"] for f in frames)
            check(closest > 0.95,
                  f"nothing anywhere is closer than the O-H bond "
                  f"(closest approach {closest:.2f} A) - no group was ever "
                  f"rebuilt on top of a hydrogen")

    print("\n" + ("All GO-MDMC flake checks passed."
                  if failures == 0 else f"{failures} check(s) FAILED."))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
