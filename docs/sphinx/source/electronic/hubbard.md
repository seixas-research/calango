# Hubbard parameters from linear response

{menuselection}`Electronics --> Hubbard Parameter Calculation…` computes a
first-principles Hubbard U by the linear-response method of Cococcioni and
de Gironcoli, *Phys. Rev. B* **71**, 035105 (2005) — a U for *this* site in
*this* structure, not a literature value transplanted from another compound.

A localized potential α is added to the Hubbard manifold of one atom, and the
occupation $n$ of that manifold is measured two ways: after a **single
diagonalization at the unperturbed self-consistent potential** (the
non-interacting response $\chi_0$) and after **full self-consistency** (the
screened response $\chi$). The spurious curvature the +U term exists to cancel
is the difference of the inverse responses:

$$
U_\text{eff} = \left(\chi_0^{-1} - \chi^{-1}\right)_{ii},
\qquad \chi_{ij} = \frac{\partial n_i}{\partial \alpha_j}.
$$

:::{important}
$\chi_0$ is **not** "the response at U = 0". It is the response *before* the
other electrons have screened the perturbation. Converging it
self-consistently measures $\chi$ twice — and yields U = 0.
:::

---

## Setting up the run

**Perturbed sites.** U belongs to a *site*, not to an element: two
crystallographically inequivalent atoms of the same species have different
ones. Every atom is listed; tick each site you want a U for. Every ticked site
is perturbed in turn and *measured in all runs*, which is what makes the
response a matrix rather than a set of independent numbers — and the
off-diagonal elements of $\chi_0^{-1} - \chi^{-1}$ are the inter-site V of
the extended DFT+U+V functional, reported rather than discarded. The wizard
pre-ticks the *first* atom of each open-d/f element only: symmetry-equivalent
copies have the same U by construction, so perturbing each one multiplies cost
without adding an independent number.

**Hubbard manifold.** One shell (d / f / p) for the whole run — a single U
matrix mixes the sites it spans, and both engines take one manifold per
species anyway. Sites needing different shells are separate calculations. The
default follows the element: d for transition metals, f for the lanthanides
and actinides, p otherwise (a U on the O-2p of a transition-metal oxide is a
real calculation).

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Supercell` | Repetitions of the cell before perturbing | 2 × 2 × 2 |
| {guilabel}`Perturbations α` | Strengths in eV, comma- or space-separated; zero is dropped (the unperturbed run supplies that point) | −0.15, −0.10, −0.05, 0.05, 0.10, 0.15 |

**The supercell is the convergence parameter of the whole method, not a
performance knob.** In a periodic cell the perturbation is applied to every
image of the atom at once, so what is measured is the response to a *lattice*
of perturbations — which makes U come out too small. Repeat on a larger
supercell; U is converged when it stops moving. 1×1×1 is almost never the
answer, and the wizard flags it.

The α list pulls two ways: large enough that the occupation moves out of the
numerical noise, small enough that the response is still linear — symmetric
about zero so the fit is not biased by residual curvature. The wizard refuses
to continue with no site ticked or fewer than two α values: one α gives a line
through two points that fits perfectly whether or not the response is linear,
so it reports no error even when it is wrong.

The summary line prices the job before it is queued: **1 unperturbed
reference + 2 runs per (site, α)** — one self-consistent χ run and one
single-diagonalization χ₀ run each.

% TODO screenshot: Hubbard wizard setup page with site table, supercell and alpha list, summary line showing the run count
```{figure} /_static/img/elec_hubbard.png
:alt: Hubbard U wizard with perturbed-site table and run-count summary
:width: 92%
:figclass: screenshot

The setup stage: sites, manifold, supercell and α list, with the total SCF
count computed before anything runs.
```

---

## How the engines apply α

Both engines apply Hubbard tags **per species, not per atom** — so each
measured atom must become a species of its own, otherwise the α meant for one
atom lands on the whole sublattice and measures a different response entirely.
The two engines need different mechanisms.

**VASP** (the reference implementation — the recipe this module automates is
the VASP-wiki one):

- The perturbation is `LDAUTYPE = 3`, which applies no Hubbard correction at
  all: it adds a *constant potential shift* to the selected shell, `LDAUU` on
  spin up and `LDAUJ` on spin down — both set to α, and to zero on every other
  species. (`LDAUTYPE` 1 or 2 would add a real +U on top of the shift — a
  different calculation.)
- The atom is split into its own POSCAR species through ASE's integer-keyed
  `setups={index: symbol}`; the script mirrors ASE's exact species ordering,
  because the LDAU arrays are positional and a disagreement silently perturbs
  the wrong species.
- `LDAUPRINT = 2` prints the on-site occupancy matrices; the occupation is the
  spin-summed trace of the *last* electronic step's matrix.
- `LMAXMIX = 4` (d) or `6` (f) **is not optional**: with the default of 2 the
  occupation matrix is not converged even when the energy is, and the response
  comes out wrong by tens of percent with no other symptom.
- χ₀ is one diagonalization at the frozen reference density: `ICHARG = 11`
  with `NELM = 1`, seeded from the *unperturbed* run's `CHGCAR` — never from
  the perturbed one, which would measure χ a second time.

**Quantum ESPRESSO:**

- The perturbation is `Hubbard_alpha(i)` — the α of the method by name,
  applied per atomic *type*.
- The type is split by giving each measured atom an initial magnetic moment
  offset by a negligible ε (10⁻³ µ_B): with `nspin = 2`, ASE writes atoms of
  one element with different moments as separate `ATOMIC_SPECIES` sharing one
  pseudopotential. Consequence: **the QE path is always spin-polarized**, at
  roughly twice the cost — there is no other way to split a type.
- A vanishingly small `Hubbard_U = 10⁻⁸ eV` on every measured type is what
  makes `pw.x` build the projectors and print `Tr[ns]` at all.
- χ₀ comes from `electron_maxstep = 1` with `startingpot = 'file'`, restarting
  from the unperturbed run's density — the same single-diagonalization trick.
- The species index `Hubbard_alpha(i)` refers to is the first-appearance order
  of each (symbol, moment) pair — *not* "perturbed first" as in VASP — and the
  script reproduces that ordering exactly.

---

## The fit, the outputs, and what to check

$\chi_{ij}$ is a least-squares straight-line slope through every
$(\alpha, n)$ point *including* the unperturbed one at α = 0 — a fit rather
than a finite difference, because **its residual is the only warning that the
response has left the linear regime**. The run prints a warning when the worst
residual exceeds 10⁻³ electrons: every number is then a fit to a curve, and
the α values should be reduced.

Outputs:

- `hubbard_u.json` — the χ and χ₀ matrices, the full
  $U$ matrix, and per site the element, shell, unperturbed occupation and
  $U_\text{eff}$ (also printed as a `CALANGO_RESULT` line per site).
- `hubbard_response.json` (optional) — every raw $(\alpha, n)$ point and the
  per-element fit residuals, for plotting the response lines yourself.

Two checks before believing the number: the **supercell convergence** (repeat
larger until U stops moving) and the **fit residuals**. A singular response
matrix — the occupation did not move at all — means the projectors are not
active or α is too small, and the script says so.

:::{note}
The failure modes of this recipe are all silent — a converged χ₀, a
species-wide perturbation, a default `LMAXMIX`, a Hubbard correction instead
of a shift each produce a plausible U that is wrong. `HubbardLinearResponseTest`
pins the generated script against every one of them, and mirrors the
inverse-response arithmetic against a reference implementation.
:::

The resulting U is applied where a band structure needs it — see the
{guilabel}`Hubbard parameters…` button in {doc}`/electronic/bands`, which
attaches U to a named orbital shell via GPAW's `setups`.
