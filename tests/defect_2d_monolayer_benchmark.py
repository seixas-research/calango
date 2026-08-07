#!/usr/bin/env python3
"""End-to-end run of the Charged Defects in 2D Materials module.

The companion test, defect_2d_correction_test.py, checks the CORRECTION against
closed forms without ever touching GPAW. This one checks the other half — that
the generated script runs at all against real `.gpw` files, that GPAW accepts a
charged slab through `calc.new(charge=...)`, and that what comes out is a
diagram the viewer can read. Neither test substitutes for the other: a correct
correction inside a script that dies on restart ships nothing, and a script that
runs perfectly while applying a wrong prefactor is worse than one that crashes.

The system is a 2x2 hexagonal boron nitride monolayer with one nitrogen
removed, in enough vacuum to be a slab. BN rather than graphene because the
diagram needs a GAP to place transition levels in, and graphene has none: with
a metallic host the Fermi-level axis has zero width and every check downstream
is vacuous.

Physics asserted, all of it sign-and-order rather than value (these parameters
are deliberately cheap plumbing parameters, not converged ones):

  * The correction is POSITIVE for every q != 0 and ZERO for q = 0. A periodic
    array of like charges plus a neutralizing background is over-bound relative
    to the isolated defect, so removing that binding raises the formation
    energy. A negative correction means the two solves are subtracted the wrong
    way round.
  * It goes as q^2 across the charge states actually run, since it is an
    electrostatic self-energy — checked on the run's own numbers, not on the
    model in isolation.
  * The correction MATTERS: it must move at least one formation energy by more
    than a few meV, or the module is decorative.
  * The lines have slope exactly q, which is what lets a charge state be read
    off the diagram, and the envelope is the pointwise minimum of them.

Skips cleanly (exit 0) without GPAW.

Usage:  defect_2d_monolayer_benchmark.py <calango_script_test binary>
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


def write_baselines(workdir):
    """The two Single-Point Calculations the wizard inherits, as it produces
    them: a pristine monolayer and the same cell with one atom removed."""
    import numpy as np
    from ase import Atoms
    from gpaw import GPAW, PW

    # The honeycomb built by hand rather than via ase.build: the sheet has to
    # be genuinely flat and the vacuum has to be the c axis, and neither is
    # guaranteed by a builder shortcut.

    a = 2.51
    vacuum = 12.0
    cell = np.array([[a, 0.0, 0.0],
                     [-a / 2.0, a * np.sqrt(3.0) / 2.0, 0.0],
                     [0.0, 0.0, vacuum]])
    sheet = Atoms("BN",
                  scaled_positions=[[1 / 3, 2 / 3, 0.5],
                                    [2 / 3, 1 / 3, 0.5]],
                  cell=cell, pbc=[True, True, True])
    host = sheet.repeat((2, 2, 1))

    common = dict(mode=PW(200.0), xc="PBE", kpts=(2, 2, 1),
                  occupations={"name": "fermi-dirac", "width": 0.05})

    host.calc = GPAW(txt=os.path.join(workdir, "host.txt"), **common)
    host.get_potential_energy()
    host_path = os.path.join(workdir, "host.gpw")
    host.calc.write(host_path, mode="all")

    # One nitrogen removed: the vacancy the diagram is about. Its index is
    # needed downstream to place the model charge, so it is returned.
    defect = host.copy()
    nitrogen = [i for i, s in enumerate(defect.get_chemical_symbols())
                if s == "N"]
    removed = nitrogen[0]
    del defect[removed]
    defect.calc = GPAW(txt=os.path.join(workdir, "defect.txt"), **common)
    defect.get_potential_energy()
    defect_path = os.path.join(workdir, "defect.gpw")
    defect.calc.write(defect_path, mode="all")
    # The site index in the DEFECT cell that sits nearest the vacancy: any atom
    # in the sheet will do, because only its coordinate along the normal is
    # used, and the whole sheet shares it.
    return host_path, defect_path, 0


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    binary = sys.argv[1]

    try:
        import gpaw  # noqa: F401
        import numpy as np
        import scipy.special  # noqa: F401  (the dielectric profile uses erf)
    except Exception as exc:
        print(f"SKIP: GPAW/numpy/scipy unavailable ({exc})")
        return 0

    with tempfile.TemporaryDirectory() as workdir:
        # 1. The generated script, straight from the generator under test.
        dump = os.path.join(workdir, "scripts")
        os.mkdir(dump)
        subprocess.run([binary, "--dump", dump], check=True,
                       stdout=subprocess.DEVNULL)
        source = os.path.join(dump, "charged_defects_2d.py")
        if not os.path.exists(source):
            print("FAIL: --dump produced no charged_defects_2d.py")
            return 1

        print("Baseline single points:")
        host_path, defect_path, site = write_baselines(workdir)
        check(os.path.exists(host_path), "wrote a pristine h-BN monolayer .gpw")
        check(os.path.exists(defect_path),
              "wrote the same cell with an N vacancy")

        # 2. Point it at the real baselines and at a sheet-sized profile. Only
        #    the inputs the wizard would have collected are substituted; the
        #    physics of the script is untouched.
        with open(source) as handle:
            script = handle.read()
        script = script.replace("/jobs/host/single_point.gpw", host_path)
        script = script.replace("/jobs/defect/single_point.gpw", defect_path)
        script = script.replace("DEFECT_INDEX = 0", f"DEFECT_INDEX = {site}")
        # h-BN: eps_par ~ 4.9, eps_perp ~ 2.5, interlayer spacing 3.33 A.
        script = script.replace("EPS_PAR = 6.9", "EPS_PAR = 4.9")
        script = script.replace("EPS_PERP = 2.8", "EPS_PERP = 2.5")
        script = script.replace("THICKNESS = 6.15", "THICKNESS = 3.33")
        check(host_path in script and defect_path in script,
              "both baseline paths substituted")
        run_py = os.path.join(workdir, "run.py")
        with open(run_py, "w") as handle:
            handle.write(script)

        # 3. Run it the way the job runner would.
        print("Generated 2D charged-defect script:")
        completed = subprocess.run([sys.executable, run_py], cwd=workdir,
                                   capture_output=True, text=True)
        if completed.returncode != 0:
            print(f"  FAIL the generated script exited {completed.returncode}")
            print(completed.stdout[-4000:])
            print(completed.stderr[-4000:])
            return 1
        check(True, "runs to completion")
        check("CALANGO_DONE" in completed.stdout, "reports completion")

        result = os.path.join(workdir, "charged_defects_2d.json")
        check(os.path.exists(result), "wrote charged_defects_2d.json")
        if not os.path.exists(result):
            return 1
        with open(result) as handle:
            summary = json.load(handle)

        # 4. The schema the viewer reads. Asserted by key rather than by
        #    eyeballing, because DefectDiagramWindow renders BOTH modules and a
        #    silent rename here shows up as an empty plot, not as an error.
        print("Diagram schema:")
        for key in ("host", "defect", "correction", "charges",
                    "fermi_level_eV", "envelope_eV", "stable_charge",
                    "transitions"):
            check(key in summary, f"charged_defects_2d.json carries {key}")
        correction = summary["correction"]
        check(correction.get("scheme", "").startswith("2D image charge"),
              "names its own scheme rather than passing for FNV")
        check(correction.get("applied") is True, "the correction was applied")

        # 5. The physics.
        print("Corrections:")
        entries = {int(e["charge"]): e for e in summary["charges"]}
        check(0 in entries, "q = 0 was evaluated as the reference")
        check(abs(entries[0]["correction_eV"]) < 1e-12,
              "a neutral defect gets exactly zero correction")

        charged = sorted(q for q in entries if q != 0)
        check(len(charged) >= 2, f"charged states were run ({charged})")
        for q in charged:
            e = entries[q]["correction_eV"]
            terms = entries[q].get("correction_terms", {})
            print(f"    q={q:+d}:  E_corr = {e:+.4f} eV   "
                  f"(isolated {terms.get('isolated_eV', float('nan')):.4f}, "
                  f"periodic {terms.get('periodic_eV', float('nan')):.4f})")
            # Positive: the periodic array is over-bound, and undoing that
            # RAISES the formation energy. A negative value is the two solves
            # subtracted the wrong way round.
            check(e > 0.0, f"q={q:+d}: the correction is positive")
            check(e > 1e-3,
                  f"q={q:+d}: the correction is larger than a rounding error")

        # q^2 scaling, on the run's own numbers rather than on the model in
        # isolation, so a caller that forgot to scale the model charge with q
        # cannot pass by having a correct model.
        ref = min((q for q in charged if q != 0), key=abs)
        for q in charged:
            ratio = entries[q]["correction_eV"] / entries[ref]["correction_eV"]
            expected = (q / ref) ** 2
            check(abs(ratio - expected) < 1e-6,
                  f"q={q:+d}: E_corr/E_corr({ref:+d}) = {ratio:.4f} "
                  f"(expect {expected:.4f})")

        # It has to MATTER. A correction that rounds to nothing at these cell
        # sizes would mean the module reports a number without changing one.
        biggest = max(abs(entries[q]["correction_eV"]) for q in charged)
        check(biggest > 0.01,
              f"the largest correction is {biggest:.4f} eV, i.e. not decorative")

        # 6. The diagram itself.
        print("Diagram:")
        fermi = np.array(summary["fermi_level_eV"])
        check(len(fermi) > 2, "the Fermi-level axis has samples")
        gap = summary["host"]["E_gap_eV"]
        print(f"    host gap = {gap:.3f} eV, "
              f"{len(summary['transitions'])} transition level(s)")
        check(gap > 0.1, "the host has a gap to place the levels in")

        for q, entry in entries.items():
            line = np.array(entry["formation_energy_eV"])
            check(len(line) == len(fermi),
                  f"q={q:+d}: the line is sampled on the Fermi axis")
            if len(fermi) > 1:
                slope = (line[-1] - line[0]) / (fermi[-1] - fermi[0])
                # Slope q exactly: this is what lets the charge state be read
                # off the diagram, and it fails if E_F enters anywhere else.
                check(abs(slope - q) < 1e-6,
                      f"q={q:+d}: the line has slope {slope:+.4f}")

        envelope = np.array(summary["envelope_eV"])
        stack = np.array([entries[q]["formation_energy_eV"] for q in entries])
        check(np.allclose(envelope, stack.min(axis=0)),
              "the envelope is the pointwise minimum of the lines")
        stable = np.array(summary["stable_charge"])
        check(len(stable) == len(fermi) and set(stable) <= set(entries),
              "every envelope point names a charge state that was run")

    print("\nAll 2D charged-defect checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
