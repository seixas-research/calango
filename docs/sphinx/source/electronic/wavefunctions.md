# Wavefunctions

{menuselection}`Electronics --> Wavefunctions…` computes individual
Kohn-Sham orbitals on a real-space grid — pseudo-wavefunctions, or the
all-electron PAW reconstruction — as volumetric data, one cube per selected
state, in a single pass.

:::{warning}
**GPAW only, and a baseline is mandatory** — the same shape as
{doc}`/electronic/ldos`, which this module shares its restart and
wavefunction-access layer with (`AseScriptGenerator::
gpawRestartFromBaselineScript` / `gpawWaveFunctionHelperScript` — ONE
implementation of "restart a GPAW baseline and read back a state" behind
both modules, not two). A `.gpw` must have been written with `mode='all'`.
:::

---

## Selecting states

Selecting a baseline reads back every stored state (`gui::
peekGpawEigenvalues` — the same fast, SCF-free restart LDOS and
{doc}`/electronic/energy_diagrams` use) into a table: band index, k-point,
spin channel, energy, and occupation. Tick as many rows as you want — each
becomes one cube, written in the same job, so exporting a dozen orbitals
costs one restart rather than a dozen.

% TODO screenshot: WavefunctionsWizard state table with several rows ticked, pseudo/all-electron toggle and quantity combo visible
```{figure} /_static/img/elec_wavefunctions_wizard.png
:alt: Wavefunctions wizard state-selection table
:width: 92%
:figclass: screenshot

The state table: one row per stored (band, k-point, spin), several ticked
for a batch export.
```

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Process` | The completed GPAW single-point whose `.gpw` supplies the states | mandatory |
| {guilabel}`Quantity` | {guilabel}`\|psi\|^2 (density)`, {guilabel}`Real part`, {guilabel}`Imaginary part` — see below | Density |
| {guilabel}`All-electron (PAW reconstruction)` | Pseudo (off) vs all-electron (on) | off |
| {guilabel}`All-electron grid spacing` | Real-space grid for the reconstruction only; the pseudo path reuses the SCF's own grid | 0.05 Å |

---

## Pseudo vs all-electron

**Pseudo** (default) is the smooth pseudo-wavefunction the plane-wave/grid
basis actually represents —
`calc.get_pseudo_wave_function(periodic=True)`, the same call
{doc}`/electronic/bands`'s band-symmetry classification already uses.

**All-electron** reconstructs the true, cusped orbital near each nucleus via
the PAW correction —
`calc.dft.ibzwfs.get_all_electron_wave_function(band, kpt, spin,
grid_spacing)` — and needs the **new** GPAW engine's internal state; a
restart under the legacy engine (`GPAW_NEW=0`, which nothing but
{doc}`/electronic/xas` ever sets) raises a specific error rather than
failing on a missing attribute several frames down.

By construction, the PAW correction is exactly zero **outside** each atom's
augmentation sphere: pseudo and all-electron agree away from every nucleus
and differ only close to one. `wavefunctions_h2o` (the test this page's
physics claims are checked against) verifies this directly on water's HOMO:
the density difference between the two reconstructions, integrated near the
nuclei, comes out more than 10× the same quantity integrated away from
every atom.

:::{note}
The two reconstructions run on **independent grids** — the all-electron
path's own `grid_spacing`, unrelated to the SCF's native one — so their
cubes are not necessarily the same shape. Comparing them (in the viewer, or
programmatically) means comparing region-integrated quantities, not a
voxel-by-voxel difference.
:::

---

## Density, real, imaginary — and why there is no separate "signed" option

- **|psi|² (density)** is always real and non-negative.
- **Real part** IS the signed, two-lobe orbital picture for a real (Gamma-
  point or molecular) state — {doc}`/analysis/volumetric`'s isosurface
  extraction already gives **any** field with negative values the
  positive/negative dual-isosurface treatment (`VolumetricPanel`'s own
  signed-field detection, the same mechanism Wannier orbitals use), keyed
  off the field's own minimum value rather than a per-field flag. There is
  therefore no separate "signed psi" quantity to select — Real already is
  that, whenever the orbital is real.
- **Imaginary part** is identically zero for a real orbital and offered
  "for completeness" — a genuinely complex Bloch state at $\mathbf{k} \neq
  \Gamma$ has a nonzero one, and the option exists uniformly rather than
  only appearing for periodic parents.

---

## Output

Each state writes one cube named `psi_n<band>_k<kpt>_spin-<up|down>_
<quantity>.cube` (e.g. `psi_n12_k0_spin-up_density.cube`) plus
`wavefunctions.json` (one entry per state: the file, its spin/k-point/band,
energy, occupation, whether the parent is periodic, and whether the
underlying wavefunction is complex-valued). Every cube registers in the
{doc}`/analysis/volumetric` dock **unchecked**, the same "add it, let the
user pick" default LDOS and Wannier orbitals use.

For a **periodic** parent, a Bloch orbital's cube is given the same
periodic-continuation treatment Wannier orbitals get (their isosurface is
continued across the cell boundary instead of being cut at the faces,
`DatasetOrigin::wannier`) — appropriate for a state genuinely extended
across periodic images. A **molecular** parent's orbital already sits
entirely inside its vacuum padding, where continuation would be
meaningless, and is left off.

For an energy-window SUM of many states rather than one orbital at a time,
see {doc}`/electronic/ldos`, which this module's wavefunction-access layer
is shared with; for the discrete level structure these states belong to,
see {doc}`/electronic/energy_diagrams`.
