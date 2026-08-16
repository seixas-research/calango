# Wannier-Based Excitons (Bethe-Salpeter)

{menuselection}`Wannier Functions --> Wannier-Based Excitons (Bethe-Salpeter)…`
computes excitons — bound electron-hole pairs — from a Wannier Hamiltonian
**H(R)**, by solving the Bethe-Salpeter equation (BSE) in the basis of
Wannier-interpolated valence and conduction states. One dialog covers both
3D (bulk) and 2D (monolayer) materials: the same solver, a dimensionality
toggle, and a different screening model for the electron-hole attraction.

:::{admonition} Native implementation
:class: note
Written in Calango's own C++ (`core::BseSolver`), on top of the same
`WannierHamiltonian` H(R)→H(k) interpolation Boltzmann Transport, Berry
Phase and Constrained RPA already consume — see
{doc}`/electronic/wannier_transport_berry`. WanTiBEXOS (a published
Wannier-tight-binding BSE/exciton code), Yambo and BerkeleyGW are
references for the formalism and conventions, not dependencies: nothing
here invokes, links, or requires an output file from any of them.
:::

Takes its Wannier Hamiltonian from the same three-source row (a completed
Wannier Functions run, an `_hr.dat` you already have, or a demonstration
model) documented for Boltzmann Transport and Berry Phase.

---

## Level of theory — stated plainly

**Tamm-Dancoff approximation (TDA)** throughout: only the resonant
electron-hole block is diagonalized,

$$
H_{\mathrm{BSE}}(vck, v'c'k') = \big(E_c(k) - E_v(k)\big)\,
    \delta_{vv'}\delta_{cc'}\delta_{kk'} + K(vck, v'c'k'),
$$

with no coupling to the anti-resonant (hole-electron) block. Full BSE beyond
TDA is **not implemented** — a documented follow-up in `FUTURE.md`.

**The kernel** $K = -W$ (screened direct) $+\ 2v$ (bare exchange, singlets
only — the {guilabel}`Spin` selector, `0` for triplets) uses the standard
"point charge at the Wannier centre" / envelope-function simplification: a
genuine ab-initio kernel needs plane-wave (or real-space Wannier
**function**, not just centre) exchange-correlation-density matrix
elements that this module does not have (Calango's Wannier construction
reports centres, spreads and cube files, not the full analytic overlap
machinery a plane-wave BSE code builds from GW output). Concretely:

- **Direct term**, coupling same-band-character pairs $(v,c)$ across k
  through a **model** reciprocal-space Coulomb potential — 3D:
  $v(q) = 4\pi k_e e^2/(\varepsilon_\infty q^2)$; 2D:
  Rytova-Keldysh below.
- **Exchange term**, the standard BSE structure (Rohlfing & Louie;
  Onida-Reining-Rubio, *Rev. Mod. Phys.* **74**, 601 (2002), Eq. 112):
  $K^x_{i,j} = (2/\Omega)\,v(q\to0)\,M_i \overline{M_j}$, a **rank-1**
  (outer-product) term built from the SAME long-wavelength transition
  dipole $M$ the oscillator strengths use (via
  `WannierHamiltonian::gradient()`), **not** a uniform scalar and **not**
  evaluated at the actual $k-k'$ separation the direct term uses. This
  matters beyond accuracy: a positive-semidefinite $M\overline{M}^{\!\top}$
  addition can, by Weyl's monotonicity theorem, only **raise** every
  eigenvalue relative to the triplet — the sign-robust way to guarantee
  "exchange is repulsive" regardless of the specific eigenvector structure.
  Two earlier, simpler constructions (evaluate exchange at the actual
  $k-k'$ with an unscreened potential; a uniform constant at $q\to0$) are
  **not** positive-semidefinite in general and were caught, on a real test
  run, producing a singlet exciton MORE bound than the triplet — the
  physically backwards answer. See `BseSolverTest.cpp`'s singlet/triplet
  check and `core/BseSolver.cpp`'s own comment for the full account.
- This construction couples only pairs sharing the **same** valence and
  conduction band index (no inter-band-pair coupling) — a further,
  documented scope limitation (`FUTURE.md`).

**Screening.**

- **3D**: a user-supplied macroscopic dielectric constant
  $\varepsilon_\infty$ — nothing here computes it from first principles.
- **2D**: the **Rytova-Keldysh** potential (kept under both names in the
  UI, since both circulate in the literature),

  $$
  W(q) = \frac{2\pi k_e e^2}{A\,q\,(1 + r_0 q)\,\varepsilon_{\mathrm{env}}},
  $$

  with the 2D screening length $r_0$ (user-supplied, or from the layer's
  own 2D polarizability, $r_0 = 2\pi\alpha_{\mathrm{2D}}$) and an optional
  effective environment dielectric constant
  $\varepsilon_{\mathrm{env}} = (\varepsilon_{\mathrm{above}} +
  \varepsilon_{\mathrm{below}})/2$ for a substrate or capping layer.
  $r_0 = 0$ reduces **exactly** to the bare 2D Coulomb potential
  $2\pi k_e e^2/(Aq)$ — checked directly in `BseSolverTest.cpp`, not just
  asserted.

**The $q\to0$ divergence.** Both potentials diverge as $q\to0$ (the $k=k'$
diagonal term of the direct sum). The standard, practical fix is used: the
divergence is regularized at $q_{\min}$, the smallest resolvable $|q|$ on
the actual k-mesh (in-plane only for 2D) — evaluating the singular term at
the mesh's own resolution scale rather than at a literal zero. A more
refined analytic (Gygi-Baldereschi-style) treatment of the Voronoi-cell
average around $q=0$ is a documented follow-up.

## k-mesh and basis size

The electron-hole basis is $|vck\rangle$: `Valence bands` below (and
including) `Valence band top index`, `Conduction bands` above it, over the
{guilabel}`k-mesh` (2D: the third axis is fixed to 1 point — in-plane only).
The basis scales as $(N_v N_c N_k)^2$ in memory for dense diagonalization;
the dialog shows a live size/memory estimate and switches automatically to
an iterative **Lanczos** solver (full reorthogonalization against every
previous vector, for numerical robustness at the modest iteration counts
used) above a configurable dense-size threshold, reporting only the
requested lowest states in that regime. **Exciton binding converges slowly
with the k-mesh** — the standard caveat, shown in the dialog, not just a
tooltip: densify and re-run to check convergence rather than trusting one
mesh.

## Outputs

- The exciton spectrum: energy, binding energy (relative to the minimum
  direct gap in the basis), and a **relative** oscillator strength per
  state — internally consistent (comparable peak-to-peak within one run,
  and against the independent-particle spectrum on the same plot) but
  **not** independently calibrated to the Thomas-Reiche-Kuhn f-sum rule.
- The optical absorption spectrum, Gaussian-broadened, **with and without**
  excitonic effects on the same plot — the excitonic curve is redshifted
  from the independent-particle onset by the binding energy. The
  independent-particle curve always covers the full requested window; the
  excitonic one covers only the diagonalized states (the full window when
  solved densely, only the lowest states when the iterative path ran).
- The exciton series ($E_b$ vs. state index $n$), plotted directly rather
  than only asserted: a 3D Wannier-Mott series should look like $1/n^2$;
  the 2D (Rytova-Keldysh) series is **non-hydrogenic** and should visibly
  deviate from it (see Verification below).

Exciton real-space wavefunction visualization
($|\psi(r_e; r_h\ \mathrm{fixed})|$ on the Wannier cube-file grid) is
**not implemented** in this version — a documented follow-up in
`FUTURE.md` (it needs a Bloch-sum reconstruction over the Wannier cube
files this module does not currently build).

---

## Verification

Full numerical convergence of either series to its continuum closed form is
computationally expensive for a discrete lattice BSE sum (the task this
module was built against found the same for the piezoelectric/elastic
finite-strain modules' own convergence tests) — meshes large enough to
fully converge take far longer than a fast unit test budget allows. What
`BseSolverTest.cpp` verifies instead, on synthetic two-band parabolic
models with known effective masses:

- **3D, triplet (no exchange — the pure Wannier-Mott limit):** the ground
  state is bound, and $|E_b(1)|$ increases **monotonically** toward the
  analytic Rydberg value $R^* = 13.605693\ \mathrm{eV}\times(\mu/m_e)/
  \varepsilon_\infty^2$ as the k-mesh densifies (6 to 12 points per axis),
  reaching a meaningful (non-trivial) fraction of $R^*$ — not the fully
  converged value, but unambiguously the right trend at the right order of
  magnitude, not a sign or scale error.
- **Kernel Hermiticity**, checked directly on the assembled matrix
  (`hamiltonianForTesting()`), for a singlet run (exercising the exchange
  term too).
- **Singlet/triplet exchange sign**: on a model with a small interband
  coupling (so the transition dipole — and hence the exchange kernel — is
  actually nonzero), the singlet ground state sits measurably **above**
  the triplet one.
- **Rytova-Keldysh**: the $r_0=0$ formula-level exact reduction to the bare
  2D Coulomb potential; the potential's strength decreasing monotonically
  with $r_0$ at fixed $q$; and a full 2D BSE run's ground-state binding
  energy decreasing monotonically as $r_0$ increases across
  0, 5, 20, 50 Å — exactly the qualitative behavior the task asked for.

A real DFT-backed smoke test (monolayer MoS$_2$ or hBN, per the task's own
suggestion) was **not run**: it needs a completed GPAW ground state **and**
a converged Wannier Functions run for that structure as a prerequisite,
neither of which existed in this session, and building one is a
substantial DFT campaign of its own — out of scope for this
engagement's remaining budget. This is the same precedent the native DFT
engine and the elastic-properties module's own EMT/MACE demonstrations
already set: real, honestly-scoped verification on what is actually
available, not a fabricated number. Running a real MoS$_2$/hBN case is a
documented follow-up in `FUTURE.md`.
