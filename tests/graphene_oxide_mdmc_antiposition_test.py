#!/usr/bin/env python3
"""Hydroxyls antiposition survives MDMC: run the GENERATED script for real.

Before this test existed, GrapheneOxideMdmcScriptGenerator moved every
hydroxyl as an independent single carbon, even when the input structure was
built with GrapheneOxideBuilder::Config::hydroxylAntiposition — a bonded,
opposite-face PAIR of hydroxyls (a trans-diol). The first accepted move on
either half of a pair split it, and nothing ever re-formed it: the sampler
silently drifted away from the material it was asked to refine.

The fix tracks each pair as one compound "hydroxyl_pair" group, recovered
from the STARTING geometry exactly once and carried through the run from
then on (see the generator's own docstring). This test does not trust that
bookkeeping — it independently re-derives, from raw geometry, on EVERY frame
of a REAL trajectory:

  * each of the KNOWN pairs this test itself built is still intact: its two
    hosts are still bonded to each other, and its two hydroxyls are still on
    OPPOSITE faces;
  * no hydroxyl atom is silently gained, lost, or reclassified;
  * no other functional group (an epoxide, here) ever lands on a carbon a
    pair has claimed.

"KNOWN" is deliberate: this test tracks each pair by the fixed OXYGEN ATOM
INDICES it assigned when it built the fixture (atom indices never change
during a run — only positions do — so this is exactly as valid an identity
anchor here as it is inside the generator's own `groups` list), rather than
re-discovering "which hydroxyl belongs to which pair" from local adjacency
each frame. An early draft tried the latter and was WRONG on a real
trajectory in two different ways — first over-counting pairs when one host
was the opposite-face neighbour of two unrelated hosts, then, once that was
fixed by requiring a mutual/unique match, under-counting whenever an
unrelated group merely landed next to a real pair (which nothing prevents,
since only the antiposition pair's OWN two carbons are protected from being
someone else's site — see the module's other invariant below). Anchoring to
known atom indices sidesteps the ambiguity entirely: it never has to guess
whose neighbour is whose partner.

Runs on plain ASE Lennard-Jones — no MACE/GPAW/xTB needed — with the MD
burst turned off (mdStepsPerCycle=0), which generic LJ needs: it has no
notion of a covalent bond, and a burst of dynamics on the real 0.98 A O-H
length explodes within a handful of steps at any usable temperature.

A contrasting run with the option OFF, on the SAME initial structure,
demonstrates the protection is not vacuous: without it, a pair the test
built IS eventually split by an ordinary independent hydroxyl move — the
exact failure this test exists to catch, and confirmation this behaviour
(moving each hydroxyl independently) is completely unchanged by the fix.

Self-skips (exit 0) when no interpreter has `ase` — set CALANGO_ASE_PYTHON
to one, or the test looks through the Conda environments the same way the
application does (see tests/xtb_integration_test.py, which this mirrors).

Usage:  graphene_oxide_mdmc_antiposition_test.py <calango_script_test>
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

N_PAIRS = 5
N_EPOXIDES = 1

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


# Builds a 10x10 periodic graphene supercell (200 framework carbons) and
# hand-places N_PAIRS antiposition hydroxyl pairs plus N_EPOXIDES epoxides
# at bonded sites chosen far enough apart that none of their own hand-placed
# atoms can clash with a neighbour's. Bond lengths (1.48 A C-O for both the
# hydroxyl and the epoxide bridge's projected reach, 0.98 A O-H) match
# GrapheneOxideMdmcScriptGenerator's own build_group() constants exactly, so
# the generated script's bond-perception (natural_cutoffs * 1.2) reads this
# structure exactly the way it would read one the real C++ builder made.
#
# Prints the PAIRS/EPOXIDES/ATOMS summary, then a line starting
# "FIXTURE_INDEX " with a JSON object naming exactly which oxygen atom
# indices belong to each pair and each epoxide — the identity anchor the
# trajectory verification below tracks (see the module docstring for why).
BUILD_STRUCTURE = f"""
import json as _json
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

rng = np.random.default_rng(0)
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
    if len(chosen) == {N_PAIRS} + {N_EPOXIDES}:
        break
assert len(chosen) == {N_PAIRS} + {N_EPOXIDES}, (
    f"fixture too small: only found {{len(chosen)}} well-separated bonds")

pair_bonds = chosen[:{N_PAIRS}]
epoxide_bonds = chosen[{N_PAIRS}:]

normal = np.array([0.0, 0.0, 1.0])
extra_symbols = []
extra_positions = []
# Each pair recorded as [O_a, H_a, O_b, H_b] -- both the oxygen AND its OWN
# hydrogen's index, not just the oxygen. The verification below scopes its
# "does this oxygen still have its hydrogen" check to this specific H, the
# same way the generator's own topology_intact() scopes its search to a
# group's known members -- an unrelated group's hydrogen landing nearby
# (which nothing prevents) must not read as broken chemistry.
pair_atoms = []
for i, j in pair_bonds:
    sign = 1.0 if rng.random() < 0.5 else -1.0
    indices = []
    for host, s in ((i, sign), (j, -sign)):
        base = positions[host]
        o = base + s * 1.48 * normal
        h = o + np.array([0.94 * 0.98, 0.0, s * 0.33 * 0.98])
        indices += [n + len(extra_symbols), n + len(extra_symbols) + 1]
        extra_symbols += ["O", "H"]
        extra_positions += [o, h]
    pair_atoms.append(indices)

epoxide_oxygens = []
for i, j in epoxide_bonds:
    mid = 0.5 * (positions[i] + positions[j])
    half = 0.5 * np.linalg.norm(positions[i] - positions[j])
    height = max(1.44 ** 2 - half ** 2, 0.25) ** 0.5
    epoxide_oxygens.append(n + len(extra_symbols))
    extra_symbols.append("O")
    extra_positions.append(mid + height * normal)

combined = atoms + Atoms(extra_symbols, positions=extra_positions)
write(r"__OUTPUT__", combined)
print(f"PAIRS={{len(pair_bonds)}} EPOXIDES={{len(epoxide_bonds)}} "
      f"ATOMS={{len(combined)}}")
print("FIXTURE_INDEX " + _json.dumps(
    {{"pairs": pair_atoms, "epoxide_oxygens": epoxide_oxygens}}))
"""

# Runs entirely inside the ase-python subprocess: reads a trajectory and,
# for EVERY frame, checks each KNOWN pair (by its fixed oxygen atom indices,
# passed in as sys.argv[2] — see the module docstring for why identity is
# anchored this way rather than re-derived from local adjacency) is still a
# bonded, opposite-face pair — completely independent of the generator's own
# SiteGraph/collect_groups code, so this is a check against the generator,
# not an echo of it. Emits one JSON record per frame on stdout.
VERIFY_TRAJECTORY = r"""
import json
import sys
import numpy as np
from ase.io import read
from ase.neighborlist import NeighborList, natural_cutoffs

def bond_graph(atoms):
    cutoffs = [1.2 * r for r in natural_cutoffs(atoms)]
    nl = NeighborList(cutoffs, skin=0.0, self_interaction=False, bothways=True)
    nl.update(atoms)
    return [set(int(j) for j in nl.get_neighbors(i)[0])
            for i in range(len(atoms))]

def hydroxyl_host(o, h, symbols, graph, framework_set):
    # This oxygen's host framework carbon, iff it is presently a
    # well-formed hydroxyl: exactly one framework host, AND its OWN known
    # hydrogen `h` (not just "some hydrogen or other") still bonded to it --
    # else None. Scoped to `h` deliberately: an unrelated group's hydrogen
    # landing within bonding distance (which nothing prevents -- see the
    # module docstring) must not read as broken chemistry, exactly the
    # reasoning behind the generator's own topology_intact() scoping its
    # search to a group's known members.
    hosts = [j for j in graph[o] if j in framework_set]
    return hosts[0] if len(hosts) == 1 and h in graph[o] else None

def analyze(atoms, fixture):
    symbols = atoms.get_chemical_symbols()
    graph = bond_graph(atoms)
    framework = [i for i, s in enumerate(symbols) if s == "C"
                and len([j for j in graph[i] if symbols[j] == "C"]) >= 2]
    framework_set = set(framework)
    positions = atoms.get_positions()
    points = positions[framework]
    centered = points - points.mean(axis=0)
    _, _, vh = np.linalg.svd(centered)
    normal = vh[2] / np.linalg.norm(vh[2])

    n_pairs_intact = 0
    n_halves_valid = 0
    pair_hosts = []
    for o_a, h_a, o_b, h_b in fixture["pairs"]:
        host_a = hydroxyl_host(o_a, h_a, symbols, graph, framework_set)
        host_b = hydroxyl_host(o_b, h_b, symbols, graph, framework_set)
        n_halves_valid += (host_a is not None) + (host_b is not None)
        if host_a is None or host_b is None or host_b not in graph[host_a]:
            continue
        side_a = float(np.dot(positions[o_a] - positions[host_a], normal))
        side_b = float(np.dot(positions[o_b] - positions[host_b], normal))
        if side_a * side_b < 0.0:
            n_pairs_intact += 1
            pair_hosts += [host_a, host_b]

    epoxide_hosts = []
    for o in fixture["epoxide_oxygens"]:
        hosts = [j for j in graph[o] if j in framework_set]
        if len(hosts) == 2:
            epoxide_hosts += hosts

    return {
        "n_pairs_intact": n_pairs_intact,
        # Each KNOWN half's OWN chemistry (host + its own hydrogen), tallied
        # independently of pairing -- distinguishes "the pair split because
        # a half's own chemistry broke" (this would drop) from "the pair
        # split only because the two halves are no longer on opposite
        # faces / no longer bonded to each other" (this stays at the max).
        "n_halves_valid": n_halves_valid,
        "host_overlap": len(set(pair_hosts) & set(epoxide_hosts)),
    }

fixture = json.loads(sys.argv[2])
frames = read(sys.argv[1], index=":")
print(json.dumps([analyze(f, fixture) for f in frames]))
"""


def run_ase(python, source, *args, timeout=300):
    return subprocess.run([python, "-c", source, *args],
                          capture_output=True, text=True, timeout=timeout)


def run_script(python, script, job):
    shutil.copy(script, job / "run.py")
    return subprocess.run([python, "run.py"], cwd=job, capture_output=True,
                          text=True, timeout=600)


def verify(python, trajectory, fixture):
    result = run_ase(python, VERIFY_TRAJECTORY, str(trajectory),
                     json.dumps(fixture))
    check(result.returncode == 0,
          f"the trajectory could be re-analyzed independently: "
          f"{result.stderr.strip()[-300:]}")
    return json.loads(result.stdout) if result.returncode == 0 else None


def main():
    if len(sys.argv) < 2:
        raise SystemExit(
            "usage: graphene_oxide_mdmc_antiposition_test.py "
            "<calango_script_test>")
    binary = sys.argv[1]

    python = find_ase_python()
    if python is None:
        print("no interpreter with ase found - skipping the MDMC "
              "antiposition test\n(set CALANGO_ASE_PYTHON, or install ase "
              "in a Conda environment this machine can find)")
        return 0
    print(f"ase interpreter: {python}\n")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        scripts = tmp / "scripts"
        scripts.mkdir()
        subprocess.run([binary, "--dump", str(scripts)], check=True,
                       stdout=subprocess.DEVNULL, timeout=300)

        structure = tmp / "structure.extxyz"
        build = run_ase(python,
                        BUILD_STRUCTURE.replace("__OUTPUT__", str(structure)))
        check(build.returncode == 0 and structure.is_file()
                  and "FIXTURE_INDEX " in build.stdout,
              f"fixture: an antiposition-decorated graphene sheet built "
              f"({build.stdout.strip().splitlines()[0] if build.stdout else build.stderr.strip()[-200:]})")
        if build.returncode != 0 or "FIXTURE_INDEX " not in build.stdout:
            print(f"\n{failures} check(s) FAILED.")
            return 1
        fixture = json.loads(
            next(line for line in build.stdout.splitlines()
                if line.startswith("FIXTURE_INDEX "))[len("FIXTURE_INDEX "):])

        print("Hydroxyls antiposition ON — every frame of a real trajectory:")
        job = tmp / "job_on"
        job.mkdir()
        shutil.copy(structure, job / "structure.extxyz")
        done = run_script(python, scripts / "graphene_oxide_mdmc_antiposition.py",
                          job)
        check(done.returncode == 0,
              f"the MDMC run completed (exit {done.returncode}): "
              f"{done.stderr.strip()[-300:] if done.returncode else 'ok'}")
        check("CALANGO_DONE" in done.stdout, "and reported completion")

        summary_path = job / "mdmc_summary.json"
        trajectory_path = job / "mdmc_trajectory.extxyz"
        if done.returncode == 0 and summary_path.is_file():
            summary = json.loads(summary_path.read_text())
            check(summary.get("accepted", 0) > 0,
                  f"at least one move was actually accepted "
                  f"({summary.get('accepted')} of {summary.get('cycles')} "
                  f"cycles) — otherwise nothing below would be exercised")

        if done.returncode == 0 and trajectory_path.is_file():
            frames = verify(python, trajectory_path, fixture)
            if frames is not None:
                check(len(frames) > 1,
                      f"the trajectory has more than the starting frame "
                      f"({len(frames)} frames)")
                check(all(f["n_pairs_intact"] == N_PAIRS for f in frames),
                      f"every one of the {N_PAIRS} KNOWN antiposition pairs "
                      f"is still a bonded, opposite-face pair on EVERY "
                      f"frame — none was ever split by a swap "
                      f"({sorted(set(f['n_pairs_intact'] for f in frames))} "
                      f"intact-count(s) seen)")
                check(all(f["n_halves_valid"] == 2 * N_PAIRS for f in frames),
                      "and each known half's OWN chemistry (its host, its "
                      "own hydrogen) is intact on every frame too — a split "
                      "pair, if one ever showed up, would be because the "
                      "two halves parted ways, never because a half's own "
                      "bond broke")
                check(all(f["host_overlap"] == 0 for f in frames),
                      "no other group (the epoxide) ever lands on a carbon "
                      "a pair has claimed")
        else:
            check(False, "a trajectory was written to analyze")

        print("\nHydroxyls antiposition OFF, same starting structure — "
              "the behaviour this option has always had is unchanged, and "
              "is not what ON does:")
        offJob = tmp / "job_off"
        offJob.mkdir()
        shutil.copy(structure, offJob / "structure.extxyz")
        # graphene_oxide_mdmc_off.py is the antiposition dump's OFF twin —
        # identical Lennard-Jones / no-MD-burst / cycle-budget settings,
        # differing only in hydroxyl_antiposition (see ScriptGenerationTest
        # .cpp's --dump block), so this is an apples-to-apples contrast.
        offDone = run_script(python, scripts / "graphene_oxide_mdmc_off.py",
                             offJob)
        check(offDone.returncode == 0,
              f"the OFF run completed (exit {offDone.returncode})")
        offTrajectory = offJob / "mdmc_trajectory.extxyz"
        if offDone.returncode == 0 and offTrajectory.is_file():
            offFrames = verify(python, offTrajectory, fixture)
            if offFrames is not None:
                check(any(f["n_pairs_intact"] < N_PAIRS for f in offFrames),
                      "without the option, an ordinary independent-hydroxyl "
                      "move DOES eventually split one of the pairs the "
                      "fixture built — confirming the ON path's protection "
                      "is not vacuous and OFF is genuinely the old, "
                      "unguarded behaviour")

    print("\n" + ("All hydroxyl-antiposition MDMC checks passed."
                  if failures == 0 else f"{failures} check(s) FAILED."))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
