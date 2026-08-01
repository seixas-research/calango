# Editing structures

Every operation in this chapter is a single undo step.
{menuselection}`Edit --> Undo` ({kbd}`Ctrl+Z`) and {menuselection}`Edit -->
Redo` ({kbd}`Ctrl+Shift+Z`) walk a **per-document history up to 50 states
deep** — each open tab keeps its own stack.

---

## Atoms

| Action | Where | What it does |
|---|---|---|
| {menuselection}`Edit --> Add Atom` | {kbd}`Ctrl+Shift+A` | Pick the element from the graphical periodic table and type exact $x$, $y$, $z$ coordinates. For placing atoms by eye, use the viewport's Insertion mode ({kbd}`I`) instead — see {doc}`/viewport`. |
| {menuselection}`Edit --> Selection --> Change Element of Selection` | — | Replaces the species of every selected atom, again via the periodic table |
| {menuselection}`Edit --> Selection --> Translate Selection` | — | Shifts the selection by a $\Delta x$, $\Delta y$, $\Delta z$ vector in Å |
| {menuselection}`Edit --> Delete Selected Atoms` | {kbd}`Delete` | Removes the selection; with the viewport focused in Selection mode, {kbd}`Backspace` does the same |

Deleting atoms re-indexes everything that referenced them — bond overrides,
bond orders and per-atom fields all follow correctly.

Wherever an element must be chosen, Calango opens a graphical periodic
table: **$Z = 1$ to 118** in the standard 18-group layout, f-block below,
colored by chemical family. One click selects and closes.

---

## The Edit Structure dialog

{guilabel}`Edit Structure…` in the {guilabel}`Structure` panel opens a
two-section editor over a working copy — the unit cell above,
{guilabel}`Atomic Configurations` below. Changes land on the document (and
its undo stack) only when the dialog is accepted.

### Coordinates

The table shows **one coordinate triple**, and the {guilabel}`Fractional
coordinates` checkbox decides which. Unchecked — the default — gives
Cartesian $x$, $y$, $z$ in Å; checked converts the values in place and
relabels the columns $u$, $v$, $w$. Both are editable: a fractional edit
recombines all three components of that row and converts back through the
cell, so changing $u$ alone does not discard $v$ and $w$. The checkbox is
disabled without a unit cell — fractional coordinates there are not merely
unavailable, they are undefined.

:::{note}
One triple rather than two side by side is what makes room for the extended
per-atom properties. It also removes a real hazard of the old layout: with
$x$…$w$ all editable at once it was possible to type into a Cartesian cell
and a fractional cell of the same row before either was committed, and only
one of the two edits survived.
:::

### Extended per-atom properties

Any per-atom array the structure carries gets its own column, to the right
of the magnetic moments: partial charges (`initial_charges`), velocities,
forces — anything an extended-XYZ file put in `atoms.arrays`. A scalar array
gives one column; a vector array gives three, suffixed $x$, $y$, $z$.
Columns appear and disappear with the data, so a plain `.xyz` opens with
none and an MD frame opens with several.

**These columns are read-only, and deliberately so — they are results.** A
charge came out of a Bader partitioning, a velocity out of an integrator.
Typing over them in a geometry editor would leave a frame whose arrays no
longer correspond to anything that was computed, with no sign of it in the
file. The magnetic moments remain editable, through the spin-mode columns,
because those are an *input*.

### Sorting

{guilabel}`Sort by` reorders the atoms by element or by any coordinate,
ascending or {guilabel}`descending`. This **renumbers the atoms** — it
permutes the structure itself, not just the view. That is normally the
point: grouping by element is what VASP's POSCAR/POTCAR pairing wants, and
sorting by $z$ gives a slab a layer-ordered index list you can select ranges
from. Everything index-aligned travels with its atom: per-atom fields,
magnetic moments, residue annotation, bond overrides and manual bond orders.
The sort is *stable*, so atoms that tie on the key keep their existing
relative order rather than being reshuffled on each press.

### Spin polarization

The {guilabel}`Spin polarization` dropdown adds editable magnetic-moment
columns:

| Mode | Columns | Meaning |
|---|---|---|
| {guilabel}`Unpolarized` | none | **No moments — not moments of zero.** An all-zero column would still switch a calculator into spin-polarized mode and cost the run its speed for nothing. |
| {guilabel}`Collinear Spin-Polarized` | one signed {guilabel}`m (μB)` | The sign is what makes a seed antiferromagnetic rather than ferromagnetic — which is why it is per atom, not one number |
| {guilabel}`Non-collinear Spin` | {guilabel}`mx`, {guilabel}`my`, {guilabel}`mz` | Full moment vectors |

What you type is stored as the structure's *initial* magnetic moments
(`initial_magmoms`). It travels with the geometry into every calculation —
the simulation wizards read these rather than asking for a list — and can be
drawn via {menuselection}`Spatial References --> Vectors -->
Initial magnetic moments` ({doc}`/representation`). That overlay is
deliberately separate from {guilabel}`Magnetic moment`, which shows moments
a calculation *produced*: a guess and a result are different claims, and the
two use different arrow colors. The dialog opens on whatever spin mode the
structure is already in, and the summary line reports the net moment
alongside $\Sigma|m|$ — a column of $2.2$ and a column of $\pm 2.2$ look
nearly identical and mean opposite things.

% TODO screenshot: Edit Structure dialog with the Fractional coordinates toggle on, collinear moment column and a read-only forces column visible
```{figure} /_static/img/editing_structure_dialog.png
:alt: The Edit Structure dialog showing fractional coordinates and per-atom properties
:width: 92%
:figclass: screenshot

The Edit Structure dialog — one coordinate triple (Cartesian or fractional), editable moments, read-only computed arrays.
```

---

## Bond perception

By default Calango perceives bonds by distance: atoms $i$ and $j$ are bonded
when

$$
d_{ij} < t \cdot \bigl(r_\mathrm{cov}(i) + r_\mathrm{cov}(j)\bigr),
$$

with covalent radii from Cordero *et al.* and a tolerance $t$ you control.
With a periodic cell the minimum-image convention applies, so bonds across
cell boundaries are found and drawn correctly.

{menuselection}`Edit --> Bond Editor` ({kbd}`Ctrl+B`) exposes the automatic
rule and the manual overrides. {guilabel}`Automatic bond detection` can be
turned off to draw *only* manual bonds — the right choice for coarse-grained
or schematic figures — and the {guilabel}`Covalent cutoff multiplier` is the
tolerance $t$, **0.50–2.50, default 1.15**, applied live so you can dial it
until the picture is right. Manual rules come in three kinds, one tab each:

- {guilabel}`By Atomic Indices` — a specific pair by 1-based index (or
  {guilabel}`From Selection` from a two-atom viewport selection), created as
  a {guilabel}`Single`, {guilabel}`Double`, {guilabel}`Triple` or
  {guilabel}`Aromatic` bond, or suppressed. The info line shows the pair's
  distance next to the automatic cutoff for that pair, which makes borderline
  bonds obvious.
- {guilabel}`By Chemical Elements` — an element pair (Si–O, C–C) and a
  min/max distance window (0.0–20.0 Å, default 0.0–2.0): every matching pair
  in range is bonded — or unbonded — in one action, with a live match count.
- {guilabel}`Hydrogen Bonds` — geometric D–H⋯A perception, rendered as
  dashed lines: maximum donor–acceptor separation 2.0–6.0 Å (default
  **3.5 Å**) and minimum D–H⋯A angle 90–180° (default **120°**), plus a
  color and a live count. These are *not* stored overrides — they re-derive
  from the geometry, so they follow a trajectory instead of freezing at the
  frame they were detected on.

{guilabel}`Active overrides` lists every override as *added* or *suppressed*
with the pair and its distance; {guilabel}`Clear Override` and
{guilabel}`Clear All` undo them. Overrides live on the structure and are
saved in the project file.

### Rules apply to the whole trajectory

Every rule entered in the Bond Editor is applied to **every frame of the
active trajectory**, not only the frame on screen. Bonding is a statement
about the chemistry of a system, and a system does not change its chemistry
between two samples of the same run — an override that existed on frame 0
and vanished on frame 1 was never a physical statement, only an artifact of
which frame was displayed when the button was pressed. It also made every
trajectory-wide export (the extended-XYZ writer, the ray-traced film, the
animated GIF — see {doc}`/output`) disagree with the viewport.

The two kinds of rule propagate *differently*, and the difference is the
point:

- **Index rules name atoms.** An atom keeps its index for the whole run, so
  the same pair — and the same bond order — is marked on every frame.
- **Element rules name a geometric condition** — every Si–O pair between 1.4
  and 1.9 Å — so they are re-evaluated against each frame's own coordinates.
  Copying the first frame's match list forward would freeze a bond onto a
  pair that has since dissociated: precisely the event a reactive trajectory
  is being watched for, rendered invisible.

The scope is stated on the panel where the rules are entered ("Applied to
all 250 trajectory frames."), and frames whose atom count differs from the
displayed one are skipped and reported — an index there refers to a
different atom.

% TODO screenshot: Bond Editor on the By Chemical Elements tab, Si–O rule with a match count, override list below
```{figure} /_static/img/editing_bond_editor.png
:alt: The Bond Editor with an element-pair rule and the active override list
:width: 92%
:figclass: screenshot

The Bond Editor — one element rule bonds every matching pair on every frame; the override list records what was added or suppressed.
```

### Bond orders

**Calango never guesses multiple bonds.** Every perceived bond starts as a
single bond, and orders are assigned deliberately in the Bond Editor's
{guilabel}`By Atomic Indices` tab — distance-based perception of double and
triple bonds is unreliable, particularly for inorganic and metallic systems
where short contacts are common. An order of two or three renders as that
many parallel cylinders, and also forces the bond to exist even outside the
automatic cutoff. Orders persist in project files.

:::{note}
A consequence worth knowing: molecules like benzene or CO₂ render with
all-single bonds when first opened. Assign the orders you want to display —
they are a presentation property, not an input to any calculation.
:::

---

## Geometry constraints

{guilabel}`Geometry constraints…` in the Geometry Optimization wizard
defines which degrees of freedom a relaxation is not allowed to move. Two
tabs, because the two ways of naming the frozen atoms are genuinely
different jobs:

- {guilabel}`Atoms` — a per-atom table (index, element, $x/y/z$) with a
  per-row $x$/$y$/$z$ freeze mask: the direct answer to "hold *this* atom",
  or "let that adatom slide in the surface plane but not along $z$". Rows
  can be filtered and mass-assigned, so a 400-atom slab does not have to be
  ticked one row at a time, and a clear button frees every row again.
- {guilabel}`Regions` — bounds along one Cartesian axis (the classic
  "freeze everything with $z < 5$ Å", i.e. the bottom layers of a slab) plus
  the same axis mask. **A region keeps its bounds, not the atoms they
  currently select**: the generated script re-evaluates the bounds against
  the geometry it reads, so re-running on a thicker slab still freezes the
  bottom rather than whatever the old indices pointed at.

A summary line counts what is frozen. The result is translated into ASE
`FixAtoms` / `FixCartesian` objects in the generated script — see
{doc}`/simulations/scripts`.
