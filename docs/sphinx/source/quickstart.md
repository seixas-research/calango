# Quickstart

Ten minutes from a fresh install to a finished calculation. This page launches Calango, opens one of the bundled example structures, walks the viewport basics, and runs a single-point energy with the EMT calculator — no DFT stack required, because **EMT ships with ASE**, so a minimal `ase` + `numpy` environment is enough for everything here. If `calango --probe-python` does not yet report an ASE version, do {doc}`/installation` and {doc}`/python_environment` first.

---

## Launch and the welcome screen

Start Calango from the launcher, or from a terminal — every file passed on the command line opens in its own tab:

```bash
calango                          # empty workspace + welcome screen
calango examples/Au_cubic.cif    # straight into a structure
```

Launched without files, Calango greets you with the **welcome screen**: the brand banner, two columns of recent files, and three quick actions — {guilabel}`New Project`, {guilabel}`Open Project`, {guilabel}`Open Geometry`. Recent *projects* (saved `.calproj` workspaces) and recent *structures* (bare geometry files) are listed separately, because reopening a session and opening a single geometry are different intents. A persisted {guilabel}`Show Welcome Screen on startup` checkbox (default **on**) governs whether it appears next launch.

% TODO screenshot: welcome dialog on first launch, with the two recent-file columns and the three quick-action buttons visible
```{figure} /_static/img/quickstart_welcome.png
:alt: The Calango welcome screen with recent projects, recent structures, and quick actions
:width: 92%
:figclass: screenshot

The welcome screen. Recent projects and recent structures are separate columns; the checkbox at the bottom controls whether the dialog returns next launch.
```

:::{note}
If a warning about ASE appears instead, Calango started but its embedded Python cannot import ASE. The GUI stays usable so you can fix the setting, but structure I/O and job features are disabled until you point Calango at a working interpreter — see {doc}`/python_environment`.
:::

---

## Open an example

The repository (and the installed application's examples) ships small, known-good structures — among them `Au_cubic.cif`, `Si_diamond.vasp`, `diamond.vasp`, `graphene_unitcell.vasp`, `MoS2.cif`, `water.xyz`, `benzene.xyz`, and `coronene.xyz`. Open one with {menuselection}`File --> Open --> Structure…` ({kbd}`Ctrl+O`) and pick `Au_cubic.cif` — a cubic gold cell, chosen here because gold is one of the metals EMT is parametrized for.

Two other ways to get atoms on screen:

- {menuselection}`Build --> From Database…` — bulk crystal prototypes (`ase.build.bulk`), the Materials Project, and PubChem.
- The command line, as above; a `.calproj` argument restores a whole saved session.

The structure opens in a new tab above the 3D viewport, and the **Structure** dock on the left immediately reports what Calango detected: formula, atom and bond counts, lattice parameters, and — with `spglib` installed — the space group.

---

## Move, look, select

The viewport starts in *Rotate* mode. The essentials:

| Action | Input |
|---|---|
| Orbit | Left-drag (arcball) |
| Pan | Middle-drag, or {kbd}`Shift`+left-drag |
| Zoom | Scroll wheel (two-finger scroll on a trackpad) |
| Frame the structure | Double-click |
| Select an atom | Click it; click empty space to clear |
| Toggle atoms in/out of a selection | {kbd}`Ctrl`+click ({kbd}`Cmd` on macOS) |

Single-key mode switches: {kbd}`R` rotate, {kbd}`T` translate, {kbd}`I` insert. Picking is a true ray–sphere test at the drawn radius, so what you hit is what you see. The full tour — measurement modes, rubber-band selection, per-axis rotation buttons — is in {doc}`/viewport`.

---

## Run an EMT single point

Open {menuselection}`Simulation --> Single-point Calculation…` ({kbd}`Ctrl+R`). The wizard walks a short sequence of stages; {guilabel}`Next` carries you through them:

1. **Calculator & Execution Environment.** Choose {guilabel}`EMT` — effective medium theory, fast, useful for testing a workflow before committing compute. Leave the {guilabel}`Execution Environment` group empty, so the job runs on the same interpreter Calango embeds (the status line names it).
2. **Calculator Settings.** EMT has essentially nothing to configure — that is the point of using it first.
3. **ASE Script Review.** The final stage shows the complete, standalone ASE script the wizard produced — syntax-highlighted and editable. This script *is* the job; nothing else will run. Press {guilabel}`Run`.

:::{important}
EMT is parametrized for a handful of fcc metals (Al, Cu, Ag, Au, Ni, Pd, Pt). It will happily produce numbers for other elements, but they are for workflow testing only — for real materials, switch the calculator to GPAW or MACE and pick an execution environment that carries the stack ({doc}`/python_environment`).
:::

---

## Watch the Results dock

The job runs as a separate process in its own working directory, so a crashing solver cannot take the GUI with it. Two panels track it:

- **Processes** (foot of the left column) lists every background task with a colour-coded status — *queued*, *running*, *completed*, *failed* — and its start time.
- **Results** (bottom row) shows the live picture: the {guilabel}`Log` tab carries a status label, a progress bar, a {guilabel}`Kill` button and the streamed output (standard error in red, a clean exit in green), while the {guilabel}`Energy`, {guilabel}`Temperature`, {guilabel}`Force` and {guilabel}`Pressure` tabs plot metrics as the generated script reports them. The {guilabel}`Process:` selector at the top switches all tabs between runs — every job keeps its own metric history.

An EMT single point on a small cell finishes in about a second. When it does, the row in **Processes** turns *completed*.

% TODO screenshot: workspace after the EMT run — Au structure in the viewport, Processes row completed, Results dock Log tab showing the green clean-exit line
```{figure} /_static/img/quickstart_results.png
:alt: Calango after a finished EMT single point, with the Processes and Results docks populated
:width: 92%
:figclass: screenshot

A completed run. The Processes dock keeps the link to the job directory; the Results dock holds its log and metric history.
```

---

## View the summary

Results open *from the process that produced them*: double-click the completed row in the **Processes** dock (or use its {guilabel}`Open Viewer` button / context menu). Calango inspects the job directory and opens the matching viewer — for a single point, the summary of total energy and maximum residual force (plus the Fermi level, for DFT calculators that report one). {guilabel}`Open Folder` reveals the raw working directory — `run.py`, the staged geometry, and the logs — for anything Calango does not import.

The status bar at the bottom of the window keeps its own accounting during runs: Calango's CPU, RAM, GPU and thread usage, and a second group with the running job's name, state, elapsed time and the resource usage of its whole process tree.

---

## Next steps

- {doc}`/tutorials/silicon` — the same workflow with real physics: build silicon, relax it, and carry one converged ground state through phonons, bands and PDOS.
- {doc}`/workspace` — the dock layout, projects and the managed temporary directory.
- {doc}`/python_environment` — how interpreters are resolved and how to route heavy jobs to their own Conda environments.
