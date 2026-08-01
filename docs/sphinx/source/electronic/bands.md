# Band structure and PDOS

{menuselection}`Electronics --> Electronic Structure…` computes a band
structure along a high-symmetry k-path and, where the backend supports it, an
element- and orbital-resolved projected density of states. Two optional
readouts of the same run go further: **irreducible-representation labels** at
the high-symmetry points, and **orbital-projected bands (fatbands)**.

:::{warning}
The GPAW route needs a **baseline ground state**: a completed
{menuselection}`Simulation --> Single-point Calculation…` run with the GPAW
calculator, which writes `single_point.gpw` into its job directory. The bands
run loads that converged density and evaluates non-self-consistently at fixed
density — it never re-runs the SCF. Without a baseline the wizard refuses to
open and says so.
:::

---

## Setting up the calculation

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Baseline SCF density` | The completed single point whose `single_point.gpw` supplies the charge density | mandatory |
| {guilabel}`Backend` | {guilabel}`Free electrons (ASE — bands only)`, {guilabel}`GPAW (DFT, bands + PDOS)`, or {guilabel}`Quantum ESPRESSO (needs pw.x + pseudos)` | GPAW |
| k-path | Pre-filled with ASE's suggestion for the Bravais lattice, e.g. `GXWKGLUWLK,UX`; edit it freely in the embedded Brillouin-zone builder | suggested path |
| k-points | Total samples along the path (points per segment × segments) | 80 |
| {guilabel}`PW cutoff` | Plane-wave cutoff | 340 eV |
| {guilabel}`SCF k-grid` | Monkhorst–Pack $n^3$ grid for the self-consistent step | 4 |
| {guilabel}`Compute element/orbital PDOS (GPAW backend)` | Adds the projected DOS on its own denser mesh | on |
| {guilabel}`PDOS k-mesh` | Fixed-density mesh for the DOS integral, auto-seeded at 2× the SCF grid per non-vacuum axis | 14 per axis |
| {guilabel}`Gaussian smearing σ` | Broadening of the PDOS curves | 0.1 eV |
| {guilabel}`Energy points (N)` | Energy grid for the PDOS | 401 |

The PDOS is a Brillouin-zone integral and converges far more slowly with
k-points than the total energy does — the grid that converged the SCF is
routinely too coarse for a clean density of states, which is why the PDOS mesh
is separate and denser. A 2D sheet should leave its vacuum direction at 1.

:::{tip}
The free-electron backend needs nothing beyond ASE and finishes in seconds. It
computes empty-lattice bands — the exact free-electron dispersion folded into
your Brillouin zone — which is genuinely useful for checking that a k-path is
the one you meant before committing a DFT run. Real bands need a real solver:
`gpaw` in the selected environment, or `pw.x` plus pseudopotentials for the
Quantum ESPRESSO route (that script is a scaffold with an `EDIT ME` line for
your pseudopotential set).
:::

The job runs like any other and writes `bands.json` and, when computed,
`pdos.json` into the job directory. The viewer opens automatically on
completion and can be reopened any time with {guilabel}`Load Result` in the
process manager.

---

## Reading the plots

The viewer shows the band structure and, when present, the PDOS side by side
on a shared energy axis. Bands are plotted as $E - E_\text{ref}$ against
k-path distance, with vertical lines and labels at every high-symmetry point
(ASE's `G` is rendered as $\Gamma$), spin channels in different colours, and a
dashed horizontal line at zero. The PDOS pane shows one curve per element and
orbital projection — `Si s`, `Si p`, `O p` — accumulated over all atoms of
each species and colour-keyed to the legend.

% TODO screenshot: BandPdosWindow with silicon bands + PDOS side by side, irrep labels at Γ and X
```{figure} /_static/img/elec_bands_viewer.png
:alt: Band structure and PDOS viewer with shared energy axis
:width: 92%
:figclass: screenshot

The two-pane viewer: bands against k-path distance on the left, projected DOS
on the right, both referenced to the same adjustable Fermi level.
```

| Control | Effect |
|---|---|
| {guilabel}`Fermi level` | The reference energy, pre-filled from the calculation; shifting it moves the zero line |
| {guilabel}`E min` / {guilabel}`E max` | Energy window, defaults −10 and +10 eV |
| {guilabel}`Projections` | Checklist of PDOS curves; the vertical scale re-normalises to what remains visible |
| {guilabel}`Export Bands…` | CSV or `.dat`: one row per k-point, path distance followed by every band (and spin channel) |
| {guilabel}`Export PDOS…` | CSV or `.dat`: one row per energy, one column per projection |
| {guilabel}`Export Fatbands…` | Present only when the run wrote orbital projections — see below |

---

## Band symmetry (irreducible representations)

A band structure says where the states are. Their *symmetry* says what they
are: which crossings are protected (bands of different irreps cross rather
than repel), which optical transitions are allowed, and what a degeneracy is
made of — a two-dimensional irrep at the Fermi level is a Dirac point. Tick
{guilabel}`Assign irreducible representations at high-symmetry k-points` and
the run writes `band_symmetry.json` beside `bands.json`; the viewer draws each
band's Mulliken symbol beside its high-symmetry tick.

At a wave vector $\mathbf{k}$ the operations $\{R\mid\mathbf{t}\}$ satisfying
$R^{-\mathsf{T}}\mathbf{k} = \mathbf{k} + \mathbf{G}$ form the little group.
For each degenerate multiplet the character

$$
\chi(R) = \sum_{n \in \text{multiplet}}
\bigl\langle \psi_{n\mathbf{k}} \bigm| \{R\mid\mathbf{t}\}
\bigm| \psi_{n\mathbf{k}} \bigr\rangle
$$

is evaluated in the plane-wave representation of the Kohn–Sham states: the
operation is one permutation of the coefficient array plus a phase
$e^{-2\pi i (\mathbf{k}+\mathbf{G}')\cdot\mathbf{t}}$ — **exact on any grid
and for any operation, nonsymmorphic ones included**. The characters are then
reduced against the character table of the little co-group, computed
numerically from its own class-sum algebra (no hard-coded tables), exactly as
the Raman-modes analysis does for phonons.

The origin is *found rather than assumed*: labels at a zone-boundary point are
only convention-free once the origin sits at the symmetry centre, and spglib
reports operations in whatever cell it was handed. ASE's `graphene()` puts an
atom at the origin, where the site symmetry is $\bar{6}m2$ — the $6/mmm$
centre is the hexagon at $(1/3, 2/3)$.

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Also classify the symmetry lines between them` | One generic point per path segment, making the compatibility relations readable — how a degenerate level splits and which branch goes where | on |
| {guilabel}`Symmetry tolerance` | spglib's `symprec`; too tight loses operations a relaxed structure physically has, too loose gains ones it does not | 10⁻⁴ Å |
| {guilabel}`Degeneracy window` | Bands closer than this share one character — states degenerate *by symmetry* still emerge from a diagonalization a few µeV apart | 0.02 eV |
| {guilabel}`Energy window` | Only bands within this distance of $E_\text{F}$ are classified | ±25 eV |

:::{tip}
Graphene along $\Gamma$–K–M–$\Gamma$ reproduces the classification of Kogan
and Nazarov, *Phys. Rev. B* **85**, 115418 (2012): the two bands meeting at
$E_\text{F}$ at K form the two-dimensional $E''$ — that degeneracy *is* the
Dirac point — with $A_1'$ and $E'$ below it. **At $\Gamma$ the paper itself
misprints two labels** ($A_{1u}$/$E_{1u}$); the correct occupied-band irreps
are $A_{1g}$, $A_{2u}$ and $E_{2g}$, and that is what Calango's
`graphene_band_symmetry` integration test asserts.
:::

:::{warning}
At a zone-boundary point of a *nonsymmorphic* group the little-group
representations are projective and no ordinary Mulliken symbol applies. The
run detects this from the factor system, marks the point, and reports the raw
characters rather than inventing a label. The viewer says so too.
:::

:::{note}
Unavailable together with spin-orbit coupling. The spinor bands are a
different set of states from the scalar-relativistic ones, in a different
number, and classifying them needs the double groups. The wizard disables the
group and explains why.
:::

---

## Orbital-projected bands (fatbands)

A PDOS says which orbitals contribute at a given *energy*. A fatband says
which orbitals contribute to a given *band, at a given k-point* — the
difference between "there is a band crossing $E_\text{F}$" and "the band
crossing $E_\text{F}$ is Fe *d*, so the magnetism lives there". Tick
{guilabel}`Compute orbital-projected bands (fatbands)` and the run carries the
weight $|\langle \phi \mid \psi_{n\mathbf{k}}\rangle|^2$ alongside every band
energy, writing `fatbands.json`.

Each row of the channel table is one projection:

- {guilabel}`Atoms` — an element symbol (`Fe`), a 0-based index list
  (`0, 2, 5-8`), or empty for every atom. Individual indices are what
  distinguish a surface layer from the bulk underneath it.
- {guilabel}`Orbital` — the shell ($s$, $p$, $d$, $f$) or one magnetic
  sub-level ($p_z$, $d_{z^2}$, …). Separating $p_z$ from $(p_x, p_y)$ is what
  separates the π bands of a layered material from its σ bands.
- {guilabel}`Label` — the name shown in the viewer; blank generates one.

Leave the table empty for one channel per element and per shell present in the
structure; {guilabel}`From structure` refills it with one channel per element
on its valence shell.

The viewer's {guilabel}`Orbital projections` panel offers three drawing modes:
{guilabel}`Line width` (the classic fatband), {guilabel}`Colour scale`
(constant width, weight on the channel's colormap — readable where bands run
close together), and {guilabel}`Width + colour` (the default). Several
channels can be shown at once, and all share one normalization, so a channel
contributing 2 % of a state does not look as strong as one contributing 90 %.

Each channel gets its own *sequential* colormap, assigned in order: Greens,
Blues, Reds, Oranges, Greys, Purples — the ColorBrewer maps matplotlib ships,
with one deliberate modification: **the alpha channel ramps linearly with the
weight, so the lowest value is fully transparent rather than white.** An
opaque low end would paint over every channel already drawn and over the
dispersion itself; with a transparent floor each channel deposits ink only
where it has weight, and overlaps composite. On the dark default plot
background the lightness of each ramp is mirrored so maximum weight is a
bright hue instead of near-black; hue and saturation are untouched, and each
swatch's tooltip names the colormap — the name to put in a figure caption.

% TODO screenshot: fatband plot with two channels (metal d + ligand p) superimposed in width+colour mode
```{figure} /_static/img/elec_fatbands.png
:alt: Fatband viewer with two superimposed orbital channels
:width: 92%
:figclass: screenshot

Two projection channels composited over the same dispersion — hybridization is
visible exactly where both colormaps deposit ink.
```

{guilabel}`Export Fatbands…` writes a *tidy* table — **one row per electronic
state**, not one per k-point: `k_distance, spin, band, energy_eV`, then one
underscored column per channel (`C_p_z`). A weight is uninterpretable without
the energy of the state it belongs to, and this shape drops straight into
pandas or gnuplot as the $(x, y, \text{weight})$ triples a fatband plot
consumes. Energies are absolute, matching {guilabel}`Export Bands…`.

:::{note}
Fatbands are **GPAW-only** — the weights are PAW projector overlaps read
through `gpaw.dos`, which the file-based DFT templates do not expose — and,
like the symmetry labels, unavailable together with spin-orbit coupling.
:::

For unfolding a supercell band structure onto a primitive-cell path, see
{doc}`/electronic/unfolding`; for the optical response built on the same kind
of baseline, see {doc}`/electronic/optics`.
