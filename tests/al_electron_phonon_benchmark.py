#!/usr/bin/env python3
"""Electron-phonon coupling end to end, on fcc aluminium.

Runs the SCRIPT THE GENERATOR EMITS — all three gpaw.elph stages — and checks
that it produces a relaxation time a Drude model can actually be built on.

Aluminium is the reference for a reason. It is the textbook nearly-free-electron
metal, its electron-phonon coupling constant is well established at
lambda ~ 0.4, and the room-temperature relaxation time that follows from
Allen's hbar/tau = 2*pi*lambda*k_B*T is ~10 fs — within a few femtoseconds of
what a Drude fit to the measured optical conductivity of Al gives. So the
number this test checks is anchored outside the calculation.

What is asserted is strict on STRUCTURE and, at this mesh, loose on the VALUE.

A 2x2x2 supercell on a 6^3 k-mesh cannot resolve aluminium's Fermi surface:
only ~6 of 864 states sit within 0.1 eV of E_F. That is a MESH limitation and
it remains one; lambda here is under-converged and the literature window below
is correspondingly wide.

What changed is that it is no longer also ARBITRARY. This benchmark previously
asserted only that the module knew it had not converged, because lambda was
computed with a Gaussian Fermi-surface smearing and ran 0.009, 0.22, 0.49,
1.55, 4.99, 16.6, 31.0 as that width went 0.05 to 0.8 eV — no plateau, so the
"answer" was whichever width was configured, and asserting a value would have
been asserting that one wrong smearing happened to land on the right number.

The integration is now the linear tetrahedron method (TetrahedronBz), which
interpolates within tetrahedra rather than requiring states to fall near E_F,
and has no width at all. So there is a definite number to check, and the
smearing sweep that used to police the parameter is gone with the parameter.
What must hold:

  * all three gpaw.elph stages complete and the raw arrays appear;
  * the analysis runs over them and writes epc.json;
  * lambda and tau are finite and positive — the two failures this benchmark
    already caught were a NaN (from masking imaginary modes into the frequency
    grid) and the inf that followed;
  * lambda is of metallic order, which a broken pipeline (empty Fermi-surface
    sums, or a missing normalization) is not;
  * tau and the reported rate are consistent, hbar/tau == rate;
  * the Drude rate handed to the optics module is exactly half of it — the
    factor of two between GPAW's convention and the textbook one, which is
    the single easiest thing in this chain to get wrong.

THE COST. This is the most expensive test in the suite by construction: stage 1
is 6N+1 self-consistent runs on the supercell and there is no way to make it
cheap without making it meaningless. The settings are the smallest that
exercise the whole pipeline — they are NOT settings that converge lambda, and
a production run needs an electron k-mesh larger by one to two orders of
magnitude.

Skips cleanly (exit 0) without GPAW's elph module.

Usage:  al_electron_phonon_benchmark.py <calango_script_test binary>
            [calango-elph-analyze binary]
"""
import json
import math
import os
import subprocess
import sys
import tempfile

# fcc Al. 4.05 A is the experimental lattice constant; PBE gives 4.04.
LATTICE_A = 4.05

# Physical expectations, from the literature rather than from a previous run of
# this code. Al's lambda is ~0.4; the window is wide because a 2x2x2 supercell
# on a coarse mesh is not converged, and narrow enough that a broken pipeline
# (lambda = 0 from empty Fermi-surface sums, or a runaway from a missing
# normalization) fails.
LAMBDA_MIN, LAMBDA_MAX = 0.05, 3.0
# tau in fs at 300 K. Al is ~8 fs measured; the bounds admit an order of
# magnitude either way while excluding "picoseconds" (a metal that does not
# scatter) and "attoseconds" (a units error).
TAU_MIN_FS, TAU_MAX_FS = 0.5, 200.0

HBAR_EV_FS = 0.6582119569


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
    print(f"  {'ok  ' if condition else 'FAIL'} {what}", flush=True)
    if not condition:
        failures += 1


def main() -> int:
    if len(sys.argv) < 2:
        print("SKIP: no calango_script_test binary given")
        return 0
    binary = sys.argv[1]

    try:
        from ase.build import bulk
        from ase.io import write
        import gpaw.elph  # noqa: F401
        from gpaw.elph import (DisplacementRunner, ElectronPhononMatrix,  # noqa
                               Supercell)
    except Exception as exc:
        print(f"SKIP: GPAW's elph module is unavailable ({exc})")
        return 0

    # CALANGO_KEEP_BENCHMARK_DIR keeps the run's artifacts. This test costs
    # ~66 minutes, and when it failed with lambda = 21 the temporary directory
    # had already been deleted — so the raw arrays that would have explained
    # it were gone and the only way to look was to pay the hour again. Never
    # again: diagnosing a slow test must not require re-running it.
    keep = os.environ.get("CALANGO_KEEP_BENCHMARK_DIR")
    if keep:
        os.makedirs(keep, exist_ok=True)
        print(f"Keeping benchmark artifacts in {keep}", flush=True)
        return _run(keep, binary, bulk, write)
    with tempfile.TemporaryDirectory() as workdir:
        return _run(workdir, binary, bulk, write)


def _run(workdir: str, binary: str, bulk, write) -> int:
    # `if True:` only to keep the original body's indentation; the block was
    # lifted out of a `with tempfile.TemporaryDirectory()`.
    if True:
        # The generated script, in its deliberately small variant — the same
        # generator the wizard drives, not a transcription of it.
        dump = os.path.join(workdir, "scripts")
        os.makedirs(dump, exist_ok=True)
        subprocess.run([binary, "--dump", dump], check=True,
                       stdout=subprocess.DEVNULL)
        source = os.path.join(dump, "electron_phonon_small.py")
        if not os.path.exists(source):
            print("FAIL: --dump produced no electron_phonon_small.py")
            return 1

        job = os.path.join(workdir, "job")
        os.makedirs(job, exist_ok=True)
        with open(source) as handle:
            script = handle.read()
        check("gpaw.elph" in script, "the generated script drives gpaw.elph")
        with open(os.path.join(job, "run.py"), "w") as handle:
            handle.write(script)
        write(os.path.join(job, "structure.extxyz"),
              bulk("Al", "fcc", a=LATTICE_A))

        print("Running the three elph stages on fcc Al "
              "(this is the slow part):", flush=True)
        completed = subprocess.run([sys.executable, "run.py"], cwd=job,
                                   capture_output=True, text=True)
        if completed.returncode != 0:
            print("FAIL: the generated script exited "
                  f"{completed.returncode}")
            print(completed.stdout[-3000:])
            print(completed.stderr[-3000:])
            return 1

        # The script's job ends at the raw arrays. alpha^2F, lambda and tau
        # come from calango-elph-analyze, which is the same C++ the GUI runs
        # — and the reason a headless binary exists at all: on a production
        # mesh |g|^2 is tens of gigabytes and the analysis has to happen
        # beside the run.
        manifest = os.path.join(job, "elph_raw.txt")
        check(os.path.exists(manifest), "the raw manifest was written")
        check(os.path.exists(os.path.join(job, "gsqklnn.npy")),
              "GPAW's own g_sqklnn.npy is there to be read in place")
        if not os.path.exists(manifest):
            print(completed.stdout[-3000:])
            return 1

        analyzer = (sys.argv[2] if len(sys.argv) > 2
                    else os.path.join(os.path.dirname(binary),
                                      "calango-elph-analyze"))
        if not os.path.exists(analyzer):
            print(f"SKIP: {analyzer} not built; configure with "
                  f"-DCALANGO_BUILD_TESTS=ON and build calango-elph-analyze")
            return 0
        analysis = subprocess.run([analyzer, job], capture_output=True,
                                  text=True)
        print(analysis.stdout, flush=True)
        if analysis.stderr:
            print(analysis.stderr, flush=True)
        check(analysis.returncode == 0,
              "the tetrahedron analysis ran over the raw arrays")

        results_path = os.path.join(job, "epc.json")
        check(os.path.exists(results_path), "epc.json was written")
        if not os.path.exists(results_path):
            return 1
        with open(results_path) as handle:
            results = json.load(handle)

        lam = results.get("lambda")
        tau = results.get("relaxation_time_fs")
        rate = results.get("scattering_rate_eV")
        drude = results.get("drude_rate_eV")
        temperature = results.get("temperature_K")
        print(f"    lambda = {lam:.4f}, omega_log = "
              f"{results.get('omega_log_eV', 0.0) * 1000:.1f} meV, "
              f"tau({temperature:.0f} K) = {tau:.2f} fs", flush=True)

        # 1. The spectral function exists and is a spectral function.
        a2f = results.get("alpha2F") or []
        omega = results.get("omega_eV") or []
        check(len(a2f) == len(omega) and len(a2f) > 10,
              "alpha^2F(w) is on a frequency grid of its own length")
        check(all(v >= -1e-9 for v in a2f),
              "and is non-negative everywhere, as a spectral function must be")
        check(max(a2f) > 0.0, "and is not identically zero")

        # 2. Which integration produced it. Recorded in the file because a
        #    tetrahedron lambda and a smeared one are not comparable numbers,
        #    and a stored result with no method on it is unreadable later.
        check(results.get("integration") == "tetrahedron",
              "the result records that it was integrated on tetrahedra")
        check("lambda_vs_smearing" not in results,
              "and carries no smearing sweep, there being no smearing left "
              "to sweep")

        # 3. The value. Assertable now that it is not a function of a
        #    parameter — still under-converged at this mesh, hence the wide
        #    window, but a definite number rather than an arbitrary one.
        check(lam is not None and math.isfinite(lam) and lam > 0.0,
              f"lambda = {lam:.4f} is finite and positive — not the NaN an "
              f"earlier masking bug produced")
        check(tau is not None and math.isfinite(tau) and tau > 0.0,
              f"and tau = {tau:.3f} fs likewise, not the inf that followed "
              f"from it")
        check(lam is not None and LAMBDA_MIN < lam < LAMBDA_MAX,
              f"lambda is of metallic order ({LAMBDA_MIN}-{LAMBDA_MAX}); Al "
              f"is ~0.4 converged, and this mesh is not converged")
        check(tau is not None and TAU_MIN_FS < tau < TAU_MAX_FS,
              f"tau is in the range a metal lives in ({TAU_MIN_FS}-"
              f"{TAU_MAX_FS} fs); Al is ~8 fs measured")

        # 4. Internal consistency. These are exact relations, not estimates:
        #    a mismatch means the reported numbers do not describe each other.
        check(abs(rate - HBAR_EV_FS / tau) < 1e-6 * max(rate, 1e-9),
              "hbar/tau agrees with the reported scattering rate")
        check(abs(drude - 0.5 * rate) < 1e-12,
              "and the Drude rate for the optics module is exactly half of "
              "it — the factor of two between GPAW's convention and the "
              "textbook one")

        # 5. The Allen relation the rate was built from.
        kb = 8.617333262e-5
        check(abs(rate - 2.0 * 3.141592653589793 * lam * kb * temperature)
              < 1e-9,
              "and the rate follows hbar/tau = 2*pi*lambda*k_B*T")

    if failures == 0:
        print("\nPASS: the electron-phonon workflow ran end to end and "
              "produced a usable relaxation time.")
        return 0
    print(f"\n{failures} electron-phonon check(s) FAILED.")
    return 1


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
