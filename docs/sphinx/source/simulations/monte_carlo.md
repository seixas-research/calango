# Monte Carlo

{menuselection}`Simulation --> Monte Carlo Simulation` opens a three-stage wizard (Monte Carlo Settings → Calculator Settings → ASE Script Review) with the full engine list — a Metropolis walk is only as good as the energies underneath it, so anything from EMT to a foundation MLIP or full DFT can drive it. Two methods share the settings page; fields enable themselves for the one selected.

---

## Methods

**Basin Hopping (global optimization).** Each step perturbs every atomic position by a uniform random displacement, relaxes the perturbed structure to a local minimum, and accepts or rejects the *relaxed* energy by the Metropolis criterion. The random walk therefore hops between basins of attraction rather than between raw configurations — the classic recipe for finding low-lying isomers and ground-state candidates of clusters.

**Swap-atoms (alloy sampling).** Each step picks one atom and, with the configured probability, swaps its species with a randomly chosen atom of a *different* species, then accepts or rejects the swap by Metropolis. Composition is conserved by construction; the walk samples chemical orderings at the given temperature — site preference, ordering vs segregation. The script aborts up front if the structure holds fewer than two species, because there is nothing to swap.

Acceptance in both methods is the standard Metropolis rule — a move is accepted when

$$
E_\text{new} < E_\text{old}
\quad\text{or}\quad
r < \exp\!\left(-\frac{E_\text{new} - E_\text{old}}{k_B T}\right),\ r \sim U(0,1).
$$

---

## Settings

| Setting | Default | Applies to |
|---|---|---|
| {guilabel}`Method` | Basin Hopping | — |
| {guilabel}`Monte Carlo steps` | 50 | both |
| {guilabel}`Temperature` | 1000 K | both — the Metropolis temperature |
| {guilabel}`Perturbation amplitude` | 0.4 Å | Basin Hopping — max random displacement per coordinate |
| {guilabel}`Swap probability` | 1.0 | Swap-atoms — chance of attempting a swap each step |
| {guilabel}`Energy convergence (fmax)` | 0.05 eV/Å | Basin Hopping — local-relaxation criterion per hop |
| {guilabel}`Local optimizer` | FIRE | Basin Hopping — also BFGS, LBFGS, QuasiNewton |
| {guilabel}`Random seed` | 42 | both — NumPy's `default_rng` |

Each Basin-Hopping local relaxation is capped at 200 optimizer steps. **The step count is the honest cost estimate**: a Basin-Hopping run performs one full local relaxation per step, so 50 steps on a DFT engine is 50 relaxations — start with a classical or ML potential, and reserve DFT for refining the best candidates.

:::{tip}
The wizard offers the {guilabel}`van der Waals Correction (DFTD4)` toggle: a Monte Carlo run compares energies *between* geometries or orderings, which is exactly where a missing dispersion term changes the answer. See {doc}`/simulations/calculators`.
:::

---

## Outputs and monitoring

The generated script streams every step's geometry into a live viewport tab and logs the current (accepted) energy per step, so the Energy plot in the Job panel shows the walk descending in real time — see {doc}`/simulations/jobs`. Reproducibility comes from the seed: the same structure, settings and seed replay the same walk.

On completion:

- Basin Hopping writes `basin_hopping_best.extxyz` — the lowest-energy structure visited — and reports `e_min_eV`.
- Swap-atoms writes `swap_mc_best.extxyz` and reports `e_min_eV` together with the number of accepted swaps, from which the acceptance ratio follows.

:::{note}
The best structure is the *lowest energy visited*, which is not a convergence claim. A Metropolis walk at one temperature has no stopping criterion; if the energy trace is still trending down at the end, run more steps — or run several seeds and compare the minima.
:::

% TODO screenshot: Monte Carlo settings stage with Basin Hopping selected, showing the method combo and the per-method fields
```{figure} /_static/img/sim_monte_carlo.png
:alt: The Monte Carlo settings page with method, temperature and step controls
:width: 92%
:figclass: screenshot

The Monte Carlo settings stage. Fields enable themselves for the selected method only.
```
