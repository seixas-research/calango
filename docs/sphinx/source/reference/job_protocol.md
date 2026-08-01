# Job protocol

A generated script and the GUI communicate over two channels: **markers on
standard output** (parsed line-by-line, in real time, by the job runner)
and **JSON files in the job directory** (replaced atomically, polled by the
Results panel). Both are plain conventions — if you hand-edit `run.py` or
write your own script, emitting them keeps the panel live; omitting them
costs you the plots, never the run.

---

## Stdout markers

One marker per line, space-separated payload:

| Marker | Payload | Consumer |
|---|---|---|
| `CALANGO_PROGRESS <step> <total>` | integers | Progress bar |
| `CALANGO_ENERGY <step> <value>` | energy in eV | Energy plot |
| `CALANGO_TEMP <step> <value>` | temperature in K | Temperature plot |
| `CALANGO_FMAX <step> <value>` | max force component in eV/Å | Force plot (optimizations and MD) |
| `CALANGO_PRESSURE <step> <value>` | $-\mathrm{tr}(\sigma)/3$ in GPa | Pressure plot (barostatted MD only) |
| `CALANGO_TARGET_TEMP <value>` | K | Thermostat reference line (never emitted for NVE) |
| `CALANGO_TARGET_PRESSURE <value>` | GPa | Barostat reference line (NPT/NPH only) |
| `CALANGO_CELL <9 floats>` | row-major 3×3 cell | Opens a streamed-frame block |
| `CALANGO_FRAME <natoms> [FV]` | atom count, then `natoms` atom lines | Live trajectory streaming |
| `CALANGO_RESULT <key>=<value> …` | free-form summary | Final status line; also announces artifacts (e.g. `CALANGO_RESULT density_cube=<file> <label>`) |

The frame stream is a small state machine: a `CALANGO_CELL` line declares
the cell, `CALANGO_FRAME n` declares the atom count, and the next *n* lines
carry one atom each. With the optional `FV` flag, each atom line carries
force and velocity components after the position, and the finished frame
arrives in the viewport with both vector fields attached. **Atom lines are
consumed by the parser, not echoed to the log** — a thousand-atom MD run
does not scroll its geometry through the console.

Everything that is not a marker passes through as ordinary output: stdout
to the console view, stderr flagged as such.

---

## JSON metric files

The same script also maintains three files in its working directory,
written by an embedded, dependency-free logging block (standard library
only — the script stays a single portable file):

| File | Shape | Consumer |
|---|---|---|
| `metrics.json` | `{"metrics": [{"step": n, …numeric fields…}], "progress": {"step", "total", "percent"}}` | Results panel — polled live for the metric plots and progress bar |
| `log.json` | `{"log": [{"level": "...", "message": "..."}]}` | Results panel Log tab |
| `warnings.log` | plain text | Python warnings (ASE, PyTorch, SciPy, GPAW …), kept out of stdout |

Every write goes through *write-to-`.tmp`, then `os.replace`* — *atomic
replacement*, so a poll never reads half a file. The warning filter is
`"default"`, recording each distinct warning once per location.

Why two channels? The markers give sub-second interactivity for a local
process; the JSON files survive it — they are what a **remote** job leaves
behind for collection, and what reopening a project restores the plots
from. Delete the logging block from a script and it still runs; you lose
the telemetry, not the physics.

---

## Job directory layout

Each run gets a private directory:

```text
.calango_tmp/proc_<id>/        # beside the .calproj (saved projects), or
                               # the simulations dir from Preferences
├── run.py                     # the whole program — self-contained
├── structure.extxyz           # staged input geometry
├── calculator.json            # provenance sidecar (simulation wizards only)
├── metrics.json  log.json  warnings.log
└── <result artifacts>         # bands.json, opt.traj, *.cube, …
```

`run.py` alone is the whole program: its logging is embedded, so remote
submission is "upload this directory, run this file" on a machine where
Calango has never been installed. Remote runs add `job.sh` (the scheduler
wrapper) and capture `calango_job.out` / `calango_job.err`.

The runner launches the command **through the system shell** with the job
directory as CWD; the selected Python environment's `bin/` is prepended to
`PATH` (and `CONDA_PREFIX` set for conda layouts), so solver binaries
installed beside the interpreter — `pw.x`, `siesta` — win over global ones.
Per-engine variables such as `ASE_ESPRESSO_COMMAND` are injected the same
way.

---

## calculator.json provenance

When a simulation wizard stages a job, it writes a `calculator.json`
sidecar describing the calculator it configured:

```json
{
  "engine": "GPAW", "engine_kind": 4,
  "xc": "PBE", "mode": "PW", "cutoff_ev": 500.0,
  "grid_spacing": 0.2, "kpts": [4, 4, 4], "symmetry_off": false,
  "python": "/path/to/env/bin/python", "conda_env": "gpaw_env"
}
```

This is **how baseline-inheriting wizards find and adopt an SCF**: Optics,
GW, Wannier Functions, Born Effective Charges, and Raman/IR list completed
runs, read this sidecar from the chosen directory, and inherit the engine,
XC functional, mode/cutoff, k-grid, and — critically — the interpreter and
conda environment, so the follow-up runs where the baseline ran. A baseline
without the sidecar is flagged in the wizard rather than silently
re-parameterized. The sidecar is written only when a launcher supplies it
and cleared between stagings, so an unrelated job never carries a stale
copy.

The physical baseline artifacts travel separately: GPAW workflows restart
from the run's `.gpw` file, VASP ones read `CHGCAR`/`AECCAR*`, QE ones
export through `pp.x`.

---

## Completion semantics

The runner reports `finished(exitCode, crashed)`: **success is exit code 0
and no crash** — a Python traceback that kills the script marks the process
failed regardless of which markers were seen. On completion, the process
entry offers viewers based on which result files the directory actually
contains ({doc}`/reference/file_formats`); a `CALANGO_RESULT` line, when
present, becomes the one-line summary shown for the finished job.

One local job runs at a time; further submissions **queue** rather than
being refused, each with its inputs captured at submit time — editing the
structure while a job waits does not change what the queued job will run.
Termination is polite first ({guilabel}`Stop` sends terminate, then
escalates to kill after a grace period), and a terminated job is marked
crashed, not completed.
