#!/usr/bin/env python3
"""End-to-end validation of the Energy Diagrams module.

Runs the GENERATED script (calango_script_test's --dump-energy-diagram,
built from EnergyDiagramScriptGenerator — the same one the wizard drives)
against two real baselines:

  H2O   -- HOMO/LUMO identification, and a DISCRIMINATION check on the
           transition-dipole computation: among the frontier occupied ->
           virtual pairs, the computed oscillator strengths are NOT all the
           same order of magnitude, and at least one comes out on each side
           of the allowed/forbidden threshold — real molecular selection
           rules single some pairs out, and a module that always reported
           "allowed" (or always "forbidden") would fail this trivially.

  NH3   -- degeneracy detection. NH3 is C3v; group theory guarantees a
           doubly-degenerate 'e'-symmetry valence level. A real-space grid
           (FD or PW) does not exactly respect a 3-fold rotation, so the
           level splits by a small, GRID-DEPENDENT amount rather than being
           bit-identical — the check is against the tolerance the module
           itself exposes (EnergyDiagramConfig::degeneracyToleranceEv), not
           against exact equality.

  H2O with many empty bands -- REGRESSION test for the level-diagram
           display bug: a completed SCF with GPAW's nbands set to request
           18 empty bands (22 total, the same scale a real GPAW run's own
           `nbands` default routinely reaches) used to hand the LEVEL
           DIAGRAM every single one of them, unbounded — 18 virtual states
           spread over a real ~8 eV span dominated the widget's shared
           linear energy axis and rendered as an unreadable, densely
           overlapping cluster (confirmed by rendering the actual
           EnergyDiagramViewer widget against this exact baseline). Checks
           that the level diagram is now bounded to the configured
           occupied/virtual window regardless of how many bands the SCF
           actually converged, while HOMO/LUMO/gap stay correct (unaffected
           by the window) and levels_total still reports the true band
           count.

Skips cleanly (exit 0) without GPAW.

Usage:  energy_diagram_benchmark.py <calango_script_test binary>
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


def write_baseline(workdir, name, molecule_name, **gpaw_kwargs):
    from ase.build import molecule
    from gpaw import GPAW

    # Each baseline gets its OWN subdirectory: the restart script globs its
    # directory for '*.gpw' and takes the alphabetically-first match, so two
    # baselines sharing one directory silently cross-contaminate (h2o.gpw
    # sorts before nh3.gpw and would be the one actually read back).
    baseline_dir = os.path.join(workdir, name)
    os.makedirs(baseline_dir, exist_ok=True)
    atoms = molecule(molecule_name, vacuum=4.0)
    calc = GPAW(mode="fd", h=0.25, xc="PBE",
               occupations={"name": "fermi-dirac", "width": 0.01},
               txt=os.path.join(baseline_dir, "scf.txt"), **gpaw_kwargs)
    atoms.calc = calc
    atoms.get_potential_energy()
    calc.write(os.path.join(baseline_dir, "single_point.gpw"), mode="all")
    return baseline_dir


def run_generated(binary, workdir, baseline_dir, tag, tolerance_eV=None):
    dump = os.path.join(workdir, f"scripts_{tag}")
    os.mkdir(dump)
    args = [binary, "--dump-energy-diagram", dump, baseline_dir]
    if tolerance_eV is not None:
        args.append(f"{tolerance_eV:.6f}")
    subprocess.run(args, check=True)
    source = os.path.join(dump, "energy_diagram.py")
    if not os.path.exists(source):
        print(f"FAIL: --dump-energy-diagram produced no script for {tag}")
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
    with open(os.path.join(run_dir, "energy_diagram.json")) as handle:
        return json.load(handle)


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    binary = sys.argv[1]

    try:
        import numpy as np
        import gpaw  # noqa: F401
        from gpaw.utilities.dipole import dipole_matrix_elements_from_calc  # noqa: F401
    except Exception as exc:
        print(f"SKIP: GPAW dipole stack not available ({exc})")
        return 0

    with tempfile.TemporaryDirectory() as workdir:
        # --- H2O: HOMO/LUMO + dipole checks --------------------------
        print("H2O baseline:")
        h2o_dir = write_baseline(workdir, "h2o", "H2O")
        check(os.path.exists(os.path.join(h2o_dir, "single_point.gpw")),
              "wrote a .gpw the run can inherit")

        summary = run_generated(binary, workdir, h2o_dir, "h2o")
        check(summary is not None, "the generated script runs to completion")
        if summary is not None:
            homo = summary["homo"]
            lumo = summary["lumo"]
            check(homo is not None and lumo is not None,
                  "HOMO and LUMO were both identified")
            if homo and lumo:
                check(homo["occupation"] > 0.5, "HOMO is occupied")
                check(lumo["occupation"] <= 0.5, "LUMO is not")
                check(lumo["energy_eV"] > homo["energy_eV"],
                      "LUMO lies above HOMO")
                print(f"    HOMO {homo['energy_eV']:.3f} eV, "
                      f"LUMO {lumo['energy_eV']:.3f} eV, "
                      f"gap {summary['gap_eV']:.3f} eV")

            ref = summary.get("reference_dipole_eA")
            check(ref is not None, "the reference dipole moment was written")
            if ref is not None:
                print(f"    ground-state dipole moment (GPAW, "
                      f"independent of the transitions code) = {ref}")

            # Discrimination among frontier transitions.
            strengths = [t["oscillator_strength"]
                        for t in summary["transitions"]]
            check(len(strengths) >= 4,
                  "enough frontier transitions were tabulated to say "
                  "anything about their spread")
            if strengths:
                lo, hi = min(strengths), max(strengths)
                print(f"    oscillator strengths span [{lo:.2e}, {hi:.2e}] "
                      f"over {len(strengths)} transitions")
                check(hi > 10.0 * max(lo, 1e-8),
                      "the computed oscillator strengths are NOT all one "
                      "order of magnitude — the module discriminates "
                      "allowed from forbidden character rather than "
                      "reporting a flat, uninformative set")
                allowed_count = sum(1 for t in summary["transitions"]
                                    if t["allowed"])
                check(0 < allowed_count < len(strengths),
                      "at least one transition is classified allowed and "
                      "at least one forbidden — not all-or-nothing")

        # --- NH3: degeneracy detection --------------------------------
        print("NH3 baseline (degeneracy detection):")
        nh3_dir = write_baseline(workdir, "nh3", "NH3")
        # A generous tolerance: real-space grids do not exactly respect a
        # 3-fold rotation, so the 'e' level splits by a small but nonzero,
        # grid-dependent amount rather than the bit-identical energies a
        # fresh diagonalization on a perfectly symmetric operator would
        # give — 0.057 eV at this h=0.25 grid (measured directly against
        # calc.get_eigenvalues(), independent of this module's own
        # grouping code), comfortably inside 0.08 eV and comfortably
        # smaller than the >4 eV gap to the next distinct level.
        summary = run_generated(binary, workdir, nh3_dir, "nh3",
                                tolerance_eV=0.08)
        check(summary is not None, "the generated script runs to completion")
        if summary is not None:
            degeneracies = [g["degeneracy"] for g in summary["groups"]]
            max_degeneracy = max(degeneracies) if degeneracies else 1
            print(f"    level degeneracies: {degeneracies}")
            check(max_degeneracy >= 2,
                  "NH3 (C3v) has at least one doubly-degenerate 'e'-"
                  "symmetry valence level, and the grouping finds it")

        # --- H2O, many empty bands: level-diagram windowing regression --
        print("H2O baseline with 18 empty bands (level-diagram bound):")
        wide_dir = write_baseline(workdir, "h2o_wide", "H2O", nbands=-18)
        summary = run_generated(binary, workdir, wide_dir, "h2o_wide")
        check(summary is not None, "the generated script runs to completion")
        if summary is not None:
            total = summary.get("levels_total")
            shown = len(summary["levels"])
            groups = len(summary["groups"])
            print(f"    levels_total={total}  levels shown={shown}  "
                  f"groups={groups}")
            check(total is not None and total >= 20,
                  "the SCF really did converge a large empty-band manifold "
                  "(levels_total reports the true, unwindowed count)")
            # Default window: 5 occupied below HOMO (H2O only has 4
            # occupied bands total, so all of them) + 5 virtual above LUMO.
            check(shown <= 10,
                  "the level diagram is bounded to the frontier window "
                  "instead of showing every one of the >=20 stored bands "
                  "(this is the bug: it used to equal levels_total)")
            check(shown < total,
                  "and is strictly smaller than the full stored count for "
                  "this baseline — the window is actually doing something, "
                  "not accidentally wide enough to be a no-op")
            check(groups == shown,
                  "no accidental degeneracy grouping shrank the count "
                  "further at this (default, tight) tolerance — every "
                  "shown level is its own group here")

            homo = summary["homo"]
            lumo = summary["lumo"]
            check(homo is not None and lumo is not None
                  and homo["occupation"] > 0.5 and lumo["occupation"] <= 0.5,
                  "HOMO/LUMO occupation is still read correctly with the "
                  "wide band count")
            # The true HOMO/LUMO must be UNCHANGED by the window — same
            # physical answer as the plain H2O baseline above (up to the
            # different nbands not otherwise perturbing the converged
            # occupied manifold).
            check(abs(summary["gap_eV"] - 6.548) < 0.05,
                  "the HOMO-LUMO gap is exactly what the plain H2O "
                  "baseline (same geometry/grid, default band count) gave "
                  f"— the window must not be able to move it "
                  f"(got {summary['gap_eV']:.3f} eV)")

    print("\nAll Energy Diagram checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
