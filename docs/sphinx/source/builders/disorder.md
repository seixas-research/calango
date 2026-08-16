# Disorder: alloys and noise

Two tools introduce controlled randomness. The Special Quasirandom Structure generator
decorates a lattice so a small periodic cell mimics a random alloy; the Random Noise
wizard displaces atoms to shake a structure off a saddle point or to generate training
ensembles for machine-learning potentials. Both are seeded — **the same seed reproduces
the same structure exactly**, which is what makes a published disordered cell a result
rather than an anecdote.

---

## Special quasirandom structures

{menuselection}`Modules --> Alloys --> Special Quasirandom Structure (SQS)` builds a
substitutional alloy whose short-range order approximates a truly random solid solution
— the standard way to model a disordered alloy in a small periodic cell. It operates on
the structure in the current tab.

| Control | Default | Meaning |
|---|---|---|
| {guilabel}`Supercell (n₁ × n₂ × n₃)` | 2 × 2 × 2 (each 1–20) | repetitions of the parent cell before decoration |
| {guilabel}`Replace element` | — | sites of this element form the alloy sublattice; other species are untouched |
| {guilabel}`Composition` | — | target occupancies as symbol:fraction pairs, e.g. `Cu:0.75, Au:0.25` — fractions are normalised and rounded to whole atoms by largest remainder |
| {guilabel}`First shell cutoff` | 3.2 Å | first pair shell whose Warren–Cowley parameter is driven toward zero |
| {guilabel}`Second shell cutoff` | 4.8 Å | second shell; set to 0 (*off*) to optimise one shell only |
| {guilabel}`MC steps` | 20 000 (100–1 000 000) | Monte Carlo swap attempts in the simulated annealing |
| {guilabel}`Random seed` | 42 | reproducibility |

The objective is the Warren–Cowley short-range-order parameter per shell,

$$
\alpha_j = 1 - \frac{P_j(B \mid A)}{c_B},
$$

which vanishes for an ideally random decoration. If the `icet` package is importable,
Calango uses its `generate_sqs`; otherwise it falls back to a **built-in simulated
annealer** that minimises $\sum_j \alpha_j^2$ over the chosen shells using only NumPy
and ASE neighbour lists. The resulting tab is labelled with the backend that produced
it, and the internal backend reports its residual objective — a value near zero means
the decoration is essentially ideal.

### Composition rounding and cell size

Fractions are honoured as exactly as the cell allows. A 2×2×2 supercell of a
conventional fcc cell has 32 sites on the alloy sublattice, so `Cu:0.75, Au:0.25`
decorates it with 24 Cu and 8 Au — exact. The same composition on a 27-site sublattice
cannot be exact; largest-remainder rounding then lands on 20 Cu + 7 Au, and **the
achieved composition is the rounded one, not the requested one**. When the fractions
matter to the third decimal, pick a supercell whose sublattice size divides them.

More MC steps buy a lower residual with diminishing returns: small cells anneal to
effectively zero well within the default 20 000 swaps, while larger cells and
three-component compositions benefit from more. The reported residual is the check —
rerun with more steps, or another seed, if it has not flattened out.

:::{tip}
Verify the result with {menuselection}`Modules --> Alloys --> Warren-Cowley` analysis: a
good SQS shows $\alpha \approx 0$ for exactly the shells you optimised — and says
nothing about the shells you did not. A 2×2×2 cell cannot randomise arbitrarily
long-ranged correlations; if a property converges slowly with cell size, that is
physics, not a generator defect.
:::

% TODO screenshot: SQS dialog with Cu:0.75, Au:0.25 composition on an fcc cell, and the status line reporting the annealer's residual objective
```{figure} /_static/img/builders_disorder_sqs.png
:alt: SQS generator dialog with supercell, composition and shell-cutoff fields, and the backend-labelled result status
:width: 92%
:figclass: screenshot

The SQS generator. The status line names the backend (icet or the built-in annealer)
and, for the internal one, the residual objective.
```

---

## Random noise setup

{menuselection}`Simulation --> Random Noise Setup` perturbs the structure in the current
tab into a trajectory. It is a pure generator — native throughout, with no script and no
job, since there is nothing to run: the perturbation is evaluated in process and the
result opens immediately as a scrubbable trajectory tab.

:::{note}
Evaluating the ensemble (energies, forces, …) is not this dialog's job. Save the
generated trajectory ({menuselection}`File --> Save Trajectory As…` on the tab it opens)
and load it into an Orchestration **Structure Container** node
({doc}`/simulations/orchestration`), which fans a Single-point Calculation node out once
per structure — the same batch machinery every other multi-structure Orchestration
pipeline uses, rather than a second, wizard-embedded copy of it. An earlier version of
this dialog ran a single-point pass on every member itself; nothing about that
configuration was ever saved to a project file, so there is no old setup to migrate —
only, potentially, a completed job directory's results from before this change, which
still open normally.
:::

### The perturbation

| Control | Default | Meaning |
|---|---|---|
| {guilabel}`Distribution` | Gaussian (normal) | or Uniform. Gaussian is the physical choice — it is what a harmonic mode at finite temperature actually produces |
| {guilabel}`Amplitude` | 0.05 Å (0.001–5) | Gaussian: $\sigma$ per Cartesian component; Uniform: half-width of the interval. With the ramp on, this is the value the LAST frame reaches |
| {guilabel}`Random seed` | 42 | the same seed regenerates the same trajectory exactly, ramp on or off |
| {guilabel}`Perturb atomic positions` | on | the usual mode |
| {guilabel}`Perturb unit cell vectors (random strain)` | off | atoms follow the cell affinely — fractional coordinates preserved — so this is a random strain, not a second position noise. Disabled when the structure has no cell |
| {guilabel}`Structures` | 20 (1–1000) | how many displaced copies to generate, on top of the always-included reference frame |
| {guilabel}`Accumulation` | Independent | each member drawn from the original structure; *Cumulative* performs a random walk from the previous member |
| {guilabel}`Amplitude schedule` | Constant amplitude | or *Linear ramp (0 → max)* — see below |

**Frame 0 is always the unperturbed structure**, so the spread has a reference, and each
ensemble member draws from its own seeded stream derived from the one seed — the whole
trajectory is reproducible, not just its first member.

The generated trajectory opens immediately as a scrubbable tab, so the displacement
magnitude can be judged by eye — and regenerated with a different seed or amplitude —
before it is saved anywhere.

### Linear ramp

With {guilabel}`Amplitude schedule` set to **Linear ramp (0 → max)**, the amplitude no
longer stays fixed across the trajectory: frame $i$ of $N$ total frames (reference plus
displaced members) uses $i/(N-1)$ of the set amplitude. Frame 0 is already the
unperturbed reference — exactly the ramp's zero-noise endpoint, with no special case
needed — and the last frame reaches the full amplitude. Each frame still draws its own
independent random displacement; only the *magnitude* they are drawn at is interpolated,
so the same seed still reproduces the same trajectory exactly. With a single displaced
structure requested, that one member is also the trajectory's last frame and reaches
full amplitude — the smallest possible ramp is exactly "zero, then full".

A ramped trajectory is what a training set for an interatomic potential usually wants:
near-equilibrium configurations at the start (where the potential-energy surface is
smooth and easy to fit) shading into the more anharmonic displacements a model needs to
see to generalize, all from a single generated set rather than several runs stitched
together by hand.

### Choosing an amplitude

At 0.05 Å the typical per-atom displacement is comfortably inside the harmonic basin of
most solids — good for breaking symmetry before a relaxation, where the goal is to let
the optimizer *find* the downhill direction a saddle point hides. For ML training
ensembles, larger amplitudes (0.1–0.3 Å) sample the anharmonic region a potential must
learn, at the cost of occasional unphysical close contacts; that is exactly the regime
where {guilabel}`Continue past a failed structure` earns its default.

:::{note}
Random displacement is not thermal sampling. A Gaussian of fixed σ weights every atom
equally, whereas a canonical ensemble at temperature $T$ weights soft modes more than
stiff ones. For configurations distributed like an MD trajectory, run MD; use noise for
what it is — a cheap, reproducible spread around a reference geometry.
:::

% TODO screenshot: Random Noise wizard on the perturbation page, 20 structures, Gaussian 0.05 Å, with the generated ensemble open as a trajectory behind it
```{figure} /_static/img/builders_disorder_noise.png
:alt: Random Noise Setup with distribution, amplitude, seed and accumulation controls and a generated ensemble trajectory
:width: 92%
:figclass: screenshot

The perturbation page. The ensemble is generated during the wizard and opens as a
trajectory whose frame 0 is the unperturbed reference.
```
