# CVM / alloy thermodynamics

{menuselection}`Modules --> Alloys --> CVM / Alloy Thermodynamics…` computes
the **configurational entropy** of a substitutional alloy by the Cluster
Variation Method, and — with the long-range-order option — its **order-disorder
transition temperature**.

The question it answers is how much of an alloy's entropy is really
$k_\mathrm{B}\ln(\text{number of arrangements})$. The textbook answer,
$S_\mathrm{ideal} = -k_\mathrm{B}\sum_i x_i \ln x_i$, assumes the species are
placed **independently** on every site. They are not: if unlike neighbours are
favoured the alloy orders, if like neighbours are favoured it clusters, and
either way there are fewer distinguishable arrangements than the ideal count.
$S_\mathrm{ideal}$ is therefore an **upper bound** — and for a high-entropy
alloy advertised as "stabilised by configurational entropy", the gap between
the bound and the truth is the entire question.

% TODO screenshot: CVM window with the three entropy curves and the shaded gap against the ideal baseline
```{figure} /_static/img/sim_cvm_comparison.png
:alt: The CVM comparison window with ideal, pair and tetrahedron entropy curves
:width: 92%
:figclass: screenshot

The three approximations against temperature. The shaded gap *is* the result.
```

---

## Kikuchi's construction, and why three curves

Kikuchi writes the entropy as a sum over clusters with alternating-sign
corrections, so that the correlations a cluster already accounts for are not
counted again by its subclusters:

$$
S/k_\mathrm{B} = -\sum_\alpha a_\alpha \sum_{\text{configs}}
                  \rho_\alpha \ln \rho_\alpha
$$

with $\rho_\alpha$ the probability of each decoration of cluster $\alpha$ and
$a_\alpha$ the Kikuchi-Barker coefficients, fixed by requiring every
subcluster's contribution to cancel down to exactly one net counting.

Three approximations are provided, and they are a **hierarchy** rather than
alternatives — each is the previous one plus more correlation:

| Approximation | Also known as | What it sees |
|---|---|---|
| **Point** | Bragg-Williams | Nothing. Sites independent; this *is* $S_\mathrm{ideal}$, and no interaction enters |
| **Pair** | Bethe-Peierls-Guggenheim | Nearest-neighbour pair correlations. **Exact on a 1D chain** |
| **Tetrahedron** | Kikuchi | Points, pairs, triangles and tetrahedra — the smallest cluster containing the frustrated triangles FCC is built from |

**Computing all three is the deliverable.** The spread between them *is* the
size of the correlation correction, and a single number from any one of them
does not tell you whether it mattered. The window plots all three at once and
shades the gap to the ideal baseline, because the gap is the result.

The free energy is minimised by the **Kikuchi-Sanchez natural iteration**
rather than by a general optimiser: NI is guaranteed to decrease the functional
and so cannot wander onto a saddle the way a naive fixed point can.

---

## What the numbers look like

$S_\mathrm{ideal} = \ln 2 = 0.693\,k_\mathrm{B}$ for an equiatomic binary and
$\ln 5 = 1.609\,k_\mathrm{B}$ for an equiatomic quinary — the latter being the
number quoted for high-entropy alloys.

For an FCC binary at $x = 0.5$ with $e_{AB} = -0.02$ eV, at 500 K:

$$
S_\mathrm{pair} = 0.5358
\;<\; S_\mathrm{tet} = 0.6220
\;<\; S_\mathrm{ideal} = 0.6931 \;\; k_\mathrm{B}
$$

The ordering is the physically meaningful part. The **pair approximation cannot
see the frustration** of the close-packed tetrahedron — on FCC you cannot make
every triangle of nearest neighbours unlike-decorated — so it believes the
alloy orders more than it does, and therefore **over**estimates ordering and
**under**estimates the entropy. The tetrahedron, which contains those triangles,
recovers part of the gap.

:::{tip}
**Warren-Cowley α is reported per temperature**, and it is what makes the
temperature dependence legible: $S$ alone does not say *which way* the alloy is
departing from random. $\alpha = 1 - P(j|i)/x_j$ is zero for a random alloy,
**negative for ordering** (unlike neighbours preferred) and **positive for
clustering**.

It is also the more sensitive probe. The residual short-range order is **first
order in $\beta J$** while the entropy deviation is **second order**, so at high
temperature $\alpha$ is still visibly non-zero when $S$ has already collapsed
onto the ideal value to four decimal places. In the disordered phase $\alpha$ is
directly comparable against a **diffuse-scattering measurement**; the entropy is
not comparable against anything measured.
:::

---

## Triplets: why A₃B and AB₃ are different alloys

The tetrahedron approximation consumes a nearest-neighbour **triplet**
interaction as well as the pair one. This is not a refinement; it changes what
the model can express at all.

A binary with **pair interactions only** is exactly symmetric under
A ↔ B at complementary compositions. It therefore assigns
$x = 0.25$ and $x = 0.75$ the *same* energy and cannot distinguish A₃B from
AB₃ — which is to say, it cannot choose an ordered structure. Measured on an
FCC tetrahedron at 600 K:

| Model | $E(x{=}0.25) - E(x{=}0.75)$ |
|---|---|
| Pair only | $9.4\times10^{-17}$ eV — **machine zero** |
| With a triplet ECI | $2.41\times10^{-2}$ eV |

The first number is an identity, not a small residual. A cluster expansion
fitted *with* triplets and then evaluated with a pair-only CVM has thrown away
exactly the part of the fit that chose the phase.

$\varepsilon_3(i,j,k) = J_3 s_i s_j s_k$ with $s = +1$ for A and $-1$ for B, so
the tensor alternates in sign with the number of B atoms on the triangle.

:::{note}
**Triplets only work with the tetrahedron approximation.** A triangle is not a
subcluster of a pair, so a pair model has nowhere to put one. Supplying triplets
with the Pair approximation is not silently accepted: they are **dropped**, and
the result carries the warning *"the entropy and the energy below describe a
pair-only model, so do not read them as the cluster expansion that was
fitted."*

The window itself has no triplet field — it exposes a single $e_{AB}$. Triplets
reach the solver through the {doc}`/simulations/orchestration` canvas, whose
CVM node takes a fitted triplet ECI from the {doc}`/simulations/eci_fit` node
and only supplies it when the approximation is the tetrahedron.
:::

---

## Long-range order and $T_\mathrm{c}$

:::{warning}
**The homogeneous solver cannot produce an order-disorder transition at all.**
One sublattice means every site is statistically identical, and there is no
symmetry left to break. Run Cu₃Au through it and you get a perfectly smooth
$S(T)$ and $\alpha(T)$ straight through 663 K. That is not a bug — it is the
approximation stating its own limits.

For a transition, tick {guilabel}`Allow long-range order (4 sublattices)`.
:::

That checkbox switches to a four-sublattice solver. The geometry is what makes
it work: the FCC nearest-neighbour tetrahedron has **exactly one vertex on each
of the four simple-cubic sublattices** FCC decomposes into, so the tetrahedron
distribution can be indexed by *position = sublattice, value = species* — and,
unlike the homogeneous case, it is **not symmetric under permuting the
positions**. That asymmetry *is* the long-range order. Only the overall
composition is constrained; the per-sublattice compositions are free, which is
what lets L1₂ form at $x = 0.25$ with three A-rich sublattices and one B-rich
one.

Both the ordered and the disordered branch are solved at every temperature, and
$T_\mathrm{c}$ is located by **which branch has the lower free energy** — by
the free energies *crossing*, not by an order parameter decaying to zero.

:::{note}
The FCC L1₂ transition is **first order**: the order parameter **jumps**. A
search for $\eta \to 0$ would either find nothing or find the spinodal where
the ordered branch stops existing, which is *above* $T_\mathrm{c}$ and is not
the transition. The window reports $\eta$ immediately below and above
$T_\mathrm{c}$, plus $\eta$ of the ordered branch where it is *metastable*
above $T_\mathrm{c}$ — a second, independent signature of first order, since
the ordered solution does not cease to exist at $T_\mathrm{c}$, it merely stops
winning.
:::

Ticking the box does two other things, both deliberate:

* **The composition snaps to the structure's stoichiometry** — L1₂ ↔ $x = 1/4$,
  L1₀ ↔ $x = 1/2$. Without it, asking for L1₂ at $x = 0.5$ correctly reports
  "no transition", which reads as a broken feature rather than as a true answer
  about a composition where that structure is not stoichiometric.
* **The minimum temperature is raised to at least 300 K**, because of the
  domain-mixed fixed point described below.

While long-range order is on, the {guilabel}`Lattice` combo has no effect: the
four-sublattice solver is FCC by construction.

### Cu₃Au

The benchmark, and the reason the module can be trusted at all:

| Quantity | Value |
|---|---|
| Tetrahedron CVM, L1₂ at $x = 1/4$ | $k_\mathrm{B}T_\mathrm{c} = \mathbf{1.9245\,V_2}$ |
| Bragg-Williams (mean field), same system | $k_\mathrm{B}T_\mathrm{c} = 3.2806\,V_2$ |
| $V_2$ reproducing the measured 663 K | **29.69 meV** |

The mean-field comparison is a **provable** check rather than a tolerance. For
L1₀ at $x = 1/2$ the Bragg-Williams transition has a closed form,
$k_\mathrm{B}T_\mathrm{c} = 4V_2$ exactly — derived independently by
linearising the mean-field equations, and equivalently by Fourier-transforming
the FCC nearest-neighbour interaction, whose minimum sits at the X point with
$J(X) = -4V_2$. The solver returns 3.99972, and one-sidedly — the mean-field
L1₀ transition is *second* order, so both the order parameter and the
free-energy difference vanish continuously at $T_\mathrm{c}$ and the finite
thresholds the bisection needs bite a hair below the true temperature. And CVM
must come out **below** mean field, because it includes correlations that mean
field ignores; FCC is frustrated enough that it is not merely a little below.

:::{note}
The bracket **MC < CVM < mean field** is the robust statement, and both its ends
are provable. The particular literature values often quoted for the middle —
about 1.89 for the tetrahedron CVM and about 1.74 for Monte Carlo, and the
25–30 meV window for the Cu-Au nearest-neighbour $V_2$ — are **recalled from
standard literature and are not traced to a cited source anywhere in this
codebase**. Calango measures 1.893 for L1₀ at $x = 1/2$ and 1.9245 for L1₂ at
$x = 1/4$; treat the agreement as encouraging rather than as validation against
a reference you can look up from here.
:::

---

## What this is not

These limits are structural. They are reported as warnings with every result
rather than left to documentation, but they belong here too.

:::{warning}
**This is not a phase diagram.** Neither solver does a **common-tangent
construction**. Both work at **fixed composition**, comparing homogeneous
phases. There are therefore no two-phase fields, no solvus lines, and no
composition range over which an ordered phase is stable — asking for L1₂ at a
composition away from $x = 1/4$ gets you a "no transition" answer and an
explanation, not a phase boundary.
:::

* **Nearest-neighbour interactions only.** No second-neighbour terms, which on
  FCC are what select between competing ordered structures in several systems.
  The four-sublattice solver additionally takes **pair interactions only** — no
  multi-body ECIs are wired into it.
* **BCC borrows FCC's counting and is not quantitative.** The Kikuchi-Barker
  coefficients depend on how many pairs, triangles and tetrahedra share a site,
  which is a property of the lattice and nothing else. BCC needs the *irregular*
  tetrahedron built from first and second neighbours, with different
  coefficients. Choosing BCC does not fail loudly — it produces a smooth,
  plausible, wrong entropy — so the run says so in its warnings.
* **Configurational entropy only.** No vibrational, electronic or magnetic
  contribution.
* **Long-range order is FCC-only** (L1₂ and L1₀). B2/D0₃ ordering on BCC is not
  available.

### Two documented pathologies

Both are real behaviours of the truncated Kikuchi expansion on a frustrated
lattice, both are reported rather than hidden, and neither is a bug in the
iteration.

1. **Negative entropy on the disordered branch.** Well below the transition the
   *disordered* branch returns $S < 0$ — at $x = 1/2$ it tends to
   $-0.929\,k_\mathrm{B}$. The Kikuchi expansion is not a probability, and
   nothing forces its truncation to stay positive on a lattice this frustrated.
   It **never affects the stable branch or $T_\mathrm{c}$**, which is what you
   read; it does mean the disordered free energy is meaningless deep inside the
   ordered field.
2. **A domain-mixed fixed point.** At $x = 1/2$, below roughly
   $0.35\,T_\mathrm{c}$, the L1₀ start converges onto an equal mixture of the
   two symmetry-related L1₀ domains. It has exactly the ground-state energy,
   exactly $S = \ln 2$, and **zero** long-range order — the point marginals of
   the two domains average away while their pair correlations do not. It is a
   fixed point only because zeros in the pair marginals are absorbing under a
   multiplicative update, and it is not a physical homogeneous phase (a real
   crystal would pay a domain wall). Solutions with vanishing order parameter
   are therefore never accepted as ordered, and hitting one is reported. The
   practical consequence is that an L1₀ transition must be bracketed **from
   inside the ordered field** — which is why ticking the checkbox raises the
   minimum temperature. L1₂ at $x = 1/4$ does not suffer from this at any
   temperature.

---

## Using it

| Control | Default | Notes |
|---|---|---|
| {guilabel}`Lattice` | FCC (z = 12) | Also BCC (z = 8) and 1D chain (z = 2), where the pair approximation is exact |
| {guilabel}`Species` | 2 | Up to 5 — five gives the $\ln 5 = 1.61\,k_\mathrm{B}$ ideal entropy quoted for HEAs |
| Composition table | A 0.5 / B 0.5 | Normalised on entry |
| {guilabel}`e_AB (unlike-pair energy)` | −0.02 eV | With $e_{AA} = e_{BB} = 0$. **Negative orders**, positive clusters, zero returns the ideal entropy at every temperature — which is the check that the solver is honest |
| {guilabel}`Allow long-range order (4 sublattices)` | off | See above |
| {guilabel}`Ordered structure` | L1₂ (Cu₃Au-type, x = 1/4) | Or L1₀ (CuAu-type, x = 1/2); enabled only with long-range order on |
| {guilabel}`Plot against` | Temperature | Or composition, for a binary — the ideal entropy is then a *curve* peaking at ln 2, not a baseline |
| {guilabel}`T min` / {guilabel}`T max` / {guilabel}`Points` | 100 K / 2000 K / 120 | |

Nothing recomputes on its own except the stoichiometry snap; press
{guilabel}`Compute`. {guilabel}`Export Image…` renders at 3×;
{guilabel}`Export Data…` writes CSV.

:::{note}
{guilabel}`Export Data…` always writes the **homogeneous** temperature sweep —
`temperature_K, ideal_kB, bpg_kB, tetrahedron_kB` — regardless of the axis and
of whether long-range order is on. With the composition axis selected the first
column is still temperature despite the header, and with long-range order on the
CSV carries the homogeneous curves rather than the ordered/disordered pair that
is plotted, and no $T_\mathrm{c}$. Export the image for those.
:::

The interaction can be typed in, or it can come from a fit — see
{doc}`/simulations/eci_fit`, whose {guilabel}`Send to CVM…` button opens this
window with the fitted nearest-neighbour pair ECI already converted and applied.
The convention it uses is spelled out beside the field, because half the
literature writes the opposite one:

$$
e_{AB} = -J_2, \qquad e_{AA} = e_{BB} = +J_2
\qquad\Longrightarrow\qquad J_2 > 0 \ \text{ORDERS.}
$$

---

## Validation

The `cluster_variation` and `sublattice_cluster_variation` tests check
identities, not previous outputs.

**The pair approximation is exact on a 1D chain**, and the 1D Ising chain has a
closed-form transfer-matrix solution. Calango matches it to **$10^{-9}\,
k_\mathrm{B}$ in entropy and $10^{-12}$ eV in energy** — and measures residuals
of order $10^{-16}$. That is an identity, not a tolerance. Both signs of $J$
are run, because a sign error in the energy convention passes one and fails the
other.

**The zero-interaction limit pins the Kikuchi-Barker coefficients**
$(a_4, a_3, a_2, a_1) = (2, 0, -6, 5)$ for FCC — the triangles genuinely vanish
— together with the composition multiplier, by requiring $S$ to be *exactly*
$S_\mathrm{ideal}$ when nothing interacts. It is checked at
**$x = 0.3$, not $x = 0.5$**, and measures $1.7\times10^{-14}\,k_\mathrm{B}$.
The composition is the point: an earlier derivation dropped the composition
multiplier, giving $w \sim (\prod x)^{7/8}$, which at $x = 0.5$ is completely
invisible — every tuple picks up the same factor and normalisation hides it —
and wrong everywhere else. The four-sublattice version of the same identity runs
at $x = 0.25$ and measures $2.2\times10^{-16}\,k_\mathrm{B}$.

The four-sublattice solver's **disordered branch reproduces the homogeneous
solver exactly** ($10^{-11}\,k_\mathrm{B}$), and an L1₂ run started from three
*deliberately unequal* sublattices converges back to three equal ones — so
"three sublattices are equal" is a result rather than something true by
construction.
