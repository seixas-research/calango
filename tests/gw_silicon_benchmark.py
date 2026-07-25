#!/usr/bin/env python3
"""End-to-end validation of the GPAW G0W0 pipeline on silicon.

Unlike the other benchmarks, this one does not mirror the generator — it runs
the script the generator actually emits, against a real baseline .gpw, and
checks the gw.json that comes out. That is deliberate: the first version of
this pipeline died on GPAW's "Increase GS-nbands or decrease chi0-nbands!"
assertion, a failure no amount of reading or byte-compiling would have shown,
because it lives in the interaction between the NSCF band count and G0W0's
ecut-derived default.

Physics asserted:
  * G0W0 OPENS the gap. The self-energy replaces a too-shallow DFT exchange-
    correlation potential, and for a semiconductor the correction is positive
    and of order 0.5-2 eV. A negative renormalization means the run is broken,
    not that silicon is unusual.
  * The DFT and quasiparticle matrices have the same shape, so they are
    comparable band for band (the whole premise of a one-shot correction).

The parameters here are deliberately cheap (2x2x2 k-points, 150 eV PW cutoff,
100 eV screening) — this is a plumbing test, not a converged calculation, and
the absolute gap is not asserted for that reason.

Skips cleanly (exit 0) without GPAW's response stack.

Usage:  gw_silicon_benchmark.py <calango_script_test binary>
"""
import json
import os
import shutil
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
    """A completed Single-Point Calculation, exactly as the wizard produces."""
    from ase.build import bulk
    from gpaw import GPAW, PW

    atoms = bulk("Si", "diamond", a=5.43)
    atoms.calc = GPAW(mode=PW(150.0), xc="PBE", kpts=(2, 2, 2),
                      occupations={"name": "fermi-dirac", "width": 0.01},
                      txt=os.path.join(workdir, "scf.txt"))
    atoms.get_potential_energy()
    path = os.path.join(workdir, "single_point.gpw")
    atoms.calc.write(path, mode="all")
    return path


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    binary = sys.argv[1]

    try:
        import gpaw  # noqa: F401
        from gpaw.response.g0w0 import G0W0  # noqa: F401
    except Exception as exc:
        print(f"SKIP: GPAW response stack unavailable ({exc})")
        return 0

    with tempfile.TemporaryDirectory() as workdir:
        # 1. The generated script, straight from the generator under test.
        dump = os.path.join(workdir, "scripts")
        os.mkdir(dump)
        subprocess.run([binary, "--dump", dump], check=True,
                       stdout=subprocess.DEVNULL)
        source = os.path.join(dump, "gw_gpaw_ppa.py")
        if not os.path.exists(source):
            print("FAIL: --dump produced no gw_gpaw_ppa.py")
            return 1

        print("Baseline single point:")
        baseline = write_baseline(workdir)
        check(os.path.exists(baseline), "wrote a .gpw the GW run can inherit")

        # 2. Point it at the real baseline. The dumped script carries the
        #    placeholder path the dumper used; everything else is untouched.
        with open(source) as handle:
            script = handle.read()
        script = script.replace("/jobs/proc_1/single_point.gpw", baseline)
        check(baseline in script, "baseline path substituted")
        run_py = os.path.join(workdir, "gw.py")
        with open(run_py, "w") as handle:
            handle.write(script)
        shutil.copy(os.path.join(dump, "calango_log.py"), workdir)

        # 3. Run it the way the job runner would.
        print("Generated G0W0 script:")
        completed = subprocess.run([sys.executable, run_py], cwd=workdir,
                                   capture_output=True, text=True)
        if completed.returncode != 0:
            print("  FAIL the generated script exited "
                  f"{completed.returncode}")
            print(completed.stdout[-3000:])
            print(completed.stderr[-3000:])
            return 1
        check(True, "runs to completion")
        check("CALANGO_DONE" in completed.stdout, "reports completion")

        # 4. The result.
        with open(os.path.join(workdir, "gw.json")) as handle:
            summary = json.load(handle)

        print("Quasiparticle result:")
        for key in ("engine", "frequency_treatment", "dft_gap_eV", "gw_gap_eV",
                    "gap_renormalization_eV", "dft_eigenvalues_eV",
                    "qp_eigenvalues_eV"):
            check(key in summary, f"gw.json carries {key}")

        dft_gap = summary["dft_gap_eV"]
        gw_gap = summary["gw_gap_eV"]
        renorm = summary["gap_renormalization_eV"]
        print(f"    DFT gap {dft_gap:.3f} eV -> GW gap {gw_gap:.3f} eV "
              f"({renorm:+.3f} eV)")
        check(dft_gap is not None and dft_gap > 0.0,
              "silicon comes out as a semiconductor at the DFT level")
        # The headline physics. Loose bounds on purpose: these parameters are
        # far from converged, so the SIGN and the ORDER are what mean anything.
        check(renorm > 0.0, "G0W0 opens the gap rather than closing it")
        check(0.1 < renorm < 3.0,
              f"the opening is of the expected order ({renorm:.3f} eV)")

        dft_rows = summary["dft_eigenvalues_eV"]
        qp_rows = summary["qp_eigenvalues_eV"]
        check(len(dft_rows) == len(qp_rows)
              and all(len(a) == len(b) for a, b in zip(dft_rows, qp_rows)),
              "DFT and QP matrices are the same shape, band for band")
        check(len(dft_rows) > 0 and len(dft_rows[0]) > 0,
              "the corrected window is not empty")

    print("\nAll GW checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
