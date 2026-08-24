#!/usr/bin/env python3
"""GO/MCMD on a REAL potential: the generated script, end to end, under
MACE-MP-0.

The two Lennard-Jones harnesses (graphene_oxide_mcmd_antiposition_test.py,
graphene_oxide_percolation_mcmd_test.py) run the generated script with the
molecular dynamics turned OFF (mdStepsPerCycle = 0, no equilibration) —
generic Lennard-Jones has no covalent bond to keep intact through a burst.
That leaves the dynamics, which is the only relaxation mechanism the whole
protocol has, completely untested. It is exactly where the first real run of
this module died: the builder places every group on a FLAT sheet, so the
as-built structure carries ~10 eV/Å on its host carbons and tens of eV of
strain, and releasing that in a single 20-step burst was a thermal shock
that opened whichever epoxide had been placed closest to a neighbour —
after which the script exited before its first Monte Carlo cycle.

This test builds a sheet the way the builder used to, INCLUDING one of the
contacts that killed that run — an epoxide with a same-face hydroxyl on an
adjacent carbon, 1.89 Å oxygen to oxygen, which no MACE-MP-0 size accepts
as a minimum — and runs the real protocol: initial equilibration (with its
checkpointed relocation of a group that comes apart), the per-cycle bursts,
the clearance filter, NPT with the in-plane-only barostat. It then
independently re-derives, from raw geometry, on EVERY frame the run wrote:

  * the inventory is conserved — exactly the epoxides and hydroxyls that
    were built, all of them chemically intact (judged, as the script judges
    a thermal snapshot, at 1.3 × the covalent radii), one connected
    molecule;
  * the known antiposition pair is still a bonded, opposite-face pair;
  * the vacuum axis of the cell was NEVER scaled (NPT on a sheet must move
    the in-plane vectors only) and no C-C bond was stretched across the
    periodic boundary (the cell is part of the reverted state).

Self-skips (exit 0) when no interpreter has `mace` — set CALANGO_MACE_PYTHON
to one, or the test looks through the Conda environments the same way the
application does — or when the MACE-MP-0 "small" checkpoint is not in the
local cache (it is downloaded on first use of mace_mp(model="small"); this
test does not go to the network itself).

Usage:  graphene_oxide_mcmd_mace_test.py <calango_script_test>
"""
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

# Must match the graphene_oxide_mcmd_mace.py dump in ScriptGenerationTest.cpp.
N_CYCLES = 12
N_EPOXIDES = 3   # two clean + the planted clash
N_HYDROXYLS = 3  # one antiposition pair + the planted single
CACHED_SMALL = (pathlib.Path.home() / ".cache" / "mace"
                / "20231210mace128L0_energy_epoch249model")

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def candidate_interpreters():
    explicit = os.environ.get("CALANGO_MACE_PYTHON")
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


def find_mace_python():
    for exe in candidate_interpreters():
        try:
            done = subprocess.run(
                [exe, "-c",
                 "import ase.io, ase.build, ase.md.nptberendsen, "
                 "mace.calculators; print('yes')"],
                capture_output=True, timeout=300)
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0 and b"yes" in done.stdout:
            return exe
    return None


# An 8x5 periodic sheet (80 framework carbons, 10 A of vacuum each side)
# decorated by hand the way GrapheneOxideBuilder decorates — the same
# analytic geometry (flat hosts, 1.44 A epoxide C-O, 1.48 A hydroxyl C-O,
# 0.98 A O-H) — with two well-separated epoxides, one antiposition hydroxyl
# pair, and the PLANTED CLASH: a hydroxyl on a carbon adjacent to the first
# epoxide's host, on the same face, so its oxygen sits 1.89 A from the
# bridging oxygen. That contact is exactly what the builder's old 1.55 A
# clearance admitted and what the real run died on.
BUILD_STRUCTURE = """
import json as _json
import numpy as np
from ase import Atoms
from ase.build import graphene
from ase.io import write
from ase.neighborlist import NeighborList, natural_cutoffs

atoms = graphene(vacuum=10.0) * (8, 5, 1)
positions = atoms.get_positions()
n = len(atoms)
cutoffs = [1.2 * r for r in natural_cutoffs(atoms)]
nl = NeighborList(cutoffs, skin=0.0, self_interaction=False, bothways=True)
nl.update(atoms)
neigh = [set(int(j) for j in nl.get_neighbors(i)[0]) for i in range(n)]
bonds = sorted({tuple(sorted((i, j))) for i in range(n) for j in neigh[i]})

def footprint(carbons, radius=2):
    front, seen = set(carbons), set(carbons)
    for _ in range(radius):
        front = {j for i in front for j in neigh[i]} - seen
        seen |= front
    return seen

used = set()
chosen = []
for i, j in bonds:
    if footprint((i, j)) & used:
        continue
    chosen.append((i, j))
    used |= footprint((i, j))
    if len(chosen) == 4:
        break
assert len(chosen) == 4, "fixture too small"
(clash_i, clash_j), pair_bond, epo_b, spare = chosen
z = np.array([0.0, 0.0, 1.0])
extra_symbols, extra_positions = [], []

def add(sym, pos):
    extra_symbols.append(sym)
    extra_positions.append(pos)
    return n + len(extra_symbols) - 1

def epoxide(i, j, face):
    d = atoms.get_distance(i, j, mic=True, vector=True)
    mid = positions[i] + 0.5 * d
    height = max(1.44 ** 2 - (0.5 * np.linalg.norm(d)) ** 2, 0.25) ** 0.5
    return add("O", mid + face * height * z)

def hydroxyl(c, face, azimuth_dir):
    o = positions[c] + face * 1.48 * z
    h = o + 0.94 * 0.98 * azimuth_dir + face * 0.33 * 0.98 * z
    return add("O", o), add("H", h)

o_clash_epoxide = epoxide(clash_i, clash_j, +1.0)
# The planted clash: a same-face hydroxyl on a neighbour of clash_i that is
# not clash_j, its O-H pointing AWAY from the epoxide (the O...O contact
# alone is the point).
c = next(k for k in sorted(neigh[clash_i]) if k != clash_j)
# Minimum image: clash_i sits at the cell corner, so its neighbour may be a
# whole cell away in raw coordinates and a plain difference points the O-H
# straight back onto the epoxide oxygen.
away = atoms.get_distance(clash_i, c, mic=True, vector=True)
away[2] = 0.0
away /= np.linalg.norm(away)
o_clash_hydroxyl, h_clash_hydroxyl = hydroxyl(c, +1.0, away)
o_epoxide_b = epoxide(*epo_b, -1.0)
o_epoxide_c = epoxide(*spare, +1.0)
a, b = pair_bond
o_a, h_a = hydroxyl(a, +1.0, np.array([1.0, 0.0, 0.0]))
o_b, h_b = hydroxyl(b, -1.0, np.array([-1.0, 0.0, 0.0]))

combined = atoms + Atoms(extra_symbols, positions=extra_positions)
write(r"__OUTPUT__", combined)
oo = combined.get_distance(o_clash_epoxide, o_clash_hydroxyl, mic=True)
print(f"ATOMS={len(combined)} CLASH_OO={oo:.3f}")
print("FIXTURE_INDEX " + _json.dumps({
    "pair": [o_a, h_a, o_b, h_b],
    "epoxide_oxygens": [o_clash_epoxide, o_epoxide_b, o_epoxide_c],
    "hydroxyl_oxygens": [o_clash_hydroxyl, o_a, o_b],
    "clash": [o_clash_epoxide, o_clash_hydroxyl],
}))
"""

# Independent of everything the generator does: bonds from distances, groups
# from bonds, one JSON record per frame.
VERIFY_TRAJECTORY = r"""
import json
import sys
import numpy as np
from ase.io import read
from ase.neighborlist import NeighborList, natural_cutoffs

# Thermal snapshots are judged at 1.3x the covalent radii, the same
# tolerance the generated script's own survival check uses (and for the
# same measured reason: an intact O-H on this sheet sits within 0.5 % of
# the 1.2x cutoff at 300 K, and a hydroxyl hydrogen-bonded to an adjacent
# epoxide oxygen keeps its proton near 1.2 A permanently). Independent in
# code, not in physics: a bond that has really gone is at 2 A or more.
THERMAL_SCALE = 1.3

def bond_graph(atoms):
    cutoffs = [THERMAL_SCALE * r for r in natural_cutoffs(atoms)]
    nl = NeighborList(cutoffs, skin=0.0, self_interaction=False, bothways=True)
    nl.update(atoms)
    return [set(int(j) for j in nl.get_neighbors(i)[0])
            for i in range(len(atoms))]

def components(graph):
    seen, count = set(), 0
    for start in range(len(graph)):
        if start in seen:
            continue
        count += 1
        stack = [start]
        seen.add(start)
        while stack:
            node = stack.pop()
            for k in graph[node]:
                if k not in seen:
                    seen.add(k)
                    stack.append(k)
    return count

def analyze(atoms, fixture):
    symbols = atoms.get_chemical_symbols()
    graph = bond_graph(atoms)
    framework = {i for i, s in enumerate(symbols) if s == "C"
                 and len([j for j in graph[i] if symbols[j] == "C"]) >= 2}
    positions = atoms.get_positions()
    epoxides = [o for o in range(len(atoms)) if symbols[o] == "O"
                and len([j for j in graph[o] if j in framework]) == 2
                and not any(symbols[j] == "H" for j in graph[o])]
    hydroxyls = [o for o in range(len(atoms)) if symbols[o] == "O"
                 and len([j for j in graph[o] if j in framework]) == 1
                 and len([j for j in graph[o] if symbols[j] == "H"]) == 1]
    o_a, h_a, o_b, h_b = fixture["pair"]
    def host(o, h):
        hosts = [j for j in graph[o] if j in framework]
        return hosts[0] if len(hosts) == 1 and h in graph[o] else None
    host_a, host_b = host(o_a, h_a), host(o_b, h_b)
    pair_ok = (host_a is not None and host_b is not None
               and host_b in graph[host_a]
               and (positions[o_a][2] - positions[host_a][2])
                   * (positions[o_b][2] - positions[host_b][2]) < 0.0)
    cc = [atoms.get_distance(i, j, mic=True) for i in framework
          for j in graph[i] if j in framework and j > i]
    return {
        "n_epoxides": len(epoxides),
        "n_hydroxyls": len(hydroxyls),
        "components": components(graph),
        "pair_ok": bool(pair_ok),
        "cell_z": float(atoms.cell[2][2]),
        "cell_xy": [float(atoms.cell[0][0]), float(atoms.cell[1][1])],
        "max_cc": float(max(cc)),
        "clash_oo": float(atoms.get_distance(*fixture["clash"], mic=True)),
    }

fixture = json.loads(sys.argv[2])
frames = read(sys.argv[1], index=":")
print(json.dumps([analyze(f, fixture) for f in frames]))
"""


def run_py(python, source, *args, timeout=300):
    return subprocess.run([python, "-c", source, *args],
                          capture_output=True, text=True, timeout=timeout)


# ---------------------------------------------------------------------------
# Reading extended XYZ without ase
# ---------------------------------------------------------------------------
# Deliberately by hand, in THIS interpreter, rather than through ase in the
# MACE environment: these three checks are about how many frames are in a file
# and whether consecutive ones are the same, which is a property of the FILE.
# Parsing it with the same library that wrote it would let a writer bug hide
# behind a reader that compensates for it, and the format needed here is three
# lines of code - a count, a comment, and `count` atom lines.


def _iter_extxyz(path):
    """Yield (comment, [atom lines]) per frame."""
    with path.open() as handle:
        while True:
            header = handle.readline()
            if not header:
                return
            header = header.strip()
            if not header:
                continue
            count = int(header)
            comment = handle.readline()
            yield comment, [handle.readline() for _ in range(count)]


def count_extxyz_frames(path):
    return sum(1 for _ in _iter_extxyz(path))


def _coordinates(atom_lines):
    """The (species, x, y, z) of every atom, as text - exact, not rounded."""
    return tuple(tuple(line.split()[0:4]) for line in atom_lines)


def consecutive_duplicate_frames(path):
    """How many frames repeat the coordinates of the frame before them."""
    duplicates = 0
    previous = None
    for _, atom_lines in _iter_extxyz(path):
        current = _coordinates(atom_lines)
        if previous is not None and current == previous:
            duplicates += 1
        previous = current
    return duplicates


def _decoration(comment, atom_lines):
    """The functional-group CONFIGURATION of a frame: which atom carries
    which group kind and which group instance. Thermal distortion moves the
    coordinates and leaves this untouched, which is exactly what makes it the
    right key for "is this the same arrangement as before".

    Read from the go_group_type / go_group_id columns the run writes; returns
    None when a frame does not carry them, so the caller can tell "no repeats"
    from "nothing to compare".
    """
    properties = re.search(r'Properties=(\S+)', comment)
    if not properties:
        return None
    fields = properties.group(1).split(":")
    names = fields[0::3]
    widths = [int(w) for w in fields[2::3]]
    if "go_group_type" not in names or "go_group_id" not in names:
        return None
    start = {}
    column = 0
    for name, width in zip(names, widths):
        start[name] = column
        column += width
    a = start["go_group_type"]
    b = start["go_group_id"]
    return tuple((line.split()[a], line.split()[b]) for line in atom_lines)


def repeated_decorations(path):
    """How many frames share a functional-group configuration with an EARLIER
    frame in the same file. Zero is the only acceptable answer for a file of
    accepted structures: two identical arrangements are one arrangement, and
    the second one was a move that did not move anything."""
    seen = set()
    repeats = 0
    for comment, atom_lines in _iter_extxyz(path):
        key = _decoration(comment, atom_lines)
        if key is None:
            continue
        if key in seen:
            repeats += 1
        seen.add(key)
    return repeats


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: graphene_oxide_mcmd_mace_test.py "
                         "<calango_script_test>")
    binary = sys.argv[1]

    python = find_mace_python()
    if python is None:
        print("no interpreter with mace found - skipping the GO/MCMD MACE "
              "test\n(set CALANGO_MACE_PYTHON, or install mace-torch in a "
              "Conda environment this machine can find)")
        return 0
    if not CACHED_SMALL.is_file():
        print(f"MACE-MP-0 small is not cached at {CACHED_SMALL} - skipping "
              "(run any MACE-MP small calculation once with network access "
              "to cache it)")
        return 0
    print(f"mace interpreter: {python}\n")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        scripts = tmp / "scripts"
        scripts.mkdir()
        subprocess.run([binary, "--dump", str(scripts)], check=True,
                       stdout=subprocess.DEVNULL, timeout=300)

        structure = tmp / "structure.extxyz"
        build = run_py(python,
                       BUILD_STRUCTURE.replace("__OUTPUT__", str(structure)))
        first = build.stdout.strip().splitlines()[0] if build.stdout else \
            build.stderr.strip()[-300:]
        check(build.returncode == 0 and "FIXTURE_INDEX " in build.stdout,
              f"fixture: a decorated sheet with one planted 1.89 A O...O "
              f"clash ({first})")
        if build.returncode != 0 or "FIXTURE_INDEX " not in build.stdout:
            return 1
        fixture = json.loads(next(
            line for line in build.stdout.splitlines()
            if line.startswith("FIXTURE_INDEX "))[len("FIXTURE_INDEX "):])

        job = tmp / "job"
        job.mkdir()
        shutil.copy(structure, job / "structure.extxyz")
        script = (scripts / "graphene_oxide_mcmd_mace.py").read_text()
        check('mace_mp(model="small"' in script,
              "the dump asks for MACE-MP-0 small")
        # Pin the cached checkpoint so the run never touches the network.
        script = script.replace('mace_mp(model="small"',
                                f'mace_mp(model=r"{CACHED_SMALL}"')
        (job / "run.py").write_text(script)

        print("The real protocol, MACE-MP-0 small, NPT, 300 K:")
        done = subprocess.run([python, "run.py"], cwd=job,
                              capture_output=True, text=True, timeout=1500)
        check(done.returncode == 0,
              f"the run completed (exit {done.returncode})"
              + (f": {done.stderr.strip()[-400:]}" if done.returncode else ""))
        check("CALANGO_DONE" in done.stdout, "and reported completion")
        log_path = job / "log.json"
        if log_path.is_file():
            for entry in json.loads(log_path.read_text())["log"]:
                print(f"      [{entry['level']}] {entry['message']}")

        summary_path = job / "mcmd_summary.json"
        if done.returncode != 0 or not summary_path.is_file():
            check(False, "a summary was written")
            print(f"\n{failures} check(s) FAILED.")
            return 1
        summary = json.loads(summary_path.read_text())
        check(summary["cycles"] == N_CYCLES, f"all {N_CYCLES} cycles ran")
        judged = (summary["accepted"] + summary["rejected_metropolis"]
                  + summary["rejected_topology"] + summary["rejected_clash"]
                  + summary["cycles_without_site"])
        check(judged == N_CYCLES,
              f"every cycle is accounted for exactly once "
              f"({summary['accepted']} accepted, "
              f"{summary['rejected_metropolis']} by energy, "
              f"{summary['rejected_topology']} by chemistry, "
              f"{summary['rejected_clash']} by clearance, "
              f"{summary['cycles_without_site']} without a site)")
        check(summary["equilibration_steps"] > 0,
              f"the equilibration stage ran "
              f"({summary['equilibration_steps']} steps, "
              f"{summary['equilibration_relocations']} relocation(s))")
        check(summary["group_counts"]["epoxide"] == N_EPOXIDES
              and summary["group_counts"]["hydroxyl"] == 1
              and summary["group_counts"]["hydroxyl_pair"] == 1,
              "the inventory the script recognized is the one that was built")

        # THE RECORD IS EXACTLY AS LONG AS THE RUN.
        #
        # mcmd_all_structures.extxyz used to be longer: run_md() wrote a frame
        # after every burst step INCLUDING the last, and the cycle then wrote
        # the trial configuration again under a different `phase`. A
        # 10-equilibration / 200-cycle / 5-step run held 1113 frames instead
        # of 1010, and every one of the 103 extras was a coordinate-for-
        # coordinate copy of the frame before it.
        #
        # Checked three ways, because the interesting failure is the two
        # counts agreeing with each other and both being wrong: the summary's
        # own arithmetic, the closed form, and the file itself.
        expected = (summary["equilibration_steps"]
                    + summary["cycles"]
                    * max(1, summary["md_steps_per_cycle"]))
        check(summary["all_structures_frames_expected"] == expected,
              f"the run expects {expected} frames "
              f"(equilibration + cycles x burst)")
        check(summary["all_structures_frames"] == expected,
              f"and wrote exactly that many "
              f"({summary['all_structures_frames']})")
        all_structures = job / "mcmd_all_structures.extxyz"
        check(all_structures.is_file(), "the all-structures record exists")
        if all_structures.is_file():
            counted = count_extxyz_frames(all_structures)
            check(counted == expected,
                  f"and the FILE holds {counted} frames, not "
                  f"{counted - expected:+d}")
            duplicates = consecutive_duplicate_frames(all_structures)
            check(duplicates == 0,
                  f"with no frame repeating the coordinates of the one "
                  f"before it ({duplicates} found)")

        # No NULL MOVES. relocate() releases a group's own hosts before
        # drawing, so the site it just left was in the pool and could be drawn
        # again - a "move" that changes nothing, passes the clash and topology
        # tests trivially, and is accepted about half the time. Two accepted
        # frames with the same functional-group configuration is the symptom.
        accepted_path = job / "accepted_structures.extxyz"
        if accepted_path.is_file():
            repeats = repeated_decorations(accepted_path)
            check(repeats == 0,
                  f"no two accepted structures share a functional-group "
                  f"configuration ({repeats} repeat(s))")

        trajectory = job / "accepted_structures.extxyz"
        check(trajectory.is_file(), "a trajectory was written")
        if trajectory.is_file():
            result = run_py(python, VERIFY_TRAJECTORY, str(trajectory),
                            json.dumps(fixture))
            check(result.returncode == 0,
                  f"the trajectory could be re-analyzed independently"
                  + (f": {result.stderr.strip()[-300:]}"
                     if result.returncode else ""))
            if result.returncode == 0:
                frames = json.loads(result.stdout)
                check(len(frames) >= 1, f"{len(frames)} frame(s)")
                check(all(f["n_epoxides"] == N_EPOXIDES for f in frames),
                      f"every frame still has exactly {N_EPOXIDES} intact "
                      f"epoxides "
                      f"({sorted(set(f['n_epoxides'] for f in frames))} seen)")
                check(all(f["n_hydroxyls"] == N_HYDROXYLS for f in frames),
                      f"and exactly {N_HYDROXYLS} intact hydroxyls "
                      f"({sorted(set(f['n_hydroxyls'] for f in frames))} seen)")
                check(all(f["components"] == 1 for f in frames),
                      "one connected molecule on every frame - nothing was "
                      "released")
                check(all(f["pair_ok"] for f in frames),
                      "the antiposition pair is still a bonded, opposite-face "
                      "pair on every frame")
                z0 = frames[0]["cell_z"]
                check(all(abs(f["cell_z"] - z0) < 1e-9 for f in frames),
                      f"the vacuum axis of the cell was never scaled by the "
                      f"barostat ({z0:.3f} A throughout)")
                # Thermal snapshots at 300 K: an sp3 C-C under an epoxide
                # sits near 1.55 A and fluctuates; what must never appear
                # is a bond past the perception cutoff (1.82 A), which a
                # cell left unreverted after rejected NPT bursts used to
                # produce at the periodic boundary.
                check(all(f["max_cc"] < 1.82 for f in frames),
                      f"no C-C bond is stretched past the bond-perception "
                      f"cutoff on any frame (longest "
                      f"{max(f['max_cc'] for f in frames):.3f} A)")
                xy0 = frames[0]["cell_xy"]
                check(all(abs(f["cell_xy"][k] / xy0[k] - 1.0) < 0.03
                          for f in frames for k in (0, 1)),
                      "the in-plane cell moved by less than 3 % under the "
                      "barostat (it is reverted with every rejected move)")
                print(f"      planted O...O contact: "
                      f"{frames[0]['clash_oo']:.2f} A on frame 0 "
                      f"(built at 1.89 A), "
                      f"{frames[-1]['clash_oo']:.2f} A on the last frame")

        # ------------------------------------------------------------------
        # GO/MC-Opt: the same fixture, relaxed to minima instead.
        # ------------------------------------------------------------------
        # Run here rather than in a test of its own because it shares
        # everything expensive: the MACE environment, the cached checkpoint,
        # and the fixture with its planted 1.89 A contact. What is checked is
        # what DIFFERS - the frame invariant for a stage whose length is not
        # known in advance, the optimizer settings reaching the run, and the
        # acceptance test comparing minima rather than placements.
        print("\nGO/MC-Opt on the same fixture, BFGS to fmax:")
        optJob = tmp / "job_opt"
        optJob.mkdir()
        shutil.copy(structure, optJob / "structure.extxyz")
        optScript = (scripts / "graphene_oxide_mc_opt.py").read_text()
        check("relax_to_minimum = True" in optScript,
              "the dump is the optimization branch")
        optScript = optScript.replace('mace_mp(model="small"',
                                      f'mace_mp(model=r"{CACHED_SMALL}"')
        # Cut the dumped script's size down for the test. What this pass has
        # to establish is that the optimization branch RUNS under a real
        # calculator, that its frame accounting holds for a stage whose length
        # is not known in advance, and that its Metropolis test compares
        # minima — none of which needs twelve cycles or a 60-step ceiling.
        # Five cycles judges enough moves to have a median, and the frame
        # invariant is exact at any size.
        #
        # The DUMP keeps the full settings (they are what ScriptGenerationTest
        # asserts on); only this run is trimmed, and the assertions below read
        # the summary rather than these numbers, so the two cannot drift.
        optScript = re.sub(r"^mc_cycles = \d+", "mc_cycles = 5",
                           optScript, flags=re.M)
        optScript = re.sub(r"^optimizer_max_steps = \d+",
                           "optimizer_max_steps = 30", optScript, flags=re.M)
        optScript = re.sub(r"^equilibration_steps = \d+",
                           "equilibration_steps = 30", optScript, flags=re.M)
        (optJob / "run.py").write_text(optScript)
        optDone = subprocess.run([python, "run.py"], cwd=optJob,
                                 capture_output=True, text=True, timeout=1500)
        check(optDone.returncode == 0,
              f"the GO/MC-Opt run completed (exit {optDone.returncode})"
              + (f": {optDone.stderr.strip()[-400:]}"
                 if optDone.returncode else ""))
        optLog = optJob / "log.json"
        if optLog.is_file():
            for entry in json.loads(optLog.read_text())["log"]:
                print(f"      [{entry['level']}] {entry['message'][:150]}")
        optSummaryPath = optJob / "mcmd_summary.json"
        if optDone.returncode == 0 and optSummaryPath.is_file():
            opt = json.loads(optSummaryPath.read_text())
            check(opt["relaxation"] == "optimization",
                  "and says so in its summary")
            check(opt["optimizer"] == "BFGS",
                  f"under the optimizer it was given ({opt['optimizer']})")
            check(opt["optimizer_fmax_eV_per_A"] == 0.05,
                  "to the force criterion it was given")
            check(opt["cycles"] == 5,
                  f"over the {opt['cycles']} cycles this pass was trimmed to")
            check(opt["md_steps_per_cycle"] is not None,
                  "the MD field is still recorded (it is simply unused here)")
            check(opt["relaxation_steps_total"] > 0,
                  f"and it took {opt['relaxation_steps_total']} optimizer "
                  f"step(s)")

            # THE INVARIANT, for a stage whose length is not fixed: one frame
            # per optimizer step, initial relaxation included, with each
            # cycle's last frame carrying that cycle's verdict.
            optAll = optJob / "mcmd_all_structures.extxyz"
            check(optAll.is_file(), "the all-structures record exists")
            if optAll.is_file():
                counted = count_extxyz_frames(optAll)
                check(opt["all_structures_frames_expected"]
                      == opt["relaxation_steps_total"],
                      "the expected frame count IS the optimizer step count")
                check(opt["all_structures_frames"]
                      == opt["all_structures_frames_expected"],
                      f"the summary agrees with itself "
                      f"({opt['all_structures_frames']})")
                check(counted == opt["all_structures_frames_expected"],
                      f"and the FILE holds exactly that many ({counted})")
                dupes = consecutive_duplicate_frames(optAll)
                check(dupes == 0,
                      f"with no frame repeating the one before it ({dupes})")

            optAccepted = optJob / "accepted_structures.extxyz"
            if optAccepted.is_file():
                repeats = repeated_decorations(optAccepted)
                check(repeats == 0,
                      f"no two accepted structures share a functional-group "
                      f"configuration ({repeats} repeat(s))")

            # The point of the module. Both sides of the Metropolis test are
            # minima, so the trial delta measures the LANDSCAPE rather than
            # leftover placement strain — which is what made GO/MCMD's median
            # delta 150 kT on a real run. Bounded loosely: this is a 12-cycle
            # run on a small fixture, and the claim is an order of magnitude,
            # not a number.
            if opt.get("median_trial_delta_kT") is not None:
                print(f"      median trial dE = "
                      f"{opt['median_trial_delta_eV']:+.3f} eV "
                      f"({opt['median_trial_delta_kT']:.0f} kT), "
                      f"{opt['unconverged_cycles']}/{opt['cycles']} cycles "
                      f"unconverged")
                check(opt["median_trial_delta_kT"] < 50.0,
                      f"the Metropolis test compares minima, not placements: "
                      f"median dE {opt['median_trial_delta_kT']:.0f} kT "
                      f"(GO/MCMD measured 150 kT on a real run)")
        else:
            check(False, "a GO/MC-Opt summary was written")

    print("\n" + ("All GO/MCMD + GO/MC-Opt MACE checks passed."
                  if failures == 0 else f"{failures} check(s) FAILED."))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
