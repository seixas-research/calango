# Thermodynamic integration

{menuselection}`Simulation --> Thermodynamic Integration…` computes an
**absolute** free energy — $F$, and $G = F + PV$ — by molecular dynamics.

That is not something a simulation can measure directly. Energy, temperature,
pressure and stress are all mechanical averages over a trajectory; the free
energy is the logarithm of a partition function, and no average of any
observable is equal to it. Thermodynamic integration recovers it by building a
**reversible path** from a reference system whose free energy is known in
closed form to the system you care about, and integrating along it.

$$
U(\lambda) = (1-\lambda)\,U_\mathrm{ref} + \lambda\,U_\mathrm{target},
\qquad
\frac{\partial F}{\partial \lambda}
  = \Big\langle \frac{\partial U}{\partial\lambda} \Big\rangle_\lambda
  = \big\langle U_\mathrm{target} - U_\mathrm{ref} \big\rangle_\lambda
$$

$$
\Delta F = \int_0^1 \big\langle U_\mathrm{target} - U_\mathrm{ref}
           \big\rangle_\lambda \, \mathrm{d}\lambda,
\qquad
F = F_\mathrm{ref} + \Delta F,
\qquad
G = F + PV
$$

Each $\lambda$ is one MD run — a **window** — that reports one number. The
module is the machinery around those numbers: the closed-form $F_\mathrm{ref}$,
the quadrature, the error bars, and the diagnostics that say when the answer
should not be believed.

% TODO screenshot: TI wizard stage 2 with the reference combo, the lambda schedule and the path summary
```{figure} /_static/img/sim_thermodynamic_integration_wizard.png
:alt: The Integration Path and Sampling stage of the Thermodynamic Integration wizard
:width: 92%
:figclass: screenshot

Stage 2 — the reference system, the λ path and the per-window dynamics.
```

:::{note}
Every energy the module reports is **per simulation cell**, matching the
phonon thermodynamics module. Per-atom values are reported beside them and are
spelled out in the field names, so a consumer never has to guess which is
which.
:::

---

## The three references

The whole method rests on $F_\mathrm{ref}$ being *exact*. An approximate
reference does not add noise to the answer; it adds a bias that no amount of
MD removes.

| Reference | Exact? | Natural for | Endpoint behaviour |
|---|---|---|---|
| {guilabel}`Ideal gas (Sackur-Tetrode) — liquids` | **Yes**, no caveat | liquids | **Worst.** Nothing keeps atoms apart at λ → 0 |
| {guilabel}`Einstein crystal (Frenkel-Ladd) — solids` | **Yes**, no caveat | solids | Free of it — tethered atoms never overlap |
| {guilabel}`Lennard-Jones fluid` | **No closed form exists** | see below | Has a core, so well behaved |

**Ideal gas.** $F_\mathrm{id} = k_\mathrm{B}T \sum_s N_s [\ln(\rho_s\Lambda_s^3)
- 1]$, summed over *species*: atoms are grouped by mass, each group gets its own
thermal de Broglie wavelength and its own $N!$. The factorial is the whole
point — dropping it produces a number that scales almost right and is wrong by
$Nk_\mathrm{B}T(\ln N - 1)$. A mixture collapsed onto one average mass gets a
silently different answer, which is why the masses travel with the run.

**Einstein crystal.** $F_\mathrm{Ein} = 3Nk_\mathrm{B}T\ln(\beta\hbar\omega)$
with $\omega = \sqrt{\alpha/m}$, evaluated two independent ways (the oscillator
partition function and the Gaussian configurational integral) and pinned
against each other by a test, because agreeing is evidence that neither dropped
a $2\pi$.

There is **no $1/N!$ here**, deliberately — the opposite of the ideal-gas case
and exactly where a copy-paste between the two goes wrong. Einstein atoms are
tethered to distinguishable lattice sites and cannot exchange, so a crystal
samples *one* permutation and there is no indistinguishability factor to divide
out.

{guilabel}`Hold the centre of mass fixed` is on by default and is not a
convenience. A rigid translation of the whole crystal costs the target nothing
and is quadratically penalised by the springs, so the two Hamiltonians differ
by a soft mode the tether kills. Fixing the centre of mass removes it, at the
price of an $O(\ln N)$ finite-size correction that the closed form then applies
— and the one checkbox drives *both* the `FixCom` constraint in the run and the
correction in the assembly, so the two cannot disagree.

:::{warning}
**The Lennard-Jones reference cannot carry a liquid.**

There is no exact closed form for the free energy of an LJ fluid. Calango
evaluates the exact Hirschfelder-Curtiss-Bird second-virial series —
$\beta A_\mathrm{ex}/N = B_2\rho + O(\rho^2)$, an exact expansion and not a fit
— which is quantitative only for a reduced density
$\rho^* = (N/V)\sigma^3 \lesssim 0.05$. Above that the reference returns
`valid = false` and the run reports no free energy at all, naming the density
that broke it.

A liquid is an order of magnitude denser than that. **So for an actual liquid,
use the ideal gas or the Einstein crystal.** The LJ entry is there to put an LJ
fluid *on the path* — as a target of its own ideal-gas integration whose result
you then supply back — not to start one at liquid density.

The alternative would have been to transcribe a 33-parameter equation-of-state
fit. An unverifiable table of coefficients that silently produces plausible
numbers is worse than a function that refuses.
:::

---

## The endpoint singularity

This is the failure mode of the method, and it deserves naming before the
settings that guard against it.

With **linear** coupling, $U(\lambda) = (1-\lambda)U_\mathrm{ref} +
\lambda U_\mathrm{target}$, and an ideal-gas reference, atoms at small $\lambda$
are almost free and may sit on top of one another. A target with a hard
repulsive core then charges an unbounded energy for those configurations, and
$\langle\partial U/\partial\lambda\rangle$ **diverges as $\lambda \to 0$**. A
uniform grid integrates that divergence without ever noticing: every window
returns a finite number, the quadrature returns a finite number, and the answer
is wrong.

Calango puts three layers against it:

1. **The default schedule never samples λ = 0.** Gauss-Legendre nodes are
   strictly interior to $[0,1]$, and they already cluster towards both ends,
   which is where the integrand bends. This is the first and cheapest line of
   defence.
2. **A runtime detector.** After the run, the variance and the magnitude of the
   two end windows are compared against the **median over the whole path** —
   a ratio, not a fixed energy, because the integrand's scale is whatever the
   cohesive energy happens to be and only its *shape* says whether a divergence
   is being integrated. Above 8× either way, the result carries the diagnosis
   and its two ratios.
3. **A warning in the wizard.** Choosing the ideal gas together with a schedule
   whose first node is λ = 0 (Uniform, or Power law) turns a red note on the
   settings page, before anything has been paid for.

:::{admonition} Why there is no soft-core coupling
:class: tip

The textbook fix is a **soft-core** path: replace the linear mixing with a
coupling that regularises the pair potential itself, so that overlapping atoms
cost a finite energy at small λ.

That fix is not available here, and cannot be. A soft core modifies the
**pair form** of the potential. Calango's target is an arbitrary ASE
calculator — MACE, GPAW, xTB, a LAMMPS force field — and none of them exposes a
pair form to modify. There is no general way to soften a machine-learning
potential or a self-consistent DFT energy.

So the module does the honest thing instead: avoid the endpoint, detect it when
it bites anyway, and say so. It does not pretend to have solved it.
:::

---

## Setting it up

The wizard is three stages: **Calculator Settings** (the target Hamiltonian —
any engine from {doc}`/simulations/calculators`), **Integration Path &
Sampling**, then the editable {doc}`/simulations/scripts` review.

### The λ path

| Setting | Default | Notes |
|---|---|---|
| {guilabel}`λ schedule` | Gauss-Legendre (interior) | Also Uniform, Power law (clustered at λ = 0), Clustered at both ends |
| {guilabel}`Clustering exponent` | 2.0 | Read only by the two clustering laws; 1 degenerates to uniform |
| {guilabel}`Quadrature` | Gauss-Legendre | Forced to match, and disabled, while the schedule is Gauss-Legendre |
| {guilabel}`λ windows` | 12 | Each is an independent MD run; this is also what fixes the λ-grid error |
| {guilabel}`Dispatch as separate jobs` | 1 | See [below](#how-the-windows-run) — resumability, *not* speed |
| {guilabel}`Also sweep λ backwards` | off | Hysteresis check; doubles the cost, withdrawn when jobs > 1 |

Gauss weights on non-Gauss nodes are not an approximation of anything, so that
combination is **refused rather than silently reweighted**; Simpson needs a
uniform grid with the right point count and falls back to the trapezoid, saying
so in the report. The schedule and the quadrature are interlocked in the wizard
for the same reason.

### Dynamics, per window

| Setting | Default | Notes |
|---|---|---|
| {guilabel}`Ensemble` | NVT — Langevin | Six thermostats; **NVE is deliberately absent** |
| {guilabel}`Temperature` | 300 K | One set point for the whole path |
| {guilabel}`Pressure` | 1 bar | Drives the barostat, and *always* the PV term of G |
| {guilabel}`Time step` | 1.0 fs | |
| {guilabel}`Equilibration steps / window` | 2000 | Discarded before any sample is taken |
| {guilabel}`Production steps / window` | 10000 | |
| {guilabel}`Sampling interval` | 10 | Record $\partial U/\partial\lambda$ every N steps |

**NVE is not offered** because $\langle\partial U/\partial\lambda\rangle$ has to
be a canonical average at *one* temperature, and a microcanonical window sits at
a different temperature for every λ.

**NPT is offered but warned about.** The reference free energy is a function of
$V$; under a barostat the cell breathes and there is no single volume to
evaluate it at. Equilibrate at NPT, then integrate at the resulting volume under
NVT. The results reader independently checks the volume spread across the path
and says so when it exceeds 1 %.

**Equilibration is not optional in practice**, and setting it to zero raises its
own note. Averaging over the transient biases every window in the *same*
direction — each is still relaxing towards its own λ-coupled ensemble — so the
bias survives the λ integral instead of cancelling, and it looks nothing like
noise on a plot.

A live summary line under the form restates the path: the λ values, the window
count, the samples per window, the total picoseconds of MD, and the number of
jobs.

(how-the-windows-run)=

### How the windows run

{guilabel}`Dispatch as separate jobs` splits the path into **contiguous**
slices — contiguous because within a job each window inherits its neighbour's
configuration, and evenly sized because wall time is set by the slowest slice.

:::{warning}
**This is not a speed-up.** The slices go through the ordinary
{doc}`/simulations/jobs` queue, which has a single job runner, so they execute
**one after another** exactly as the unsplit path would. What splitting buys is

* **resumability** — a slice that dies costs its own windows, not the path, and
  re-running that slice is the resume mechanism; and
* **cluster dispatch** — separate scripts that a scheduler can place
  independently.

λ windows run **sequentially**. Do not budget wall time as though they did not.
:::

Every job writes into the same results directory and re-scans the whole path
when it finishes, so whichever job finishes last finds a complete set and
writes the summary. Earlier ones write a summary naming exactly which windows
are still missing.

---

## Error bars

A free energy with no error bar cannot be compared with anything — not with an
experiment, not with another code, not with the same code at a different cell
size. Two independent errors are reported, and the module says **which of the
two to spend effort on**.

**Statistical.** The generated script writes the **raw
$\partial U/\partial\lambda$ series**, not just its mean and variance. That is
the point: the autocorrelation time cannot be recovered from moments, and MD
samples are not independent. Calango computes the integrated autocorrelation
time $\tau_\mathrm{int}$ with **Sokal's automatic windowing** — truncated,
because the tail of the correlogram is pure noise whose variance grows with the
number of terms added, so the naive full sum does not merely lose precision, it
does not converge — and reports

$$
\sigma_\mathrm{mean} = \sqrt{\frac{\mathrm{var}\cdot\tau_\mathrm{int}}{N}} .
$$

Naive $\sigma/\sqrt N$ over correlated samples under-reports by $\sqrt{2\tau}$,
routinely a factor of three: exactly the size of the discrepancies people then
explain away. A **block-averaging** estimate is computed independently as a
cross-check, and the two disagreeing by more than 2.5× is reported as *the
production run is too short for either*.

These are propagated **exactly** through the quadrature, which is why the rules
are expressed as weights rather than as a number:

$$
\sigma_I = \sqrt{\textstyle\sum_i w_i^2 \sigma_i^2}.
$$

**λ-grid.** The chosen rule is compared against a closed trapezoid on the same
nodes and, when the grid allows, against the trapezoid on **every second node**.
That difference costs nothing and catches both an under-resolved grid and an
integrand with a singular end. When it exceeds 3× the statistical error the
report says so in as many words: *the lambda grid, not the sampling, dominates
the error — add windows rather than MD steps.*

The two are combined in quadrature into the number to quote.

---

## Partial failure

`expectedWindows` is passed to the integrator **separately from the list of
windows that came back**. That parameter exists precisely so that handing it
the length of the list defeats the check.

A path with a dead window is not a noisier path — it is a **different
integral**. Quadrature weights are a property of the node set, so dropping a
node and reweighting the survivors integrates a curve that was never sampled. A
run missing any window therefore reports

```text
*** INCOMPLETE — NO FREE ENERGY IS REPORTED ***
Missing / failed windows: 3, 7
```

and leaves ΔF at zero rather than integrating over what is left. The summary
distinguishes **missing** (no file — the window never ran) from **failed** (a
file with a failure status — it ran and died), which is what tells you whether
to re-run a slice or to change the physics.

---

## The integrand viewer

The free energy is a *number*; what goes wrong is a *shape*. Every
characteristic TI failure is visible in the integrand and invisible in ΔF, so
the plot is not decoration.

% TODO screenshot: TI integrand plot with error bars, shaded integral and the endpoint band
```{figure} /_static/img/sim_thermodynamic_integration_integrand.png
:alt: The TI integrand plot showing dU/dlambda against lambda with error bars and the shaded integral
:width: 92%
:figclass: screenshot

⟨∂U/∂λ⟩ against λ. The shaded area *is* ΔF.
```

The plot shows $\langle\partial U/\partial\lambda\rangle$ against λ with

* **1σ error bars from the autocorrelation-corrected estimate**, not from the
  raw variance — the raw variance understates a correlated series by exactly
  the factor this module exists to compute;
* the **shaded integral**, because ΔF *is* that area and nothing else;
* **failed windows as vertical rules**, so a gap in the path is visible rather
  than inferred from a warning list;
* the **endpoint band** and the label *endpoint singularity suspected* when the
  detector fired;
* forward and backward sweeps overlaid (solid and dashed) when the hysteresis
  check ran — they must lie on top of each other, and hysteresis does not look
  like noise.

**The window opens by itself when a run finishes.** There is no menu entry
for it, deliberately: an "open the results" action whose only job is to ask
which file you meant is a question the application can already answer. The
three other ways in all skip that question — the {guilabel}`Load Results…`
button on the wizard's *Integration Path & Sampling* stage, the same button on
this window (which walks from one run to the next), and the Processes panel,
which offers {guilabel}`Thermodynamic Integration Viewer` on any job directory
holding a `ti.json`.

Two export buttons sit beside them. {guilabel}`Export Results…` writes either
the per-window table as CSV — with the run's conditions and the assembled
$F$, $PV$ and $G$ as commented header lines, because the same $\langle \partial
U/\partial\lambda \rangle$ numbers integrate to a different free energy under a
different quadrature rule — or the report text exactly as shown. Failed windows
keep their row, empty and labelled, so the file cannot be mistaken for a
shorter path that converged. {guilabel}`Export Plot…` renders the figure on
screen at 3× through the same drawing path, so an exported figure cannot drift
from the one you looked at.

---

## Outputs

Each window writes `ti_window_NNN_<sweep>.json` into the run's results
directory; the aggregate is `ti.json`, schema
`calango.thermodynamic_integration/1`.

| Key | Meaning |
|---|---|
| `reference` / `schedule` / `quadrature` | `ideal_gas` / `einstein_crystal` / `lennard_jones_fluid`; the schedule and rule, carried explicitly so the reader never has to guess the node-weight pairing |
| `lambdas` / `windows_expected` | The **whole** path, not this job's slice — this is what the completeness check is made against |
| `temperature_K`, `pressure_GPa`, `natoms`, `volume_A3`, `masses_amu` | The thermodynamic state the reference is evaluated at |
| `einstein_spring_eV_per_A2`, `einstein_fixed_com`, `lj_epsilon_eV`, `lj_sigma_A` | Reference parameters |
| `sweeps` | `["forward"]`, or both directions when the hysteresis check ran |
| `paths.<sweep>.windows` | One record per window: `lambda`, `samples`, `mean_dudl_eV`, `variance_dudl_eV2`, **`series_eV` (the raw series)**, `final_temperature_K`, `mean_volume_A3` |
| `paths.<sweep>.missing` / `.failed` | Never-ran versus ran-and-died |
| `paths.<sweep>.complete` | `false` ⇒ no free energy is reported |
| `paths.<sweep>.delta_F_trapezoid_eV` | A convenience trapezoid from the script, `null` when the path is incomplete. Calango **re-integrates** with the configured rule, the exact error propagation, the reference free energy and the endpoint diagnostics at read time |

The quadrature, the statistics and the closed-form references all live in
C++ and run when the file is read — so a run downloaded from a cluster is
analysed with the same code as a local one, and an old `ti.json` picks up
later improvements to the analysis.

:::{note}
The LJ cut-off radius is emitted into the generated script but is **not**
recorded in `ti.json`. If you use the Lennard-Jones reference, keep the script.
:::

---

## A worked example, and what it shows

Liquid copper, 32 atoms at 1600 K, MACE as the target, ideal-gas reference,
five Gauss-Legendre windows. This was a development run, not part of the
shipped test suite — an MD run against a downloaded ML potential is not
something a unit test can pin — so the numbers below are reported, not
asserted.

| Quantity | Value |
|---|---|
| ⟨∂U/∂λ⟩ across the path | −53.6 → −120.9 eV |
| Per-sample variance across the path | 301 → 1.17 (a factor of **257**) |
| Endpoint detector | **fired**, at ×107 against the path median |
| ΔF | −106.35 eV |
| Statistical error | 0.46 eV |
| **λ-grid error** | **6.57 eV** |

The conclusion is worth stating plainly, because it is the opposite of the
instinct: **more MD time would buy nothing here.** The sampling error is already
0.46 eV and the λ-grid error is 6.57 eV — **14× larger**. Five nodes cannot
resolve an integrand whose variance changes by a factor of 257 across the path.
The run needs *more windows*, clustered harder towards λ = 0; longer production
runs would shrink the smaller of the two errors and leave the answer exactly as
wrong.

This is precisely what the separate reporting of the two errors is for, and it
is why the module says which one to spend on rather than adding them and
quoting a total.

---

## What has and has not been exercised

:::{warning}
**Only EMT and MACE have been run through this module end to end.** GPAW and
xTB targets are supported by construction — the target is an ordinary ASE
calculator and the coupling is engine-agnostic — but they are **untested**
here. The cost is also different in kind: a DFT target pays a full SCF per MD
step, per window.

The C++ half is covered by the `thermodynamic_integration` test (161
assertions): the closed-form references against their analytic values, the
Gauss-Legendre rule against the polynomial exactness that defines it, the
autocorrelation estimator against block averaging, the error propagation, and
the refusal paths — the LJ density limit, the incomplete path, Gauss weights on
non-Gauss nodes.
:::

Two further limits are structural rather than provisional. The path integrates
at **fixed composition and fixed cell** (see the NPT note above), and the
reference is evaluated **classically** — the ideal gas warns when
$\rho\Lambda^3$ approaches 1 and the Einstein crystal warns when
$\hbar\omega/k_\mathrm{B}T$ does, because below those the quantum partition
function is the right one and this is not it.

---

## On the canvas

The {doc}`/simulations/orchestration` canvas carries a **Thermodynamic
Integration** node. It is the only simulation-family node with **no defaults**: it
refuses to run until it has been configured and saved, because the reference
system and the temperature *are* the experiment, not settings around one.

The node runs **one job for the whole path** — the job-splitting option is
offered only on the menu path, since the canvas owns the queue and a node
quietly becoming twelve jobs would break its one-node-one-process sequencing.

```{seealso}
{doc}`/simulations/tasks`
: The molecular-dynamics ensembles and thermostats each window is built from.

{doc}`/simulations/jobs`
: The queue the windows are dispatched through, and where a failed slice is
  re-run from.

{doc}`/simulations/phonons`
: The harmonic free energy of a *solid*, which needs no integration at all —
  and the per-cell convention this module matches.
```
