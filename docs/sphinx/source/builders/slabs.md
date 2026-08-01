# Supercells and surface slabs

Two tools turn a bulk crystal into the geometry a surface calculation actually needs:
the supercell dialog, which repeats or re-shapes the periodic cell, and the Surface Slab
wizard, which cleaves an oriented slab out of it. Both leave the parent structure
untouched — the supercell transform requires a defined cell, and the slab wizard opens
its result as a new tab.

---

## Supercells

Supercell creation lives on the {guilabel}`Structure` panel's action row (the
{guilabel}`Supercell…` button), not in a menu — it is a whole-structure transform, like
centring and vacuum padding, and it sits with them. It is disabled until the structure
has a periodic cell: a supercell needs vectors to repeat.

The dialog takes a full **3×3 integer transformation matrix** $P$, not just three repeat
counts. The new lattice vectors are integer combinations of the old ones,

$$
\mathbf{A}_i = \sum_j P_{ij}\, \mathbf{a}_j,
$$

so diagonal matrices give plain repetitions while off-diagonal entries build rotated or
sheared cells — a $\sqrt{2}\times\sqrt{2}$ surface cell, an orthogonalised monoclinic
box. Each entry ranges from −20 to 20; the default is the identity. A lattice preview
draws the original cell dashed and the transformed cell solid, and a label reports
$\det P$ and the resulting atom count live — $\det P$ is the volume multiple, and **a zero
determinant is rejected** because three coplanar vectors are not a cell.
{guilabel}`Reset to identity` starts over.

---

## The Surface Slab wizard

{menuselection}`Build --> Surface Slab` cleaves a slab from the bulk structure in the
current tab through four guided stages. Unlike a plain Miller-index dialog, **each stage
shows the consequence of a choice before you commit to it**. The cutting itself runs
through `ase.build.surface`, which rotates the slab so the surface normal lies along
$+z$.

### Stage 1 — Miller index and surface normal

Set the Miller indices $(h\,k\,l)$ with three spin boxes (−9 to 9), or work the other
way round: drag the red **u** and green **v** handles on the axonometric lattice canvas
to any two lattice points, and Calango computes the plane they span — the indices are
exactly $\mathbf{p} \times \mathbf{q}$ for handle coefficients $\mathbf{p}, \mathbf{q}$,
reduced by their gcd. The canvas header shows the current coefficients and the resulting
$(h\,k\,l)$ live; the info line reports $|u|$, $|v|$ and the angle between them.

Dragged handles snap to the nearest lattice point that keeps the pair non-collinear — a
collinear pair spans no plane. $(0\,0\,0)$ is rejected as not a plane, and
{guilabel}`Next` unlocks only after a one-layer test cut succeeds for the chosen
orientation.

% TODO screenshot: Stage 1 with the u/v handles dragged to span a (111) plane on an fcc lattice, the plane parallelogram highlighted
```{figure} /_static/img/builders_slabs_orientation.png
:alt: The orientation canvas with draggable u and v vectors spanning a lattice plane and the computed Miller indices in the header
:width: 92%
:figclass: screenshot

Stage 1. Dragging a handle to another lattice point recomputes the nearest integer (h k
l) — the spin boxes and the canvas stay in sync in both directions.
```

### Stage 2 — in-plane cell vectors

Looking straight down the surface normal, this stage sets the in-plane supercell. Two
spin boxes, {guilabel}`Repeat a_slab (na)` and {guilabel}`Repeat b_slab (nb)`, each
**1–20 with default 1**, tile the surface cell; the canvas draws the primitive surface
cell in orange inside the $n_a \times n_b$ supercell in blue, with the single surface
layer's atoms tiled across it. The info line reports $|a_\text{slab}|$,
$|b_\text{slab}|$, their angle, and the total surface area in Å².

A wider surface cell is not a luxury for adsorption or solvation work — it is what keeps
an adsorbate from interacting with its own periodic image.

### Stage 3 — thickness and terminations

Calango builds a reference stack of **eight bulk repeats** and clusters its atoms into
atomic layers with a **0.10 Å** $z$-tolerance. The cross-section canvas draws each layer
as a dashed line labelled with its $z$ coordinate, with the current selection band
highlighted between an orange *top termination* and a teal *bottom termination*.

| Control | Effect |
|---|---|
| Click a layer | Moves whichever termination boundary is nearer the click — either boundary can be set directly |
| {guilabel}`Layers in slab` | Sets the layer count numerically (up to the stack's layer count) |
| {guilabel}`Thickness` | The same choice in Å; it snaps to whole atomic layers above the bottom termination |

The default selection is **8 layers** from the bottom of the stack. The info line reads
the selection back, for example *Selected: layers 1 – 8 of 16 (8 layers, 12.450 Å)*.

:::{important}
For compound crystals the termination *is* a physical choice: an MoS₂ slab cut
mid-sandwich exposes a different chemistry than one cut between sandwiches, and a polar
oxide slab can be built dipolar without noticing. The cross-section view exists so the
terminating species is chosen by eye, not inherited by accident.
:::

% TODO screenshot: Stage 3 cross-section with 16 layers, an 8-layer band selected between the orange and teal termination lines
```{figure} /_static/img/builders_slabs_termination.png
:alt: Cross-section canvas showing dashed atomic-layer lines, a highlighted selection band, and labelled top and bottom terminations
:width: 92%
:figclass: screenshot

Stage 3. Clicking a layer moves the nearer termination boundary; count and thickness
stay synchronised with the picture.
```

### Stage 4 — vacuum and orthogonalization

{guilabel}`Vacuum spacing` and {guilabel}`Bottom vacuum` pad the cell along the surface
normal, each **0–80 Å with a default of 15 Å**.
{guilabel}`Symmetric vacuum (centered slab)` is checked by default and mirrors the top value to the bottom; uncheck it to set
the two independently — an asymmetric slab with a dipole correction, for instance.

{guilabel}`Orthogonalize c-axis (box cell)`, also checked by default, forces the third
cell vector perpendicular to the surface, giving a rectangular box. Unchecking it keeps
ASE's native (possibly tilted) $c$ direction, stretched to the slab height.

A live 3D preview shows the finished slab with its atom count, slab thickness and total
cell height. {guilabel}`Finish` inserts it as a new tab labelled, for example, *(111)
slab, 8 layers, 2×2 supercell*.

---

## What the wizard does not do

**The cut is purely geometric.** Atoms keep their ideal bulk positions; no surface
relaxation, rumpling or reconstruction is applied. A Si(111) slab from this wizard is
the 1×1 bulk-truncated surface, not the 7×7 reconstruction — relax the slab (with the
bottom layers fixed, typically) before computing a surface energy or work function. The
available terminations are also limited to the stacking sequence of the bulk you started
from: the wizard selects among the layers the crystal provides, it does not invent new
ones.

---

## A worked example: Pt(111)

1. {menuselection}`Build --> From Database`, {guilabel}`Bulk` tab: type `Pt` — the
   reference state auto-fills fcc with the experimental lattice constant — and press
   {guilabel}`Build Crystal` (see {doc}`/builders/database`).
2. {menuselection}`Build --> Surface Slab`: set $(1\,1\,1)$, or drag **u** and **v**
   onto the close-packed plane and watch the header agree.
3. Stage 2: 2 × 2 in-plane repeats — enough for a single adsorbate at 1/4 ML coverage.
4. Stage 3: click layers, or type, until the info line reads 4 layers.
5. Stage 4: keep the 15 Å symmetric vacuum and the orthogonal box; {guilabel}`Finish`.

The new tab — *(111) slab, 4 layers, 2×2 supercell* — is ready for
{doc}`/builders/adsorption`, and the parent bulk tab is still open for the bulk
reference energy that a surface-energy calculation needs.
