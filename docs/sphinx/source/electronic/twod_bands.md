# 2D band surfaces

{menuselection}`Modules --> 2D Materials --> 2D Bands…` computes the band
structure of a sheet as **surfaces** $E_n(k_x, k_y)$ over the two-dimensional
Brillouin zone, rather than along a k-path. A path Γ→M→K→Γ is a set of *cuts*
through the dispersion — useful, but not the object. Sampling the plane
itself is what makes a Dirac cone look like a cone, and a band touching
visible as a touching rather than as a near-miss between two lines.

The workflow reuses {doc}`/electronic/bands`'s own per-engine chaining
exactly, substituting the sampled k-points for the path: **GPAW** and
**VASP** restart a completed baseline (`single_point.gpw` /
`CHGCAR` via `ICHARG = 11`) and evaluate the grid non-self-consistently, so
cutoff, functional and mode come from a calculation you already inspected;
**Quantum ESPRESSO** and **SIESTA** have no single-file restart artifact
this application uses for either, so they run their own SCF first (sized
by cutoff and k-grid controls shown only for them) before sampling the
grid on top of it.

:::{note}
**Spin-orbit coupling and the Brillouin-zone map option (below) stay GPAW
only.** Neither has an equivalent this application drives for the other
three engines — no non-perturbative spinor re-diagonalization of a
converged NSCF state, and no cheap second fixed-density pass. Selecting
another engine hides both controls rather than silently ignoring them.
:::

---

## The wizard

Two stages: **Engine & Brillouin-Zone Sampling**, then the editable script
review (`bands_2d.py`). Because the run's SCF state is either inherited
(GPAW/VASP) or minimal and self-contained (Quantum ESPRESSO/SIESTA), this
wizard carries its own small {guilabel}`Engine:` picker on the settings
page rather than the standard engine/DFT chrome the other DFT wizards show
— that chrome offers far more (full mode/XC/convergence groups, INCAR
groups) than either case needs.

| Control | Default | Meaning |
|---|---|---|
| {guilabel}`Engine:` | GPAW | GPAW, VASP, Quantum ESPRESSO or SIESTA — switches which group below is shown |
| {guilabel}`Baseline SCF density:` *(GPAW / VASP)* | first available baseline | the completed single point the run restarts from — `.gpw` for GPAW, `CHGCAR` for VASP |
| {guilabel}`Plane-wave cutoff:` *(Quantum ESPRESSO / SIESTA)* | 500 eV | self-contained SCF cutoff, converted to Ry for Quantum ESPRESSO |
| {guilabel}`SCF k-grid (n × n):` *(Quantum ESPRESSO / SIESTA)* | 7 × 7 | in-plane Monkhorst-Pack grid; the vacuum axis always gets exactly one k-point |
| {guilabel}`Grid samples (N × N):` | 24 (range 3–512) | samples per reciprocal direction; cost is quadratic, and a live note states the k-point count |
| {guilabel}`Surfaces below E_F:` / {guilabel}`above E_F:` | 4 / 4 | how many bands either side become surfaces |
| {guilabel}`Bands diagonalized:` *(not SIESTA)* | inherit / engine default | raise it when the conduction bands you want are not in that set — unconverged empty states are the usual reason an upper surface looks noisy. Hidden for SIESTA: its finite atomic-orbital basis sets the band count implicitly, with nothing to override |
| {guilabel}`Include spin-orbit coupling` *(GPAW only)* | off | re-diagonalize the converged states in the spinor basis (`gpaw.spinorbit`) |

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

**GPAW only** (see the note above). {guilabel}`Also sample the full first Brillouin zone` (off by default) adds
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

Every engine samples the **same** explicit fractional k-grid, Γ-centred and
**inclusive of both zone edges** so the surface closes without a seam — only
how each one is handed that grid, and how the eigenvalues come back, differs:

- **GPAW** restarts the baseline (`GPAW(r"….gpw")`) and evaluates the grid
  through `calc.fixed_density(kpts=…, symmetry='off')`. With SOC on, the
  states are re-diagonalized through `gpaw.spinorbit.soc_eigenstates` and
  the Fermi level is the spinor one; when the BZ map is on, SOC applies to
  the map sampling too, so surfaces and map can never disagree.
- **VASP** copies the baseline `CHGCAR` into the job directory (VASP takes
  no path option for it) and runs `Vasp(icharg=11, isym=0, kpts=<grid>,
  reciprocal=True)` — `isym=0` for the same reason GPAW sets
  `symmetry='off'`: an unreduced 2D grid, not a path VASP would visit once
  each regardless of symmetry.
- **Quantum ESPRESSO** runs its own SCF (`calculation: "scf"`, kz = 1 in
  the SCF mesh since the geometry check already guarantees z is vacuum),
  then a `calculation: "bands"` pass on the grid, widened to the explicit
  `(N, 4)` array (`kx, ky, kz, weight`) `K_POINTS crystal` needs — a plain
  `(N, 3)` array is silently misread as a Monkhorst-Pack grid *shape*
  instead of raising, which a static read of the generated script cannot
  catch. `nosym`/`noinv` are set for the same reason as VASP's `isym=0`,
  even though pw.x does not reduce an explicit `crystal` list either way.
  The "bands" pass runs and populates every eigenvalue but reports no
  total energy — `get_potential_energy()` is still what triggers it, so
  the one exception it then raises is caught by name.
- **SIESTA** runs its own SCF, then a second `Siesta(bandpath=<BandPath
  built directly from the grid>)` instance — `bandpath=`, never `kpts=`:
  SIESTA's `kpts=` is the SCF Monkhorst-Pack grid *dimensions*, not an
  explicit point set, and passing one through it crashes. `DM.UseSaveDM`
  is set automatically once `bandpath=` is given, restarting from the SCF
  density matrix. The eigenvalues are read back through
  `band_calc.band_structure()`, which SIESTA's own ASE calculator
  overrides to return the `%block BandPoints` result specifically — the
  generic `get_eigenvalues(kpt, spin)` loop every other engine uses reads
  the *ordinary* SCF eigenvalue output instead and silently has far too
  few k-points in it.

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

The generator is pinned by the script tests for all four engines: the grid
is Γ-centred and edge-inclusive, `symmetry='off'`/`isym=0`/`nosym`+`noinv`
per engine, $k_z$ is never written, `bandpath()` is labels-only (using it
as sampling is asserted *absent*), the sample counts clamp to their ranges
(3–512 surface, 6–96 map, GPAW only), SOC and the Brillouin-zone map route
through `soc_eigenstates` for GPAW and are asserted **absent** from the
other three engines' scripts even when both are requested on the config —
and a GPAW run without the map option contains **no `bz_map` text at all**,
so its output is byte-identical to what the module has always produced.

Beyond the string-level checks, this is verified by actually **running**
the generated script:

- **GPAW** — the existing reference behavior, exercised live while this
  extension was built: a real baseline on monolayer graphene, sampled at
  13×13 (chosen to land exactly on the K point), shows the π/π* bands
  touching to within 1.6 meV at K and nowhere near it elsewhere — the
  Dirac cone this feature exists to draw.
- **Quantum ESPRESSO** — `graphene_2d_bands_qe` in the test suite runs the
  *actual generated script* against a real `pw.x` and a real carbon
  pseudopotential on monolayer graphene, self-skipping cleanly without
  them. Two real bugs (the `(N, 4)` k-point array and the
  `PropertyNotImplementedError` above) were found this way, not by
  inspection.
- **SIESTA** — `graphene_2d_bands_siesta` does the same against a real
  `siesta` binary; the `band_structure()` bug above was found this way.
- **VASP** — no license here, so this is verified to the standard of the
  other license-gated engines in this application (FHI-aims, SIESTA
  before Task 3): real construction of the `ase.calculators.vasp.Vasp`
  object and a real call into ASE's own `format_kpoints()`/KPOINTS writer,
  which is what caught the raw-`BandPath`-object crash this generator (and
  the 1D Electronic Structure module's own VASP branch, fixed alongside
  it) no longer makes.
