# Brillouin zone and k-paths

{menuselection}`Build --> Brillouin Zone Builder…` shows the first Brillouin
zone of the current structure as the **Wigner–Seitz cell of the reciprocal
lattice**, with high-symmetry points labelled from ASE's Bravais-lattice
detection (Γ is displayed for ASE's `G`). Its job is to turn a click
sequence on that zone into a band-structure k-path any code can consume.

The same widget is embedded, in a compact form, inside the wizards that
need a k-path (the Electronic Structure wizard's path stage,
{doc}`/electronic/bands`) — what you learn here transfers directly.

---

## Navigating the zone

Drag rotates, {kbd}`Shift`+drag pans, the wheel zooms.
{guilabel}`Orthographic projection` is **on by default** — symmetric zone
geometry reads more clearly without perspective foreshortening — and can be
toggled off. {guilabel}`Customize Appearance…` opens a style dialog for
colors, transparency, line thickness, and label visibility of the zone and
the path.

% TODO screenshot: the Brillouin Zone Builder with an fcc zone, a partially built path with direction arrows, and the side panel
```{figure} /_static/img/analysis_bz.png
:alt: Brillouin Zone Builder showing the Wigner-Seitz cell, labeled high-symmetry points, and a k-path with arrows
:width: 92%
:figclass: screenshot

The fcc Brillouin zone with its labelled high-symmetry points. Each path leg
carries a direction arrow at 60 % of its length, so the traversal order is
readable from the figure alone.
```

---

## Building a path

**Click high-symmetry points in the 3D view to append them to the path.**
The list beside the view shows the sequence with fractional coordinates;
clicking the same point twice in a row is ignored. Each leg of the drawn
path carries a directional arrow, so the traversal order stays visible.

The icon bar under the list:

| Button | Action |
|---|---|
| {guilabel}`Suggested` | Load ASE's suggested path for the detected lattice |
| {guilabel}`Break` | Start a new discontinuous section — paths like $\Gamma \rightarrow X \,\vert\, M \rightarrow R$ |
| {guilabel}`Undo` | Remove the last point |
| {guilabel}`Remove` | Delete the selected point or break from the middle of the list |
| {guilabel}`Clear` | Start over |

{guilabel}`Points per segment` sets the sampling density per leg — range
5–500, default **40**. Removing a point from the middle cleans up any break
markers it leaves dangling, so the path always stays well-formed.

Breaks matter for real materials: the conventional paths of several
lattices are discontinuous, and a code fed a path with the jump spelled out
as a segment would interpolate eigenvalues across a straight line no
experiment corresponds to.

---

## Exporting the path

{guilabel}`Export k-Path…` needs at least two connected points and offers
six formats:

| Format | Typical file | Notes |
|---|---|---|
| Calango k-path (JSON) | `kpath.json` | The primary format — see below |
| VASP `KPOINTS` (line mode) | `KPOINTS` | Line-mode segments |
| Quantum ESPRESSO `K_POINTS crystal_b` | `kpath_qe.in` | Ready to paste into a `bands` input |
| CASTEP `SPECTRAL_KPOINT_PATH` | `kpath_castep.cell` | Block for a `.cell` file |
| SIESTA `BandLines` | `kpath_siesta.fdf` | fdf block |
| ASE / Python script | `kpath_ase.py` | Rebuilds the path with `ase`; total sampling matched to legs × points-per-segment |

The JSON export is a small, self-describing document
(`"format": "calango-kpath"`, version 1) carrying the ASE path string
(`"GXWKG,UX"`-style, comma = break), the per-segment divisions, every
high-symmetry point's fractional coordinates (with `G` spelled out as
`Gamma`), and the explicit segments with cumulative reciprocal-space
distances in Å⁻¹ — enough for any external plotting script to reproduce the
axis of a band structure exactly.

```python
# kpath_ase.py sketch — what the ASE export produces
from ase.io import read
atoms = read("structure.extxyz")
path = atoms.cell.bandpath("GXWKG,UX", npoints=240)
```

---

## Exporting the figure

{guilabel}`Export Figure (PNG/SVG)…` renders the zone at high resolution on
a 1600 × 1400 canvas. **The SVG output is vector** — labels, zone edges, and
path arrows stay sharp at any scale and drop straight into a paper figure;
the PNG variant renders on a white background at the same size.

---

## Limitations

The builder offers the high-symmetry points ASE's Bravais detection yields
for the *current* cell. A supercell or a slightly distorted cell detects as
a lower-symmetry lattice with different (or fewer) labelled points — reduce
to the primitive standardized cell first if the textbook labels are what
you expect. Arbitrary unlabelled k-points cannot be clicked; the path is
built from the detected special points only.
