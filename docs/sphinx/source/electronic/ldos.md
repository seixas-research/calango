# Local Density of States (LDOS)

{menuselection}`Electronics --> Local Density of States (LDOS)…` sums

$$
\mathrm{LDOS}(\mathbf{r}) = \sum_{n,\mathbf{k},s \,\in\, [E_{\min},E_{\max}]}
w_{\mathbf{k}} \, |\psi_{n\mathbf{k}s}(\mathbf{r})|^2
$$

over every Kohn-Sham state a completed GPAW single-point already computed,
weighted only by k-point weight — **not** by occupation, so an "unoccupied
near $E_\text{F}$" window is a real, nonzero field rather than being zeroed
out by construction. The result is a real-space grid through the same
pipeline as every other volumetric field ({doc}`/analysis/volumetric`):
isosurfaces, slices, the isovalue histogram, HDF5 compression.

:::{warning}
**GPAW only**, and a **baseline is mandatory** — unlike Wannierization, LDOS
has no fresh-SCF fallback. It sums wavefunctions an existing calculation
already saved, so the wizard refuses to open without a completed
{menuselection}`Simulation --> Single-point Calculation…` run whose `.gpw`
was written with `mode='all'` (wavefunctions included, not just the
density). A `.gpw` written with the default mode has no per-state
coefficients to sum, and the generated script says so if you pick one.
:::

:::{note}
Unlike Wannierization, LDOS carries **no "Symmetry: off" pre-condition**. It
sums over whatever k-points the baseline actually stored, weighted by
GPAW's own k-point weights — which already account for however much the
mesh was folded by symmetry — so a symmetry-reduced baseline is exactly as
usable as a full-zone one.
:::

---

## Setting up the calculation

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Process` | The completed GPAW single-point whose `.gpw` supplies the states | mandatory |
| Energy window | Two draggable handles over the baseline's own eigenvalue spectrum, or the {guilabel}`Min` / {guilabel}`Max` spin boxes beside it | 0.5 eV below $E_\text{F}$ to $E_\text{F}$ |
| {guilabel}`Relative to Fermi level` | Checked: the two bounds are offsets from $E_\text{F}$, so the run stays correct even if the baseline is re-run with a slightly different Fermi level (relevant on the Orchestration canvas, where the baseline may not exist yet when the window is chosen). Unchecked: absolute Kohn-Sham eigenvalues | checked |
| {guilabel}`Spin channel` | {guilabel}`Sum` (both channels, or the only one), {guilabel}`Spin up`, {guilabel}`Spin down` — the last two only matter for a spin-polarized baseline | Sum |
| {guilabel}`Preset width` + {guilabel}`Occupied near E_F` / {guilabel}`Unoccupied near E_F` | One-click windows of the chosen width, straddling $E_\text{F}$ on one side or the other | 0.5 eV |

Selecting a baseline reads back its eigenvalues, occupations and k-point
weights with a real (but fast — no SCF, just a restart) GPAW invocation, and
draws them as a weight-binned histogram: taller bars mean more k-point
weight lands at that energy, exactly like a real density of states. Drag
either edge of the shaded band to move that bound; the dashed line marks
$E_\text{F}$.

% TODO screenshot: LdosWizard settings page with the energy-window widget, a shaded band over the histogram, and both handles visible
```{figure} /_static/img/elec_ldos_wizard.png
:alt: LDOS wizard with the draggable energy-window widget
:width: 92%
:figclass: screenshot

The energy-window widget: a k-weight-binned histogram of the baseline's
eigenvalues, with the selected window shaded and the Fermi level dashed.
```

If the peek fails — a baseline whose `.gpw` has no saved wavefunctions, or
an interpreter without GPAW — the wizard reports why and falls back to the
plain spin boxes; the run itself still re-checks everything (see below).

---

## Output

The job writes `ldos.cube` and `ldos.json` (the window actually used, in
both eV conventions, plus the full list of states it summed — spin, k-point
index, band index, eigenvalue, weight) and registers the cube in the
{doc}`/analysis/volumetric` dock **unchecked** — the same "add it, let the
user pick" default Wannier orbitals use, since a wide window on a large
system can be one field among several a user wants to compare rather than
render immediately.

Recomputing with a different window means re-running the wizard (there is
no in-place "widen the window" action on a finished job): the sum only
touches the states inside the requested bounds, so a narrow window is cheap
regardless of system size, and the restart itself — not a fresh SCF — is
what makes even a wide window fast.

---

## What the generated script actually does

Like every GPAW-restarting module here, the failure modes are checked twice:
the wizard's own baseline peek is advisory, and the **generated script
re-verifies unconditionally** at run time, because it may execute long after
(or on a different machine from) the wizard session that built it. Reading
`calculator.json`, a non-GPAW baseline is refused by name in milliseconds,
before the `.gpw` glob even runs; a directory with no `.gpw` at all raises a
`RuntimeError` naming the fix. This restart-and-check block
(`AseScriptGenerator::gpawRestartFromBaselineScript`) and the wavefunction
accessor beneath it (`AseScriptGenerator::gpawWaveFunctionHelperScript`,
`calc.get_pseudo_wave_function(..., periodic=True)`) are the SAME two
building blocks {doc}`/electronic/wavefunctions` is built on — one
implementation of "restart a GPAW baseline and read back a state", not two.

If no stored state falls inside the chosen window the script raises rather
than silently writing an all-zero cube.
