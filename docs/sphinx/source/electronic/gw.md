# G₀W₀ quasiparticle corrections

{menuselection}`Electronics --> GW Calculations…` computes one-shot G₀W₀
quasiparticle corrections,

$$
E^{\text{qp}}_{n\mathbf{k}} = \varepsilon^{\text{DFT}}_{n\mathbf{k}}
+ Z_{n\mathbf{k}}\left[\Sigma_{n\mathbf{k}}(\varepsilon^{\text{DFT}})
- V^{xc}_{n\mathbf{k}}\right],
$$

i.e. the DFT eigenvalue with its exchange–correlation potential replaced by
the energy-dependent, non-local self-energy, weighted by the renormalization
factor $Z_{n\mathbf{k}}$. **Nothing here is self-consistent, and the result is
only meaningful relative to the baseline it corrects** — G₀W₀ cannot fix a
qualitatively wrong ground state, it can only perturb the one it is given.

Like Optics and Wannier Functions, this wizard inherits its ground state
rather than converging one, and therefore shows no Calculator Settings stage
({doc}`/electronic/index` explains the design rule). Re-converging inside the
job would give corrections to a *different* SCF solution than the one you
validated — different smearing, a different k-grid, possibly a different
magnetic state — with nothing in the output to say so. Because the baseline
arrives whole, the functional, cutoff and k-grid all come back from it, and
the wizard's baseline note is the one place those values are visible.

---

## Engines

Two engines, each bound to the DFT code that produced its baseline. They are
not interchangeable — G₀W₀ perturbs a *specific* DFT solution, so the engine
follows from which baseline exists rather than from preference.

| Engine | Baseline | What the run does |
|---|---|---|
| {guilabel}`GPAW — native G₀W₀ (from .gpw)` | a completed GPAW single-point `.gpw` | restarts the baseline, adds a generous set of empty bands at fixed density, then applies `gpaw.response.g0w0.G0W0` |
| {guilabel}`Yambo — G₀W₀ (from a Quantum ESPRESSO .save)` | a Quantum ESPRESSO `.save` directory | converts it with `p2y`, generates the G₀W₀ input, executes `yambo` under MPI, and parses the quasiparticle report |

---

## Settings

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Screening cutoff` | Plane-wave cutoff of the screened interaction $W$ — **the convergence parameter that matters**; a G₀W₀ gap is not converged until it stops moving with this | 100 eV |
| {guilabel}`Frequency treatment` | {guilabel}`Plasmon-pole approximation` models the frequency dependence of the screening with a single pole — much cheaper, usually good for the gap of a simple semiconductor; {guilabel}`Full frequency (real axis)` performs the full integration | plasmon-pole |
| {guilabel}`Bands` | Empty-band count entering the self-energy sum | auto (8× occupied) |
| {guilabel}`Bands below/above` | How many occupied and empty states around the gap receive a correction | 4 / 4 |
| {guilabel}`MPI ranks` | Yambo only | 1 |

:::{note}
The screening cutoff and the empty-band count converge *together*: raising one
without the other stalls the gap at a plateau that looks converged and is not.
Run the same system at two or three cutoffs before quoting a number.
:::

---

## What the run does

**GPAW.** The script restarts the baseline `.gpw`, performs a fixed-density
diagonalization to extend the state set with the requested empty bands (the
self-energy is a sum over them — the handful of empty states an SCF leaves
behind is nowhere near enough), then constructs
`gpaw.response.g0w0.G0W0` with the screening cutoff, the frequency treatment,
and the band window around the gap, and evaluates the corrections. The
baseline's density is never re-converged.

**Yambo.** The script runs `p2y` inside the `.save` directory to build the
Yambo `SAVE/` database, initializes it, generates the G₀W₀ input with
`yambo -g n -p p` (plasmon-pole) or the real-axis variant, executes the run —
under `mpirun -np N` when more than one rank is requested — and parses the
`.qp` quasiparticle report. Each external step is logged with the exact
command line, so a failure names the stage it died in.

Both engines end at the same place: a shared `gw.json` with, per corrected
state, the DFT eigenvalue, the quasiparticle energy and the correction — which
is what makes a single viewer possible.

---

## The viewer

Both engines write their quasiparticle report into one shared result schema,
so a single {guilabel}`GW Viewer` — also reachable from
{menuselection}`Results --> GW Viewer…` — serves either. It reports the DFT
and quasiparticle band edges, the **gap renormalization as its headline
number**, and a per-state table of $\varepsilon_{\text{DFT}}$,
$E_{\text{qp}}$ and the correction between them.

% TODO screenshot: GW viewer showing DFT vs QP gap and the per-state correction table
```{figure} /_static/img/elec_gw_viewer.png
:alt: GW viewer with DFT gap, quasiparticle gap, and per-state corrections
:width: 92%
:figclass: screenshot

The GW viewer: the gap renormalization up top, and the per-state corrections
that tell you whether to believe it.
```

:::{warning}
G₀W₀ essentially always *opens* a semiconductor gap, typically by 0.5–2 eV. A
gap renormalization that is **negative or near zero** — or a renormalization
factor $Z$ near zero — is far more likely an unconverged screening cutoff or
too few empty bands than a physical result, and the viewer says so rather than
presenting the number neutrally. The per-state corrections are the other thing
to read: they should vary smoothly across bands, not jump erratically between
neighbouring states.
:::

---

## Reading the numbers

A practical checklist for the per-state table:

- **The gap should open.** For an sp semiconductor expect the QP gap above
  the DFT one by 0.5–2 eV; a correction in the other direction is a
  convergence symptom until proven otherwise.
- **Corrections should be smooth in band index.** Occupied states typically
  shift down and empty states up by slowly varying amounts; a single state
  jumping against its neighbours usually means too few empty bands in the
  self-energy sum.
- **Quote the correction, not the absolute energies.** G₀W₀ energies are
  referenced to the baseline's own zero; only differences — gaps,
  band-edge shifts — transfer between calculations.

---

## Limitations

- **One-shot only.** There is no self-consistency in $G$ or $W$; the known
  starting-point dependence of G₀W₀ is inherited in full from the baseline's
  functional.
- **Corrections, not a band structure.** The run corrects a window of states
  around the gap ({guilabel}`Bands below/above`), not every band along a
  path — it cannot produce a full quasiparticle dispersion by itself. For an
  interpolated band structure on top of a corrected calculation, see
  {doc}`/electronic/wannier`.
- **No optical spectra.** G₀W₀ moves eigenvalues; it does not include the
  electron–hole interaction, so it will not reproduce excitonic absorption.
  The independent-particle spectra live in {doc}`/electronic/optics` and
  {doc}`/electronic/nonlinear_optics`.
- **Insulators and semiconductors are the home ground.** The plasmon-pole
  model in particular is built for a screening dominated by one plasmon;
  metals want the real-axis treatment and a far denser k-grid in the
  baseline — which, being inherited, has to be decided when the baseline is
  run.

A pragmatic alternative for gap correction: the scissors shift in
{doc}`/electronic/nonlinear_optics` applies a rigid opening at a fraction of
the cost — no self-energy, no screening — and a converged G₀W₀ gap is
precisely the number to feed it.
