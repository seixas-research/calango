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

**On a VASP parent the same menu entry does something structurally
different**: it hands the selection to VASP's own `LPARD` post-process
rather than summing anything itself. Same window, same volumetric output,
different machinery and one real behavioural consequence — see
[VASP: LPARD](#vasp-lpard) below.

:::{warning}
**A baseline is mandatory** — unlike Wannierization, LDOS has no fresh-SCF
fallback on either engine. It works from wavefunctions an existing
calculation already saved, so the wizard refuses to open without a
completed {menuselection}`Simulation --> Single-point Calculation…` run
that wrote them:

- **GPAW** — a `.gpw` written with `mode='all'` (wavefunctions included,
  not just the density). A `.gpw` written with the default mode has no
  per-state coefficients to sum, and the generated script says so if you
  pick one.
- **VASP** — a `WAVECAR`, which means the parent ran with
  `LWAVE = .TRUE.`. That is *not* the default for every workflow Calango
  generates: the {guilabel}`Write:` row of the VASP calculator settings is
  where it is turned on. A parent without one is greyed out of the list
  with the reason, rather than failing inside VASP later.
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
| {guilabel}`Process` | The completed single-point that supplies the states — GPAW's `.gpw` or VASP's `WAVECAR`. Its engine is read back from its own `calculator.json`, and the rest of the wizard follows from that | mandatory |
| Energy window | Two draggable handles over the baseline's own eigenvalue spectrum, or the {guilabel}`Min` / {guilabel}`Max` spin boxes beside it | 0.5 eV below $E_\text{F}$ to $E_\text{F}$ |
| {guilabel}`Relative to Fermi level` | Checked: the two bounds are offsets from $E_\text{F}$, so the run stays correct even if the baseline is re-run with a slightly different Fermi level (relevant on the Orchestration canvas, where the baseline may not exist yet when the window is chosen). Unchecked: absolute Kohn-Sham eigenvalues | checked |
| {guilabel}`Spin channel` | {guilabel}`Sum` (both channels, or the only one), {guilabel}`Spin up`, {guilabel}`Spin down` — the last two only matter for a spin-polarized baseline. **GPAW only**: LPARD has no INCAR tag that selects a channel, so the row is absent on a VASP parent rather than present and ignored | Sum |
| {guilabel}`Preset width` + {guilabel}`Occupied near E_F` / {guilabel}`Unoccupied near E_F` | One-click windows of the chosen width, straddling $E_\text{F}$ on one side or the other | 0.5 eV |

Selecting a baseline reads back its eigenvalues, occupations and k-point
weights and draws them as a weight-binned histogram. On GPAW that is a real
(but fast — no SCF, just a restart) GPAW invocation; on VASP nothing is
executed at all, because the run already wrote everything needed as plain
text: `EIGENVAL` carries the eigenvalues, occupations and k-point weights,
and `DOSCAR`'s sixth line carries $E_\text{F}$ (with `OUTCAR`'s
`E-fermi :` as the fallback). In the histogram: taller bars mean more k-point
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

---

(vasp-lpard)=
## VASP: LPARD

VASP computes the state-resolved density itself, so Calango does not sum
anything on this route — it configures the post-process and converts the
result. `LPARD = .TRUE.` "determines whether partial (band and/or
k-point-decomposed) charge densities are evaluated"; the VASP wiki is
explicit that "an LPARD run is a postprocessing step that requires a
pre-converged calculation" in which "no electronic (or ionic) minimization
is performed, so the calculation is rapid".

The tags the wizard writes, all from the wiki:

| Tag | Value | Why |
|---|---|---|
| `LPARD` | `.TRUE.` | switches the partial-charge pass on |
| `ISTART` | `1` | "orbitals are read from the WAVECAR file" |
| `NBMOD` | `-3` / `-2` | `-3` "add the Fermi energy to the passed values" (the {guilabel}`Relative to Fermi level` case); `-2` "an absolute energy interval" |
| `EINT` | the two bounds, eV | VASP flips them internally if the first is larger, so their order is not load-bearing |
| `IBAND` | an explicit band list | **overrides the window**: with `IBAND` set, "`NBMOD` is set automatically to the number of values passed in `IBAND`" and `EINT` is not consulted — so Calango writes one or the other, never both |
| `KPUSE` | 1-based k-point list | restricts which k-points contribute |
| `LSEPB` / `LSEPK` | `.FALSE.` / `.FALSE.` | one summed `PARCHG`; either `.TRUE.` splits the output into `PARCHG.<band>.<kpt>` |
| `LWAVE` / `LCHARG` | `.FALSE.` | this pass updates neither the orbitals nor the density it read |

`ICHARG` is deliberately **not** set: the wiki's own recipe for this run is
"copy POSCAR, KPOINTS, and WAVECAR to a new directory", and an LPARD pass is
not a fixed-density band pass.

The parent's `KPOINTS` is copied over verbatim rather than regenerated from
the recorded $(n_1, n_2, n_3)$ grid. The grid is a fallback: it cannot
express an explicit k-point list or a line-mode path at all, and for an even
mesh a Monkhorst-Pack and a Γ-centred grid of the same size are *different*
meshes — the orbitals in the WAVECAR belong to one of them.

### The one behavioural difference that matters

:::{important}
**The energy window is not adjustable afterwards.** On the GPAW route every
selected state's $|\psi(\mathbf{r})|^2$ is kept, so a new window is a re-sum
the viewer can do for free. VASP recomputes from the `WAVECAR` for each
window, so **changing the window means running the module again — one job
per window**. The wizard says so on the settings page rather than offering a
slider that silently queues a run.
:::

Two smaller ones:

- **No spin-channel selection.** There is no INCAR tag for "channel 1 only";
  with `ISPIN = 2` LPARD writes the total and the magnetization the way
  `CHGCAR` does. The control is hidden, not disabled.
- **Noncollinear parents are refused.** "LPARD is not supported for
  noncollinear calculations" — a spin-orbit parent gets a refusal naming that,
  read out of the parent's own `INCAR`, rather than a failure inside VASP.

### Output

`PARCHG` (CHGCAR format) becomes `ldos.cube` and registers in the Volumetric
Data dock like the GPAW route's. With `LSEPB`/`LSEPK` on, **every** file is
converted and registered separately, labelled by its band and k-point
indices — a wide window with `LSEPB` on produces a lot of grids, which is
what the checkbox's tooltip warns about.

`ldos.json` carries `engine: "VASP"` and `nstates: null` — VASP does the
selection internally and reports no per-state list, and `null` is what
distinguishes "not reported" from "none selected".

### What this was validated against

`vasp_lpard_ldos` in the test suite runs the real thing against a real VASP
binary (self-skipping without one): a Si parent with `LWAVE = .TRUE.`, then
the generated LPARD script over an absolute window covering every occupied
band. The partial charge density is then the valence pseudo-density, whose
integral over the cell is the valence electron count — exactly $8\,e$ for
two Si atoms with `PAW_PBE Si`. It comes out at $7.99999\,e$.

That check earned its keep immediately: the PARCHG-to-cube conversion
divided the ASE-returned grid by the cell volume, on the strength of a
comment (copied from the Charge Density Difference module) claiming
`VaspChargeDensity` returns values *multiplied* by it. ASE's own
`_read_chg` does `chg /= volume` before returning, so the values were
already in $e/\text{Å}^3$ and the second division was off by the cell
volume — a factor of 40 here. The integral read $0.1999$ instead of $8$.

