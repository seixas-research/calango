# Fermi surfaces and topological invariants

{menuselection}`Electronics --> Fermi Surface…` and
{menuselection}`Electronics --> Topological Invariants…` are both
post-processes of a **completed Wannier Functions run**
({doc}`/electronic/wannier`): the first interpolates $E_n(\mathbf{k})$ from
the localized Wannier Hamiltonian onto a dense 3D grid and extracts the sheets
$E_n(\mathbf{k}) = E_\text{F}$; the second transports the occupied manifold
around Wilson loops and reads the Chern number and the ℤ₂ index off the
hybrid Wannier centre flow. Both dialogs open on the same source selector, and
the {guilabel}`OK` button waits on a valid MLWF process.

---

## Fermi surface

The Fermi surface is the sheet $E_n(\mathbf{k}) = E_\text{F}$. It is
interpolated rather than sampled directly because $H(\mathbf{R})$ is
short-ranged: $E_n(\mathbf{k})$ costs one small diagonalization per point, and
a grid dense enough to resolve a surface becomes affordable.

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Grid samples (k₁ × k₂ × k₃)` | Samples per reciprocal direction, 4–128 each | 32 × 32 × 32 |
| {guilabel}`Energy offset` | Energy the sheets are taken at, relative to the calculation's own Fermi level | 0 eV |
| {guilabel}`Localization iterations` | `localize()` sweeps before interpolating | 50 |

The counts are **per direction, not one for all three** — reciprocal cells
are rarely cubic, and on a layered material the out-of-plane direction carries
almost no dispersion, so sampling it as finely as the other two multiplies the
cost to resolve nothing. The dialog restates the product as a diagonalization
count (32³ is 32 768 of them) before the job is queued; 24–32 per direction
gives a recognizable surface, and a nesting study or a narrow neck wants 48 or
more along the directions that carry the structure.

A non-zero energy offset is a rigid-band doping study — and because the whole
energy grid is stored, **the viewer can scan the energy afterwards without
re-running anything**. The localization matters too: a poorly localized
$H(\mathbf{R})$ is long-ranged, and its interpolated bands ring between the
computed k-points, which shows up as a Fermi surface with spurious ripples.

The script rebuilds the *same* localization the MLWF run produced (same
wavefunctions, seed and count), samples fractional k on
$[-\tfrac12, \tfrac12)$ with the endpoint excluded (−½ and +½ are the same
point by periodicity; including both would duplicate a grid face that marching
cubes would then close the surface across), and writes `fermi_surface.json`:
the Fermi level, target energy, per-direction sample counts, the reciprocal
vectors with the $2\pi$ restored (so axes are the Å⁻¹ every Fermi-surface
figure is drawn in), and per band its energy range, whether it crosses the
target energy, and the full grid. Only crossing bands can contribute a sheet;
a system with none is reported as having no Fermi surface at that energy.

### The viewer

Two things make the window more than an isosurface. The interpolation grid
spans the reciprocal *unit cell* — a parallelepiped — while a Fermi surface is
conventionally drawn in the *Wigner–Seitz* zone, so the sheets are **clipped
to the first Brillouin zone** ({guilabel}`Clip` toggle, with the zone outline
and point labels switchable). And a Fermi surface is per band: each crossing
band contributes its own sheet, listed and coloured separately, because which
sheet is which *is* the electron/hole distinction.

The energy spinbox re-extracts the sheets at any energy on the stored grid;
an interpolation control refines the field before marching cubes (marching
cubes reproduces the grid it is given, so refining the field is what actually
smooths a faceted sheet); opacity, lighting and the colour gradient are
adjustable. {guilabel}`Export` writes a CSV — one row per k-point, one energy
column per band, k₃ fastest — reconstructable as a structured grid in ParaView
(*Table To Structured Grid*) or Mayavi (`reshape`), plus a high-resolution
image.

% TODO screenshot: FermiSurfaceWindow showing a multi-sheet surface clipped to the Brillouin zone with the band list
```{figure} /_static/img/elec_fermi_surface.png
:alt: Fermi surface sheets clipped to the first Brillouin zone
:width: 92%
:figclass: screenshot

Per-band sheets clipped to the Wigner–Seitz zone; the band list colours and
toggles each sheet independently.
```

---

## Topological invariants

Both invariants are read off one object: the **hybrid Wannier centre flow** —
the Berry phases of the occupied manifold accumulated along one reciprocal
direction, resolved against the perpendicular k. They differ only in what is
counted: the net winding for the Chern number, the parity of largest-gap
crossings for ℤ₂. The computation uses `gpaw.berryphase.parallel_transport`
on the occupied Bloch states.

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Compute` | {guilabel}`Chern number and Z₂`, or either alone | both |
| {guilabel}`Wilson loop` | The reciprocal direction the Berry phase is accumulated along (b₁/b₂/b₃); for a 2D sheet in the xy-plane pick an in-plane direction | b₃ |
| {guilabel}`Occupied bands` | Bands in the transported manifold | from the electron count |
| {guilabel}`Loop samples` | Samples along the flow coordinate, 5–2001 | 51 |
| {guilabel}`Spin-orbit coupling` | Spinor treatment | on |

The invariant belongs to a **gapped** manifold, so the filling is checked
rather than assumed: the default derives the occupied count from the electron
count (with SOC the states are spinors — one electron per band, not two), and
the script warns loudly when it finds no gap at that filling
($E_\text{gap} < 10^{-3}$ eV) — the integers then describe a partition that is
not actually separated, and are not meaningful. Too few loop samples and the
centres jump further than half a period between steps, which breaks the branch
tracking; the reported residual is what exposes that.

**Chern number.** $C$ is the net winding of the *summed* centres over a full
period — the sum is gauge-invariant where individual centres are not. Each
step is brought onto the nearest branch before accumulating, the loop is
closed, and the result is rounded to an integer with the **residual reported**:
a clean calculation lands within a few 10⁻³, while a large residual means the
loop is under-sampled or the manifold is not gapped, and the integer is a
rounding of noise.

**ℤ₂ index.** The Soluyanov–Vanderbilt largest-gap method: follow the midpoint
of the widest gap between centres across *half* the zone and count, modulo 2,
how many centres the gap sweeps past. Counting crossings of a fixed reference
line is the textbook statement but is numerically fragile — a centre near the
line is ambiguous, and the whole point of choosing the largest gap is that no
centre is ever near it.

:::{important}
The applicability constraints are physics, and the dialog states them live:
**ℤ₂ requires time-reversal symmetry** and is not defined for a magnetic
system — use the Chern number there. **A non-zero Chern number requires broken
time-reversal symmetry** — in a non-magnetic material it is zero by symmetry,
and computing it is a consistency check rather than a result. And a ℤ₂
computed with spin-orbit coupling off is expected to be trivial regardless of
the material: for most candidates SOC is what opens the inverted gap in the
first place.
:::

Results land in `topology.json`: the WCC flow itself (`wcc[k][m]` in
$[0, 1)$), the per-loop-point spin expectations, the Chern value with winding
and residual, and the ℤ₂ value with its crossing count and gap-midpoint
trace.

### The viewer

The Topology window shows the invariants with their caveats and the
**Wilson-loop flow they were read off** — one marker per centre at each loop
point, with the largest-gap midpoint overlayable for ℤ₂. The plot is the
evidence for the integer, not decoration: a Chern number is the visible net
drift of the centres from one side to the other, a non-trivial ℤ₂ is the gap
being crossed an odd number of times, and in neither case is the integer alone
falsifiable — a badly sampled loop produces a confident wrong answer that only
the flow reveals.

% TODO screenshot: TopologyWindow with a non-trivial WCC flow and the gap-midpoint trace
```{figure} /_static/img/elec_wcc_flow.png
:alt: Hybrid Wannier centre flow with largest-gap midpoint
:width: 92%
:figclass: screenshot

The hybrid Wannier centre flow: the centres drift and switch partners across
the half-zone, and the gap-midpoint trace counts the crossings ℤ₂ is the
parity of.
```
