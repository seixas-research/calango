#!/usr/bin/env python3
"""Yambo G0W0 orchestration test, against stand-in executables.

STATUS: WRITTEN BUT NEVER RUN, and deliberately NOT registered in CMakeLists.
Finish it before trusting it — it has not been debugged even once, so treat a
failure as more likely a bug in this file than in the pipeline.

Yambo is an external MPI code that is not installed here, so the pipeline
cannot be validated for real. What CAN be validated is everything Calango
controls: the step order, the working directory each step runs in, the SAVE/
database copy, the input patching, the MPI invocation, the report parsing and
the gw.json that comes out.

So the generated script is run for real against stand-in `p2y`, `yambo` and
`mpirun` executables placed on PATH. They record how they were called and
produce the artifacts the real tools would, which lets the test assert on the
orchestration itself rather than on a transcription of it.

What this does NOT prove: that real Yambo accepts these flags, or that its
report columns are what we expect (that is tests/gw_qp_parser_test.py's job,
and ultimately the first real run's). It proves the plumbing between them.

Usage:  yambo_pipeline_test.py <calango_script_test binary>
"""
import json
import os
import stat
import subprocess
import sys
import tempfile

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


# The stand-ins. Each logs its argv and cwd to calls.log so the test can assert
# on the sequence, then produces what the real tool would.
P2Y = '''#!/usr/bin/env python3
import os, sys
with open(os.environ["CALLS_LOG"], "a") as log:
    log.write(f"p2y|{os.getcwd()}|{' '.join(sys.argv[1:])}\\n")
# p2y writes a SAVE/ database into its working directory.
os.makedirs("SAVE", exist_ok=True)
with open(os.path.join("SAVE", "ns.db1"), "w") as handle:
    handle.write("fake yambo database\\n")
'''

YAMBO = '''#!/usr/bin/env python3
import os, sys
args = sys.argv[1:]
with open(os.environ["CALLS_LOG"], "a") as log:
    log.write(f"yambo|{os.getcwd()}|{' '.join(args)}\\n")

if os.environ.get("YAMBO_FAIL_ON_RUN") and "-J" in args:
    sys.stderr.write("fake yambo: deliberate failure\\n")
    sys.exit(1)

if "-g" in args:
    # Input generation: write the input file named by -F, with the keys the
    # real tool emits (including the two the script patches).
    target = args[args.index("-F") + 1]
    with open(target, "w") as handle:
        handle.write("gw0                          # [R] GW\\n")
        handle.write("EXXRLvcs= 1000          RL    # [XX] cutoff\\n")
        handle.write("NGsBlkXp= 1             RL    # [Xp] block size\\n")
        handle.write("%QPkrange\\n 1|2|1|8|\\n%\\n")
elif "-J" in args:
    # The run: record the patched input so the test can prove the patch
    # reached the tool, then write a quasiparticle report.
    source = args[args.index("-F") + 1]
    with open(source) as handle:
        text = handle.read()
    with open(os.environ["PATCHED_INPUT"], "w") as handle:
        handle.write(text)
    label = args[args.index("-J") + 1]
    with open(f"o-{label}.qp", "w") as handle:
        handle.write("#\\n")
        handle.write("# K-point   Band     Eo [eV]   E-Eo [eV]  Sc|Eo [eV]\\n")
        handle.write("#\\n")
        handle.write("  1         4       -0.400000  -0.300000   -1.0\\n")
        handle.write("  1         5        1.000000   0.500000    2.0\\n")
        handle.write("  2         4       -0.900000  -0.200000   -1.1\\n")
        handle.write("  2         5        1.400000   0.600000    2.1\\n")
else:
    # Database initialization.
    with open("initialized", "w") as handle:
        handle.write("ok\\n")
'''

MPIRUN = '''#!/usr/bin/env python3
import os, subprocess, sys
args = sys.argv[1:]
with open(os.environ["CALLS_LOG"], "a") as log:
    log.write(f"mpirun|{os.getcwd()}|{' '.join(args)}\\n")
# Strip "-np N" and exec the rest, like a real launcher would.
if args and args[0] == "-np":
    args = args[2:]
sys.exit(subprocess.run(args).returncode)
'''


def install_stub(bindir, name, source):
    path = os.path.join(bindir, name)
    with open(path, "w") as handle:
        handle.write(source)
    os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC | stat.S_IXGRP
             | stat.S_IXOTH)


def build_script(binary, workdir, baseline):
    """The generated Yambo script, pointed at `baseline`."""
    dump = os.path.join(workdir, "scripts")
    os.mkdir(dump)
    subprocess.run([binary, "--dump", dump], check=True,
                   stdout=subprocess.DEVNULL)
    source = os.path.join(dump, "gw_yambo_ppa.py")
    if not os.path.exists(source):
        raise SystemExit("--dump produced no gw_yambo_ppa.py")
    with open(source) as handle:
        script = handle.read()
    script = script.replace("/jobs/proc_2", baseline)
    run_py = os.path.join(workdir, "gw.py")
    with open(run_py, "w") as handle:
        handle.write(script)
    return run_py


def run_pipeline(binary, workdir, fail_on_run=False):
    """Set up a fake QE baseline + stand-in tools and run the script."""
    bindir = os.path.join(workdir, "bin")
    os.makedirs(bindir)
    install_stub(bindir, "p2y", P2Y)
    install_stub(bindir, "yambo", YAMBO)
    install_stub(bindir, "mpirun", MPIRUN)

    # A completed Quantum ESPRESSO run: a job directory holding a *.save.
    baseline = os.path.join(workdir, "baseline")
    os.makedirs(os.path.join(baseline, "si.save"))
    with open(os.path.join(baseline, "si.save", "data-file.xml"), "w") as h:
        h.write("<qes/>\n")

    job = os.path.join(workdir, "job")
    os.makedirs(job)
    run_py = build_script(binary, workdir, baseline)

    env = dict(os.environ)
    env["PATH"] = bindir + os.pathsep + env["PATH"]
    env["CALLS_LOG"] = os.path.join(workdir, "calls.log")
    env["PATCHED_INPUT"] = os.path.join(workdir, "patched.in")
    if fail_on_run:
        env["YAMBO_FAIL_ON_RUN"] = "1"

    completed = subprocess.run([sys.executable, run_py], cwd=job, env=env,
                               capture_output=True, text=True)
    calls = []
    if os.path.exists(env["CALLS_LOG"]):
        with open(env["CALLS_LOG"]) as handle:
            calls = [line.strip().split("|") for line in handle if line.strip()]
    return completed, calls, job, env


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    binary = sys.argv[1]

    with tempfile.TemporaryDirectory() as workdir:
        completed, calls, job, env = run_pipeline(binary, workdir)

        print("Pipeline runs to completion:")
        if completed.returncode != 0:
            print(f"  FAIL exited {completed.returncode}")
            print(completed.stdout[-3000:])
            print(completed.stderr[-3000:])
            return 1
        check(True, "the generated script completes")
        check("CALANGO_DONE" in completed.stdout, "reports completion")

        print("Step order and working directories:")
        names = [c[0] for c in calls]
        check(names[:2] == ["p2y", "yambo"],
              "p2y runs first, then yambo initializes the database")
        # p2y writes into its cwd, so it MUST run inside the .save — running it
        # in the job directory would leave no database to copy.
        check(calls[0][1].endswith("si.save"), "p2y runs inside the .save")
        check(calls[1][1] == os.path.realpath(job)
              or os.path.realpath(calls[1][1]) == os.path.realpath(job),
              "yambo initializes in the job directory")
        check(os.path.isdir(os.path.join(job, "SAVE")),
              "the SAVE database is copied into the job directory")

        generation = [c for c in calls if "-g" in c[2].split()]
        check(len(generation) == 1, "the input is generated exactly once")
        check("-g n -p p" in generation[0][2],
              "plasmon-pole G0W0 input requested")

        print("MPI invocation:")
        check(any(c[0] == "mpirun" for c in calls),
              "the run goes through mpirun when cores > 1")
        mpirun = next(c for c in calls if c[0] == "mpirun")
        check(mpirun[2].startswith("-np 8"), "the requested rank count is used")

        print("Input patching reaches the tool:")
        with open(env["PATCHED_INPUT"]) as handle:
            patched = handle.read()
        check("EXXRLvcs= 100 eV" in patched,
              "the exchange cutoff is rewritten in place")
        check("NGsBlkXp= 100 eV" in patched,
              "the screening block size is rewritten in place")
        # A duplicate key would silently lose to the first occurrence, so the
        # patch must REPLACE, not append.
        check(patched.count("EXXRLvcs") == 1,
              "no duplicate key is appended alongside the original")
        check("%QPkrange" in patched,
              "the rest of the generated input survives the patch")

        print("Result:")
        with open(os.path.join(job, "gw.json")) as handle:
            summary = json.load(handle)
        check(summary["engine"] == "Yambo", "engine recorded as Yambo")
        # From the stand-in report: VBM = max Eo <= 0 = -0.4 -> qp -0.7;
        # CBM = min Eo > 0 = 1.0 -> qp 1.5. So DFT gap 1.4, GW gap 2.2.
        check(abs(summary["dft_gap_eV"] - 1.4) < 1e-9,
              f"DFT gap read from the report ({summary['dft_gap_eV']:.3f} eV)")
        check(abs(summary["gw_gap_eV"] - 2.2) < 1e-9,
              f"GW gap from Eo + (E-Eo) ({summary['gw_gap_eV']:.3f} eV)")
        check(abs(summary["gap_renormalization_eV"] - 0.8) < 1e-9,
              "renormalization is the difference of the two")
        # Two k-points x two bands: the rows must be grouped, not flattened.
        check(summary["qp_eigenvalues_eV"] == [[-0.7, 1.5], [-1.1, 2.0]],
              "eigenvalues are grouped (k-point x band)")

    # -- A failed step must abort, not report stale numbers -----------------
    print("A failing yambo run aborts:")
    with tempfile.TemporaryDirectory() as workdir:
        completed, _, job, _ = run_pipeline(binary, workdir, fail_on_run=True)
        check(completed.returncode != 0, "the script exits non-zero")
        check(not os.path.exists(os.path.join(job, "gw.json")),
              "no gw.json is written from a failed run")

    print("\nAll Yambo orchestration checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
