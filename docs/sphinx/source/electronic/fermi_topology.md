# Fermi surfaces and topological invariants

{menuselection}`Wannier Functions --> Fermi Surface…` and
{menuselection}`Wannier Functions --> Topological Invariants…` are both
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
crossings for ℤ₂. On a GPAW-sourced run the flow comes from
`gpaw.berryphase.parallel_transport` on the occupied Bloch states; on a
VASP-sourced one it is a Wilson loop built directly from $H(\mathbf{R})$ —
see [Where $H(\mathbf{k})$ comes from](#where-hk-comes-from) below.

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Compute` | {guilabel}`Chern number and Z₂`, or either alone | both |
| {guilabel}`Wilson loop` | The reciprocal direction the Berry phase is accumulated along (b₁/b₂/b₃); for a 2D sheet in the xy-plane pick an in-plane direction | b₃ |
| {guilabel}`Occupied bands` | Bands in the transported manifold | from the electron count |
| {guilabel}`Loop samples` | Samples along the flow coordinate, 5–2001 | 51 |

```{note}
{guilabel}`Loop samples` applies to the **VASP/$H(\mathbf{R})$ route only**.
On the GPAW route `parallel_transport` takes its loop from the `.gpw`'s own
k-mesh, and the setting has no effect there — it had none at all before the
$H(\mathbf{R})$ route existed.
```
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
trace. On the $H(\mathbf{R})$ route `spin` is `null` rather than a plot: see
[the spinor note](#spinors-and-the-spin-field) below.

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

---

(where-hk-comes-from)=
## Where $H(\mathbf{k})$ comes from

Wannier Interpolation ({doc}`/electronic/wannier`), the Fermi surface and the
topological invariants all need exactly one thing from a completed
localization: $H(\mathbf{k})$ at an arbitrary $\mathbf{k}$. There are two
ways to get it, and the module picks by **what the run actually left behind**,
not by engine name.

**A GPAW-sourced run** reopens its `.gpw` and rebuilds the
`ase.dft.wannier` localization — the same manifold, from the same
wavefunctions, with the same trial-orbital seed recorded in `wannier.json`.
This is why those runs need the full Brillouin zone saved
(*Symmetry: off*) and why the `.gpw` path is recorded rather than guessed.

**A VASP-sourced run** has no restartable wavefunction at all: its
localization ran inside VASP's own linked Wannier90 library, and
`wannier.json` records `engine: "VASP"`, `gpw: null`. But that library
already wrote the answer — `wannier90_hr.dat`, copied to `wannier_hr.dat`
and recorded in `wannier.json`'s `hr` field — which is $H(\mathbf{R})$, and
therefore $H(\mathbf{k})$ at any $\mathbf{k}$:

$$H(\mathbf{k}) = \sum_{\mathbf{R}} \frac{e^{2\pi i\,
\mathbf{k}\cdot\mathbf{R}}}{N_{\mathbf{R}}}\, H(\mathbf{R})$$

with $N_\mathbf{R}$ the Wigner–Seitz degeneracy wannier90 writes alongside
each block. All three modules read that file through one shared reader and
one shared dispatch, so nothing below the dispatch is engine-aware. Until
2026-08-24 all three **refused a VASP-sourced run outright** — the
information was never missing, it simply had no reader.

Two consequences worth knowing:

- **A VASP-sourced run whose `hr` is missing is refused, and says why.**
  `LWANNIER90` produces no Hamiltonian if the linked library never ran to
  completion; the error names `OUTCAR` / `wannier90.wout` rather than
  reporting a generic "no wavefunction found".
- **The Fermi level travels in `wannier.json`.** $H(\mathbf{R})$ is a
  hopping table with no zero of energy attached, so the MLWF run records
  `efermi` (both engines do). A run that predates this records none, and
  every module then says out loud that energies are referenced to 0 eV
  rather than quietly defaulting.

(spinors-and-the-spin-field)=
### Spinors and the `spin` field

A wannier90 `_hr.dat` is an $N_w \times N_w$ matrix with **no spin labelling
at all**. For a spinor (`LSORBIT`) run its Wannier functions *are* spinors and
$N_w$ counts them, but the file records neither the up/down decomposition nor
which convention was used to order the blocks, so there is nothing in it to
project a spin expectation onto.

This costs a plot series and no result. Both invariants are computed from the
centres alone — the Chern winding and the Soluyanov–Vanderbilt largest-gap
ℤ₂ never read the spin expectation — so on the $H(\mathbf{R})$ route
`topology.json` carries `spin: null` and the flow plot simply has no spin
colouring. On the GPAW route `parallel_transport` returns $S_{km}$ along the
easy axis and the field is populated as before.

The occupied count is determined differently on this route too, and better:
there is no electron count in a hopping table, so the filling is read off the
spectrum as the number of states below $E_\text{F}$ — which can only be
constant across the zone if the manifold is *actually* gapped, the very
precondition the invariant needs. A varying count is a hard error naming both
extremes, not a silently-averaged integer.

### What this was validated against

`cu_wannier_vasp_fixture` in the test suite runs the real generated scripts
against closed forms and one independent algorithm — no VASP binary involved,
since the fixture is the `_hr.dat` itself:

| Check | Reference |
|---|---|
| One-band chain $H(\mathbf{k})$ | $\varepsilon_0 + 2t\cos 2\pi k$, exact |
| Wigner–Seitz degeneracies divided out | the same chain with $N_\mathbf{R}=2$ must halve the bandwidth |
| `_hr.dat` `m`/`n` column order | round-trip of an asymmetric $H[m,n]$ — eigenvalues alone cannot catch a transpose |
| fcc Cu nearest-neighbour $s$ band, 12 neighbours | $\varepsilon_0 + 4t[\cos X\cos Y + \cos Y\cos Z + \cos Z\cos X]$ (Ashcroft & Mermin ch. 10) |
| Wannier Interpolation, end to end | the same closed form over all 200 band-path k-points |
| Fermi surface, end to end | the band's sampled range against the closed form's |
| Chern number, end to end | the Qi–Wu–Zhang model at $u = \pm 1, 3$, cross-checked against a Fukui–Hatsugai–Suzuki plaquette Berry flux that shares no code with the Wilson loop |

