# Volumetric data

Three-dimensional scalar fields — charge densities, electrostatic
potentials, wavefunctions, ELF — load into the **Volumetric Data dock** and
render directly in the main 3D viewport, over the same atoms, bonds, and
camera as everything else. Toggle the dock from the {guilabel}`View` menu.

---

## The dock

The dock is a registry of every field loaded for the active workspace tab:
one row per dataset with its label and grid dimensions
($n_x{\times}n_y{\times}n_z$). **Tick a row to render it; several ticked
rows render at once**, which is what makes "two orbitals side by side" a
checkbox rather than a workflow. Select a row to style it, press
{kbd}`Delete` (or {kbd}`Backspace`) to unload it.

Four icon buttons under the list:

| Button | Action |
|---|---|
| {guilabel}`Load External Files…` | Import `.cube`, `.xsf`, or `CHGCAR`-family grids (multi-select) |
| {guilabel}`Show Metadata…` | Dimensions, grid origin, voxel spacing, min/max, associated structure |
| {guilabel}`Edit Render…` | Opens the Edit Volumetric Render dialog (below) |
| {guilabel}`Remove Dataset` | Unload the selected field |

Datasets are **bound to the workspace tab they belong to** — a field
registered by a calculation stays with the tab that launched it, and each
tab keeps its own render style, mode, and isovalue. Switching tabs never
resets your settings, and never leaks one tab's isovalue onto another
tab's field (whose value range it usually would not even intersect).

### Supported formats

Gaussian `.cube` (including files written by Quantum ESPRESSO's `pp.x`),
VASP `CHGCAR` / `LOCPOT` / `PARCHG` / `ELFCAR`, and XCrySDen `.xsf` grids.

### Where fields come from

Beyond {guilabel}`Load External Files…`, Calango's own calculations
register their grids here automatically with friendly labels:

- The **Single-point wizard**'s density exports (GPAW):
  `density_all_electron.cube`, `density_pseudo.cube`, `density_spin.cube`,
  `potential_hartree.cube`, `elf.cube`, and `kinetic_energy_density.cube` —
  ELF has no separate module because it is simply one of these exports.
- **Charge Density Difference** writes `cdd.cube` and loads it here
  ({doc}`/electronic/charges`).
- **Wannier Functions** runs register their orbital cubes ψ(r).
- **Local Density of States (LDOS)** runs register the summed |ψ(r)|² over
  the selected energy window ({doc}`/electronic/ldos`).
- **Wavefunctions** runs register one cube per selected Kohn-Sham state —
  |ψ(r)|², Re(ψ(r)), or Im(ψ(r)), pseudo or all-electron
  ({doc}`/electronic/wavefunctions`).
- VASP outputs (`CHGCAR`, `LOCPOT`, `AECCAR*`, `ELFCAR`) open as external
  files from the job directory.

% TODO screenshot: the Volumetric Data dock with two ticked fields and an isosurface + atoms in the viewport
```{figure} /_static/img/analysis_volumetric.png
:alt: Volumetric Data dock listing registered fields, with an isosurface rendered over the structure
:width: 92%
:figclass: screenshot

The dock's registry with two fields ticked. Isosurfaces render in the main
viewport, sharing the structure's lights, camera, and ambient occlusion.
```

---

## Edit Volumetric Render

{guilabel}`Edit Render…` opens a modeless dialog whose
{guilabel}`Render Mode` selector switches between **Isosurfaces**,
**Color Slice**, and **Direct Volume (ray march)**. Every control applies
live.

### Isosurfaces

The isovalue slider and numeric box **re-extract the surface in real time**,
on a background thread. Extraction uses a marching-cubes-family algorithm
on a tetrahedral decomposition of each grid cell — topologically
unambiguous — with smooth normals taken from the field gradient and
periodic stitching so surfaces close correctly across cell boundaries.

For a signed field (a wavefunction, a spin density, a CDD) a positive
isovalue automatically extracts the $-$isovalue companion too, drawn in the
{guilabel}`Negative phase color`; the default isovalue is 25 % of the peak
magnitude so both lobes show. Unsigned fields default to mid-range. An
isovalue outside a particular field's range falls back to that field's
mid-range rather than extracting an empty surface.

| Control | Meaning |
|---|---|
| {guilabel}`Draw style` | Solid surface · Wireframe mesh · Solid + mesh · Dot cloud |
| {guilabel}`Dot size` / {guilabel}`Dot density` | Mark size in Å and vertex stride, for the dot cloud |
| {guilabel}`Mesh darkening` | How dark the wires are drawn over the fill (Solid + mesh) |
| {guilabel}`Opacity` | Alpha blending of the surface |
| {guilabel}`Lit surface (GPU shading)` | Shade on the GPU with the same lights as the atoms — the highlight tracks the camera and the surface takes part in ambient occlusion. Off is the legacy CPU-baked path, kept for reproducing older figures; it applies application-wide and persists |
| {guilabel}`Shading` / {guilabel}`Ambient` / {guilabel}`Specular finish` | Flat / Diffuse / Glossy baked shading for the legacy path (and the standalone volume viewers) |
| {guilabel}`Mesh smoothing` | 0–20 Laplacian passes over the extracted mesh |
| {guilabel}`Grid Interpolation` | None · trilinear · tricubic — 2× grid refinement *before* extraction |

:::{note}
Smoothing and interpolation attack the same stair-steps from opposite
ends. Grid interpolation refines the voxels and re-extracts — smoother and
more faithful, but costs memory. Mesh smoothing only nudges vertices — 
cheap, but high pass counts shrink lobes slightly, so **read isovalues off
an unsmoothed surface**.
:::

### Potential-map colouring (dual-field maps)

The {guilabel}`Potential Map Color` group inside the Isosurfaces page makes
the classic electrostatic-potential map: **field A shapes the surface,
field B colours it**. Choose the colouring field under {guilabel}`Color by`
— it is sampled at every surface vertex and mapped through the
{guilabel}`Color map`; {guilabel}`Invert palette` flips the ramp.

{guilabel}`Custom range` pins the colour scale to an explicit min/max
instead of the colouring field's own extremes. A potential runs from deeply
negative at the nuclei to nearly flat in the vacuum, so on the full range
everything interesting sits in a sliver of the ramp — and a fixed window is
also what lets two molecules be compared on one scale.

Because the colouring is an *option on the surface* rather than a separate
mode, it applies to **every ticked dataset at once**: two orbitals shown
side by side, both painted by the same potential.

:::{tip}
The classic recipe: load the charge density and the local potential, pick
an isovalue on the density that traces the molecular surface, and choose
the Coolwarm map — the diverging palette puts the neutral potential at
white, so electron-rich and electron-poor regions read immediately.
:::

### Color slice

{guilabel}`Color Slice` cuts the volume with a colour-mapped plane defined
by **Miller indices $(h\,k\,l)$ against the grid's own lattice** — the
plane normal is the reciprocal-lattice vector
$\mathbf{G} = h\,(\mathbf{b}\times\mathbf{c}) + k\,(\mathbf{c}\times\mathbf{a}) + l\,(\mathbf{a}\times\mathbf{b})$,
so $(0\,0\,1)$ cuts along the $ab$-plane and the definition survives a
triclinic cell. The {guilabel}`Offset` slider sweeps the plane along its
normal through the whole box.

- {guilabel}`Extent` — how far the plane is drawn: this unit cell only, or
  tiled 2×2, 3×3, 5×5 over the periodic neighbours. One cell is the honest
  extent for reading values; the replicated options are what make a surface
  reconstruction or an adsorbate pattern legible.
- {guilabel}`Outline the slice` — draws the plane's boundary, useful where
  the field has gone to zero and the quad would otherwise fade out.
- {guilabel}`Custom color range` — pins the ramp: a few outlier voxels (a
  nuclear cusp, a boundary spike) otherwise compress everything else into
  one end of it.
- {guilabel}`Grid Interpolation` and {guilabel}`Transparency` as above. The
  slice samples at the grid's own resolution, so it never invents detail
  the data does not carry.

### Direct volume

{guilabel}`Direct Volume (ray march)` uploads the field as a 3D texture and
integrates along each view ray — no isovalue to pick, so the core, the
bonding region, and the tail all show at once, weighted by the colour ramp
(shared with the Color Slice page) with opacity rising as $t^2$.

| Control | Meaning | Default |
|---|---|---|
| {guilabel}`Ray steps` | Samples per ray — the quality/cost dial, paid every frame | 256 |
| {guilabel}`Density` | Global opacity scale on the transfer function | 1.0 |
| {guilabel}`Threshold` | Normalized value below which a sample contributes nothing — without it the vacuum tail fogs the whole box | 0.02 |
| {guilabel}`Shade from the field gradient` | Light each sample by the local gradient — what makes an orbital read as a shape rather than coloured smoke | on |

It is the only mode whose cost is paid *every frame* rather than once per
parameter change — 128 steps orbits fluidly, 512 is what a still wants.

---

## Getting images out

The dock renders into the main viewport, so publication output goes through
the ordinary channels: {menuselection}`File --> Import / Export --> Export
Image…` ({kbd}`Ctrl+E`) for high-resolution stills, and the ray-traced and
animation exporters described in {doc}`/output`. The dock itself does not
export meshes or sampled planes.
