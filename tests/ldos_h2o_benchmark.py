#!/usr/bin/env python3
"""End-to-end validation of the LDOS module (Local Density of States) on H2O.

Runs the GENERATED script — the one calango_script_test's `--dump-ldos`
emits from the real C++ generator (LdosScriptGenerator), the same one the
LDOS wizard drives — against a real baseline `.gpw`, not a reimplementation.

Physics asserted, both from generic principles, not tuned constants:

  1. TOTAL DENSITY. Summing LDOS over an energy window that captures every
     occupied valence state (floor far below the lowest level, ceiling at
     the Fermi level) and integrating over the cell reproduces the number
     of valence electrons GPAW tracks, UP TO THE KNOWN SPIN-DEGENERACY
     FACTOR of 2 for a non-spin-polarized calculation: LDOS(r) sums
     w_k*|psi|^2 with no occupation weight (by design — an "unoccupied
     near E_F" window has to stay nonzero), so it undercounts a doubly-
     occupied manifold by exactly that factor. This is the module's own
     documented relationship (LdosConfig's doc comment), not a fudge
     factor invented for the test.

  2. HOMO LOCALIZATION, by an EXACT symmetry argument, not a fitted
     threshold. Water's C2v point group makes the HOMO (1b1: the
     out-of-plane oxygen lone pair) ANTISYMMETRIC under reflection through
     the molecular plane, which both hydrogens sit exactly on — so a
     perfectly symmetric input geometry (ase.build.molecule('H2O')) makes
     the HOMO's amplitude on both H atoms EXACTLY zero by symmetry, not
     merely small. An energy window bracketing only the HOMO should
     therefore integrate to a real, order-1 weight near the oxygen atom
     and to noise-level weight near either hydrogen.

Skips cleanly (exit 0) without GPAW. Cheap: mode='fd' at a coarse grid
spacing, one molecule, one SCF.

Usage:  ldos_h2o_benchmark.py <calango_script_test binary>
"""
import json
import os
import subprocess
import sys
import tempfile


def _bootstrap_gpaw_env() -> None:
    """Re-exec under the GPAW conda env from ~/.calango/settings.json when the
    current interpreter has no GPAW, so the benchmark really runs."""
    try:
        import gpaw  # noqa: F401
        return
    except Exception:
        pass
    if os.environ.get("_CALANGO_GPAW_REEXEC"):
        return
    envs = []
    try:
        with open(os.path.expanduser("~/.calango/settings.json")) as fh:
            jobs = json.load(fh).get("jobs", {})
        presets = json.loads(jobs.get("environmentPresets", "{}") or "{}")
        if presets.get("GPAW"):
            envs.append(presets["GPAW"])
        if jobs.get("environmentPath"):
            envs.append(jobs["environmentPath"])
    except Exception:
        return
    for env in envs:
        for py in (os.path.join(env, "bin", "python3"),
                   os.path.join(env, "bin", "python")):
            if os.path.isfile(py) and \
                    os.path.realpath(py) != os.path.realpath(sys.executable):
                os.environ["_CALANGO_GPAW_REEXEC"] = "1"
                os.execv(py, [py, os.path.abspath(__file__)] + sys.argv[1:])


failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def write_baseline(workdir):
    """A completed Single-Point Calculation on H2O, exactly as the wizard
    produces — including the exactly-symmetric C2v geometry the HOMO check
    below depends on."""
    from ase.build import molecule
    from gpaw import GPAW

    atoms = molecule("H2O", vacuum=4.0)
    calc = GPAW(mode="fd", h=0.25, xc="PBE",
               occupations={"name": "fermi-dirac", "width": 0.01},
               txt=os.path.join(workdir, "scf.txt"))
    atoms.calc = calc
    atoms.get_potential_energy()
    path = os.path.join(workdir, "single_point.gpw")
    calc.write(path, mode="all")
    return path, atoms, calc


def run_generated(binary, workdir, baseline_dir, e_min, e_max, tag):
    """--dump-ldos, then execute the generated script for real."""
    dump = os.path.join(workdir, f"scripts_{tag}")
    os.mkdir(dump)
    subprocess.run([binary, "--dump-ldos", dump, baseline_dir,
                    f"{e_min:.6f}", f"{e_max:.6f}"], check=True)
    source = os.path.join(dump, "ldos.py")
    if not os.path.exists(source):
        print(f"FAIL: --dump-ldos produced no ldos.py for {tag}")
        return None

    run_dir = os.path.join(workdir, f"run_{tag}")
    os.mkdir(run_dir)
    completed = subprocess.run([sys.executable, source], cwd=run_dir,
                               capture_output=True, text=True)
    if completed.returncode != 0:
        print(f"  FAIL [{tag}] the generated script exited "
              f"{completed.returncode}")
        print(completed.stdout[-3000:])
        print(completed.stderr[-3000:])
        return None
    with open(os.path.join(run_dir, "ldos.json")) as handle:
        summary = json.load(handle)
    with open(os.path.join(run_dir, "ldos.cube")) as handle:
        from ase.io.cube import read_cube
        cube = read_cube(handle)
    return summary, cube


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    binary = sys.argv[1]

    try:
        import numpy as np
        import gpaw  # noqa: F401
    except Exception as exc:
        print(f"SKIP: GPAW not available ({exc})")
        return 0

    with tempfile.TemporaryDirectory() as workdir:
        print("Baseline single point (H2O):")
        baseline_path, atoms, calc = write_baseline(workdir)
        baseline_dir = os.path.dirname(baseline_path)
        check(os.path.exists(baseline_path),
              "wrote a .gpw the LDOS run can inherit")

        eigs = calc.get_eigenvalues(kpt=0, spin=0)
        occ = calc.get_occupation_numbers(kpt=0, spin=0)
        n_electrons = calc.get_number_of_electrons()
        occupied = [i for i, f in enumerate(occ) if f > 0.5]
        check(len(occupied) > 0, "SCF converged with at least one occupied "
                                 "state")
        homo_index = occupied[-1]
        homo_energy = eigs[homo_index]
        print(f"    {len(occupied)} occupied state(s), HOMO index "
              f"{homo_index} at {homo_energy:.3f} eV, "
              f"{n_electrons:.1f} valence electrons")

        cell_volume = atoms.get_volume()  # Angstrom^3

        # 1. Total density: window from far below the lowest level up to
        #    (and including) the HOMO.
        print("All-occupied-states window:")
        result = run_generated(binary, workdir, baseline_dir,
                               eigs[0] - 50.0, homo_energy + 1e-3,
                               "occupied")
        check(result is not None, "the generated script runs to completion")
        if result is not None:
            summary, cube = result
            check(summary["nstates"] == len(occupied),
                  f"selected exactly the {len(occupied)} occupied states "
                  f"(got {summary['nstates']})")
            occ_data = np.asarray(cube["data"], dtype=float)
            # dV computed from the CUBE's own grid — self-consistent with
            # what was actually integrated, rather than a second, possibly
            # differently-shaped grid queried straight from the calculator.
            dV = cell_volume / occ_data.size
            ldos_total = float(occ_data.sum() * dV)
            # The known factor of 2: LDOS carries no occupation weight, so a
            # doubly-occupied non-spin-polarized manifold is undercounted by
            # exactly the spin degeneracy.
            reconstructed = 2.0 * ldos_total
            print(f"    integral(LDOS)*2 = {reconstructed:.3f}, "
                  f"n_electrons = {n_electrons:.3f}")
            check(abs(reconstructed - n_electrons) < 0.15 * n_electrons,
                  "2x the occupied-window LDOS integral reproduces the "
                  "valence electron count within 15%")

        # 2. HOMO only: a narrow window bracketing just that one state.
        print("HOMO-only window:")
        result = run_generated(binary, workdir, baseline_dir,
                               homo_energy - 0.05, homo_energy + 0.05,
                               "homo")
        check(result is not None, "the generated script runs to completion")
        if result is not None:
            summary, cube = result
            check(summary["nstates"] == 1,
                  f"the narrow window isolates exactly one state "
                  f"(got {summary['nstates']})")
            data = np.asarray(cube["data"], dtype=float)
            dV = cell_volume / data.size
            homo_integral = float(data.sum() * dV)
            print(f"    integral(LDOS_HOMO) = {homo_integral:.4f} "
                  "(expect ~1: one normalized orbital, k-weight 1)")
            # Same ~5-10% coarse-grid discretization shortfall as the
            # occupied-window integral above (this h=0.25 FD grid trades
            # accuracy for CI speed) — same 15% tolerance, not a tighter
            # one just because the target here happens to be a round
            # number.
            check(abs(homo_integral - 1.0) < 0.15,
                  "a single Gamma-point state's LDOS integrates to ~1 "
                  "(one normalized orbital, k-weight 1)")

            # Symmetry check: weight near each H vs near O. A generous
            # radius and a generous margin — this asserts the SIGN of the
            # effect (concentrated on O, negligible on H), not a tuned
            # number.
            grid_shape = data.shape
            o_index = [i for i, s in enumerate(atoms.get_chemical_symbols())
                      if s == "O"][0]
            h_indices = [i for i, s
                        in enumerate(atoms.get_chemical_symbols())
                        if s == "H"]

            def weight_near(atom_index, radius_A=0.6):
                pos = atoms.positions[atom_index]
                frac = np.linalg.solve(atoms.cell[:].T, pos)
                center = np.round(frac * grid_shape).astype(int)
                r_vox = max(
                    1, int(radius_A / (cell_volume ** (1 / 3) /
                                       np.mean(grid_shape))))
                lo = np.clip(center - r_vox, 0, grid_shape)
                hi = np.clip(center + r_vox, 0, grid_shape)
                sub = data[lo[0]:hi[0], lo[1]:hi[1], lo[2]:hi[2]]
                return float(sub.sum() * dV)

            o_weight = weight_near(o_index)
            h_weight = max(weight_near(h) for h in h_indices)
            print(f"    weight near O = {o_weight:.4f}, "
                  f"max weight near H = {h_weight:.4f}")
            check(o_weight > 5.0 * max(h_weight, 1e-6),
                  "the HOMO is concentrated near oxygen, negligible near "
                  "either hydrogen (C2v forbids 1b1 on either H exactly)")

    print("\nAll LDOS checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
