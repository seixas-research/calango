# The workspace

Calango opens into two full-height side columns flanking the 3D viewport, with a row of job panels along the bottom. **Every panel is a Qt dock widget**, so the layout is a starting point rather than a constraint — any panel can be resized, re-docked, tabbed with another, floated onto a second monitor, or hidden entirely.

---

## The default layout

| Region | Panels (top → bottom / left → right) |
|---|---|
| Left column | Calango (branding) · Structure · Volumetric Data · *Additional Overlays* · Processes |
| Center | Document tab bar · 3D viewport · playback timeline |
| Right column | Representation · Spatial References · Visual Effects |
| Bottom row | Workflow · *Remote Access* · Results |

Two panels — italicized above — exist in the layout but start **hidden**: *Additional Overlays* (lattice planes, text annotations, geometric primitives — a finishing step, not an every-session panel) and *Remote Access* (a login form that says nothing until used). Both are one {menuselection}`View` click away.

- **Calango** — the branding strip heading the left column. Purely decorative; hide it from {menuselection}`View` if you want the vertical space.
- **Structure** — formula, atom and bond counts, lattice parameters, detected symmetry, plus the structure-editing action row (see below).
- **Volumetric Data** — 3D scalar fields (cube/xsf/CHGCAR: charge density, ELF, Wannier orbitals) as isosurface and color-slice overlays on the main viewport.
- **Processes** — the process manager: every background task with live status.
- **The viewport** — the document tab bar, the 3D canvas, and the playback timeline (visible only for trajectories). Covered in {doc}`/viewport`.
- **Representation** — appearance controls: style, mode, color mapping, radii and bond widths.
- **Spatial References** — three tabs, {guilabel}`Unit cell`, {guilabel}`Axes triad` and {guilabel}`Vectors`: everything that answers "where/which way is this?" about the scene without being the atoms. Under {guilabel}`Unit cell`, *Show atoms of the neighboring unit cell* draws exactly the periodic images that terminate a bond leaving the cell — only those, so no bond ends in mid-air.
- **Visual Effects** — lighting, shadow, fog, blur and ambient occlusion, all off by default.
- **Workflow** — the node canvas: build a pipeline of connected calculation nodes and dispatch it as a DAG. It leads the bottom row, which reads left to right in the order the work happens — build the pipeline (Workflow), choose where it runs (Remote Access), read what came back (Results).
- **Results** — the job console: a {guilabel}`Log` tab plus live {guilabel}`Energy`, {guilabel}`Temperature`, {guilabel}`Force` and {guilabel}`Pressure` metric plots, with a {guilabel}`Process:` selector that switches every tab between runs (each run keeps its own metric history), and an {guilabel}`Export Data…` button per plot.
- **Remote Access** — SSH connection, cluster job submission and queue monitoring. It is held to the narrowest width that still shows its whole form.

% TODO screenshot: full main window in the default layout with a structure loaded, all four regions annotated or at least visible
```{figure} /_static/img/workspace_default_layout.png
:alt: The default Calango workspace: left and right dock columns flanking the viewport, job panels along the bottom
:width: 92%
:figclass: screenshot

The default layout. Side columns run the full window height; the bottom row spans the space between them.
```

---

## Dock mechanics and persistence

Drag a dock by its title bar to float it or re-dock it elsewhere; drop it onto another dock to tab the two together; drag the splitters to resize. Every panel has a toggle at the bottom of the {menuselection}`View` menu, so a panel closed by accident is one menu click away — and {menuselection}`View --> Reset Layout` puts everything back to the default arrangement shown above.

Your layout — including floating panels and the window geometry — is **saved when you quit and restored on the next launch**.

:::{note}
After an upgrade that changes the default arrangement, Calango discards the saved layout once so the new default appears; any rearrangement you make after that persists as usual.
:::

---

## Documents, tabs and the timeline

Each structure or trajectory you open occupies its own *tab* above the viewport, and **each tab carries its own undo history**. All tabs share one accelerated viewport — switching tabs changes which document the views observe, so display settings stay consistent and no OpenGL context is recreated.

When a document contains more than one frame, the **playback timeline** appears below the viewport:

- **Transport buttons** — first, previous, play/pause, next, last.
- **Scrubber** — a tick-marked slider over the frame range.
- **Rate** — playback speed in frames per second, **0.1–120, default 15**.

Trajectories arrive from several places: opening a multi-frame file, finishing an MD or relaxation run, generating displaced structures in the phonon builder, or producing a noise ensemble. During a running simulation the timeline grows in real time as frames stream in.

---

## The Structure panel

The **Structure** panel reports what Calango knows about the active document:

| Field | Content |
|---|---|
| {guilabel}`Formula` | Hill-ordered chemical formula |
| {guilabel}`Atoms`, {guilabel}`Bonds` | Counts, with bonds as currently perceived |
| {guilabel}`a, b, c` | Cell vector lengths in Å, to two decimals |
| {guilabel}`α, β, γ` | Cell angles in degrees |
| {guilabel}`Cell volume`, {guilabel}`Periodic` | Volume and per-axis periodicity |
| {guilabel}`Tolerance` | The spglib symmetry tolerance (*symprec*) in Å, up to four decimals — default **0.001** |
| {guilabel}`Space group`, {guilabel}`Point group`, {guilabel}`Crystal system` | Detected symmetry, e.g. `Fd-3m (227)`, `m-3m`, `cubic` |

Raise the tolerance for structures with numerical noise — a relaxed cell often needs 0.01 Å or more before its true symmetry is recognized. Symmetry detection needs `spglib` and a defined unit cell ({doc}`/python_environment`).

The panel's action row hosts the whole-structure operations: {guilabel}`Edit Structure…`, centering, vacuum padding, wrapping into the cell, and the supercell builder.

---

## Projects and session storage

A *project* is one `.calproj` file that restores an entire session: every tab with its structures and trajectory frames, the viewport color mapping, and the job console with its recorded metric series.

- {menuselection}`File --> Project Workspace --> Open Project…` ({kbd}`Ctrl+Shift+O`) — replaces the current session, warning first if tabs are open.
- {menuselection}`File --> Project Workspace --> Save Project` ({kbd}`Ctrl+S`).
- {menuselection}`File --> New Workspace` ({kbd}`Ctrl+N`) — closes everything and starts clean.

Calango tracks unsaved changes. Every undoable edit, tab close and job launch marks the session dirty; saving or loading a project clears the flag. Quitting with unsaved changes asks whether to {guilabel}`Save`, {guilabel}`Discard` or {guilabel}`Cancel` — cancelling (or cancelling the save dialog that follows) leaves the application open.

### The managed temporary directory

Simulation jobs and generated ensembles need somewhere to put their working files. Once a project has been saved, Calango places them in a `.calango_tmp/` folder *next to the `.calproj` file*, one timestamped subdirectory per task:

```text
my_study.calproj
.calango_tmp/
    job_20260721_143255/     run.py, structure.extxyz, md.traj, logs
    noise_20260721_144012/   perturbed.extxyz
    job_20260721_150130/     bands.json, pdos.json
```

Until a project has been saved, the same directories are created under the per-user application data location instead. Either way the process manager keeps a live link to each one.

---

## The process manager

The compact **Processes** panel at the foot of the left column lists every background task of the session — local calculations, remote submissions, electronic-structure runs, noise ensembles — with its name, color-coded status (*queued*, *running*, *completed*, *failed*) and start time.

Two buttons act on the selected task:

- {guilabel}`Open Folder` — reveals the task's working directory in your file manager, for the raw logs and any files Calango does not import.
- {guilabel}`Load Result` — brings the result back into the workspace. Double-clicking the row does the same. Calango inspects the directory and opens whatever it finds: band-structure and PDOS data open in the electronic-structure viewer, trajectories and final geometries open as tabs.

The point of keeping these links is that a finished calculation stays one click from further analysis. A completed MD run can be re-loaded weeks later and fed to the RDF, distribution or dataset tools without recomputing anything.

% TODO screenshot: Processes dock with a mix of completed and running tasks, context row buttons visible
```{figure} /_static/img/workspace_process_manager.png
:alt: The Processes dock listing background tasks with color-coded status
:width: 92%
:figclass: screenshot

The process manager. Each row keeps a live link to its job directory; double-clicking loads the result.
```

---

## The status bar

The left side of the status bar carries transient messages — the ready hint, selection counts, load and save confirmations, measurement readouts. The right side is a permanent **system monitor** showing the resource usage of Calango *and* of the background job it has spawned — never host-machine totals:

- **Application group** — Calango's own CPU %, RAM (MB and % of system RAM), GPU %, VRAM (MB) and active thread count, each with a miniature load bar.
- **Job group** — appears only while a job is running: its name, state, elapsed time, and the CPU and memory of its **whole process tree**. The tree, not the direct child, is the number that matters — the compute usually lives further down (`mpirun -n 4 gpaw …`), and sampling only the launched shell would report a few percent for a machine running flat out.

Metrics are sampled on a strict 1.0 s timer; GPU/VRAM show *N/A* where no per-process metric source exists (e.g. Metal on macOS). {menuselection}`View --> Status Bar` toggles the whole bar.

---

## Themes

Calango ships three appearance themes — **Dark**, **Light**, and **System**, which follows the operating system's dark/light preference and re-applies live when the OS switches. Choose one in {menuselection}`Edit --> Preferences…` ({kbd}`Ctrl+P`); the setting persists in `~/.calango/settings.json` and is applied at startup before any widget is constructed, so there is no flash of the wrong theme.

:::{note}
The *System* theme requires Qt ≥ 6.5 at build time; on a Qt 6.4 build only Dark and Light are offered.
:::
