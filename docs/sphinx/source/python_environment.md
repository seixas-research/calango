# The Python environment

Calango embeds a CPython interpreter and drives ASE through it. Almost everything that touches chemistry — reading a CIF, building a slab, generating a job script — goes through that interpreter, so **pointing Calango at a Python that has ASE installed is the single most important piece of setup**. This page covers how the interpreter is chosen, how to verify it, what breaks without ASE, the per-feature dependency table, and how simulation jobs pick their *own* interpreters independently of the embedded one.

---

## How the interpreter is chosen

An embedded interpreter does not inherit an activated virtualenv the way a shell does — `sys.executable` would be the Calango binary itself, and Python would fall back to the base installation without ASE. Calango therefore resolves an interpreter explicitly, in this order:

1. **`$CALANGO_PYTHON`** — an explicit path to a Python executable. Highest priority; use it to override everything else.
2. **`$VIRTUAL_ENV`** — an activated virtual environment (Calango uses `$VIRTUAL_ENV/bin/python`), when Calango was launched from that shell.
3. **A Python bundled inside the installed application** — `Calango.app/Contents/Resources/python` on macOS, `<prefix>/lib/calango/python` on Linux — when the installer was built with one.
4. **The interpreter the build was configured against** at CMake time.

The chosen path is handed to CPython as its executable, so normal `pyvenv.cfg` venv activation applies — the environment's `site-packages` is what gets imported.

---

## Verifying with `--probe-python`

Before opening any structure, confirm the interpreter is the one you expect:

```bash
calango --probe-python
```

This runs headlessly — no window — and prints the resolved interpreter, the Python version and the ASE version:

```text
interpreter: /home/user/calango/.venv/bin/python
python:      3.12.4
ase:         3.29.0
```

The exit status is **0 only when ASE imports successfully**; on failure the `ase:` line reads `NOT AVAILABLE` and the import error is printed below it. This is the first thing to run when structure loading fails, and it is also the built-in package test of the Conda recipe.

---

## What happens without ASE

If ASE cannot be imported at launch, **Calango still starts** — so you can reach Preferences and fix the configuration — but structure I/O and job features are disabled, and a warning dialog says exactly what to do:

```text
Python started, but ASE could not be imported — structure I/O and job
features will be disabled.

Point Calango at an interpreter that has ASE, e.g.:
    export CALANGO_PYTHON=/path/to/.venv/bin/python
or activate that virtualenv before launching.
Diagnose with:  calango --probe-python
```

---

## The environment file and `MP_API_KEY`

At launch Calango reads an environment file — `~/.env` by default — and exports `MP_API_KEY`, the Materials Project API key, into the process environment. A key already present in your shell takes precedence at startup.

Point Calango at a different file, or re-read it on demand, in {menuselection}`Edit --> Preferences…` ({kbd}`Ctrl+P`). The dialog reports whether the file exists and whether it actually defines a key; the same controls are mirrored in the {guilabel}`Materials Project` tab of the database browser ({menuselection}`Build --> From Database…`).

---

## Dependencies by feature

`ase` and `numpy` are the hard floor. Everything else enables a feature subset and can be added when the feature is first needed:

| Package | Minimum version | Enables |
|---|---|---|
| `ase` | 3.22 | Everything — structure I/O, building, calculators, band paths |
| `numpy` | 1.23 | Everything — arrays and linear algebra |
| `spglib` | 2.0 | Symmetry detection (Structure panel), Raman/IR mode activity |
| `scipy` | 1.9 | Interpolation and signal processing helpers |
| `matplotlib` | 3.6 | Offline figure export |
| `paramiko` | 3.0 | HPC panel (SSH/SFTP, cluster submission) |
| `pillow` | — | GIF animation export |
| `imageio` + `imageio-ffmpeg` | 2.28 / 0.4 | Video animation export — every format in the dialog (MP4, MOV, WebM, …) fails at import without the ffmpeg wheel |
| `gpaw` + `gpaw-data` | 24.1 | DFT: single points, geometry optimization, bands, PDOS, and the post-SCF workflows built on them |
| `mace-torch` (conda: `pymace`) | 0.3 | MACE machine-learning potentials (pulls `torch` ≥ 2.0, `e3nn`, …) |
| `phonopy` | 2.20 | Symmetry-reduced phonon displacements, LO–TO splitting (NAC), Γ-mode irrep labels |
| `dftd4` (conda: `dftd4-python`) | 3.6 | Grimme D4 dispersion, coupled to any calculator via ASE's `SumCalculator` |
| `torch-dftd` | 0.4 | The dispersion head behind `mace_mp(dispersion=True)` |
| `icet` | — | Preferred SQS backend (optional) |
| `pymatgen`, `mp-api` | — | Materials Project browsing and structure interop |
| `pubchempy` | — | PubChem molecule import |

A complete general-purpose environment for the embedded interpreter:

```bash
python3 -m venv .venv
.venv/bin/pip install ase numpy spglib scipy paramiko pillow imageio imageio-ffmpeg
```

Heavyweight solver stacks (`gpaw`, `mace-torch`) do not have to live here — see the next section.

:::{admonition} GPAW on Apple Silicon
:class: caution
conda-forge does not build `gpaw` for `osx-arm64`; in a Conda environment on Apple Silicon add it with `pip install gpaw`. The PAW datasets (`gpaw-data`) are available on every platform.
:::

Some engines are external binaries rather than Python packages: `pw.x` (Quantum ESPRESSO), `siesta`, VASP, `orca`, and `povray`/`tachyon` for ray-traced rendering. The wizards generate working script skeletons for them with clearly marked `EDIT ME` lines for pseudopotential paths and launch commands.

---

## Execution environments: jobs pick their own Python

Simulation jobs never run inside the GUI process — they go through the job runner as external `python script.py` subprocesses, which keeps the interface responsive and isolates crashes. That subprocess does not have to use the embedded interpreter: every simulation wizard carries an {guilabel}`Execution Environment` group that decides which Python actually runs *that* job.

| Choice | Effect |
|---|---|
| Leave it empty | The job uses the embedded interpreter; the status line names it |
| {guilabel}`Env Folder…` | Point at a Conda environment directory; Calango finds `bin/python` inside it |
| {guilabel}`Python…` | Point directly at an interpreter executable |

The status line confirms the resolution live and turns red when no interpreter can be found at the given path. The setting is remembered between sessions, and a default Conda environment can be set once in {menuselection}`Edit --> Preferences…` so every wizard binds to it automatically.

:::{tip}
This is how you keep heavyweight stacks isolated: a GPAW environment for DFT, a `mace-torch` environment with CUDA for ML potentials, and a minimal ASE environment for Calango itself. The embedded interpreter only needs what the *GUI* uses — the job environments carry the solvers.
:::

---

## Summary

| Layer | Interpreter | Set by |
|---|---|---|
| Embedded runtime (GUI, structure I/O, builders) | Resolved once at launch | `CALANGO_PYTHON` → `VIRTUAL_ENV` → bundled → configure-time |
| Simulation jobs | Per wizard, per run | {guilabel}`Execution Environment` group (empty = embedded) |
| Remote jobs | The cluster's own Python | HPC panel / generated `job.sh` |

When anything Python-adjacent misbehaves, `calango --probe-python` answers the first question — *which* interpreter — in one line.
