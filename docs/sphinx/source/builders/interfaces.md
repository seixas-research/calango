# Interfaces

Two builders put dissimilar regions of matter next to each other.
{menuselection}`Build --> Liquid / Gas Interface` opens a fluid region on a solid substrate and packs it — the
solid/liquid and solid/gas cells that electrochemistry, catalysis and adsorption studies
start from. {menuselection}`Build --> Solid Interface` builds the crystal–crystal
constructions: stacking faults, twins, bicrystals and polycrystals. Both consume the
structure in the current tab and open their result as a **new tab**, leaving the input
untouched.

---

## Liquid and gas interfaces

Build or open the substrate first — a surface slab from {doc}`/builders/slabs` is the
usual input.

### Stage 1 — geometry and region

- {guilabel}`Interface direction` — which lattice vector the region is opened along; *c*
  for a slab built by Calango. The region is bounded by planes normal to the *other* two
  lattice vectors, so it stays commensurate with the cell even when the cell is tilted.
- {guilabel}`Region thickness` — default 20 Å (1–500). This is the gap between the top
  face of the structure and the bottom face of its own periodic image, and **the cell is
  grown — or shrunk — to make it exact**, whatever vacuum the input already carried.
  Asking for 20 Å of water and silently getting 20 Å plus the slab's existing 15 Å of
  vacuum is the usual way to end up with an accidentally dilute interface.
- {guilabel}`Lateral supercell` — repeats the substrate in the two in-plane directions
  before the region is opened (each 1–64, default 1 × 1). A wider cell is usually what a
  solvation study needs: the fluid's correlation length is several Å, and a 1×1 surface
  cell forces it to be periodic on a shorter length than that.
- {guilabel}`Surface clearance` — closest approach between any fluid atom and any
  substrate atom, default 2.2 Å (0.5–10). If twice the clearance reaches the region
  thickness, the page says so and refuses — the clearance would consume the whole
  region.
- {guilabel}`Move the substrate to the bottom of the cell` — on by default, so the fluid
  region is one contiguous block. Turn it off when the slab was positioned deliberately,
  such as a symmetric slab centred in its cell.

The summary reports the cell before and after, the fillable volume, and how many water
molecules that volume holds at ambient density.

### Stage 2 — solvation and mixture

{guilabel}`Liquid / gas mixture` is a table of species and mole fractions; the fractions
are normalised, so {3 water, 1 ammonia} and {0.75, 0.25} request the same mixture.
{guilabel}`Amount` chooses between a target mass density (default 0.997 g/cm³) and an
explicit molecule count. Picking a gas switches the page to a molecule count: a gas at
its 1 bar density comes to a fraction of a molecule in a cell of interface size, and in
a periodic cell the molecule count *is* the partial pressure.

{guilabel}`Ionic species` inserts salts or bare ions by **formula unit** — a salt
expands into its ions, so three units of (NH₄)₂SO₄ is six ammonium and three sulfate,
and **the cell stays neutral by construction**. Bare ions are offered too, for when an
unbalanced charge is deliberate; the net charge is reported below the table, in orange
when non-zero. Ions are placed *before* the solvent and their mass counts against the
density target — otherwise a saturated packing could drop an ion and silently change the
stoichiometry, and a brine would come out at the density of water with salt added on
top.

### What the packer does, and does not

Molecules are proposed at random positions with random orientations and kept when they
clear everything already placed: fluid–fluid overlap is tested on molecular centres with
a tabulated effective radius per species plus an atom-level floor (so two molecules
cannot interlock their hydrogens), and the substrate is checked atom by atom. The
packing is seeded and therefore reproducible. Random sequential packing saturates below
the equilibrium density of a hydrogen-bonded liquid — **about 90 % of the target is
typical for water** and perfectly usable; a larger shortfall is reported explicitly
after the cell opens.

:::{warning}
**The generated cell is a starting geometry, not an equilibrated liquid.** It has the
right composition, right density and no overlaps, but a random packing has no
hydrogen-bond network and a potential energy far above equilibrium. A diffusion
coefficient or interface energy computed directly on it reports a property of the random
number generator. Equilibrate with molecular dynamics first.
:::

:::{note}
The species library holds only molecules whose geometry is fixed by symmetry plus a bond
length and, where needed, one angle: water, ammonia, HF, H₂S, the common diatomic and
small polyatomic gases, the noble gases, alkali and alkaline-earth cations, halides, and
the molecular ions NH₄⁺, H₃O⁺, OH⁻, NO₃⁻, CO₃²⁻, SO₄²⁻, PO₄³⁻. Alcohols and anything
with a torsional degree of freedom are deliberately absent rather than approximated.
:::

% TODO screenshot: Liquid/Gas Interface stage 2 with a NaCl-in-water mixture, ion table showing the neutral net charge readout
```{figure} /_static/img/builders_interfaces_liquid.png
:alt: Solvation stage showing the mixture table, ionic species by formula unit, and the density-including-ions amount control
:width: 92%
:figclass: screenshot

Stage 2 of the liquid interface. Salts expand into ions per formula unit; the net charge
readout turns orange when the cell is not neutral.
```

---

## Solid interfaces

{menuselection}`Build --> Solid Interface` builds five constructions — all the same
operation seen from different distances: decide which crystal orientation occupies each
point of space, fill each region with that lattice, reconcile the seams.

| Kind | Construction |
|---|---|
| Stacking fault | everything above the plane shifts rigidly by a fraction of an in-plane lattice vector |
| Twin boundary | the half above the plane is replaced by the mirror image of the half below — a coherent twin whose **boundary layer is shared** between the orientations, so the interface carries no free volume |
| Bicrystal | two grains with independent rotations about the boundary normal — a twist grain boundary |
| Polycrystal | a Voronoi tessellation of $N$ random seeds, each grain with a uniformly random orientation |
| Multi-phase polycrystal | the same tessellation, each grain drawing its lattice from a different open workspace tab |

### Stage 1 — interface and plane

{guilabel}`Boundary normal` names a **lattice** vector (*a*, *b* or *c*, default *c*)
rather than a Cartesian axis — that is what keeps the boundary plane, spanned by the
other two lattice vectors, commensurate with the cell by construction.
{guilabel}`Boundary position` places it as a cell fraction (default 0.5).

The stacking fault reads a {guilabel}`Fault vector` in fractions of the two in-plane
lattice vectors, default (1/3, 0) — the classic partial-dislocation fault vector of a
close-packed plane. Both planar defects can open a {guilabel}`Boundary gap` (default 0
Å) perpendicular to the interface, giving a relaxation somewhere to start from.

The space-filling kinds read a {guilabel}`Merge tolerance` instead (default 0.5 Å):
atoms landing closer than this to one already placed are deleted. Two crystals meeting
at an arbitrary angle always overlap somewhere — without the merge the boundary is a
pile-up. Too large a tolerance starts eating the grains themselves; watch the reported
atom count.

### Grains as casts

Every atom is tagged with the grain it was carved from as the tessellation is
filled, and that tag travels with the structure (`grain` and `phase` per-atom
fields). When a polycrystal or bicrystal opens, **one {doc}`cast </representation>` is
created per grain and each is given its own colour**, so the tessellation is
visible immediately rather than after a manual selection: a polycrystal drawn
in element colours is a uniform block of atoms, and the grains are the one
thing element colouring cannot show.

The colours come from a golden-angle hue rotation, so any *prefix* of the
sequence is well separated — the grain count is whatever you ask for, from 2
to dozens, and a fixed palette would either run out or spend its best colours
first. Saturation and value alternate as well, because past roughly a dozen
grains hue alone starts to repeat perceptually and two touching grains of the
same apparent colour is exactly what this is for.

The tag is read back rather than re-derived. A second nearest-seed pass would
disagree with the geometry precisely at the seams — where atoms were merged and
where the grains actually are.

### Stage 2 — grains

{guilabel}`Box repeats` sets the constructed cell in multiples of the parent, default 4
× 4 × 4 (bicrystal and polycrystals only; the planar defects operate on the input cell
as it stands). A bicrystal reads two rotations about the boundary normal — defaults 0°
and 36.87°, the $\Sigma 5$ coincidence twist of a cubic lattice; the misorientation is
their difference, and both are offered because the absolute orientation relative to the
box matters too. A polycrystal reads a {guilabel}`Grains` count (default 8) and a
{guilabel}`Random seed` (default 42) — the same seed rebuilds the same cell exactly.
Grain orientations are drawn uniformly over rotation space (Shoemake's quaternion
method; naive Euler-angle sampling would cluster grains near the poles as a texture
nobody asked for), and each point of space joins the nearest seed **under the
minimum-image convention, so grains wrap correctly across the periodic boundary**. A
multi-phase polycrystal adds a table of the open workspace tabs with a weight each —
weight zero excludes that phase. A 400 000-atom budget refuses runaway repeats instead
of allocating them.

Every atom of a space-filling construction carries its **grain index and phase index as
per-atom scalar fields**, so the tessellation can be *seen* in the viewport (colour by
scalar field, see {doc}`/representation`) rather than assumed.

:::{warning}
**A periodic cell cannot contain an odd number of parallel interfaces.** Insert one
boundary at mid-cell and you have inserted two — the one you asked for, and the one
where the top face meets the bottom face of the next image. That is what periodic
boundary conditions mean, and every result reports both. A calculation attributing the
whole excess energy of the cell to "the" boundary is out by a factor of two.
:::

:::{note}
Rotating a lattice and demanding it still fit the box is a strong condition: only
coincidence-site (CSL) misorientations satisfy it exactly. At any other angle the two
crystals meet the periodic boundary out of register — the mismatch is measured and
reported in Å rather than hidden. Twinning additionally requires the stacking vector
perpendicular to the boundary plane; a tilted cell is refused with a request to
orthogonalise, rather than returning a box that describes neither half. And nothing here
is relaxed — grain boundaries have free volume, reconstruction and segregation that no
geometric construction produces. These are starting points.
:::

% TODO screenshot: A polycrystal in the viewport coloured by the grain scalar field, 8 grains with visible Voronoi boundaries wrapping the cell
```{figure} /_static/img/builders_interfaces_solid.png
:alt: Voronoi polycrystal coloured by per-atom grain index showing grains that wrap across the periodic boundary
:width: 92%
:figclass: screenshot

An 8-grain polycrystal coloured by the `grain` scalar field. Minimum-image carving makes
grains continue across the cell faces.
```
