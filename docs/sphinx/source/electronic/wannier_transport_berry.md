# Boltzmann Transport and Berry Phase

Two modules on the {menuselection}`Wannier Functions` menu that consume a
Wannier Hamiltonian **H(R)** and compute from it directly, in process.

:::{admonition} Native implementations
:class: note
Both are written in Calango's own C++. Wannier90, postw90 and BoltzWann are
references for the formulas, conventions and feature scope — they are not
dependencies. Nothing is executed, linked or required to exist: no external
binary is called, and no auxiliary output file of those codes is needed.

Reading an `_hr.dat` you already have is fine and is done by Calango's own
parser; the file is a plain text table of hopping integrals.
:::

Both modules take their input from the same row, which offers three sources:

1. **From a completed run** — a Wannier Functions run finished in this session.
   Its `wannier_hr.dat`, cell, centres and spreads are read straight from the
   run directory, so nothing outside Calango is involved at any point. This is
   the normal route.
2. **Open an `_hr.dat`** — a Hamiltonian you already have, parsed by Calango.
3. **A built-in demonstration model** — a simple-cubic metal, or the two-band
   Qi–Wu–Zhang Chern insulator. These are the models the unit tests use, so
   what the panel reports can be checked against the test output directly.

A run started before Calango wrote $H(\mathbf{R})$ says so when picked, and
says what to do about it: re-run the Wannierization on the same baseline. That
is distinguished from a run that recorded the file but no longer has it, which
is a different problem with a different fix.

---

## Boltzmann Transport

{menuselection}`Wannier Functions --> Boltzmann Transport…`

Electronic and thermoelectric transport in the **constant relaxation-time
approximation**. Everything derives from one energy-resolved object, the
transport distribution function

$$\Sigma_{\alpha\beta}(\varepsilon) = \frac{1}{V N_k}\sum_{n,\mathbf{k}}
  \tau\, v_{n\alpha} v_{n\beta}\, \delta(\varepsilon - \varepsilon_{n\mathbf{k}})$$

whose Onsager moments $L^{(m)} = \int d\varepsilon\, \Sigma\,
(\varepsilon-\mu)^m (-\partial f/\partial\varepsilon)$ give

| Quantity | Expression |
|---|---|
| Electrical conductivity | $\sigma = e^2 L^{(0)}$ |
| Seebeck coefficient | $S = (eT)^{-1} (L^{(0)})^{-1} L^{(1)}$ |
| Electronic thermal conductivity | $\kappa_e = (e^2T)^{-1}[L^{(2)} - L^{(1)}(L^{(0)})^{-1}L^{(1)}]$ |
| Power factor | $S^2\sigma$ |
| Figure of merit | $zT = S^2\sigma T/(\kappa_e + \kappa_L)$ |

$\kappa_e$ is the **zero-current** thermal conductivity, which is what $zT$
wants. The subtracted term is the Peltier heat carried by the compensating
current; dropping it inflates $\kappa_e$ and deflates $zT$.

### Band velocities

Velocities come from $\partial H/\partial k$ in the Wannier gauge, evaluated
analytically. This is not an optimisation. At a band crossing the eigenvalue
branches swap, so a finite difference of *sorted* eigenvalues reports a velocity
that jumps discontinuously and can have the wrong sign; the matrix gradient has
no such problem, because the degeneracy lives in the eigenvectors.

### The relaxation time

$\tau$ enters $\sigma$ and $\kappa_e$ linearly and **cancels exactly** in $S$,
which is a ratio of two moments that each carry one factor of it. So a
constant-$\tau$ Seebeck coefficient is a genuine prediction, while $\sigma$ and
$\kappa_e$ are only as good as the $\tau$ supplied. The integrals keep $\tau$
inside the energy integral, so an energy-dependent $\tau(\varepsilon)$ can be
added later without restructuring anything.

$\kappa_L$ is a phonon quantity — nothing in an electronic structure determines
it — so it is a user input, required only for $zT$.

### Convergence, which is the part that bites

The smearing used to resolve $\delta(\varepsilon-\varepsilon_{n\mathbf{k}})$ has
to sit between two bounds:

- **above** the mean level spacing of the k-mesh, or $\Sigma(\varepsilon)$
  breaks into isolated spikes;
- **below** $k_BT$, or the smearing acts as extra temperature.

The panel reports the measured level spacing against both the smearing and
$k_BT$ after every run, so the window is visible rather than guessed at. At
300 K, $k_BT$ is 26 meV. The energy grid must resolve $k_BT$ as well: the
transport window is only a few $k_BT$ wide.

---

## Berry Phase

{menuselection}`Wannier Functions --> Berry Phase…`

| Quantity | How it is obtained |
|---|---|
| Berry phase on a closed k-path | Wilson loop — product of overlap matrices |
| Berry curvature $\Omega(k)$ | Kubo sum over band pairs of $\partial H/\partial k$ elements |
| Berry curvature map | the same, sampled over a k-plane, drawn with the app's colormaps |
| Anomalous Hall conductivity | BZ integral of $\Omega$, reported in SI and in $e^2/h$ |
| Electric polarization | Berry phase (modern theory), with its quantum |
| Hybrid Wannier centre flow | Wilson-loop eigenphases vs transverse k |

### Gauge

This is the whole difficulty of the subject, so the conventions are fixed
explicitly:

- **Berry phases** are Wilson loops. An arbitrary phase on an eigenvector
  enters once as a bra and once as a ket and cancels around the loop, so the
  result does not depend on what the eigensolver happened to return. Summing
  $\langle u|\partial u\rangle$ instead would not have that property.
- **Berry curvature** uses the Kubo form. Every factor is a matrix element
  between eigenstates, so the phases cancel pairwise; the formula never
  differentiates an eigenvector and so never requires one to vary smoothly
  with k.
- **Periodic gauge.** H(k) is built in convention I, with the orbital positions
  *not* in the phase, so $H(k+G) = H(k)$ exactly and a Wilson loop closes with
  no correction factor. Convention II would need an explicit $e^{-iG\cdot\tau}$
  on the closing overlap; mixing the two shifts a Zak phase by a constant.

### Relation to Topological Invariants

The hybrid Wannier centre flow is the same object that
{doc}`/electronic/fermi_topology` plots. That feature generates a Python script
and runs it against a completed DFT baseline; this one evaluates the same
quantity in process from H(R). They answer the same question from different
inputs, which makes them a cross-check rather than duplicates — and the panel
reports the Chern number twice, once from the curvature integral and once from
the centre winding, precisely so a disagreement flags under-sampled k.

### Curvature maps

The map defaults to a **diverging** colormap with a scale symmetric about zero.
Curvature is signed and its interesting structure is where it changes sign; a
sequential map hides exactly that, and an asymmetric scale puts zero at a colour
that moves whenever the data does.

---

## Validation

Both modules are covered by unit tests against results that are known
independently of the code:

| Test | Anchor |
|---|---|
| `boltzmann_transport` | Wiedemann–Franz: $\kappa_e/\sigma T \to \pi^2k_B^2/3e^2 = 2.44\times10^{-8}$ W·Ω/K² |
| | Seebeck sign: $S<0$ for electrons, $S>0$ for holes, $S=0$ at particle–hole symmetry |
| | $\tau$-scaling identities: $\sigma,\kappa_e \propto \tau$ exactly; $S$ independent of $\tau$ exactly |
| | analytic band velocity of a 1D cosine band |
| `berry_phase` | Qi–Wu–Zhang Chern number: $\pm1$ for $|m|<2$, $0$ outside |
| | anomalous Hall conductance quantised at $e^2/h$ |
| | Wannier centre winding, an independent route to the same Chern number |
| | SSH Zak phase: exactly $0$ and $\pi$, differing by $\pi$ between the two phases |
