# Appearance and representation

How a structure looks is split across three places, each owning one kind of
decision. The {guilabel}`Representation` dock (zone 8 — see {doc}`/viewport`)
styles the atoms and bonds themselves; the {guilabel}`Spatial References` dock
draws things *onto* the scene that are not atoms — the cell wireframe, the
axes triad and per-atom vector arrows; and the {guilabel}`Light` tab of {guilabel}`Visual Effects` owns
the lights. Quick display toggles (element symbols, atomic indices,
hydrogens, gradient bonds) live on the viewport toolbar, where flipping them
does not require an open dock.

---

## The Representation panel

Read top-down, the panel answers *what you are styling* before *how*:

| Row | What it does |
|---|---|
| {guilabel}`Background` | Viewport background color — first, because it frames how every color below reads. Distance fog, when enabled, fades into it. |
| {guilabel}`Cast` | Which **cast** (atom group) the rows below apply to |
| {guilabel}`Shading` | Which BRDF shades the scene — {guilabel}`Classic (Blinn-Phong)`, {guilabel}`PBR` or {guilabel}`Toon` |
| {guilabel}`Style` | Surface finish — {guilabel}`Standard`, {guilabel}`Shiny`, {guilabel}`Matte`, {guilabel}`Glassy` |
| {guilabel}`Mode` | Representation mode (below) |
| {guilabel}`Color by` | Atom coloring mode |
| {guilabel}`Atom radius`, {guilabel}`Bond width` | Global scale factors, **0.20–3.00×**, default 1.00 — slider for coarse moves, spin box for exact values |
| {guilabel}`Opacity` | Flat per-cast transparency, **0.00–1.00**, default 1.00 |
| *(bottom button row)* | Every editor that opens a window — see below |

The **bottom button row** gathers everything that costs a dialog, in order of
what it edits, widening outward: {guilabel}`Element Settings…` (one element),
{guilabel}`Bond Editor…` (the bonds between elements),
{guilabel}`Edit Polyhedral…` (the polyhedron a coordination shell forms),
{guilabel}`Phase colors…` (the structure a whole neighbourhood forms), then
{guilabel}`Cast colors…`, {guilabel}`Edit gradient coloring…` and the
{guilabel}`Show CN / GCN values` toggle. The last three used to ride on the
{guilabel}`Color by` row; they moved down here so that row is a plain
full-width dropdown again, and so a click anywhere in the strip means the same
kind of thing.

**Every styling row is per-cast.** All atoms start in cast 0, so with casts
unused the panel behaves as a single global style. Split a substrate and an
adsorbate into two casts (the {guilabel}`Cast change…` button beside the
dropdown) and each gets its own mode, finish, coloring and opacity — a
space-filling metal surface faded to 0.3 under a fully opaque ball-and-stick
molecule is one dropdown switch away.

(per-frame-cast)=
### Per-frame Cast

The **casts themselves** ({guilabel}`Cast colors…`'s palette, and each
cast's mode/finish/scale/opacity above) are one global set, shared by the
whole session — "Epoxide is amber" does not change frame to frame. What
*can* vary per frame is **which cast each atom belongs to**: a trajectory
may carry, per frame, its own override of the usual atom-to-cast assignment.

This is what the Graphene Oxide Builder's MDMC workflow uses (see
{doc}`/builders/nanomaterials`): a functional group relocating from one
carbon to another over the course of the run means the *set of atoms* that
counts as "epoxide-carbon" changes every accepted move, even though the
casts themselves ("Epoxide", amber) do not. Scrubbing or playing back such a
trajectory recolors the affected carbons live, frame by frame, as the
groups hop.

A frame with no override of its own falls back to the ordinary global
assignment — an existing project, or a trajectory nothing ever gave a
per-frame override, behaves exactly as before: one assignment, unchanged as
you step through frames. There is no separate UI for authoring a per-frame
override by hand today; it is written by whatever produced the trajectory
(currently, MDMC's own cast-redefinition step) and saved with the project
alongside the global cast.

### Representation modes

| Mode | Drawn as |
|---|---|
| {guilabel}`Ball-and-Stick` | Spheres at a fraction of the covalent radius, bonds as cylinders — the default |
| {guilabel}`Space-filling` | Spheres at roughly the van der Waals radius; bonds hidden inside |
| {guilabel}`Wireframe` | Bonds as lines, points at atom positions — cheapest for very large systems |
| {guilabel}`Polyhedral` | Coordination polyhedra; {guilabel}`Edit Polyhedral…` sets opacity, edge wireframe and per-cation coordination cutoffs |
| {guilabel}`Ribbon Diagram` | Chain ribbons following each α-carbon trace — needs the residue annotation of a PDB / PDBx-mmCIF file |
| {guilabel}`Molecular Surface` | Solvent-style surface replacing the per-atom spheres — also residue-annotated files |
| {guilabel}`Licorice` | No atom spheres; every bond one uniform tube — standard for organics, where ball-and-stick spheres crowd the skeleton out of view |

### Shading and finish

{guilabel}`Shading` selects the lighting model for atoms *and* bonds together
— {guilabel}`Classic (Blinn-Phong)`, {guilabel}`PBR` or {guilabel}`Toon`. A
model the graphics driver cannot run stays listed but greyed, with the
driver's reason in its tooltip. {guilabel}`Style` then picks the finish:
{guilabel}`Standard` (moderate highlights), {guilabel}`Shiny` (small, crisp
highlights), {guilabel}`Matte` (diffuse only — best for print figures, where
highlights read as artifacts) and {guilabel}`Glassy` (alpha-blended with a
Fresnel rim, so inner atoms stay visible through outer shells). Standard,
Shiny and Matte configure the Blinn-Phong branch and grey out under PBR and
Toon; **Glassy is a translucency pass, not a BRDF, so it stays live under
every shading model**. Glassy composes with the {guilabel}`Opacity` slider —
a glassy cast faded to 0.3 is fainter than an opaque glassy one. The finish
applies to every lit mesh (atom spheres, bond cylinders, cell tubes), so a
figure reads as one material — including in ray-traced exports
({doc}`/output`).

% TODO screenshot: Representation dock beside a two-cast scene — CPK slab in one cast, ball-and-stick molecule in another, Cast dropdown open
```{figure} /_static/img/representation_panel.png
:alt: The Representation dock with per-cast styling controls
:width: 92%
:figclass: screenshot

The Representation panel — every styling row edits the cast selected at the top, so a substrate and an adsorbate can carry different representations in one scene.
```

---

## Coloring atoms

{guilabel}`Color by` offers six mappings, applied consistently to atoms and
to their halves of each bond:

| Mode | Maps |
|---|---|
| {guilabel}`Element (CPK)` | The Jmol palette, with per-element overrides |
| {guilabel}`Coordination number (CN)` | Discrete coordination numbers on a gradient |
| {guilabel}`Generalized CN (GCN)` | Continuous generalized coordination numbers — distinguishes terraces, steps, edges and vertices on nanoparticles and slabs |
| {guilabel}`Custom property` | Any per-atom scalar the structure carries: charges, force magnitudes, extended-XYZ columns, computed fields |
| {guilabel}`Cast` | One flat color per cast — the substrate-vs-adsorbate figure, where the groups rather than the elements carry the story |
| {guilabel}`Phase` | The **local crystal structure** each atom's neighbours form (see below) |

The first four are scalar mappings on a gradient; {guilabel}`Cast` and
{guilabel}`Phase` are **nominal** — their categories have no ordering, so
they take a flat color each rather than a ramp, edited from the button row at
the bottom of the panel.

The rest of the scalar mapping lives in the {guilabel}`Custom Gradient Coloring`
dialog (the color-filter button in that bottom row):

- {guilabel}`Gradient` — **13 color maps**: Viridis, Plasma, Turbo, Inferno,
  Magma, Cividis, Hot, Afmhot, Coolwarm, Rainbow, Greys, Spectral, Gnuplot.
- {guilabel}`Invert palette` — reverses the scalar-to-color mapping,
  matching matplotlib's `_r` variants.
- {guilabel}`Property` — which per-atom scalar field is mapped (Custom
  property mode).
- {guilabel}`Range` — explicit min/max bounds; editing either switches off
  {guilabel}`Auto-scale to data`, which otherwise spans the property's own
  minimum and maximum. **For trajectories, auto-scale spans the range over
  every frame, not just the displayed one** — a ramp that renormalized per
  frame would make the same color mean different values as the animation
  plays.

The `#` button in the bottom row prints each atom's mapped value directly
on the viewport — the ramp says *which* atoms differ; the labels say *by how
much* (a GCN of 6.75 against 7.50 is a distinction no color scale conveys).
It is disabled in Element mode, which has no scalar to print. Color mapping
is recomputed for every frame during trajectory playback, so CN, GCN and
property maps stay live throughout an animation. The {guilabel}`Gradient
bonds` toolbar toggle blends each bond smoothly from one atom's color to the
other's instead of the classic half-and-half split.

:::{admonition} Choosing a gradient
:class: tip
Viridis and Cividis are perceptually uniform and color-vision safe; Coolwarm
and Spectral are diverging maps — right for signed quantities such as charges
or potentials, where zero should read as neutral. Rainbow is included because
reviewers sometimes ask for it, not because it is a good default.
:::

### Local structural phase

{guilabel}`Color by: Phase` labels every atom with the **crystal structure its
neighbours form**, using adaptive common-neighbour analysis (a-CNA, Stukowski
2012). Seven labels:

| Label | Signature | Default color |
|---|---|---|
| FCC | 12 neighbours, all (4,2,1) | green |
| HCP | 12 neighbours, 6 × (4,2,1) + 6 × (4,2,2) | red |
| BCC | 14 neighbours, 6 × (4,4,4) + 8 × (6,6,6) | blue |
| Icosahedral | 12 neighbours, all (5,5,5) | yellow |
| Cubic diamond | 4 neighbours whose 12 *second* neighbours form an fcc shell | light blue |
| Hexagonal diamond | as above, with an hcp second shell | cyan |
| Other / unidentified | nothing matched | grey |

**The label is per atom, not per cell**, and that is the point: in a
nanoparticle the core reads fcc while the {111} facets read hcp, and inside a
deformed metal the hcp-labelled planes *are* the stacking faults — a coherent
twin boundary shows as two adjacent hcp planes in an otherwise fcc grain. The
interesting objects are the places where the label changes.

{guilabel}`Other` is a real answer rather than a failure: every surface atom,
every defect core and all of a liquid land there, and in a melt it is correct
for nearly every atom. It is drawn a quiet grey for that reason — it is
usually most of a real structure, and a loud color for "unidentified" would
swamp the phases the figure is about.

The cutoffs are **adaptive**: each atom's bond cutoff is derived from its own
neighbour distances, placed midway between the shell that must be included and
the one that must be excluded. A single global cutoff cannot do this — the
value that resolves fcc's first shell from its second falls in the middle of
bcc's first-plus-second shell, so a cell containing both phases (which is what
a martensitic transformation *is*) would have no correct choice. There is
therefore no cutoff to set, and the analysis survives the thermal disorder of
an MD snapshot without tuning.

{guilabel}`Phase colors…` (beside {guilabel}`Edit Polyhedral…` in the bottom
button row) edits the seven colors and reports **how many atoms carry each
label**, with percentages — "how much of this cell is still fcc" is often the
whole question, so the dialog stays reachable in every color mode rather than
only under Phase coloring. {guilabel}`Reset` returns a structure to the
standard CNA palette, which follows the OVITO / AtomEye convention so a
Calango figure and one from any other structure-identification tool can be read
side by side.

Diamond has no CNA signature of its own — a four-fold-coordinated atom shares
almost no neighbours with anyone — so it is identified indirectly, from its
second shell. {guilabel}`Detect diamond structures` switches that extra pass
off for pure-metal trajectories where no diamond phase can appear.

### Per-element styling

{guilabel}`Element Settings…` lists the elements present in the structure —
each row a color swatch, a radius scale (**10–300 %**, where exactly 100 %
means "no override") and a {guilabel}`Reset` button; {guilabel}`Reset All`
clears every override. Overrides can be saved to and loaded from JSON
presets, keyed by element symbol so a preset transfers between structures.
**Loading a preset replaces the current overrides rather than merging** — a
preset describes a complete look.

---

## Spatial references — cell, axes, vectors

The {guilabel}`Spatial References` dock gathers the three overlays drawn onto
the scene, one tab each.

**Unit cell** — wireframe visibility, an optional Voronoi (Wigner–Seitz)
cell instead of the parallelepiped, translucent face fill, cell color, line
style (solid / dashed / dotted), opacity, and {guilabel}`Cell line width`
(**1.0–8.0**, default 2.0). Two periodic-image aids live here as well: a
toggle that draws the images the home cell's bonds actually reach — so a bond
crossing the boundary terminates on an atom instead of stopping in mid-air —
and {guilabel}`Show neighboring cells…`, which draws whole replicas of the
surrounding cells. Both are pure rendering duplications; the structure itself
is untouched.

:::{note}
Core-profile OpenGL clamps hardware line width to one pixel — **any width
above 1 renders the cell edges as lit tubes** rather than wide lines. The
tubes are shaded like the rest of the scene, so they also read correctly in
ray-traced exports.
:::

**Axes triad** — the corner orientation indicator: visibility, arrowheads at
the tips, {guilabel}`Cartesian (X, Y, Z)` or {guilabel}`Lattice vectors (a1,
a2, a3)` labelling, and on-screen size **48–240 px** (default 92).

**Vectors** — the per-atom arrow overlay: {guilabel}`Velocity`,
{guilabel}`Force`, {guilabel}`Magnetic moment` or {guilabel}`Initial magnetic
moments`, drawn as lit 3D arrows. **One overlay is shown at a time** — the
arrows share a single length scale and two properties drawn at once would
overlap illegibly. {guilabel}`Vector scale` is relative to a calibrated
baseline (**0.1–10.0×**, where 1.0× is half an ångström of arrow per field
unit — eV/Å for forces, μB for magnetic moments); {guilabel}`Vector width`
scales the arrow thickness (**0.1–5.0×**), head included, so the arrow stays
proportioned — thin arrows keep a dense magnetic structure readable. Each
property remembers its own arrow color, and overlays the current frame
carries no data for are greyed out. Forces and velocities stream during live
runs, so the overlay works mid-trajectory ({doc}`/simulations/jobs`).

% TODO screenshot: Spatial References dock on the Vectors tab, magnetic-moment arrows on an antiferromagnetic structure
```{figure} /_static/img/representation_vectors.png
:alt: Per-atom magnetic-moment arrows drawn by the Vectors tab
:width: 92%
:figclass: screenshot

The Vectors tab drawing initial magnetic moments — one overlay at a time, scaled against a fixed baseline so lengths stay comparable between frames.
```

---

## Lighting

The {guilabel}`Light` tab of the {guilabel}`Visual Effects` dock edits **up to four
directional lights** with
Blinn-Phong shading. A fresh viewport starts with the two-light studio
default — a warm key light carrying the speculars and a soft, cooler fill
from the opposite side. Select a light in the list, then edit:

- {guilabel}`Direction (view space)` — three components, −5 to 5. Directions
  are in *view* space, so the lighting stays fixed relative to the viewer as
  you orbit — the structure turns, the studio does not.
- {guilabel}`Ambient`, {guilabel}`Diffuse`, {guilabel}`Specular` — per-light
  colors.

{guilabel}`Add` appends a fill light from the opposite side;
{guilabel}`Remove` deletes the selected one — the last light cannot be
removed, since a scene with no lights renders black. Light sets can be saved
to and loaded from JSON presets, and {guilabel}`Reset lights` restores the
two-light default.

% TODO screenshot: the Light tab with three lights configured, viewport showing distinct key/fill/rim separation on a nanoparticle
```{figure} /_static/img/representation_lighting.png
:alt: The Light tab editing a three-light setup
:width: 92%
:figclass: screenshot

Three directional lights — warm key, cool fill and a back light separating silhouettes from the background.
```
