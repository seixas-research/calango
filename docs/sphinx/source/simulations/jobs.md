# Local job execution

Every calculation Calango launches — a ten-second EMT single-point or a week of
GPAW molecular dynamics — runs as a **separate operating-system process in its
own directory**. The application writes a script, starts a subprocess, and
reads the files that subprocess leaves behind. Nothing about the run lives
inside the GUI process, so a segfaulting solver, a misbehaving MPI stack or an
out-of-memory kill terminates *that job* — never the interface, and never the
structures you have open.

The job is launched through the system shell rather than a direct `exec`,
because the launch command comes from a user-editable template
({menuselection}`Edit --> Preferences`, *Run* page — see
{doc}`/simulations/scripts`): legitimate templates carry leading environment
assignments (`OMP_NUM_THREADS=1 gpaw …`) and redirections (`> pw.out`), which
only a shell interprets. The selected interpreter is *not* executed directly —
it locates the environment whose `bin/` is prepended to `PATH`, so solver
binaries installed beside it (`pw.x`, `siesta`) win over globally installed
ones; for conda-style layouts `CONDA_PREFIX` is set to the environment root as
well. {guilabel}`Kill` in the Results dock terminates the job politely and
escalates to a hard kill after a grace period.

---

## The job directory

Where a run stages depends on whether the session is saved:

| Session state | Jobs root |
|---|---|
| Saved project (`.calproj`) | `.calango_tmp/` next to the project file |
| Unsaved session | The simulations folder ({menuselection}`Edit --> Preferences`, *General*) |

A **saved project keeps its jobs beside the `.calproj`**, so the project stays
self-contained and movable — checkpoints, trajectory dumps and logs travel with
it. Each run gets a `proc_<id>/` subdirectory named after its Process Manager
id; if a reopened project already has a `proc_0/` on disk, a fresh run advances
to the next free `proc_<n>` instead — **an earlier run's results are never
clobbered**. Jobs staged without a process id (remote submissions) use a
`job_<timestamp>` directory instead.

What lands in the directory at submission time:

| File | When it is staged |
|---|---|
| `run.py` | Always — the generated (possibly hand-edited) script |
| `structure.extxyz` | Whenever the run takes an input geometry (almost always; MACE training reads its own dataset instead) |
| `band.extxyz` | NEB and other band jobs — the full image band |
| `configs.extxyz` | Cluster-expansion batches — the decorated configurations the job iterates over |
| `primitive.extxyz` | Band unfolding — the pristine reference cell the supercell's bands are projected onto |
| `calculator.json` | Provenance sidecar written by the simulation wizards — engine, parameters and environment, so a later wizard (MLWF, ELF) can inherit this run as a baseline |

There is nothing else to stage: `run.py` is one self-contained file whose
logging block is embedded rather than imported, so the directory's script plus
geometry *are* the whole job — which is what lets a remote submission upload
the same directory and run it on a machine that has never seen Calango
({doc}`/simulations/scripts`).

% TODO screenshot: Process Manager panel with a mix of queued, running and completed jobs, one row selected showing its proc_ directory
```{figure} /_static/img/exec_jobs_processes.png
:alt: The Processes panel listing local jobs with status and working directory
:width: 92%
:figclass: screenshot

The Processes panel — every background task with its live status and its `proc_<id>/` directory, one click from its results.
```

---

## JSON logging — the GUI reads files, not stdout

Generated scripts do **not** report their metrics by printing to standard
output. Each script's embedded logger writes three JSON files into the job
directory, and the GUI polls them on a timer while the run is live:

| File | Contents | Consumed by |
|---|---|---|
| `metrics.json` | `{"metrics": [{"step", "energy", "temperature", "max_force", "pressure", …}], "progress": {"step", "total", "percent"}}` | The four metric plots and the progress bar |
| `log.json` | `{"log": [{"level", "message"}]}` | Structured events |
| `warnings.log` | Python `warnings` output (ASE, SciPy, PyTorch, GPAW…) | Kept out of the Log tab so it stays readable |

Every write replaces the file atomically (write to `.tmp`, then rename), so
the poller never sees half a file. Because `metrics.json` carries the *full
history*, the plots survive anything stdout-scraping cannot: a reconnected
poll, a reopened project, a run whose stdout was redirected by its own launch
template.

:::{note}
Libraries that print warnings straight to stderr bypass the script's warning
capture. The Log tab filters those lines by pattern (`UserWarning`,
`DeprecationWarning`, `warnings.warn`, …) and appends them to `warnings.log`
instead of coloring them as errors — stderr red is reserved for actual
failures.
:::

---

## The Results dock

The {guilabel}`Results` dock (zone 10) has five tabs. {guilabel}`Log` shows a
status label (*Idle*, *Running…*, *Finished (exit N)*, *Crashed*), a progress
bar, the {guilabel}`Kill` button and the live console — stderr in red, the
start line in blue, a clean exit in green. The other four plot the run's
observables as they arrive:

| Tab | Quantity | Unit | Reference line |
|---|---|---|---|
| {guilabel}`Energy` | Total energy | eV | — |
| {guilabel}`Temperature` | Ionic temperature | K | Thermostat setpoint (dashed) |
| {guilabel}`Force` | Max $\lvert F \rvert$ | eV/Å | — |
| {guilabel}`Pressure` | Scalar pressure $-\mathrm{tr}(\sigma)/3$ | GPa | Barostat setpoint (dashed) |

The dashed setpoint lines make drift away from a thermostat or barostat target
visible at a glance; they appear only for runs that declared one (never for
NVE). Each tab has its own {guilabel}`Export Data…` button writing CSV or
whitespace-separated `.dat` (gnuplot-style `#` header), with the setpoint as
its own column where one exists. The series persist in the project file, and
the process selector above the plots switches the tabs between any run's
history — a live run never overwrites an earlier run's series.

% TODO screenshot: Results dock on the Temperature tab during an MD run, measured trace plus dashed setpoint line, progress bar partially filled
```{figure} /_static/img/exec_jobs_results.png
:alt: Results dock showing the live temperature plot with a dashed setpoint reference line
:width: 92%
:figclass: screenshot

The Temperature tab during a thermostatted MD run — the dashed line is the setpoint; drift from it is the first thing to check.
```

---

## The job queue

Submitting a calculation while another is running does not refuse — it
**appends**. The new job is fully staged immediately (its directory exists,
its script is on disk, and it appears in the Process Manager as *queued*), and
each finishing job pops exactly one from the front of the queue.

Everything a launch needs is captured *at submission time*, because none of it
can be recovered later: the script came from a dialog that is about to be
destroyed, the launch command depends on the engine and the wizard's editable
*Running:* line, and the live-trajectory tab must be seeded from the structure
the run was launched against — not from whatever tab you have wandered to by
the time the queue reaches the entry. A queued job whose directory has been
deleted while it waited (its process removed, or the simulations folder
cleared) is marked failed and skipped rather than launched into a directory
that no longer exists.

---

## Live trajectory streaming

MD runs and relaxations do not make you wait. **The moment the job starts,
Calango opens a trajectory tab marked *(live)*** and streams each newly
computed geometry into it — the script prints `CALANGO_CELL` / `CALANGO_FRAME`
blocks on stdout, and the runner parses them into frames, per-atom forces and
velocities included, so the Vector overlay works *during* the run.

- The timeline grows as frames arrive and follows the newest one.
- Scrub back and the view stays put — following stops so you can inspect while
  the run continues. Measurement, color mapping and representation changes all
  work on the partial trajectory.
- Relaxations stream every step; MD streams every `max(1, md_steps // 400)`
  steps, so a run yields at most about **400 streamed frames** however long it
  is. The complete trajectory is still written to `opt.traj` or `md.traj`
  (plus `md.extxyz` with forces and velocities) in the job directory.
- The input geometry is shown while the first frame is computed but is *not*
  frame 0 — it carries no evaluated forces, and the trajectory starts with the
  run's first real step.

When the job finishes, the *(live)* marker is dropped and the tab becomes an
ordinary trajectory, playhead on the final frame. A run that streamed nothing
(a single-point) drops its placeholder tab instead of leaving a one-frame
timeline behind.

% TODO screenshot: viewport with a "(live)" tab mid-MD, timeline partially filled, atoms with force vectors
```{figure} /_static/img/exec_jobs_live.png
:alt: A live trajectory tab receiving streamed MD frames while the job runs
:width: 92%
:figclass: screenshot

A *(live)* tab during molecular dynamics — frames land as they are computed, and the timeline follows the newest one until you scrub.
```

---

## When a job finishes

Calango inspects the job directory and opens the right viewer — **the marker
file is the test**, not the job's label. A run that wrote `gw.json` produced
quasiparticle data whatever it was called, which keeps the dispatch honest
when a directory is reused or a process renamed. Among the artifacts checked:
`bands.json` (band/PDOS viewer), `phonon_band.json`, `optics.json`,
`wannier.json`, `single_point.json`, `geometry_optimization.json`,
`cutoff_convergence.json` / `kpoints_convergence.json`, `fermi_surface.json`,
`topology.json`, `gw.json` — and every `.cube` file the run wrote is
registered in the Volumetric Data dock. Failing all of those, a trajectory
(`opt.traj`, `md.traj`) opens in a new tab at its final step, and a final
geometry (`optimized.extxyz`, `md_final.extxyz`) is offered for loading.

The same dispatch backs the Process Manager's right-click menu: every viewer
whose marker file exists in that run's directory is listed, alongside
{guilabel}`Load Result into Workspace`, {guilabel}`View ASE Script…` and
{guilabel}`Open Folder`. Deleting a process (Delete / Backspace on the row)
stops it if running and purges its `proc_<id>/` directory after confirmation.

:::{important}
The Process Manager id also indexes the buffered metrics and console log, so a
completed run's plots can be reloaded from the Results selector for the rest
of the session — and, in a saved project, in the next one.
:::
