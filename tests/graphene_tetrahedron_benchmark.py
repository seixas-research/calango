#!/usr/bin/env python3
"""Tetrahedron integration validated on monolayer graphene.

Graphene is the right system for this: its low-energy optical absorbance is a
UNIVERSAL constant, A = pi*alpha = 2.293%, fixed by the fine-structure constant
alone and independent of every material parameter. So the number the pipeline
produces can be compared against a value that owes nothing to the calculation.

It is also the system that motivates tetrahedron integration. The absorbance
comes from transitions across the Dirac cone at K, which occupy a vanishing
region of the Brillouin zone. Point integration replaces each transition with a
Lorentzian of width eta and needs a very dense mesh before the low-energy
plateau emerges; tetrahedron integration interpolates the bands within each
tetrahedron and integrates analytically, reaching the plateau sooner.

This runs the SCRIPT THE GENERATOR EMITS, not a re-implementation of it, so the
`integrationmode` plumbing, the 2D observables and the JSON schema are all
exercised as they ship.

A note on the mesh, because it is the whole cost of this test: the k-mesh is
the physics-critical parameter and cannot be cheapened. Measured here, at
1.04 eV:

    36x36 high-symmetry mesh   A/(pi*alpha) = 0.965   (~4 min)
    18x18 high-symmetry mesh   A/(pi*alpha) = 0.173   (~1 min)

On the coarse mesh the ratio does reach 1.0 — but at 1.95 eV, where it is the
rising interband tail crossing the universal value rather than the plateau. A
cheap version of this test would therefore PASS FOR THE WRONG REASON. Band
count and plane-wave cutoff, by contrast, were cut hard (30 -> 10 bands,
300 -> 250 eV) with no effect on the result.

Skips cleanly (exit 0) without GPAW's response stack.

Usage:  graphene_tetrahedron_benchmark.py <calango_script_test binary>
"""
import json
import os
import subprocess
import sys
import tempfile

# Universal constants — the reference, independent of the calculation.
ALPHA = 1.0 / 137.035999084
HBAR_C_EV_A = 1973.269804

# Cell / convergence parameters. VACUUM_A is the out-of-plane cell length; the
# generated script divides it back out to get the sheet observables.
VACUUM_A = 15.0
PW_CUTOFF_EV = 250.0
RESPONSE_ECUT_EV = 25.0
MESH_DENSITY = 12.0     # -> a 36x36x1 high-symmetry Monkhorst-Pack grid
NBANDS = 10
# Where the universal plateau is evaluated. Above the linear-Dirac regime the
# ratio climbs past 1 as real interband structure sets in, so the comparison
# point has to sit inside the plateau, not merely somewhere on the curve.
PROBE_EV = 1.05
TOLERANCE = 0.20        # measured 0.965; this passes without being vacuous


def _bootstrap_gpaw_env() -> None:
    """Re-exec under the GPAW conda env from ~/.calango/settings.json."""
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
    print(f"  {'ok  ' if condition else 'FAIL'} {what}", flush=True)
    if not condition:
        failures += 1


def build_baseline(workdir):
    """A graphene ground state on a grid tetrahedron integration accepts.

    Tetrahedron integration requires every vertex of the irreducible BZ to be
    present, which an ordinary Monkhorst-Pack grid does not guarantee — hence
    find_high_symmetry_monkhorst_pack. Preparing the BASELINE this way is the
    real workflow: the optics run inherits the grid and cannot fix it later.
    """
    import numpy as np
    from ase.build import graphene
    from gpaw import GPAW, PW
    from gpaw.bztools import find_high_symmetry_monkhorst_pack

    atoms = graphene(vacuum=VACUUM_A / 2.0)
    seed = os.path.join(workdir, "seed.gpw")
    atoms.calc = GPAW(mode=PW(PW_CUTOFF_EV), xc="PBE", kpts=(9, 9, 1),
                      occupations={"name": "fermi-dirac", "width": 0.05},
                      txt=os.path.join(workdir, "seed.txt"))
    atoms.get_potential_energy()
    atoms.calc.write(seed, mode="all")

    kpts = find_high_symmetry_monkhorst_pack(seed, density=MESH_DENSITY)
    print(f"    high-symmetry grid: {np.shape(kpts)[0]} k-points", flush=True)

    # symmetry="off" — the response code integrates over the unfolded zone.
    calc = GPAW(mode=PW(PW_CUTOFF_EV), xc="PBE", kpts=kpts, symmetry="off",
                occupations={"name": "fermi-dirac", "width": 0.05},
                nbands=NBANDS, convergence={"bands": NBANDS - 4},
                txt=os.path.join(workdir, "baseline.txt"))
    atoms.calc = calc
    atoms.get_potential_energy()
    baseline = os.path.join(workdir, "single_point.gpw")
    calc.write(baseline, mode="all")
    return baseline


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    binary = sys.argv[1]

    try:
        import numpy as np
        from gpaw.response.df import DielectricFunction  # noqa: F401
        from gpaw.bztools import find_high_symmetry_monkhorst_pack  # noqa: F401
    except Exception as exc:
        print(f"SKIP: GPAW response stack unavailable ({exc})")
        return 0

    with tempfile.TemporaryDirectory() as workdir:
        # 1. The generated 2D-optics script, tetrahedron variant.
        dump = os.path.join(workdir, "scripts")
        os.mkdir(dump)
        subprocess.run([binary, "--dump", dump], check=True,
                       stdout=subprocess.DEVNULL)
        source = os.path.join(dump, "optics_2d_tetrahedron.py")
        if not os.path.exists(source):
            print("FAIL: --dump produced no optics_2d_tetrahedron.py")
            return 1

        print("Baseline on a high-symmetry mesh:", flush=True)
        baseline = build_baseline(workdir)
        check(os.path.exists(baseline), "graphene ground state written")

        job = os.path.join(workdir, "job")
        os.makedirs(job)
        with open(source) as handle:
            script = handle.read()
        script = script.replace("/jobs/proc_1/single_point.gpw", baseline)
        check("tetrahedron integration" in script,
              "the generated script requests tetrahedron integration")
        run_py = os.path.join(job, "optics.py")
        with open(run_py, "w") as handle:
            handle.write(script)
        # The script reads the structure it was staged with.
        from ase.build import graphene
        from ase.io import write as ase_write
        ase_write(os.path.join(job, "structure.extxyz"),
                  graphene(vacuum=VACUUM_A / 2.0))

        print("Generated optics script:", flush=True)
        completed = subprocess.run([sys.executable, run_py], cwd=job,
                                   capture_output=True, text=True)
        if completed.returncode != 0:
            print(f"  FAIL exited {completed.returncode}")
            print(completed.stdout[-3000:])
            print(completed.stderr[-3000:])
            return 1
        check(True, "runs to completion")

        with open(os.path.join(job, "optics.json")) as handle:
            results = json.load(handle)
        check(results.get("integrationmode") == "tetrahedron integration",
              "the result records which integrator produced it")

        energy = np.asarray(results["energy_eV"], dtype=float)
        eps2 = np.asarray(results["eps_xx"]["eps2"], dtype=float)
        twod = results["twod_xx"]
        absorbance = np.asarray(twod["absorbance"], dtype=float)

        # Numerical stability. Tetrahedron integration divides by band-energy
        # differences within each tetrahedron, so degeneracies are exactly
        # where it can produce NaN — and graphene's Dirac point is a
        # degeneracy sitting on the mesh.
        print("Numerical stability:", flush=True)
        check(np.all(np.isfinite(eps2)),
              "eps_2(omega) is finite everywhere (no NaN at the Dirac point)")
        check(np.all(np.isfinite(absorbance)), "the absorbance is finite")
        check(np.all(eps2 >= -1e-9),
              "eps_2 >= 0 — a negative absorptive part is unphysical")

        # The universal value.
        print("Universal absorbance:", flush=True)
        probe = int(np.argmin(np.abs(energy - PROBE_EV)))
        universal = np.pi * ALPHA
        ratio = absorbance[probe] / universal
        print(f"    at {energy[probe]:.3f} eV: A = {absorbance[probe]:.5f}"
              f"  (pi*alpha = {universal:.5f}, ratio = {ratio:.3f})", flush=True)
        check(abs(ratio - 1.0) <= TOLERANCE,
              f"A = pi*alpha within {TOLERANCE:.0%} in the low-energy limit")

        # A sanity guard on the comparison itself: had the sheet thickness been
        # divided out wrongly, the absorbance would be off by a factor of L_z
        # (here 15x), which the tolerance above would catch — but so would a
        # completely flat or zero spectrum. Require real structure too.
        check(float(np.max(absorbance)) > 2.0 * universal,
              "the spectrum rises above the plateau at higher energy")

    print("\nAll graphene tetrahedron checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
