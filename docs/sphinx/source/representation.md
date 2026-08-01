# Appearance and representation

How a structure looks is split across three places, each owning one kind of
decision. The {guilabel}`Representation` dock (zone 8 — see {doc}`/viewport`)
styles the atoms and bonds themselves; the {guilabel}`Spatial References` dock
draws things *onto* the scene that are not atoms — the cell wireframe, the
axes triad and per-atom vector arrows; and the {guilabel}`Lighting` dock owns
the lights. Quick display toggles (element symbols, atomic indices,
hydrogens, gradient bonds) live on the viewport toolbar, where flipping them
does not require an open dock.

---

## The Representation panel

Read top-down, the panel answers *what you are styling* before *how*:

| Row | What it does |
|---|---|
| {guilabel}`Background` | Viewport background color — first, because it frames how every color below reads. Distance fog, when enabled, fades into it. |
| {guilabel}`Casting` | Which **cast** (atom group) the rows below apply to |
| {guilabel}`Shading` | Which BRDF shades the scene — {guilabel}`Classic (Blinn-Phong)`, {guilabel}`PBR` or {guilabel}`Toon` |
| {guilabel}`Style` | Surface finish — {guilabel}`Standard`, {guilabel}`Shiny`, {guilabel}`Matte`, {guilabel}`Glassy` |
| {guilabel}`Mode` | Representation mode (below) |
| {guilabel}`Color by` | Atom coloring mode, plus the gradient editor and value-label buttons |
| {guilabel}`Atom radius`, {guilabel}`Bond width` | Global scale factors, **0.20–3.00×**, default 1.00 — slider for coarse moves, spin box for exact values |
| {guilabel}`Opacity` | Flat per-cast transparency, **0.00–1.00**, default 1.00 |

**Every styling row is per-cast.** All atoms start in cast 0, so with casts
unused the panel behaves as a single global style. Split a substrate and an
adsorbate into two casts (the {guilabel}`Cast change…` button beside the
dropdown) and each gets its own mode, finish, coloring and opacity — a
space-filling metal surface faded to 0.3 under a fully opaque ball-and-stick
molecule is one dropdown switch away.

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

% TODO screenshot: Representation dock beside a two-cast scene — CPK slab in one cast, ball-and-stick molecule in another, Casting dropdown open
```{figure} /_static/img/representation_panel.png
:alt: The Representation dock with per-cast styling controls
:width: 92%
:figclass: screenshot

The Representation panel — every styling row edits the cast selected at the top, so a substrate and an adsorbate can carry different representations in one scene.
```

---

## Coloring atoms

{guilabel}`Color by` offers four mappings, applied consistently to atoms and
to their halves of each bond:

| Mode | Maps |
|---|---|
| {guilabel}`Element (CPK)` | The Jmol palette, with per-element overrides |
| {guilabel}`Coordination number (CN)` | Discrete coordination numbers on a gradient |
| {guilabel}`Generalized CN (GCN)` | Continuous generalized coordination numbers — distinguishes terraces, steps, edges and vertices on nanoparticles and slabs |
| {guilabel}`Custom property` | Any per-atom scalar the structure carries: charges, force magnitudes, extended-XYZ columns, computed fields |

The rest of the mapping lives in the {guilabel}`Custom Gradient Coloring`
dialog (the color-filter button on the {guilabel}`Color by` row):

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

The `#` button beside the dropdown prints each atom's mapped value directly
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

The {guilabel}`Lighting` dock edits **up to four directional lights** with
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

% TODO screenshot: Lighting dock with three lights configured, viewport showing distinct key/fill/rim separation on a nanoparticle
```{figure} /_static/img/representation_lighting.png
:alt: The Lighting dock editing a three-light setup
:width: 92%
:figclass: screenshot

Three directional lights — warm key, cool fill and a back light separating silhouettes from the background.
```
