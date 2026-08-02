# 2D band surfaces

{menuselection}`Modules --> 2D Materials --> 2D Bands…` computes the band
structure of a sheet as **surfaces** $E_n(k_x, k_y)$ over the two-dimensional
Brillouin zone, rather than along a k-path. A path Γ→M→K→Γ is a set of *cuts*
through the dispersion — useful, but not the object. Sampling the plane
itself is what makes a Dirac cone look like a cone, and a band touching
visible as a touching rather than as a near-miss between two lines.

The workflow is deliberately identical to {doc}`/electronic/bands` otherwise:
the run is **non-self-consistent on an inherited ground state** — a completed
GPAW Single-Point Calculation that saved its wavefunctions
(`single_point.gpw`) — so the cutoff, functional and mode come from a
calculation you already inspected.

:::{note}
**GPAW only.** The whole method is `calc.fixed_density(kpts=…)` on a saved
`.gpw`, which no other engine in this application exposes — a VASP `CHGCAR`
(accepted by the 1D Electronic Structure wizard through `ICHARG = 11`)
cannot serve here. The launcher refuses, with instructions, when no GPAW
baseline exists.
:::

---

## The wizard

Two stages: **Baseline & Brillouin-Zone Sampling**, then the editable script
review (`bands_2d.py`).

| Control | Default | Meaning |
|---|---|---|
| {guilabel}`Baseline SCF density:` | first available `.gpw` | the completed single point the run restarts from |
| {guilabel}`Grid samples (N × N):` | 24 (range 3–512) | samples per reciprocal direction; cost is quadratic, and a live note states the k-point count |
| {guilabel}`Surfaces below E_F:` / {guilabel}`above E_F:` | 4 / 4 | how many bands either side become surfaces |
| {guilabel}`Bands diagonalized:` | inherit from the baseline | raise it when the conduction bands you want are not in the baseline's set — unconverged empty states are the usual reason an upper surface looks noisy |
| {guilabel}`Include spin-orbit coupling` | off | re-diagonalize the converged states in the spinor basis (`gpaw.spinorbit`) |

The grid decides whether a Dirac cone reads as a cone or as a staircase:
~24 is a good overview; resolving a band touching or a small avoided
crossing wants 48 or more. Bands that **cross the Fermi level are always
included** whatever the counts say — those are the Fermi surface, and
dropping one because a count ran out would remove the feature the plot is
for.

Spin–orbit coupling is often the reason to draw a surface at all: Rashba
splitting and the gap opened at a band touching are simply absent from a
scalar-relativistic calculation, and both are features *of the surface*
rather than of any one cut through it.

:::{admonition} Two honesty checks on dimensionality
:class: caution

A structure that is not periodic in both x and y has no 2D Brillouin zone to
sample — the wizard says so in red and the generated script refuses at run
time. A structure that is *also* periodic along z gets a different warning:
the $k_z = 0$ plane is then one cut through a 3D dispersion, a valid thing
to plot but **not the band structure of a 2D material** — add vacuum along z
for that. The vacuum axis is fixed to z.
:::

### The Brillouin-zone map option

{guilabel}`Also sample the full first Brillouin zone` (off by default) adds
a second, independent sampling for the flat $E(k_x, k_y)$ map view in the
results window: every band evaluated on an N×N Monkhorst–Pack mesh
({guilabel}`Map k-mesh`, 6–96, default 24) spanning the primitive 2D
reciprocal cell. The mesh is reduced to the first Brillouin zone by
Wigner–Seitz folding **at render time**, so no zone geometry is baked into
the run. Off by default because it is pure extra cost when only the 3D
surfaces are wanted — and a run without it keeps exactly the output it
always had.

---

## What the run does

The script restarts the baseline (`GPAW(r"….gpw")`), checks the
periodicity, and evaluates the eigenvalues at fixed density on a Γ-centred
grid that **includes both zone edges**, so the surface closes without a
seam. Symmetry reduction is turned off (`symmetry='off'`) on purpose:
folding into an irreducible wedge and unfolding back into a plottable
surface is precisely the work being skipped. With SOC on, the states are
re-diagonalized through `gpaw.spinorbit.soc_eigenstates` and the Fermi
level is the spinor one; when the BZ map is on, SOC applies to the map
sampling too, so surfaces and map can never disagree.

k-points are exported **Cartesian, in Å⁻¹, with the 2π restored** —
fractional coordinates would shear a hexagonal cell's Dirac cones into
rhombus corners. ASE's `bandpath()` is consulted for the high-symmetry
*labels* only (Γ, M, K, …), never as the sampling; points off the
$k_z = 0$ plane are dropped.

Everything lands in one `bands_2d.json`: Fermi level, the Cartesian k
grids, the reciprocal cell, the special points, and one entry per kept
(spin, band) with its energy surface and extrema — plus, when requested,
the `bz_map` block (`n`, fractional mesh, per-band energies with spin
channels merged and sorted, Fermi level, in-plane reciprocal rows).

---

## The viewer

The **2D Band Surfaces** window opens automatically when `bands_2d.json`
appears in a finished job directory, with a {guilabel}`View:` selector
switching between two pages.

### Band surfaces (3D)

% TODO screenshot: 3D band-surface page with two surfaces, Fermi plane, zone outline and k-point labels visible
```{figure} /_static/img/elec_twod_bands_surfaces.png
:alt: Lit 3D band surfaces over the kx-ky plane with the Fermi plane outlined
:width: 92%
:figclass: screenshot

The surface page — each checked band is a lit, orbitable height field over
the $k_x$–$k_y$ plane.
```

The band list checks **only the Fermi-crossing bands by default** (an
insulator gets the two bands bracketing the gap instead) — several at once
is usually the point, since a band touching is a relationship between two
surfaces. The settings column:

- **View** — Top/Front/Side/Iso presets, 15° nudge arrows about the screen
  axes, a −180…180° roll slider ("the camera tilting its head, which is how
  a figure is levelled without re-orbiting it"), and a live yaw/pitch/roll
  read-out.
- **Appearance** — colormap-by-energy (11 gradients, Viridis default) or a
  single solid colour (better when several surfaces must be told apart);
  {guilabel}`E − E_F` (on) and a red {guilabel}`Fermi plane` outline (on) —
  where a surface crosses it *is* the sheet's Fermi surface. The
  {guilabel}`Energy scale` (Å⁻¹/eV) is the vertical exaggeration that makes
  the shape readable; it is seeded from the data and stated with the axis,
  so the distortion is never silent.
- **Surface** — interpolation None/Bilinear/Bicubic (default bicubic;
  both schemes pass exactly through the computed eigenvalues — what changes
  is only what is drawn between them) and a ×1–×8 refinement factor.
  Interpolation is not a substitute for sampling: a feature the k-grid
  missed cannot be recovered by drawing through it.
- **Domain & annotation** — axes (on), high-symmetry k-point labels (on),
  and {guilabel}`First Brillouin zone` clipping (off): the sampled grid is
  a parallelogram spanning one reciprocal cell, which covers the same area
  but is **not** the same region — for a hexagonal lattice it cuts through
  the K corners and folds parts of the second zone into view; clipping
  shows the zone the band structure is conventionally drawn over.

{guilabel}`Image…` grabs the canvas as PNG; {guilabel}`Data…` writes a tidy
CSV — `band,spin,kx_per_A,ky_per_A,energy_eV,energy_minus_EF_eV`, one row
per (band, k-point).

### Brillouin-zone map

% TODO screenshot: BZ-map page with one band folded into the hexagonal first zone, colorbar and Γ marker
```{figure} /_static/img/elec_twod_bands_bzmap.png
:alt: One band's energy painted over the first Brillouin zone with a colorbar
:width: 92%
:figclass: screenshot

The map page — one band's energy over the exact first Brillouin zone,
folded from the sampled mesh.
```

One band's energy painted over the **exact first Brillouin zone** — each
pixel is folded periodically back into the sampled Monkhorst–Pack cell and
bilinearly interpolated, so the hexagon (or whatever the lattice dictates)
is exact rather than clipped from a parallelogram. Controls:
{guilabel}`Band:` (opens on the band nearest $E_F$ — a metal's crossing
band, an insulator's gap edge), {guilabel}`E − E_F` (labels only; the ramp
always spans the band's full range), and a five-entry colormap shortlist
(Viridis default) mirroring the unfolding heatmap. The zone outline, Γ
marker, axis captions and a colorbar are drawn on the plot canvas;
{guilabel}`Export Image…` renders at 3× for print, and
{guilabel}`Export CSV…` writes the *computed mesh*, not the folded pixels
(`kx_frac,ky_frac,kx_1_per_A,ky_1_per_A,energy_eV`).

A run made before the option existed (or with it off) keeps the map entry
visible but greyed, with a tooltip saying to re-run with
{guilabel}`Also sample the full first Brillouin zone` enabled.

---

## Validated behavior

The generator is pinned by the script tests: the grid is Γ-centred and
edge-inclusive, `symmetry='off'`, $k_z$ is never written, `bandpath()` is
labels-only (using it as sampling is asserted *absent*), the sample counts
clamp to their ranges (3–512 surface, 6–96 map), SOC routes through
`soc_eigenstates` for both samplings — and a run without the map option
contains **no `bz_map` text at all**, so its output is byte-identical to
what the module has always produced.
