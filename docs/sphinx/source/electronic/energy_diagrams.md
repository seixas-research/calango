# Energy Diagrams (molecules)

{menuselection}`Electronics --> Energy Diagrams…` is {doc}`/electronic/bands`
for a system with no k-path to disperse along: discrete Kohn-Sham levels,
occupied and virtual, plus — optionally — the electric-dipole transition
moments between them.

:::{warning}
**Non-periodic (or Gamma-only) only.** The level picture and the transition-
dipole formula both assume a single k-point. A completed GPAW single-point
whose baseline stored more than one k-point is refused, both in the wizard
(a red note once the baseline's spectrum is read back) and, unconditionally,
by the generated script itself (`if len(_weights) != 1: raise …`) — the
same advisory-UI / script-re-verifies split every baseline-inheriting
module here uses. A `.gpw` must also have been written with `mode='all'`
(wavefunctions saved).
:::

:::{important}
These are **Kohn-Sham eigenvalue-difference transitions** — $E_j - E_i$ from
a single ground-state SCF — **not TDDFT or BSE excitation energies**. There
is no excited-state density relaxation and no electron-hole binding. They
are useful for orbital character and dipole selection rules; the absolute
transition energy is not a substitute for a real excited-state method. The
viewer states this on every load.
:::

---

## Setting up the calculation

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Process` | The completed GPAW single-point whose `.gpw` supplies the states | mandatory |
| {guilabel}`Compute transition dipole moments` | Adds ⟨ψᵢ\|**r**\|ψⱼ⟩ between occupied and virtual states via `gpaw.utilities.dipole` | on |
| {guilabel}`Occupied states below HOMO` / {guilabel}`Virtual states above LUMO` | How wide a band window enters the transitions table | 5 / 5 |
| {guilabel}`Oscillator-strength threshold` | Below this, a transition is reported "forbidden" | 10⁻⁴ |

Selecting a baseline reads back its stored states the same way LDOS does
(`gui::peekGpawEigenvalues` — one real, fast GPAW restart, no SCF), and shows
how many k-points it found: exactly one is required, and the wizard says so
in red otherwise, well before the job would actually run.

---

## The level diagram

Each Kohn-Sham state is a horizontal bar: solid and at full weight when
occupied, hollow/dashed when virtual. **Degenerate levels** — grouped
within a tolerance (0.01 eV by default; a real-space grid does not exactly
respect a molecule's point-group rotations, so a genuinely degenerate level
splits by a small but nonzero, grid-dependent amount rather than landing
bit-identical) — are drawn as several short parallel bars side by side with
a "×N" degeneracy count, rather than one bar picked arbitrarily from the
group. **HOMO** and **LUMO** are labelled, with the gap between them
annotated on a dotted connector. Spin-polarized baselines get two columns,
spin up and spin down, drawn independently — a level never merges across
spin.

Clicking a level shows its energy, occupation, and — for a degenerate
group — every underlying band index and the summed occupation across the
group.

% TODO screenshot: EnergyDiagramViewer with a spin-polarized level diagram, a degenerate pair marked x2, and the transitions table beside it
```{figure} /_static/img/elec_energy_diagram.png
:alt: Energy Diagram viewer with level diagram and transitions table
:width: 92%
:figclass: screenshot

The level diagram (HOMO/LUMO labelled, gap annotated) beside the
transitions table.
```

---

## Transitions and selection rules

For every occupied state $i$ inside the requested window and every virtual
state $j$ inside it, the transition dipole moment $\langle \psi_i |
\mathbf{r} | \psi_j \rangle$ comes from GPAW's own
`gpaw.utilities.dipole.dipole_matrix_elements_from_calc` — not a hand-rolled
integral — and the oscillator strength follows the standard atomic-units
form

$$
f_{ij} = \frac{2}{3} \, \Delta E_{ij} \, |\langle \psi_i | \mathbf{r} |
\psi_j \rangle|^2
$$

with $\Delta E_{ij}$ in Hartree and the dipole moment in Bohr. A transition
is reported **allowed** when $f_{ij}$ exceeds the threshold and
**forbidden** otherwise — a numeric verdict on the COMPUTED matrix element,
not a symmetry label. Point-group irrep labeling and symmetry-derived
selection rules were investigated and deliberately deferred — GPAW's own
`gpaw.point_groups` module covers only twelve high-symmetry groups tailored
to a metal-cluster paper (missing, among others, benzene's $D_{6h}$ and any
linear molecule's $C_{\infty v}$/$D_{\infty h}$), and the codebase's only
existing symmetry detector (spglib) is crystal-only; see `FUTURE.md` §8 for
what a full implementation would need.

The transitions table lists spin, band pair, energy (eV), wavelength (nm),
oscillator strength, and the allowed/forbidden verdict, sorted as computed.
{guilabel}`Export Transitions…` writes a CSV with one row per transition —
`spin, from_band, to_band, energy_eV, wavelength_nm,
oscillator_strength, allowed` — matching the column-per-quantity convention
{doc}`/electronic/xas` and {doc}`/electronic/optics` use for their own
exports.

:::{tip}
The job also writes GPAW's own independently computed ground-state dipole
moment (`atoms.get_dipole_moment()`, `reference_dipole_eA` in
`energy_diagram.json`) — a useful sanity figure, unrelated to how the
transitions above are computed, since it comes from the density rather than
from any individual state's wavefunction.
:::

For the real-space orbitals these levels label, see
{doc}`/electronic/wavefunctions`, which shares its wavefunction-access
layer with this module and with {doc}`/electronic/ldos`.
