# Orchestration

The {guilabel}`Orchestration` dock is a node-based editor for automated
simulation pipelines. Each node is one simulation process on a pannable,
zoomable canvas; drawing a link from one node's output port onto another
node makes the second run after the first, **consuming its outputs** — a
relaxed geometry becomes the next input structure, and a saved ground state
(`.gpw`) rides along. It lives in the bottom dock row beside the Processes
and Remote panels ({doc}`/viewport`), as a persistent workspace panel rather
than a window opened, used and dismissed.

Node types come in two families, separated in the {guilabel}`Add Process`
list. Each node is assigned a *material* (one of the open documents — the
list refreshes every time the Add Process dialog opens) and a calculator
engine.

**Self-contained** — reads a structure, and runs on task defaults if you
never open its wizard: {guilabel}`Geometry Optimization`,
{guilabel}`Single Point`, {guilabel}`Molecular Dynamics`,
{guilabel}`Phonon`.

**Baseline-inheriting** — reads one or more *completed runs* rather than a
structure, so each needs that many parent nodes linked to it:
{guilabel}`Electronic Bands and DOS`, {guilabel}`Optical Properties`,
{guilabel}`2D Workfunction`, {guilabel}`2D Bands`,
{guilabel}`Wannier Functions`, {guilabel}`Born Effective Charges`,
{guilabel}`GW Quasiparticles`, {guilabel}`Charge Density Difference`,
{guilabel}`Raman and IR Spectroscopy`, {guilabel}`Charged Defects` and
{guilabel}`Charged Defects in 2D Materials`.

---

## Building the graph

- {guilabel}`Add Process…` — or a **double-click on empty canvas** — puts a
  new node where you clicked. The dialog asks for the process type, the
  material and the engine.
- Each node is a draggable rounded rectangle showing the process name, the
  material it runs on and the calculator, with an **input port on the left
  edge and an output port on the right**. A status strip along the top edge
  tracks execution.
- **Drag from a node's output port** — a dashed preview follows the cursor —
  and release on another node to connect them. Duplicate links are ignored
  and **cycles are refused**: a child can never be its own ancestor.
- **Double-click a node** to configure it: its standard setup wizard opens
  in orchestration mode, with the Run button relabelled {guilabel}`Save process
  node`. Accepting commits the generated script (plus interpreter and launch
  command) to the node instead of executing anything. An unconfigured node
  runs with the task's defaults, seeded from the per-element suggested
  cutoff and k-grid.
- {guilabel}`Remove Selected` deletes the selected nodes and every link that
  touched them. The mouse wheel zooms; middle-drag pans.

% TODO screenshot: Orchestration dock with a three-node chain relax -> single point -> phonon, one node Done, one Running, status strips visible
```{figure} /_static/img/workflow_canvas.png
:alt: The Orchestration canvas with a three-node pipeline mid-execution
:width: 92%
:figclass: screenshot

A three-node chain mid-run — each node's status strip tracks it from waiting through running to done, and links carry the parent's outputs forward.
```

---

## What flows along an edge

When a child node starts, its job directory is seeded from its first
connected parent's results:

| Artifact | Condition | Becomes |
|---|---|---|
| `optimized.extxyz`, `md_final.extxyz`, `single_point.extxyz` or `structure.extxyz` — first found, in that order | always | The child's `structure.extxyz` |
| `single_point.gpw` | when the parent saved one | Copied beside it, for engines that can restart from a saved ground state |

The geometry hand-off carries **coordinates and cell** — extended XYZ
stores both, which is what lets a variable-cell relaxation hand over its
lattice, not just its positions. For a self-contained node with several
parents, the first connected one supplies the geometry — the common
pipelines are chains anyway.

### Inherited runs

A baseline-inheriting node consumes **one parent per input slot, in the
order the links were drawn**. Each slot is staged into the child's own job
directory under a fixed name, and that same name is what the node's wizard
was configured against — which is why a node can be set up before any of its
parents has ever executed:

| Node type | Slots, in link order | Staged as |
|---|---|---|
| Electronic Bands, Optics, 2D Workfunction, 2D Bands, Wannier, Born charges, GW | ground state | `baseline_1.gpw` |
| Charge Density Difference | combined system (the parent's whole results folder) | `baseline_1/` |
| Raman and IR | ground state; Born charges *(optional)*; optics *(optional)* | `baseline_1.gpw`, `baseline_2.json`, `baseline_3.json` |
| Charged Defects, Charged Defects in 2D Materials | pristine host; neutral defect | `baseline_1.gpw`, `baseline_2.gpw` |

Each node **paints its slot assignment** — `pristine host ← Single Point
(1)` — and an unconnected required slot is shown in red. Two links into the
same node look identical on the canvas, so the order they were drawn in is
the only thing distinguishing them, and for a defect diagram it is the
difference between a formation energy and its negative.

:::{note}
The GW node offers the **GPAW route only**. Yambo's baseline is a Quantum
ESPRESSO `.save` directory, and no node on this canvas produces one — an
input that can never be satisfied is not worth offering.
:::

:::{important}
**A node with a parent must inherit from it or not run at all.** If the
parent's directory holds no usable geometry, the child is refused with an
explicit message rather than falling back to its originally assigned
material — that fallback would silently execute the child on the
*un-relaxed* structure: a run that "succeeds" while computing the wrong
thing, which is strictly worse than failing.

Three refusals follow from the same principle, and all of them happen
*before* anything is launched:

- **An unconfigured baseline-inheriting node.** Unlike a relaxation, it has
  no defaults to fall back on: its script names a baseline, and no baseline
  path can be guessed. Open its wizard and save it first.
- **Too few parents.** A node with two input slots wired to one parent is
  refused, naming both slots it wanted.
- **A parent that saved nothing to inherit.** A Single-Point Calculation has
  to save its wavefunctions (`.gpw`) for anything downstream to restart from
  it; if it did not, the child says so rather than letting the failure
  surface inside Python as an apparent bug in the module.
:::

---

## Execution

{guilabel}`Send to Processes` queues every node and executes the pipeline
in dependency order, one process at a time. Node status walks a six-state
lifecycle:

| Status | Meaning |
|---|---|
| Pending | On the canvas, not yet dispatched |
| Waiting | Queued — every node enters this state when the run starts |
| Running | Its job is executing |
| Done | Exit 0 |
| Failed | Non-zero exit or crash — or staging was refused |
| Skipped | A **descendant of a failed node**: its inputs will never exist, so it is marked rather than left "pending" forever |

Failure propagates by `skipDescendants`: when a node fails, every child
still waiting is marked Skipped, recursively — the canvas says why the
pipeline stopped instead of stalling silently.

Each run creates a timestamped `orchestration_YYYYMMDD_HHmmss/` folder under the
simulations directory, and every node stages its own `node_<n>_<task>/` job
directory inside it, using the same script generators and launch-command
machinery as the wizards ({doc}`/simulations/scripts`). **The Orchestration panel
owns its own JobRunner**, separate from the queue that standalone wizard
runs share — a pipeline therefore executes concurrently with your other
jobs, while remaining strictly sequential within the canvas.

### Process Manager integration

Every dispatched node registers a row in the Processes panel — *Orchestration:
Geometry Optimization (Si)* — and mirrors its state there (Queued → Running
→ Completed/Failed; a Skipped node reports as Failed, which is what it is
from the queue's point of view). The row carries the node's job directory,
so a finished node's results are one right-click away, reloadable like any
other run ({doc}`/simulations/jobs`). Running nodes stream their metrics
into the Results dock, and a relaxation or MD node streams its geometry
frames into a live viewport tab exactly as a standalone run does.

---

## Worked example — relax, then single point, then phonons

A typical pipeline for a new structure:

1. Open the structure and add a {guilabel}`Geometry Optimization` node for
   it. Double-click the node, set the engine and convergence in the wizard,
   and press {guilabel}`Save process node`.
2. Add a {guilabel}`Single Point` node and drag a link from the
   optimization's output port onto it. It will run on the *relaxed*
   geometry — cell included — never on the structure you drew.
3. Add a {guilabel}`Phonon` node and link it after the single point. If the
   single-point run saved a ground state (`single_point.gpw`), it is copied
   into the phonon node's directory for restarting.
4. {guilabel}`Send to Processes`. The three nodes turn *waiting*, run in
   order, and land in the Processes panel individually — if the relaxation
   fails, the two children are skipped with the reason on the canvas.

:::{note}
Node configuration captures the wizard's generated script at save time.
Editing the structure afterwards does not re-run the wizard — reopen the
node (double-click) to regenerate its script against current settings.
:::

---

## Limitations

Stated plainly, so a pipeline is designed around them rather than into
them:

- **The canvas is not saved in the project file.** A pipeline lives for
  the session; the node *jobs* it produced persist on disk under their
  `orchestration_*` folder and stay reloadable from the Processes panel, but the
  graph itself must be rebuilt after a restart.
- Four node types — Geometry Optimization, Single Point, Molecular
  Dynamics, Phonon. Response calculations (bands, optics, GW) are run from
  their own wizards, which can inherit a finished single point as a
  baseline instead.
- Execution within a canvas is strictly **one node at a time**, in
  dependency order — there is no parallel fan-out even for independent
  branches.
- With multiple parents, **only the first connected parent's outputs are
  inherited**; design multi-input steps as chains.
- A node holds a *snapshot* of its material taken when the node was added;
  later edits to the open document do not retroactively change an already
  configured node's staged geometry.
