# Cluster expansion

The alloy toolchain lives under {menuselection}`Modules --> Alloys` and splits the work in two: the **Builder** enumerates symmetry-inequivalent alloy configurations, and the **Calculation** wizard relaxes that ensemble with a real calculator and draws the formation-energy convex hull. Both are native C++ implementations — **no ICET dependency**.

---

## The builder

{menuselection}`Modules --> Alloys --> Cluster Expansion Builder` decorates the active sublattice of a supercell with substitution species and keeps only the configurations that are genuinely different.

| Setting | Default | Notes |
|---|---|---|
| {guilabel}`Active (parent) element` | — | the sublattice being substituted; every atom of this species is an active site |
| {guilabel}`Substitution species` | `Cu, Au` | two or more element symbols, comma-separated |
| {guilabel}`Supercell (nx·ny·nz)` | 2×2×2 | up to 8 per axis |
| {guilabel}`Pair cutoff` | 4.0 Å | 0 = off |
| {guilabel}`Triplet cutoff` / {guilabel}`Quadruplet cutoff` | off | enable higher orders with a positive radius |
| {guilabel}`Max configurations` | 200 | cap on the inequivalent set |
| {guilabel}`Random seed (sampling)` | 42 | used only when the space is subsampled |
| {guilabel}`Fix composition` | off | target site fractions, rounded to whole sites by largest remainder |

Every decoration of the active sites is reduced to a canonical **cluster-correlation fingerprint** — per-orbit histograms of the species tuples on every pair, triplet and quadruplet whose mutual minimum-image distances fall within the cutoffs — and two decorations are treated as equivalent when their fingerprints match. The occupation space is enumerated exhaustively when it is small (up to 200 000 decorations) and randomly sampled up to that budget otherwise; clusters are grouped into orbits with a 0.05 Å distance tolerance. At least one cluster order must be enabled.

The deduplicated ensemble opens as a **multi-frame trajectory document** — ready for the Calculation wizard below, or for export to the {doc}`/simulations/mlip` Dataset Manager as training data.

---

## The calculation

{menuselection}`Modules --> Alloys --> Cluster Expansion Calculation` turns the open ensemble into a job: the frames are staged next to the script as **`configs.extxyz`**, and the generated script relaxes every configuration in sequence — one fresh calculator instance per configuration — collects the ground-state energies, and emits the data for a convex-hull plot. Any engine from {doc}`/simulations/calculators` can drive it.

Per-configuration relaxation:

| Setting | Default | Notes |
|---|---|---|
| {guilabel}`Single-point only (no relaxation)` | off | evaluate at the builder's ideal-lattice geometry — much cheaper, the right first pass over a large ensemble, but formation energies then exclude relaxation energy, which can reorder near-degenerate configurations |
| {guilabel}`Optimizer` | LBFGS | cheapest per step in batch; also BFGS, FIRE, GPMin, MDMin |
| {guilabel}`Force convergence` | 0.05 eV/Å | per configuration |
| {guilabel}`Max steps (each)` | 200 | the worst case is N × this many force evaluations — keep it modest on DFT |
| {guilabel}`Relax the unit cell (variable-cell)` | off | also optimize each configuration's lattice — see below |
| {guilabel}`Cell filter` | FrechetCellFilter | or UnitCellFilter |
| {guilabel}`Stress mask` | Anisotropic | Anisotropic / Hydrostatic / 2Dxy (in-plane only) / Custom Voigt mask |
| {guilabel}`Voigt components` | all | the six `[xx, yy, zz, yz, xz, xy]` ticks — a read-out under 2Dxy, editable under Custom |
| {guilabel}`Continue when a configuration fails` | on | a diverging decoration is recorded as failed and the batch moves on |

### Relaxing the cell

The variable-cell controls are **the same ones the standalone
{doc}`/simulations/tasks` Geometry Optimization module offers** — the identical
filters, stress-mask presets and Voigt ticks, built from one shared
implementation so the two cannot drift apart.

They belong here because a cluster-expansion hull is a *comparison of
energies*, and a fixed-cell energy and a relaxed-cell energy are not the same
quantity. Substituting an alloying element into a host almost always changes
its lattice parameter, so a fixed-cell batch charges every off-stoichiometry
configuration for a strain it would not actually carry — and the hull comes out
biased toward the endpoints, with the ordered phases in the middle looking less
stable than they are.

It costs a stress evaluation per step, so it is off by default; turn it on for
anything that will be published. The relaxed volume and cell of each
configuration are recorded in `cluster_expansion.json` alongside its energy.
{guilabel}`Single-point only` withdraws the whole group — a cell filter would
wrap an optimizer that never runs.

Formation energy and hull:

- {guilabel}`Concentration axis` — the species whose site fraction $x$ is the horizontal axis; *Automatic* picks the alphabetically-second species, so repeated runs keep the same orientation.
- {guilabel}`Reference the ensemble's own endpoints` (on, recommended) — the lowest-energy configurations at the extreme compositions define $\mu_A$ and $\mu_B$, so $E_\text{form}$ is exactly zero there and the calculator's absolute energy scale cancels. Off: supply elemental reference energies computed elsewhere — necessary when the ensemble does not contain both pure endpoints.

$$
E_\text{form} = \frac{E}{N} - \left[(1 - x)\,\mu_A + x\,\mu_B\right]
$$

---

## Outputs and the convex hull

The run writes `optimized_configs.extxyz` (the relaxed ground states) and `cluster_expansion.json` (per-configuration concentration, energy per atom and formation energy, plus the references used). On completion the Results panel gains a **Convex Hull** tab: $E_\text{form}$ vs $x$, with configurations on the hull drawn filled and connected by tie-lines, and everything above it hollow, labeled by its **energy above hull**.

Only the *lower* hull is physically meaningful: a configuration is stable exactly when no linear combination of two other compositions reaches the same concentration at lower energy; points above the envelope decompose into the neighboring stable phases, and their vertical distance to it is the standard stability metric. Duplicate concentrations keep only the lowest-energy representative as a hull vertex, and failed configurations are kept in the plot (off-hull) so nothing silently disappears.

:::{note}
A hull needs several compositions. If the current document holds a single structure, the wizard warns and the batch still runs — but open the trajectory produced by the Builder first if you want a diagram rather than a point.
:::

% TODO screenshot: the Convex Hull results tab after a cluster-expansion batch, with filled hull vertices, tie-lines and hollow above-hull points
```{figure} /_static/img/sim_cluster_expansion_hull.png
:alt: Formation energy versus concentration with the lower convex hull drawn
:width: 92%
:figclass: screenshot

The convex hull of a relaxed cluster-expansion ensemble: filled points are stable; hollow points are labeled by their energy above the hull.
```
