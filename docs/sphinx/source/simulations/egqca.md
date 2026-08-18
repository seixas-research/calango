# EGQCA (Extended Generalized Quasichemical Approximation)

{menuselection}`Modules --> Alloys --> EGQCA (Alloy Thermodynamics)…` solves
a binary substitutional solid solution's Gibbs mixing free energy — and any
composition-dependent property built on top of it — directly from a small,
explicit ensemble of non-equivalent cluster configurations, without fitting
an effective model in between.

Working theory: P.N. Ferreira, R. Lucrezi, I. Guilhon, M. Marques, L.K.
Teles, C. Heil, L.T.F. Eleno, *"Ab initio modeling of superconducting
alloys,"* Materials Today Physics **48**, 101547 (2024),
<https://doi.org/10.1016/j.mtphys.2024.101547> — Section 2. Equation numbers
below are that paper's own; `src/core/Egqca.hpp`/`.cpp` cite them directly
so the implementation can be audited against the paper equation by equation.

---

## The model

A homogeneous solid solution A$_{1-x}$B$_x$ is represented as an ensemble of
$M$ small, statistically and energetically independent supercells, each
falling into one of $J$ non-equivalent classes by symmetry — distinct cluster
$j$ with total energy $E_j$, degeneracy $g_j$, and $n_j$ atoms of type B out
of $n$ total sites. Minimizing the Gibbs mixing free energy

$$
\Delta G(x,T,P) = \Delta H(x,T,P) - T\Delta S(x,T,P) + \Delta A(x,T)
$$

over the cluster occurrence probabilities $p_j$ — subject to $\sum_j p_j = 1$
and $\sum_j n_j p_j = nx$ — gives a closed-form solution (Eq. 13-15): each
$p_j$ is a Boltzmann-weighted power of a single Lagrange multiplier $\eta$,
and $\eta$ itself is the unique positive root of one polynomial equation.
Calango solves that root by bisection in $\ln\eta$ rather than by iterating
the probabilities themselves, and the whole $(x, T)$ grid is independent
point-by-point, so a solve is milliseconds even for a wide grid.

**"Extended"** over the original GQCA: each cluster may carry a phonon
density of states, and its harmonic vibrational free energy — reusing
{doc}`/simulations/phonons`'s own $F(T) = k_BT\int D(\omega)\ln[2\sinh(\hbar\omega/2k_BT)]\,d\omega$
integral directly — enters as $\Delta A$, a genuinely temperature-dependent
correction beyond the enthalpy and configurational entropy of plain GQCA.
Omitting the DOS on every cluster (or on any one of them — Calango requires
all-or-nothing, since mixing a vibrational and a non-vibrational cluster in
one $\Delta G$ sum is not thermodynamically consistent) reduces exactly to
the original GQCA.

The **Kullback-Leibler divergence** $\Delta_{KL}(x,T) = \sum_j p_j\ln(p_j/p_j^0)$
against the ideal (regular-solution) cluster distribution $p_j^0$ is reported
alongside every point: it is the paper's own diagnostic for how far the alloy
has departed from complete randomness, and vanishes exactly when interactions
vanish (the theory's defining limit — see `tests/EgqcaTest.cpp`).

---

## Inputs

EGQCA is an **analysis layer over a finished {doc}`/simulations/cluster_expansion`
Calculation**, not its own job: opening it prompts for that run's directory
and reads `cluster_expansion.json` — the same file the binary Convex Hull
window and the ternary ground-state map (see {doc}`/simulations/cluster_expansion`'s
"Ternary systems" section) read. Every configuration needs a degeneracy
$g_j$ (written since this feature; an older ensemble needs rebuilding) and
must include both pure end-members, since EGQCA's composition range spans
between them.

EGQCA is a **binary theory** — the working paper's own stated scope ("for
simplicity, we will focus on the binary and pseudobinary descriptions",
Sec. 2). An ensemble with more than two species opens with a note explaining
why rather than silently projecting onto two of them.

Once loaded, the window exposes:

- **Reference enthalpies** $H_A$, $H_B$ — defaulted to the ensemble's own
  pure end-member energies (the paper's own recommended choice, cancelling
  the calculator's absolute energy scale exactly), editable.
- **Composition range** and step count.
- **Temperature range** and step count — also how many curves the free-energy
  plot draws, one per temperature.

---

## Outputs

Two plots, both native (drawn the same way as every other Calango chart —
see {doc}`/reference/dependencies` on the project's stance against an
external plotting dependency for a single chart type):

- **$\Delta G/M$ vs. composition**, one curve per temperature, coloured blue
  (low $T$) to red (high $T$) — the paper's own Fig. 2c/e/f convention.
- **Cluster occurrence probabilities $p_j$ vs. temperature**, at the
  composition grid point nearest $x=0.5$ — the paper's own Fig. 3 convention,
  one curve per cluster.

Both export to CSV. A future version could add composition-dependent
property averaging (Eq. 17-18 — the ambient hook already exists in
`core::EgqcaCluster::property`/`EgqcaResult::propertyAvailable` for a lattice
parameter, a superconducting $T_c$, or anything else computed per cluster)
and the miscibility-gap binodal/spinodal (Eq. 25) to the window itself.
