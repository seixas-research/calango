# Molecular systems: polymers, water and ice

Two builders generate periodic boxes of molecules, both **native C++** — they run with
or without a Python environment. {menuselection}`Build --> Macromolecules` grows polymer
chains and packs amorphous cells; {menuselection}`Build --> Water & Ice` builds liquid
water and the proton-disordered ice polymorphs. Both are two-stage wizards ending in a
{guilabel}`Generate` button, and both open their result as a new tab.

---

## Macromolecules

A chain is grown one monomer at a time along a backbone walk; the walk carries the
chain's shape, tetrahedral geometry attaches each monomer's side groups, and the
tacticity decides which side of the backbone plane each substituent goes on.

### Stage 1 — simulation box

- {guilabel}`Size the box from a density target` — on by default, with
  {guilabel}`Target density ρ` defaulting to 0.92 g/cm³ (bulk polyethylene; range 0.05–5). The cubic box
  edge is derived from the total chain mass, so the cell is at the density you asked
  for. Off, the explicit {guilabel}`Box dimensions` are used (30 Å each by default,
  5–500) and the density is whatever the chains happen to give.
- {guilabel}`Chains` — default 1 (1–500). **One chain builds a single isolated molecule;
  more packs an amorphous cell** by Monte Carlo insertion with overlap rejection.
- {guilabel}`Minimum atom distance` — default 2.2 Å (1–6), enforced between non-bonded
  atoms. Below about 2 Å the cell will not survive its first minimization step.
- {guilabel}`Random seed` — default 42.

### Stage 2 — architecture

| Control | Options |
|---|---|
| {guilabel}`Monomer` | polyethylene —[CH₂-CH₂]—, polypropylene —[CH₂-CH(CH₃)]—, polystyrene —[CH₂-CH(C₆H₅)]—, PTFE —[CF₂-CF₂]—, poly(vinyl chloride) —[CH₂-CHCl]—, Nylon-6,6 |
| {guilabel}`Degree of polymerization N` | monomers per chain, default 20 (1–5000) |
| {guilabel}`End-group capping` | —H, —CH₃ or —OH. Without a cap the fragment is a diradical, not a molecule |
| {guilabel}`Tacticity` | isotactic (all substituents one side), syndiotactic (alternating), atactic (random — the default, and the usual laboratory product) |
| {guilabel}`Chain conformation` | extended (all-trans zig-zag — the crystalline chain), helical (fixed torsion), random walk, self-avoiding walk (default) |
| {guilabel}`Helix torsion` | default 165° (60–300); 180° is all-trans, and PTFE's 15/7 helix sits near 165° |

**Tacticity is meaningless without a stereocentre** — polyethylene and PTFE carry two
identical substituents on every backbone carbon, so there is no "side" for a group to be
on, and the control disables itself with an explanation for those monomers.

The distinction between the two walks matters physically: a plain random walk may pass
through itself, giving overlapping atoms and an unusable cell, while the **self-avoiding
walk** rejects steps that come too close to the existing chain. The latter is what an
amorphous cell needs; the former is only a fast approximation for very short chains.

An estimate line tracks the settings live — chain mass in u, total mass, and the box
edge the density target implies.

### What the generator reports

A pack that cannot place every chain is never silently accepted: if some chains fail,
the dialog states how many and the density the cell actually reached — a paper quoting
the *requested* density from a partial pack would be wrong. The result also carries the
count of backbone steps the self-avoiding walk rejected; a large number relative to the
chain length means the walk is struggling and the conformation is closer to a compact
globule than a random coil.

:::{warning}
The packed cell is a starting geometry with correct topology and no overlaps — it is not
an equilibrated melt. Amorphous polymer cells need long relaxation and annealing MD
before densities, moduli, or glass transitions mean anything.
:::

% TODO screenshot: Macromolecule wizard stage 2 with polystyrene selected, tacticity note visible, and the mass/box-edge estimate line
```{figure} /_static/img/builders_molecules_polymer.png
:alt: Polymer architecture stage with monomer, degree of polymerization, tacticity and conformation controls
:width: 92%
:figclass: screenshot

Stage 2 of the Macromolecule builder. The tacticity row explains itself when the monomer
has no stereocentre.
```

---

## Water and ice

The hard part of building ice is not placing the oxygens — those come from
crystallography — but placing the protons. A real ice crystal is **proton-disordered**:
the oxygen sublattice is periodic, yet the hydrogens are not, subject to the
Bernal–Fowler ice rules — every oxygen has exactly two covalent hydrogens, and every O–O
contact carries exactly one. Calango solves the rules exactly, as an Eulerian
orientation of the 4-regular hydrogen-bond graph, then randomizes the solution over
ice-rule-preserving cycle flips. Each seed gives a different proton arrangement on the
same oxygen lattice — which is what proton disorder means. The residual (Pauling)
entropy of such configurations, $k_B \ln(3/2)$ per molecule, is the measured value; a
generator that quietly produced an ordered arrangement would give the right oxygen
positions and the wrong physics.

### Stage 1 — box and domain

- {guilabel}`Replication (nx × ny × nz)` — default 2 × 2 × 2 (each 1–20), for the
  crystalline phases.
- {guilabel}`H₂O molecules` — default 256 — and {guilabel}`Target density` — default
  0.997 g/cm³ — for liquid water. The box is sized to hold exactly that many molecules
  at that density, so the two settings are never in conflict.
- {guilabel}`Minimum O–O distance` — default 2.6 Å, just inside the first peak of the
  real O–O radial distribution; raising it much further makes a dense box unpackable.
- {guilabel}`Random seed` — default 42; both the proton disorder and the liquid packing
  are reproducible.

Controls that do not apply to the selected phase disable themselves.

### Stage 2 — phase and geometry

| Phase | Molecules per conventional cell |
|---|---|
| Liquid water (amorphous pack) | set by count and density |
| Ice Ih (hexagonal — ordinary ice, the default) | 4 |
| Ice Ic (cubic — diamond oxygen lattice) | 8 |
| Ice VII (high pressure — two interpenetrating Ic networks) | 16 |

{guilabel}`Water geometry` selects the rigid-monomer preset the molecules are built
with: Rigid and TIP3P (0.9572 Å, 104.52°), TIP4P (same geometry — the massless M site is
not emitted), SPC/E (1.0 Å, 109.47°). **These are structural presets only**: the point
charges and Lennard-Jones parameters that make a force field a force field are never
written anywhere — they belong to the MD engine's topology, not to a coordinate file.

A generated cell reports its molecule count, density, and the net dipole per molecule of
the proton arrangement — near zero for a well-disordered cell. If the proton solver
cannot satisfy the ice rules (usually a supercell too small for the bond cutoff), the
wizard refuses the cell and says so rather than shipping oxygens that look right with
hydrogen bonding that is wrong.

:::{note}
Phases III, V, VI and IX are deliberately not offered — their low-symmetry oxygen
sublattices would have to be hard-coded from published refinements — and neither are the
clathrates (46- and 136-water cages) nor the proton-*ordered* phases XI and VIII, whose
ordering needs a dipole-driven search that a label alone would misrepresent. Generating
them from approximate coordinates would produce cells that look right and are not — a
worse outcome than not offering them.
:::

:::{warning}
The liquid phase is a random packing subject to the O–O minimum distance — a starting
configuration for equilibration, not an equilibrated liquid. Run MD before measuring
anything from it.
:::

% TODO screenshot: Water & Ice wizard stage 2 with Ice Ih selected and the proton-disorder note visible, estimate showing 32 molecules for 2x2x2
```{figure} /_static/img/builders_molecules_ice.png
:alt: Phase-selection stage of the Water and Ice generator with phase and water-geometry combos
:width: 92%
:figclass: screenshot

Stage 2 of the Water & Ice generator. The note explains how the protons are placed; the
estimate tracks molecules and atoms live.
```
