# Effective cluster interactions (ECI fit)

{menuselection}`Modules --> Alloys --> Effective Cluster Interactions (ECI
Fit)…` turns a completed {doc}`/simulations/cluster_expansion` run into a
**cluster expansion**: a linear model that predicts the energy of any
decoration of the lattice from a handful of coefficients.

$$
E(\sigma)/\text{atom} = J_0 + \sum_\alpha m_\alpha J_\alpha \Phi_\alpha(\sigma)
$$

The $J_\alpha$ are the effective cluster interactions. The builder supplies
$\Phi_\alpha$ — the cluster correlations — and the calculation supplies one
energy per configuration; what is left is a least-squares problem, and the
module is almost entirely about its **regularisation** and its **validation**,
because for a cluster expansion the unregularised fit is nearly always both
computable and wrong.

% TODO screenshot: ECI fit dialog with the CV/training diagnostic line and the ranked orbit table
```{figure} /_static/img/sim_eci_fit.png
:alt: The Effective Cluster Interactions dialog showing the CV score and the fitted orbit table
:width: 92%
:figclass: screenshot

The fit, its two error numbers, and the orbits it kept.
```

---

## Why a plain least-squares solve is a trap

A cluster-expansion design matrix is **systematically ill-conditioned by
construction**, and not by accident:

* orbits at nearly the same radius have nearly the same correlation on every
  configuration you can afford to compute, so their columns are near-parallel;
* the empty cluster is exactly parallel to the intercept;
* the number of candidate orbits grows faster with the cutoff than the number
  of configurations anyone is willing to run, so the system is frequently
  **underdetermined outright**.

Forming the normal equations $X^\mathsf{T}X$ squares the condition number of an
already borderline matrix and then inverts it. The failure is not a crash. It
is a fit with a *perfect* training residual and ECIs of alternating sign and
absurd magnitude that predicts nothing.

Every solver here therefore goes through an **SVD** or through **coordinate
descent**, never through an explicit inverse. A rank-deficient design — two
identical orbits, say — splits the weight evenly between the degenerate columns
instead of blowing up.

---

## The three methods

| Method | Penalty | What it does |
|---|---|---|
| {guilabel}`LASSO (selects clusters)` | $L_1$ | **Selects** orbits — drives them to *exactly* zero. **The default** |
| {guilabel}`Ridge (keeps all clusters)` | $L_2$ | Keeps every orbit and only shrinks it. Never sparse |
| {guilabel}`ARD / Bayesian` | learned per orbit | Automatic relevance determination: learns one precision per orbit by evidence maximisation and prunes outright, with **no** regularisation parameter to choose |

LASSO is the default because **choosing which clusters to keep is the central
difficulty of a cluster expansion**, and it is the only one of the three that
decides. Ridge cannot tell you that a cluster does not matter — it can only tell
you the coefficient is small, which is a different statement. The zeros LASSO
produces are exact zeros, bit for bit; a "lasso" that only shrinks is a broken
lasso.

ARD gives sharper support recovery than LASSO when it works, has no $\lambda$
path at all, and can prune too hard on very small data sets.

Columns are centred and scaled to unit variance before fitting and unscaled
afterwards. Without that, an $L_1$ or $L_2$ penalty — neither of which is
scale-invariant — would let the *units* of an orbit decide how hard it is
penalised.

---

## The CV score is the diagnostic

:::{warning}
**A cluster expansion is not judged by its training RMSE.** It is judged by its
**cross-validation score** — the RMS error on configurations the fit never saw.
The two numbers moving in opposite directions as the regularisation is relaxed
*is the definition* of overfitting, so both are shown side by side and you are
expected to read both.
:::

The dialog reports:

```text
CV score 0.0041 eV/atom — the diagnostic.
Training RMSE 0.0033 eV/atom, 7 active clusters, λ = 0.0021
```

A fit whose **CV score exceeds 3× the training error** is flagged in red:

> The CV score is more than three times the training error: this expansion fits
> its own data and does not predict. Add configurations or reduce the cluster
> cutoffs.

{guilabel}`CV folds` defaults to **leave-one-out** (displayed as
`leave-one-out`, stored as 0). That is the cluster-expansion convention: "the CV
score" in the CE literature *means* the leave-one-out score. A positive $k$ is
useful once the configuration count reaches the hundreds.

Ridge cross-validation over 50 $\lambda$ values and 100 leave-one-out folds
costs one SVD in total, so the whole path is essentially free — which is why the
result carries `lambdaPath`, `cvPath`, `rmsePath` and `activePath` even though
the dialog currently shows only the selected point.

The table ranks the surviving orbits by **$m \cdot J$**, not by $J$. A large ECI
on a multiplicity-1 orbit and a small one on a multiplicity-24 orbit can be the
same physics, and ranking by the bare coefficient hides that. Orbits LASSO
dropped are not listed at all — showing rows of zeros buries the signal.

---

## What the builder must have produced

The fit reads `cluster_expansion.json` from a finished run directory. That file
now carries **one cluster-correlation row per configuration** — the design
matrix — alongside `orbit_labels` and the energies.

:::{note}
A run made **before correlations were emitted** has energies and no design
matrix. It is **refused, with instructions**, rather than fitted against
whatever else is present:

> *…carries energies but no cluster correlations, so there is no design matrix
> to regress against. This run predates correlation output. Rebuild the ensemble
> with the Cluster Expansion builder and re-run it; the new
> `cluster_expansion.json` will carry a "correlation" row per configuration.*

The {doc}`/simulations/orchestration` node refuses for the same reason and says
what the alternative would have cost: an ECI file of zeros that the CVM node
downstream would turn into a plausible and entirely wrong entropy curve.
:::

### Builder defaults that matter here

The {doc}`/simulations/cluster_expansion` builder's cluster cutoffs decide what
this fit can possibly find.

| Setting | Default | Why |
|---|---|---|
| {guilabel}`Pair cutoff` | 4.0 Å | |
| {guilabel}`Triplet cutoff` | **3.0 Å — on** | A pair-only basis is symmetric under A ↔ B at complementary compositions, so it cannot distinguish A₃B from AB₃. An ensemble built without triplets silently discards the term that *chooses* the ordered structure. 3.0 Å keeps it to the nearest triangles on a typical close-packed lattice |
| {guilabel}`Quadruplet cutoff` | **off** | Each orbit adds $K^4$ histogram columns to the design matrix, and with the usual handful of configurations the fit runs out of samples before it runs out of clusters |

---

## Send to CVM

{guilabel}`Send to CVM…` opens {doc}`/simulations/cluster_variation` with the
fitted **nearest-neighbour pair ECI** already converted and applied — the
smallest-radius order-2 term with a non-zero coefficient. Longer-range pairs and
all multi-body terms are ignored on purpose, because that solver is a
nearest-neighbour one.

:::{warning}
**The sign convention is the thing to get right here.** In the $\pm 1$
correlation basis the pair energy is $J s_i s_j$ with $s = +1$ for A and $-1$
for B, so the conversion is

$$
e_{AB} = -J_2, \qquad e_{AA} = e_{BB} = +J_2
$$

and therefore **$J_2 > 0$ ORDERS** the alloy. Half the literature writes the
opposite convention. Getting it backwards turns an ordering alloy into a
clustering one, and **nothing fails** — you get a smooth, plausible entropy
curve for the wrong material. That is why the conversion is done by shared code
rather than left to the caller, and why the receiving window shows the
provenance line `From a fitted ECI J₂ = … eV → e_AB = … eV`.
:::

A fit that kept **no** nearest-neighbour pair term disables the button and says
why — *"its ordering is carried by longer-range or multi-body clusters, which
this CVM does not model"* — which is a result about the alloy rather than an
error.

---

## Validation

The `cluster_expansion_fit` test pins closed-form identities rather than
previous outputs.

Under an **orthonormal design** both estimators collapse to one line each, and
those lines are the textbook ones:

$$
\text{ridge:}\ \ \beta = \frac{\beta_\mathrm{OLS}}{1+\lambda},
\qquad
\text{lasso:}\ \ \beta = \operatorname{sign}(\beta_\mathrm{OLS})
                          \max(|\beta_\mathrm{OLS}| - \lambda,\, 0)
$$

matched to $10^{-12}$ — including that the lasso zeros are asserted with
`== 0.0`, because a solver returning $10^{-9}$ there has not selected any
variables. The SVD itself is anchored on cases with exact singular values (an
$8\times8$ Hadamard matrix, every $\sigma = \sqrt{8}$, to $10^{-13}$), and the
singular design $X = [x, x, z]$ — where a normal-equation solver divides by zero
— is checked against its closed form, the degenerate pair acting as one column
at half the penalty.

The overfitting signature is pinned on a deliberately underdetermined fixture
(24 configurations, 20 columns — *fewer data than parameters is the everyday
state of a cluster expansion*): the least-regularised model has a **much lower
training error and a clearly higher CV error** than the selected one. Anyone
selecting on the training RMSE picks that model. That is the whole point of the
module.

:::{note}
The test deliberately does **not** assert exact support recovery on the
underdetermined fixture. With 17 noise columns and 24 configurations the lasso
keeps a few by chance, which is a fact about the estimator and not a defect. Nor
does it assert that ARD recovers the true support: under an orthogonal design
its relevance criterion reduces to a one-sigma test, so a pure noise column
survives with probability $P(\chi^2_1 > 1) \approx 0.32$ *independently of how
much data there is*. Claims that cannot be true are not asserted.
:::
