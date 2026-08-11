# Orchestration

The {guilabel}`Orchestration` dock is a node-based editor for automated
simulation pipelines. Each node is one simulation process on a pannable,
zoomable canvas; drawing a link from one node's output port onto another
node makes the second run after the first, **consuming its outputs** — a
relaxed geometry becomes the next input structure, and a saved ground state
(`.gpw`) rides along. It lives in the bottom dock row beside the Processes
and Remote panels ({doc}`/viewport`), as a persistent workspace panel rather
than a window opened, used and dismissed.

**Structures enter a pipeline through a {guilabel}`Structure Container` and
travel down the links.** A process node is a process, not a
process-plus-a-molecule: it has no material of its own, takes its geometry
from its input port, and says so on its face
(*Structure: from input port*). One consequence is worth stating plainly —
a node with nothing linked to it is **refused** rather than run on a guess,
and the refusal names the container it needs.

Node types come in three families, separated in the {guilabel}`Add Process`
dialog. Only the calculator engine is chosen when a node is created.

**Simulation** — reads a structure, launches a job, and runs on task
defaults if you never open its wizard: {guilabel}`Geometry Optimization`,
{guilabel}`Single Point`, {guilabel}`Molecular Dynamics`,
{guilabel}`Phonon`.

**Transform** — reads a structure and produces a structure, *on the canvas*
rather than as a job: {guilabel}`Structure Container`,
{guilabel}`Supercell Builder`, {guilabel}`Defect Generator`. They have no
calculator and no launch command, and they finish in microseconds. See
{ref}`orchestration-transforms`.

**Analysis** — reads one or more *completed runs* rather than a structure,
so each needs that many parent nodes linked to it:
{guilabel}`Electronic Bands and DOS`, {guilabel}`Optical Properties`,
{guilabel}`2D Workfunction`, {guilabel}`2D Bands`,
{guilabel}`Wannier Functions`, {guilabel}`Born Effective Charges`,
{guilabel}`GW Quasiparticles`, {guilabel}`Charge Density Difference`,
{guilabel}`Raman and IR Spectroscopy`, {guilabel}`Charged Defects` and
{guilabel}`Charged Defects in 2D Materials`.

---

## Building the graph

- {guilabel}`Add Process…` — or a **double-click on empty canvas** — puts a
  new node where you clicked. The dialog is a categorised list of the three
  families, showing what each process takes as input and explaining the
  selected one; a transform has no calculator, so the engine row greys out
  for it. Adding a container opens its contents dialog straight away, which
  is where you were going next anyway.
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
- {guilabel}`Clear Orchestration` deletes **everything**, after asking *"Are
  you sure you want to delete all nodes from the workflow?"* and defaulting
  to No. Guarded rather than undoable: the canvas is not in the undo stack,
  and a pipeline is several minutes of wiring that one mis-click would
  otherwise cost in full.
- {guilabel}`Fit to Screen` frames the whole pipeline: it takes the bounding
  box of every node, adds a margin so nothing sits against the border, and
  sets the zoom and pan to match (clamped to the same 0.2×–3× range as the
  wheel, so a single node is not blown up to fill the dock). The canvas is
  4000 units across and a graph built by double-clicking wanders; this is
  how you find it again.

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
| `transformed.extxyz`, `optimized.extxyz`, `md_final.extxyz`, `single_point.extxyz` or `structure.extxyz` — first found, in that order | always | The child's `structure.extxyz` |
| `single_point.gpw` | when the parent saved one | Copied beside it, for engines that can restart from a saved ground state |

`transformed.extxyz` comes first because it is what a transform node writes,
and a transform node writes nothing else — a Supercell Builder placed after a
relaxation must hand on the *expanded* cell, not the relaxed one it read.

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

(orchestration-transforms)=

## Transform nodes

A transform reads the structure that reaches it and produces another one,
without leaving the canvas. It runs **in process** — the whole operation is
a few hundred microseconds of array work, and spawning an interpreter for it
would spend a thousand times longer starting up than working — so a
transform node has no calculator, no launch command and nothing that can
fail inside Python. Its result is written as `transformed.extxyz`, which is
what the next node inherits.

Double-click a transform node to configure it; unlike the simulation and
analysis nodes it opens a small dialog of its own rather than a setup
wizard.

### Structure Container

The one place structures enter a pipeline. It holds several of them and makes
**everything downstream of it run once per structure, in order**.

Double-click it to edit its contents. Three ways to fill it, and none of them
requires the structure to be open in a tab:

- {guilabel}`Add Open Document…` — one of the structures you already have open.
- {guilabel}`Import from File…` — `.extxyz`, `.cif`, `.traj`, POSCAR and
  everything else ASE reads. A **multi-frame file contributes one entry per
  frame**, named `stem #n`: importing a 20-frame trajectory is one of the
  obvious ways to build a sweep, and taking only its first frame would be a
  silent loss.
- {guilabel}`Import from Database…` — search the Materials Project, select
  rows, press {guilabel}`Add Selected to Container`. Several searches can be
  added in one visit (a running count shows what you have collected), which
  is how a sweep across chemistries gets built. Needs an API key from
  *materialsproject.org/api*; it shares the key with the File menu's database
  browser, so entering it in either place is enough.

Entries can be removed and the list order is the pass order.

- The pipeline makes one pass per item. Nodes that are *not* downstream of a
  container run once, in the first pass, and keep their result — re-running
  them per item would be pure waste.
- Each pass stages into its own labelled folder,
  `batch_<n>_<structure>/node_<m>_<task>/`, so a sweep over twelve alloys
  reads as twelve labelled studies rather than one folder of sixty runs.
- Every container in a graph must hold the **same number** of structures. A
  maximum-and-clamp rule would quietly re-use the last structure of the
  shorter list, which is a study nobody asked for; unequal lengths are
  refused instead.
- A node keeps its whole run history, so a batched node's per-pass
  directories are all reachable, not just the last.

### Supercell Builder

Repeats the incoming cell $n_a \times n_b \times n_c$ along the three
lattice vectors (via `ase.Atoms.repeat`). Applied to the structure that
*reaches* the node, so a relaxation upstream is expanded after it converges,
not before. Defaults to 2 × 2 × 2; 1 × 1 × 1 is the identity and passes
through. A structure with no periodic cell is refused — repeating a molecule
in vacuum is meaningless.

### Defect Generator

An ordered recipe of edits: **substitute** the listed atoms with an element,
**remove** them (a vacancy), or **add** one atom of an element at a given
position (Cartesian Å or fractional).

Every index refers to the structure that *reaches* the node, numbered from
zero. Removals are collected and applied in one pass, so `remove 3`,
`substitute 5 with B` means atoms 3 and 5 of the geometry you were looking
at, whatever order the rows were typed in; additions happen last and append
to the end. Index lists use the same syntax as the rest of Calango —
`0, 4, 7-9`.

:::{important}
An operation that matches **no atom** is an error, not a no-op — and so is
an empty recipe. Both mean the node was written against a different
structure, and quietly forwarding the pristine cell is how a pipeline
computes a defect formation energy of exactly zero with nothing anywhere
reporting a problem.
:::

---

## Execution

{guilabel}`Send to Processes` queues every node and executes the pipeline
in dependency order, one process at a time, once per Container item. Node
status walks a six-state lifecycle:

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

## Provenance

Every executed node writes `provenance.json` into its own job directory, and
the orchestration folder carries `orchestration.json` describing the whole
graph. Two kinds of record live there, and they answer different questions.

**Logical provenance** — how the run came to be asked for: which node in the
graph it is, which parent filled which named input, whether its script came
from a wizard or from task defaults (with the script's SHA-256), the
interpreter and launch command, the parameters a transform applied, which
batch item it belongs to, and which attempt it is.

**Data provenance** — what actually moved on disk. For every input: the
absolute path it was copied *from*, the name it was staged *as*, its size
and its SHA-256. For every output: name, size and SHA-256.

The pair matters because either alone can mislead. Logical provenance says a
node inherited "the pristine host"; only the checksum says *which* pristine
host, after that parent was re-run three times with a fixed script.

```json
{
  "node":  { "id": 3, "task": "defect_generator", "title": "Defect Generator" },
  "batch": { "index": 1, "total": 3, "label": "Au" },
  "logical": {
    "parents":    [ { "id": 2, "input": "input structure ← Supercell Builder" } ],
    "configured": false,
    "attempt":    1,
    "parameters": "remove 0"
  },
  "data": {
    "inputs":  [ { "name": "structure.extxyz", "role": "input structure",
                   "source": "…/node_2_supercell/transformed.extxyz",
                   "bytes": 812, "hashed": true, "sha256": "…", "from_node": 2 } ],
    "outputs": [ { "name": "transformed.extxyz", "bytes": 704,
                   "hashed": true, "sha256": "…" } ]
  },
  "execution": { "status": "done", "exit_code": 0,
                 "started_utc": "…", "finished_utc": "…" }
}
```

Files larger than 8 MiB are recorded by name and size but **not hashed** — a
GPAW ground state is routinely hundreds of megabytes, and hashing every one
would make provenance the most expensive part of a cheap pipeline. The
record carries `"hashed": false` rather than omitting the field, so "no
checksum" is never confusable with "checksum of nothing".

---

## Saving, reopening and running a workflow elsewhere

{guilabel}`Export Workflow…` writes the whole pipeline to one JSON file, and
{guilabel}`Open Workflow…` loads one back onto the canvas. The same file is
written as `workflow.json` into every run's `orchestration_*` folder, so a
results directory always says what produced it — and reopening that copy is
how you get back the pipeline that computed a set of results.

Opening **replaces** the canvas and asks first when there is something to
replace. The file is loaded into a scratch canvas before the real one is
touched, so a document that turns out to be malformed leaves the pipeline you
already had intact — neither the old one nor the new one is the one outcome
worse than refusing.

:::{note}
A reopened pipeline has no run state: statuses start at *pending* and
{guilabel}`Resume` has nothing to continue. The document describes the
pipeline, not an execution of it; what a given run did is in the provenance
records beside its results.
:::

Three properties are deliberate, and the whole format follows from them:

- **Self-contained.** The structures travel *inside* the document, as
  extended-XYZ text. A workflow you copy to a login node has to bring its
  geometry with it — a file of paths into your laptop is not a portable
  workflow.
- **Self-describing.** Each node carries its own family and its own
  input-slot table, rather than a reader being expected to know that Raman/IR
  inherits three runs of which two are optional. That knowledge lives in one
  place and is *exported*, which is what keeps a separate executor from
  drifting away from this panel.
- **Versioned.** `"schema": "calango.workflow/1"`, checked on load. A
  pipeline is a thing people archive; a format that silently changes meaning
  between releases turns a kept run into a wrong one.

The document holds no run state — no statuses, no job directories. It is the
pipeline, not an execution of it.

### calango-cli

[`calango-cli`](https://github.com/seixas-research/calango-cli) runs an
exported workflow headlessly, which is what a batch queue needs:

```bash
pip install calango-cli

calango-cli validate workflow.json     # will it run?
calango-cli info workflow.json         # what will it do?
calango-cli run workflow.json -o results/
```

It rebuilds the DAG, fans out over the containers, applies the transforms in
process through ASE and launches the same generated scripts this panel would
have launched — producing the **same directory layout and the same
provenance records**, so `results/` copied back opens from the Processes
panel like a local run.

A workflow says *what* to compute; the cluster says *how to launch it*, which
stays on the command line rather than in the file:

```bash
calango-cli run workflow.json -o results/ \
    --cores "$SLURM_NTASKS" \
    --launch "srun -n {cores} gpaw python {script}" \
    --keep-going
```

:::{note}
`--keep-going` is the flag a sweep wants: without it the run stops at the
first failure, and one bad structure ends a twelve-structure study.
:::

---

## Resume from failure

A failed node does not cost you the pipeline. {guilabel}`Resume` re-runs
**only what has not succeeded**, in the same `orchestration_*` folder: every
node that finished keeps its status, its job directory and its results, and
is not executed again; the failed node, everything skipped behind it, and
anything never started go back in the queue.

The normal loop is:

1. A node fails — a bad cutoff, a typo in a path, a k-grid that will not
   converge. Its descendants are marked *Skipped*; everything upstream stays
   *Done* with its results on disk.
2. Double-click the failed node and fix its parameters.
3. Press {guilabel}`Resume`. That node re-runs, into a **new** directory —
   the failed attempt's files and its provenance record are left where they
   are, because the run that went wrong is exactly what you want to compare
   against — and the pipeline continues from there.

:::{note}
**Re-configuring a node that has already run invalidates it and everything
downstream of it.** Their results were computed from settings that just
changed, so leaving them *Done* would let a Resume treat stale output as
current. Their *directories* are untouched: invalidating a result is not the
same as deleting it. Nodes *upstream* of the change are never affected —
that is the whole point of resuming.
:::

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

## Worked example — a vacancy sweep over three metals

The same pipeline, run once per element, with the defect built on the canvas:

1. Add a {guilabel}`Structure Container` node. Its contents dialog opens;
   add **Cu**, **Au** and **Pt** from open documents, from CIF files or from
   the database. The node now reads *3 pipeline passes*.
2. Add a {guilabel}`Geometry Optimization` node and link the container to it.
3. Add a {guilabel}`Supercell Builder`, link it after the relaxation, and set
   2 × 2 × 2 — the expansion happens *after* each metal has relaxed.
4. Add a {guilabel}`Defect Generator`, link it next, and give it one
   operation: **remove**, atoms `0`.
5. Add a second {guilabel}`Geometry Optimization` for the defective cell and
   link it last.
6. {guilabel}`Send to Processes`. The five nodes run three times over, into
   `batch_1_Cu/`, `batch_2_Au/` and `batch_3_Pt/`.

If the Pt relaxation fails on its convergence settings, the Cu and Au
results are already on disk and stay *Done*; fix that node and press
{guilabel}`Resume` to compute Pt alone.

To run the same thing on a cluster instead, stop after step 5, press
{guilabel}`Export Workflow…`, copy the file across and
`calango-cli run workflow.json -o results/`. Later,
{guilabel}`Open Workflow…` on that same file brings the pipeline back onto
the canvas.

---

## Limitations

Stated plainly, so a pipeline is designed around them rather than into
them:

- **The canvas is not saved in the project file.** A workflow file is how a
  pipeline is kept: {guilabel}`Export Workflow…` writes it,
  {guilabel}`Open Workflow…` reads it back. It is not reopened automatically
  on startup, and Resume only works within the session that started the run —
  a reopened pipeline starts from a clean slate.
- Execution within a canvas is strictly **one node at a time**, in
  dependency order — there is no parallel fan-out even for independent
  branches, and a Container's passes are sequential rather than concurrent.
- For the **input geometry**, only the first connected parent's output is
  inherited; the numbered slots of an analysis node are the only place where
  several parents each contribute something.
- A node holds a *snapshot* of its material taken when the node was added;
  later edits to the open document do not retroactively change an already
  configured node's staged geometry.
- {guilabel}`Supercell Builder` repeats along the lattice vectors only. A
  general (non-diagonal) transformation matrix is available in the standalone
  supercell builder, not on the canvas.
