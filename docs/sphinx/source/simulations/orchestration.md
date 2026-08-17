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
{guilabel}`Single-atom Container`, {guilabel}`Supercell Builder`,
{guilabel}`Defect Generator`, {guilabel}`Random Noise Setup`. They have no
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
  for it. **MACE is the default calculator**, with **xTB** second — a machine-learned
  potential or a semi-empirical tight-binding run is what makes a batch over a
  dozen structures finish, which is the shape of work this canvas is for;
  GPAW and VASP are a deliberate decision about machine time rather than a
  default. xTB needs no trained model for the elements involved, which makes
  it the fallback when MACE has no coverage. Adding a container opens its contents dialog straight away, which
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
- {guilabel}`Auto-Layout` rearranges every node into columns in execution
  order. Each node lands one column to the right of its **last-finishing**
  parent (longest path, so no link ever runs backwards past a node), and the
  rows within a column are ordered by barycentre sweeps to keep links from
  crossing. It moves nodes only — the pipeline itself is untouched — and
  finishes by fitting the result on screen.
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
lattice, not just its positions.

A self-contained node reads **one** geometry, from **one** parent — a second
link into it is refused at the moment you draw it (see
[Multiple connections](#multiple-connections) below), rather than accepted
and then silently ignored. [Dump Trajectory](#dump-ml-training-data) is the
one node built the other way, to merge several parents on purpose.

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
- **Too many parents.** A link into a slot that is already full — including
  a self-contained node's single geometry input — is refused the moment you
  draw it, rather than accepted and then never actually read. See
  [Multiple connections](#multiple-connections) below.
- **A parent that saved nothing to inherit.** A Single-Point Calculation has
  to save its wavefunctions (`.gpw`) for anything downstream to restart from
  it; if it did not, the child says so rather than letting the failure
  surface inside Python as an apparent bug in the module.
:::

(multiple-connections)=
### Multiple connections

Drawing a link never removes one that is already there — a node can end up
with several **outgoing** links (one output feeding several consumers) and,
on the one node built to want it, several **incoming** links too (several
results merged into one). The two directions are governed by different
rules, because they mean different things.

**Outgoing — fan-out — is always allowed.** One Structure Container can feed
both a Single-Point Calculation *and* a
[Single-atom Container](#single-atom-container); one Single-Point
Calculation can feed both a Dump Trajectory node and an Electronic Bands
analysis. The
shared parent still runs **exactly once**: staging a copy into each child's
own job directory is how every parent hand-off already worked, so nothing
about a second child changes what the first one reads, and nothing a child
does to its own copy can reach back and change the parent's result or any
sibling's.

**Incoming — merging — is allowed only where it means something.** A slot
that names *the* structure, or *the* baseline of a fixed list, has room for
exactly one parent — a second link there would be drawn but never actually
read, so it is **refused when you draw it**, with a message naming the node
and explaining why. [Dump Trajectory](#dump-ml-training-data) and
[Dataset Manager](#dataset-manager) are the exception: since each already
reads every PASS of whatever feeds it, reading every pass of *several*
parents — concatenated in the order the links were drawn — is the same
operation, just over more input. That is what makes "bulk, noisy structures
from one branch and isolated-atom references from another, merged into one
training set" a single Dump Trajectory or Dataset Manager node rather than
several files stitched together by hand afterwards.

Scheduling and persistence both fall out of rules that already existed for
other reasons, not new machinery: a node becomes runnable once *every one*
of its parents is `Done` (already true the moment a node could have more
than one parent, for the named-slot case above), so a merge node
automatically waits for every branch feeding it; and a saved workflow
stores edges as a flat `{from, to}` list with no assumption that a node
appears as `to` at most once, so a fan-out-then-merge graph reloads exactly
as drawn.

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

The **TDB Generator** is the exception to both halves of that description: it
consumes a completed run's *results* rather than a structure, and produces a
thermodynamic database rather than a structure. It is a transform because it
runs on the canvas rather than as a job, which is what the family actually
decides.

**Dump Trajectory** also consumes results rather than a structure, but
differently again: where the TDB Generator reads one staged *ensemble* file,
Dump Trajectory reads **every pass** of a live fan-out directly, and is
therefore also the only transform that does not run once per Container item
— it runs **once**, after the fan-out's last pass. See
[below](#dump-ml-training-data).

**Single-atom Container** is a third kind of exception: like Structure
Container itself, it does not run once per item at all — it is computed
**once**, from *all* of its parent's items at once, before the pipeline
starts, and what it produces becomes a second, independent pass count for
whatever is linked downstream of it. See
[below](#single-atom-container).

Double-click a transform node to configure it; unlike the simulation and
analysis nodes it opens a small dialog of its own rather than a setup
wizard.

### Structure Container

The one place structures enter a pipeline. It holds several of them and makes
**everything downstream of it run once per structure, in order**.

Double-click it to edit its contents. Three ways to fill it, and none of them
requires the structure to be open in a tab:

- {guilabel}`Add Open Document…` — one of the structures you already have open.
- {guilabel}`Add Bulk Crystal…` — build standard crystals straight from
  `ase.build.bulk`. Type `Cu, Au, Pt`, leave the structure on *Ground state*
  and each element gets its own correctly-parameterised cell from ASE's
  `reference_states` table — Cu fcc, Fe bcc, Si diamond — rather than one
  structure applied to all of them. The fastest way to build a sweep.
- {guilabel}`Import from File…` — `.extxyz`, `.cif`, `.traj`, POSCAR and
  everything else ASE reads. A **multi-frame file contributes one entry per
  frame**, named `stem #n`: importing a 20-frame trajectory is one of the
  obvious ways to build a sweep, and taking only its first frame would be a
  silent loss. A file that cannot be read (wrong format, holds no
  structures) is reported by name rather than failing silently or
  half-importing. This is the natural destination for a
  {doc}`Random Noise Setup </builders/disorder>` trajectory: generate it
  there (with the linear ramp on, for a set that shades from near-equilibrium
  to more anharmonic displacements), save it, and import it here.
- {guilabel}`Import from Database…` — search the Materials Project, select
  rows, press {guilabel}`Add Selected to Container`. Several searches can be
  added in one visit (a running count shows what you have collected), which
  is how a sweep across chemistries gets built. Needs an API key from
  *materialsproject.org/api*; it shares the key with the File menu's database
  browser, so entering it in either place is enough.

Entries can be removed and the list order is the pass order. A status line
above the list always states the current count ("12 structures loaded"),
updated the moment a file import or bulk-crystal add completes.

- The pipeline makes one pass per item. Nodes that are *not* downstream of a
  container run once, in the first pass, and keep their result — re-running
  them per item would be pure waste.
- Each pass stages into its own labelled folder,
  `batch_<n>_<structure>/node_<m>_<task>/`, so a sweep over twelve alloys
  reads as twelve labelled studies rather than one folder of sixty runs.
- Every container in a graph must hold the **same number** of structures. A
  maximum-and-clamp rule would quietly re-use the last structure of the
  shorter list, which is a study nobody asked for; unequal lengths are
  refused instead. A [Single-atom Container](#single-atom-container) is
  exempt — its own pass count comes from the elements it finds, an
  unrelated quantity by design, not a second Structure Container to agree
  with this one.
- A node keeps its whole run history, so a batched node's per-pass
  directories are all reachable, not just the last.

(single-atom-container)=
### Single-atom Container

Reads every structure a **Structure Container** upstream of it holds,
collects the **unique chemical elements** across all of them, and produces
one isolated-atom reference structure per element: a single atom, centered
in a periodic cubic box. Link it downstream of a Structure Container and a
**Single-Point Calculation** (or any other node) downstream of *it* fans out
over the elements, one pass each — exactly the shape a Structure Container
itself produces, which is what lets the same downstream nodes work
unmodified whether they are fed real structures or isolated-atom references.

The node's face reports what it found once the graph is sent —
**"3 elements: Au, O, H"**, listed in first-appearance order across the
source structures — or *"No elements yet"* before that, since the element
list is not known until the parent Container's contents are read.

Double-click it to set:

`Box size`
: the side of the cubic cell, in Å. **10 Å by default** — large enough that
  periodic images do not interact for any element's interaction range,
  small enough to keep a plane-wave basis a reasonable size. This is the one
  setting the node has; everything else about the operation (which
  elements, how many, cell shape) is derived from what reaches it.

The generated cell is **always periodic**, matching the plane-wave/periodic
codes this pipeline targets (GPAW, VASP, MACE's own training convention). An
isolated atom in a non-periodic cell is a *different* reference energy (no
k-point sampling, no plane-wave cutoff truncation error) and is not offered
here.

:::{note}
Isolated atoms are frequently **spin-polarized** in their ground state (an
isolated Au atom has one unpaired electron; O has two). This node does not
force spin polarization — it has no opinion on your calculator's settings —
but if the Single-Point Calculation downstream of it is not itself
spin-polarized, its isolated-atom energies may not be physically meaningful
references. Check your calculator's spin settings before relying on them.
:::

**Execution model.** Unlike a transform that runs once per Container item,
Single-atom Container runs **once**, reading its parent's *entire* item list
in a single pass, before any node in the pipeline starts — the same way a
Structure Container's own contents are fixed before the pipeline runs,
rather than discovered pass by pass. What it produces then drives its own,
**second and independent** pass count for anything downstream of it: a
Structure Container feeding it 12 structures across 3 elements produces a
3-pass fan-out downstream, regardless of the 12. This second count does
**not** need to match any Structure Container's item count elsewhere in the
graph — the "every container holds the same number of structures" rule
above applies only among Structure Containers, since Single-atom Container's
pass count is a different, unrelated quantity by design. Its own per-pass
job directories are named `atom_batch_<n>_<element>/`, distinct from an
ordinary Container's `batch_<n>_<structure>/`, so the two dimensions stay
visually distinct in the simulations folder even when both appear in the
same run.

:::{note}
When a **Dump Trajectory** node downstream of a Single-atom Container writes
an extxyz training set, every frame it collects from that branch is tagged
`config_type = "IsolatedAtom"` automatically — no toggle needed on the Dump
Trajectory node itself. This is `mace.data.utils`' own convention (verified
against
the installed mace 0.3.15): a frame with `config_type == "IsolatedAtom"`
and exactly one atom is auto-recognized as that element's $E_0$ reference
and excluded from ordinary training by default, which is exactly what an
isolated-atom cell is for.
:::

### Supercell Builder

Takes an integer **3 × 3 transformation matrix P**: the supercell's lattice
vectors are $\mathbf{P}\cdot(\text{old cell})$, row by row. Same mathematics
as Build → *Supercell (Transformation Matrix)*.

Three multipliers would not be enough. A rotated orthorhombic cell of a
hexagonal lattice, a $\sqrt{3}\times\sqrt{3}\,R30^\circ$ surface
reconstruction and a conventional cell built from a primitive one are all
non-diagonal, and none is reachable with `(na, nb, nc)`. A diagonal matrix is
still the common case and the dialog offers it as a shortcut.

$|\det \mathbf{P}|$ is the number of primitive cells in the supercell, and it
is shown live. **det P = 0 disables OK**: the three transformed vectors are
coplanar, so there is no cell — not a degenerate case to tolerate.

Applied to the structure that *reaches* the node, so a relaxation upstream is
expanded after it converges, not before. The identity passes through. A
structure with no periodic cell is refused — repeating a molecule in vacuum is
meaningless.

:::{note}
A diagonal matrix goes through `ase.Atoms.repeat` rather than
`make_supercell`. Same cell either way, but `repeat` preserves the atom
**order** of the original — which is what lets an index list written against
the input still address the same atoms in a downstream Defect Generator.
:::

### Defect Generator

An ordered recipe of edits. The fields follow the action, and only the ones
it uses are editable:

| Action | Asks for |
|---|---|
| Substitute | atom indices + the element to put there |
| Remove | atom indices |
| Add | element + the position (X, Y, Z), Cartesian Å or fractional |

Substitute and Remove address atoms that already exist, so a position means
nothing to them; Add creates one that does not, so there is no index to give.
The unused cells are cleared as well as locked — a greyed-out `0, 0, 0` beside
a Remove row is a value the reader would have to work out is ignored.

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

### Random Noise Setup

Perturbs the ONE structure that reaches it (the ordinary geometry hand-off)
into a randomly-displaced ensemble: frame 0 is always the untouched
reference, followed by the requested number of perturbed variants — the
in-process generator behind the standalone **Simulation → Random Noise
Setup…** wizard, reused here so the two never disagree about what "20 noisy
frames" means.

Double-click it to set:

`Distribution`
: Gaussian (a standard deviation) or uniform (a half-width) — the same
  choice, and the same meaning, as the standalone wizard's.

`Amplitude`
: the displacement scale, in Å.

`Seed`
: reproducible for a given value; each member draws from `seed + member
  index`, so the whole ensemble regenerates identically from the one number.

`Perturb atomic positions` / `Perturb the cell`
: either or both. A cell perturbation moves the lattice vectors and carries
  every atom's *fractional* coordinates through unchanged, so the noise acts
  as a random strain rather than tearing the structure apart.

`Frame count`
: perturbed variants, **not counting** frame 0 (the untouched reference,
  always included on top of this count).

`Cumulative`
: each frame perturbs the *previous* one — a random walk — instead of every
  member independently perturbing the same untouched reference.

`Ramped`
: amplitude scales from zero at frame 0 (already true, since it is
  unperturbed) up to the full configured amplitude at the last frame,
  instead of every member using the same amplitude.

**Acts like a Structure Container downstream.** It is a MULTIPLYING
transform — the same kind DefectGenerator's *one material per defect* mode
and the SQS Generator already are — not a second independent phase the way
[Single-atom Container](#single-atom-container) is: with no Container
upstream at all, the frame count alone becomes the whole batch length (the
"provides multiple structures, like a Structure Container" shape), and with
one upstream, every one of its structures gets the full noisy ensemble —
`5 base structures x 21 frames` is 105 total passes, not a mistake to guard
against. Two multiplying nodes in the same pipeline (this one alongside a
DefectGenerator or SQS Generator) still have to agree on their count, for
the same reason any two of them do.

### TDB Generator (CALPHAD)

Turns a finished **cluster-expansion** ensemble into a CALPHAD thermodynamic
database. It is the only transform with an input slot:

| Node type | Slots, in link order | Staged as |
|---|---|---|
| TDB Generator | formation-energy ensemble | `cluster_expansion.json` |

That is the same file the convex-hull viewer reads, so the hull you look at
and the ensemble the assessment fits cannot drift into two descriptions of
one calculation.

What it does, in one step: take each configuration's formation energy
relative to the two pure endpoints, fit a **Redlich–Kister** excess Gibbs
energy

$$G^{\mathrm{ex}}(x) = x_A x_B \sum_\nu L_\nu (x_A - x_B)^\nu$$

to them by least squares, and write the coefficients as `PARAMETER L(…)`
statements in a `.tdb`. Two files land in the node's results:

`assessment.tdb`
: the database, readable by Calango's own CALPHAD module and by any
  CALPHAD tool.

`calphad_assessment.json`
: the fit's RMS and worst residual, the samples it used, and the static
  convex hull the configurations sit on. A badly fitted database is still a
  *valid* database, so the quality of the fit is recorded beside it rather
  than inside it.

Double-click the node to set the phase name, the Redlich–Kister order and the
temperature range the database declares itself valid over. The two endpoint
elements are optional — the ensemble file names the composition axis, and the
formulas name the other endpoint.

:::{admonition} The assessment is static, and that is a physical statement
:class: warning

A cluster-expansion ensemble carries total energies and no phonons. The
fitted excess energy is therefore a pure **enthalpy**, and every excess
entropy in the emitted database is exactly zero rather than small. A phase
diagram computed from it has solidus and liquidus lines wrong by whatever the
vibrational entropy of mixing is — typically a few tenths of $k_B$ per atom,
which is not negligible near a melting point.

To include the vibrational term, use {menuselection}`Modules --> Alloys -->
CALPHAD --> From DFT…` instead: it accepts a `phonon_dos.json` per configuration and
fits $L_\nu(T) = a + bT$ with a real excess entropy.
:::

:::{admonition} It reads energies, not a structure
:class: note

Because its input is a results file, a TDB Generator writes no
`transformed.extxyz` and claims no workspace tab. Anything linked downstream
of it inherits the structure that was staged into it, not a new one.
:::

(dump-ml-training-data)=
### Dump Trajectory (ML Training Data)

Collects every pass of the fan-out feeding it — its computed structure,
energy, forces and (when available) stress — and writes them as one
**extended-XYZ** training set, ready for an MLIP trainer such as MACE. See
also [Dump Charge Densities](#dump-charge-densities), which collects
volumetric outputs the same way.

| Node type | Slot, in link order | Staged as |
|---|---|---|
| Dump Trajectory | completed calculation | *(the whole parent directory — unused; see note below)* |

Link it downstream of a **Single-Point Calculation** in fan-out mode (i.e.
downstream of a Structure Container), or of a Geometry Optimization or
Molecular Dynamics node. Unlike every other node here, its input link exists
only so the ordinary "at least one parent" contract applies — Dump
Trajectory does not actually read the staged copy. Instead, once the
fan-out's last pass has
finished, it reads **every pass's own result file**
(`single_point.extxyz`, `optimized.extxyz` or `md_final.extxyz`, whichever
the parent task wrote) directly from the run report, because no file in this
application holds every pass's forces at once for it to stage as one slot.

That is also why Dump Trajectory **runs once**, not once per Container item like an
ordinary node downstream of a fan-out: its whole point is to see every pass
at the same time, so re-running it per structure would mean overwriting its
own output on every pass but the last.

Double-click the node to configure:

`Output file`
: where the training set is written. No default — unlike a Supercell's
  2×2×2, there is no path a Dump Trajectory node could guess that would not
  be a claim about your filesystem.

`Energy key` / `Forces key` / `Stress key`
: the extxyz info/array keys the written file uses. An extxyz file has no
  fixed vocabulary for "the reference energy" — every MLIP trainer reads
  whichever keys it is told to. Energy and forces are required; leaving the
  stress key empty omits stress from the file entirely (the honest choice
  when nothing upstream computed one).

`MACE training preset`
: fills the three keys with **`REF_energy` / `REF_forces` / `REF_stress`** —
  `mace.tools.default_keys.DefaultKeys` as shipped in mace 0.3.15, the same
  values `mace_run_train`'s own `--energy_key`/`--forces_key`/`--stress_key`
  default to. Deliberately **not** the bare `energy`/`forces`/`stress` a live
  ASE calculator's own results use: MACE's data loader refuses a bare
  `stress_key="stress"` outright, warning that since ASE 3.23.0b1 that name
  is not safe to round-trip between ASE and MACE, and names this exact
  `REF_` prefix as the fix.

`config_type`
: free text, stamped into every frame's `info['config_type']` when
  non-empty. Left empty, no such key is written.

`Include failed-calculation frames`
: off by default. A pass whose energy could not be recovered — the
  calculation failed, or its result file is missing or unreadable — is
  **dropped** rather than given a placeholder value: a written zero would
  silently teach a model a wrong number. The node reports how many frames
  were excluded, and why, once it runs.

`Append to the file if it exists`
: opens the ASE writer in append mode instead of overwriting.

Once run, the node's face reports the outcome directly — **"97 frame(s)
written"**, with **"3 excluded"** beneath it when any were — rather than the
generic per-pass progress counter every other fan-out node uses (which would
misleadingly read as batch *progress* on a node that ran exactly once). A
`dump_summary.json` beside it records the output path, the frame counts and
the excluded reasons in full.

(dump-charge-densities)=
### Dump Charge Densities

Collects the chosen charge-density (or other volumetric) file each pass of a
fan-out wrote and copies it — or HDF5-compresses it, see
{doc}`/reference/hdf5_density` — into one destination folder, one enumerated
file per pass: `density_0000.cube`, `density_0001.cube`, ... aligned to
frame index. A pass whose job directory holds no file under the chosen
product's name is **skipped**, not compacted away — `density_0001.cube`
missing in the middle of a run means pass 1 had no density, not that pass 2
became "1".

| Node type | Slot, in link order | Staged as |
|---|---|---|
| Dump Charge Densities | completed calculation | *(the whole parent directory — unused; same reason as Dump Trajectory's own slot)* |

Link it downstream of a **Single-Point Calculation** or **Geometry
Optimization** in fan-out mode (i.e. downstream of a Structure Container).
Unlike Dump Trajectory, it does **not** merge several parents — a density
file belongs to exactly one calculation, so `connectNodes()` keeps it to the
ordinary one-parent cap. Like Dump Trajectory it is a **batch aggregator**:
it runs **once**, after the fan-out's last pass, reading every pass's own
result directly from the run report rather than a staged copy — the same
reason, restated for densities rather than structures-with-forces: no file
in this application holds every pass's density at once for it to stage as
one slot.

Double-click the node to configure:

`Destination folder`
: where the enumerated files are written. No default — the same reasoning
  as Dump Trajectory's output path: there is no folder this node could guess
  that would not be a claim about your filesystem. Created if it does not
  already exist.

`File prefix`
: `density_` by default — `density_0000.cube`, `density_0001.cube`, ...

`Density product`
: which file each pass's job directory is searched for. The dropdown lists
  every named volumetric file Calango's own generators/engines can produce
  — GPAW's six (`density_all_electron.cube`, `density_pseudo.cube`,
  `density_spin.cube`, `potential_hartree.cube`, `elf.cube`,
  `kinetic_energy_density.cube`) and VASP's five (`CHGCAR`, `AECCAR0`,
  `AECCAR2`, `LOCPOT`, `ELFCAR`) — because the node is usually configured
  before its parent has ever run, so it cannot know in advance which engine
  will actually feed it. Pick the one your upstream calculation writes; a
  pass from any other engine (or one that simply never wrote that field)
  reports as missing rather than silently picking a different file.

`Compress to HDF5`
: off by default. When on, each collected file is converted through
  `core::VolumetricData::convertToHdf5()` — Calango's compressed HDF5
  container, the same conversion the calculator setup pages' own "Compress
  to HDF5" checkbox uses (see {doc}`/reference/hdf5_density`) — and named
  `density_0000.h5` rather than `density_0000.cube`: ONE extension, not the
  source format's stacked with `.h5`'s. A pass whose density was **already**
  compressed upstream stays compressed either way, copied straight through
  under its own `.h5` name — there is no cube/CHGCAR writer in this codebase
  to decompress an `.h5` back into, so "preserve the source format by
  default" stops there.

Once run, the node's face reports **"87/100 densities written"**, with
**"13 missing"** beneath it when any were — the same "spelled out rather
than the generic K/N suffix" treatment as Dump Trajectory, and for the same
reason: this node also ran exactly once, so a fan-out progress counter would
misread. A `dump_densities_summary.json` beside it records the destination,
the chosen product, the counts and the missing reasons in full.

(dataset-manager)=
### Dataset Manager

The data-preparation step of the pipeline `[noisy structures + isolated
atoms + any other labeled sets] → Dataset Manager → MACE Trainer → trained
MLIP file`: like Dump Trajectory, it **merges every connected parent's own
fan-out** — plus any `.extxyz` files loaded directly onto the node — applies
dataset hygiene, and writes a deterministic, **stratified**
train/validation/test split ready for [MACE Trainer](#mace-trainer)
downstream.

| Node type | Slot, in link order | Staged as |
|---|---|---|
| Dataset Manager | completed calculation | *(the whole parent directory — unused; same reason as Dump Trajectory's own slot)* |

Like Dump Trajectory, its input link exists only so "at least one parent, or
at least one directly-loaded file" is enforced — it does not read the staged
copy, and it is a **batch aggregator**: it runs once, after every fan-out
feeding it has finished its last pass, reading each pass's own result file
directly from the run report. `connectNodes()` gives it the identical
multi-parent exemption Dump Trajectory has, for the identical reason:
concatenation in link order is a well-defined operation on a set of
structures, so "bulk noisy structures from one branch, isolated-atom
references from another" is one Dataset Manager node, not several files
merged by hand.

Double-click the node to configure:

`Output folder`
: where `train.extxyz`, `valid.extxyz` and `test.extxyz` are written. No
  default, for the same reason Dump Trajectory's output path has none.

`Energy key` / `Forces key` / `Stress key`, `MACE training preset`
: the same fields, and the **same preset implementation**, as Dump
  Trajectory's own — REF_energy/REF_forces/REF_stress,
  `mace.tools.default_keys.DefaultKeys` from mace 0.3.15 — so a Dataset
  Manager node feeding MACE Trainer and a Dump Trajectory node feeding a
  hand-run `mace_run_train` can never quietly drift onto different key
  conventions.

`Training` / `Validation` / `Test`
: split percentages (test is whatever is left over), and:

`Random seed`
: the deterministic shuffle seed — the same (frame set, fractions, seed)
  always produces the same split (`core::DatasetSplit::makeStratified()`).

`Dataset hygiene`
: three independently optional steps, each always **reported**, never
  silently applied:
  - **Drop frames missing energy or forces** (on by default) — a frame with
    neither is useless to a trainer and worse than absent if kept.
  - **Drop exact-duplicate frames** (on by default) — identical formula,
    cell and positions to an already-kept frame, checked at full precision
    (a literal duplicate, not a similarity threshold).
  - **Flag (do not drop) frames beyond** an energy/atom threshold (off by
    default) — an outlier is for you to judge, not for the node to discard;
    flagged frames are listed in the node's summary, still written to
    whichever split they landed in.

`Directly loaded files`
: `.extxyz` files added straight to the node (Node A's second input kind,
  alongside connected parents), each optionally **tagged as an
  isolated-atom reference** for a file that did not come through an
  upstream [Single-atom Container](#single-atom-container) and so carries no
  such tag on its own.

**Stratified split.** Every isolated-atom-tagged frame — whether it arrived
via a Single-atom Container branch or a manually-tagged directly-loaded file
— lands in **train** unconditionally; the requested fractions apply to the
*rest* of the pool. An MLIP's E0 reference has to be available at every
stage of training, so a validation- or test-only isolated-atom frame would
silently defeat the point of tagging it at all.

Once run, the node's face reports **"137 frame(s) kept"**, with **"N
dropped"** beneath it when hygiene removed any — the same "ran exactly once,
not once per pass" treatment as the two Dump nodes. A `dataset_manifest.json`
beside it records the output folder, every split's frame count, the
elements covered, the energy range, the hygiene counts, and — this is the
file [MACE Trainer](#mace-trainer) reads — the absolute paths to
`train.extxyz`/`valid.extxyz`/`test.extxyz` plus the energy/forces/stress
key names, since the split itself lives in your chosen output folder, not
under this node's own job directory.

(mace-trainer)=
### MACE Trainer

Trains a [MACE](https://github.com/ACEsuit/mace) machine-learned
interatomic potential from a Dataset Manager node's split. **Simulation**
family — it launches a real job through the ordinary job-runner/Processes-
panel machinery, the same as any other Simulation node — but its setup
dialog is the pre-existing, standalone MACE Trainer dialog (also reachable
from the {guilabel}`Simulation` menu on its own), reused as-is rather than
rebuilt behind the wizard interface every other Simulation node uses.

| Node type | Slot | Staged as |
|---|---|---|
| MACE Trainer | dataset | `dataset_manifest.json` |

Link it downstream of exactly one **Dataset Manager** node. Double-clicking
it opens the MACE Trainer dialog: on first configure, if the linked Dataset
Manager has already produced a manifest (it may not have — a node is
normally configured before its parents have ever run), the dialog's
training-file, validation-file and energy/forces-key fields are **pre-wired
from it** automatically — the "typed output edge" hand-off, in the absence
of any actual edge-typing system on this canvas. Re-opening an
already-configured node instead restores exactly the YAML you last saved,
including any hand edits, rather than re-deriving it and silently discarding
them.

The dialog itself covers model size/architecture presets, the cutoff radius
and per-property loss weights, batch size/epochs/learning rate/seed, device
(cpu/cuda/mps), isolated-atom energy (E0s) handling, stage-two (SWA) and EMA
settings, and an optional Query-by-Committee ensemble — see
{doc}`/simulations/mlip` for the full reference. Two things specific to
running it from this canvas:

**Dependency pre-flight.** Both Run buttons check that `mace-torch` is
importable under the configured interpreter **before** launching anything —
mace-torch is never vendored or hard-depended-on by Calango itself. Missing,
you get a clear message naming the package and `pip install mace-torch`,
and nothing is launched; found, its version is recorded as a comment at the
top of the generated config (run metadata — which mace-torch a config was
actually generated/verified against), and which PyTorch devices are
actually usable (cpu always; cuda/mps only when the installed PyTorch build
and hardware support them) is reported next to the device selector.

**Execution.** Same job infrastructure as every other node — queue, live
log streaming, resume. MACE's own `restart_latest: true` (always emitted)
means a resumed run picks up from its last checkpoint rather than starting
over. The generated launcher also writes a per-epoch metrics file
(`mace_train_<seed>_metrics.json`: loss, energy RMSE, force RMSE, parsed
from MACE's own training log) beside the config — not yet wired into
Calango's live metric *plot*, so today it is data on disk rather than a
chart in the Results panel (see `FUTURE.md`); the raw training log itself
already streams to the live-monitoring view like any other job's output.

**Output.** The trained model file(s) land in the job directory under the
name(s) the config's `name:` field controls (one per Query-by-Committee
seed, if that ensemble is enabled). To use one afterwards, point a MACE
calculator's model source at **Custom file** and browse to it — already
fully wired (`mace.calculators.MACECalculator(model_paths=...)`), just not
yet a single click from a finished run (see `FUTURE.md`).

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

:::{note}
`status` is one field, so a node downstream of a Container only ever shows
its **last** pass's outcome once the run ends — a 100-structure sweep and a
1-structure run otherwise look identical on the canvas the moment either
finishes. A node re-run per Container item therefore also grows a live
**"37/100 done"** counter on its second line, updated after every pass —
the one thing `status` alone cannot show.
[Dump Trajectory](#dump-ml-training-data) reuses the same two numbers for a
different meaning (frames written out of
frames considered — it runs once, not once per pass), spelled out in its own
words instead.
:::

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

## Worked example — training an MLIP end to end

The whole chain `[noisy structures + isolated atoms] → Dataset Manager →
MACE Trainer → trained model`, on one canvas:

1. Add a {guilabel}`Structure Container` node with the structure(s) you want
   a potential for, and a {guilabel}`Random Noise Setup` node linked after
   it — this is the bulk training data: the reference structure plus a set
   of randomly perturbed variants, one pass each.
2. Link a {guilabel}`Single Point` node after Random Noise Setup, with the
   engine you want your reference labels from (a fast one like EMT for a
   dry run; the real DFT engine for a production potential).
3. In parallel, add a **second** {guilabel}`Structure Container` holding
   the *same* structure(s) — this branch supplies isolated-atom references
   rather than bulk data — linked to a
   {guilabel}`Single-atom Container` node, which turns "every unique
   element across what feeds it" into one isolated-atom reference cell per
   element. Link a {guilabel}`Single Point` node after that too.
4. Add a {guilabel}`Dataset Manager` node and link **both** Single Point
   nodes into it (draw two links onto the same node — this is the
   multi-parent exemption [described above](#multiple-connections)).
   Double-click it: press {guilabel}`MACE training preset`, set a split
   (80/10/10 is a reasonable start), choose an output folder, and leave
   hygiene at its defaults.
5. Add a {guilabel}`MACE Trainer` node and link the Dataset Manager into
   it. Double-click it — since Dataset Manager has not run yet, the dialog
   opens with its own defaults; that is normal. Press
   {guilabel}`Check Environment` to confirm `mace-torch` is available under
   the interpreter you have selected, adjust the epoch/architecture
   settings for a first run (a small model and a handful of epochs is
   plenty to confirm the pipeline works end to end before committing to a
   long one), and {guilabel}`Save process node`.
6. {guilabel}`Send to Processes`. Both branches fan out and run; Dataset
   Manager waits for both, merges them (isolated-atom frames pinned to
   train regardless of the split fractions), and writes the split; MACE
   Trainer waits for Dataset Manager and trains on it.
7. Re-open the MACE Trainer node once Dataset Manager has actually run —
   now its training/validation-file and energy/forces-key fields are
   pre-wired from Dataset Manager's own manifest, and you can bump the
   epoch count up for a real run and re-save.
8. Once training finishes, open a MACE calculator's setup elsewhere in
   Calango, pick {guilabel}`Custom trained model`, and browse to the
   checkpoint in the MACE Trainer node's job directory.

The same pipeline works with a real DFT engine at step 2 and a much larger
Random Noise Setup count for genuine training-set scale; nothing about the
graph shape changes, only how long {guilabel}`Send to Processes` takes to
get through it.

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
- A self-contained node's **input geometry** slot holds exactly one parent —
  a second link there is refused, not silently overridden. Several parents
  contributing at once is limited to the numbered slots of an analysis node
  (one parent per named slot) and to
  [Dump Trajectory](#dump-ml-training-data), the one node that merges. See
  [Multiple connections](#multiple-connections).
- A node holds a *snapshot* of its material taken when the node was added;
  later edits to the open document do not retroactively change an already
  configured node's staged geometry.
- {guilabel}`Supercell Builder` repeats along the lattice vectors only. A
  general (non-diagonal) transformation matrix is available in the standalone
  supercell builder, not on the canvas.
