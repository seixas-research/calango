#!/usr/bin/env python3
"""End-to-end validation of the Wavefunctions module on H2O.

Runs the GENERATED script (calango_script_test's --dump-wavefunctions,
built from WavefunctionScriptGenerator, which shares its restart and
wavefunction-access layer with LDOS's own generator) against a real
baseline, for the HOMO.

Physics asserted:

  1. NONZERO AND CORRECTLY LOCATED. The HOMO's |psi|^2 is not uniformly
     zero, and — by the SAME exact C2v symmetry argument the LDOS
     benchmark uses (water's HOMO, 1b1, is the out-of-plane oxygen lone
     pair, antisymmetric under the molecular-plane mirror both hydrogens
     sit exactly on, hence EXACTLY zero there by symmetry) — its weight is
     concentrated near oxygen and negligible near either hydrogen.

  2. NORMALIZATION. integral(|psi|^2) dV = 1 for a single, real,
     Gamma-point orbital (k-weight 1, no occupation factor applies to a
     single wavefunction's own norm).

  3. PSEUDO vs ALL-ELECTRON. The PAW correction is exactly zero outside
     each atom's augmentation sphere by construction — the pseudo and
     all-electron wavefunctions must AGREE far from every nucleus and may
     DIFFER close to one. Asserted as a ratio: the pseudo/all-electron
     difference integrated in shells near each atom must be much larger
     than the SAME difference integrated in the region away from every
     atom.

Skips cleanly (exit 0) without GPAW, or without the new engine's
get_all_electron_wave_function (older GPAW).

Usage:  wavefunctions_h2o_benchmark.py <calango_script_test binary>
"""
import json
import os
import subprocess
import sys
import tempfile


def _bootstrap_gpaw_env() -> None:
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
    from ase.build import molecule
    from gpaw import GPAW

    baseline_dir = os.path.join(workdir, "h2o")
    os.makedirs(baseline_dir, exist_ok=True)
    atoms = molecule("H2O", vacuum=4.0)
    calc = GPAW(mode="fd", h=0.25, xc="PBE",
               occupations={"name": "fermi-dirac", "width": 0.01},
               txt=os.path.join(baseline_dir, "scf.txt"))
    atoms.calc = calc
    atoms.get_potential_energy()
    calc.write(os.path.join(baseline_dir, "single_point.gpw"), mode="all")
    homo_index = max(i for i, f in
                     enumerate(calc.get_occupation_numbers(kpt=0, spin=0))
                     if f > 0.5)
    return baseline_dir, atoms, homo_index


def run_generated(binary, workdir, baseline_dir, band, tag, all_electron=False):
    dump = os.path.join(workdir, f"scripts_{tag}")
    os.mkdir(dump)
    args = [binary, "--dump-wavefunctions", dump, baseline_dir, str(band),
            "0", "0", "1" if all_electron else "0"]
    subprocess.run(args, check=True)
    source = os.path.join(dump, "wavefunctions.py")
    if not os.path.exists(source):
        print(f"FAIL: --dump-wavefunctions produced no script for {tag}")
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
    with open(os.path.join(run_dir, "wavefunctions.json")) as handle:
        summary = json.load(handle)
    cube_name = summary["states"][0]["cube"]
    from ase.io.cube import read_cube
    with open(os.path.join(run_dir, cube_name)) as handle:
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
        baseline_dir, atoms, homo = write_baseline(workdir)
        check(os.path.exists(os.path.join(baseline_dir, "single_point.gpw")),
              "wrote a .gpw the run can inherit")
        print(f"    HOMO band index {homo}")
        cell_volume = atoms.get_volume()

        # 1 & 2: pseudo HOMO, density quantity.
        print("Pseudo HOMO (density):")
        result = run_generated(binary, workdir, baseline_dir, homo, "pseudo")
        check(result is not None, "the generated script runs to completion")
        if result is not None:
            summary, cube = result
            data = np.asarray(cube["data"], dtype=float)
            dV = cell_volume / data.size
            check(data.max() > 0.0, "the HOMO density is not uniformly zero")

            integral = float(data.sum() * dV)
            print(f"    integral(|psi_HOMO|^2) = {integral:.4f} (expect ~1)")
            check(abs(integral - 1.0) < 0.15,
                  "a single Gamma-point orbital's density integrates to ~1")

            symbols = atoms.get_chemical_symbols()
            grid_shape = data.shape
            o_index = symbols.index("O")
            h_indices = [i for i, s in enumerate(symbols) if s == "H"]

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
                  "the HOMO is correctly located: concentrated near "
                  "oxygen, negligible near either hydrogen (C2v forbids "
                  "1b1 on either H exactly)")

        # 3: pseudo vs all-electron, real part. The two reconstructions run
        # on INDEPENDENT grids (the all-electron path has its own
        # grid_spacing, unrelated to the SCF's native one), so this compares
        # region-INTEGRATED quantities computed separately on each cube's
        # own grid/cell rather than a voxel-by-voxel difference, which would
        # need the two grids to coincide.
        print("Pseudo vs all-electron (real part):")
        pseudo = run_generated(binary, workdir, baseline_dir, homo,
                               "real_pseudo", all_electron=False)
        ae = run_generated(binary, workdir, baseline_dir, homo,
                           "real_ae", all_electron=True)
        check(pseudo is not None, "the pseudo script runs to completion")
        check(ae is not None, "the all-electron script runs to completion")
        if pseudo is not None and ae is not None:
            _, pcube = pseudo
            _, acube = ae

            def near_and_far_norms(cube, radius_A=0.4):
                """integral(psi^2) split into 'within radius_A of some
                atom' and 'the rest of the cell', on THIS cube's own grid
                and cell — independent of any other cube's grid."""
                data = np.asarray(cube["data"], dtype=float)
                cube_atoms = cube["atoms"]
                cell = cube_atoms.get_volume()
                shape = np.array(data.shape)
                dV = cell / data.size
                mask_near = np.zeros(data.shape, dtype=bool)
                r_vox = max(
                    1, int(radius_A / (cell ** (1 / 3) / np.mean(shape))))
                for pos in cube_atoms.positions:
                    frac = np.linalg.solve(cube_atoms.cell[:].T, pos)
                    center = np.round(frac * shape).astype(int)
                    lo = np.clip(center - r_vox, 0, shape)
                    hi = np.clip(center + r_vox, 0, shape)
                    mask_near[lo[0]:hi[0], lo[1]:hi[1], lo[2]:hi[2]] = True
                psi2 = data ** 2
                near = float(psi2[mask_near].sum() * dV)
                far = float(psi2[~mask_near].sum() * dV)
                near_volume = float(mask_near.sum()) * dV
                far_volume = float((~mask_near).sum()) * dV
                return (near / max(near_volume, 1e-9),
                       far / max(far_volume, 1e-9))

            p_near, p_far = near_and_far_norms(pcube)
            a_near, a_far = near_and_far_norms(acube)
            print(f"    pseudo:       near-nucleus density {p_near:.4f}, "
                  f"far density {p_far:.4f}")
            print(f"    all-electron: near-nucleus density {a_near:.4f}, "
                  f"far density {a_far:.4f}")

            # ABSOLUTE differences, not each compared to its own (near-zero,
            # noise-dominated) far-region scale: both "far" densities are
            # already close to zero (the wavefunction has decayed in the
            # vacuum padding), so a RELATIVE difference there blows up on
            # nothing. What matters physically is that the near-nucleus gap
            # is large on the density's own natural scale while the far-
            # region gap is not.
            near_diff = abs(p_near - a_near)
            far_diff = abs(p_far - a_far)
            print(f"    |pseudo - AE| density: near = {near_diff:.4f}, "
                  f"far = {far_diff:.4f}")
            check(near_diff > 10.0 * max(far_diff, 1e-6),
                  "pseudo and all-electron differ MUCH more near the "
                  "nuclei (the PAW correction, and the cusp the pseudo "
                  "orbital is built to avoid) than in the region away "
                  "from every atom (zero correction outside the "
                  "augmentation sphere by construction)")

    print("\nAll Wavefunctions checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
