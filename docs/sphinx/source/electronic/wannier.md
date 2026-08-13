# Wannier functions

Two menu entries share one machinery.
{menuselection}`Wannier Functions --> Calculate Wannier Functions…` runs the
Marzari–Vanderbilt localization (`ase.dft.wannier`) on top of a completed
single-point calculation, producing maximally localized Wannier functions —
their centres, spreads, and real-space orbitals.
{menuselection}`Wannier Functions --> Wannier Interpolation…` then rebuilds that
localization and uses the short-ranged Wannier Hamiltonian $H(\mathbf{R})$ to
interpolate bands and a projected DOS anywhere in the zone at the cost of one
small diagonalization per k-point. The Fermi-surface and topology modules
({doc}`/electronic/fermi_topology`) are built on the same foundation.

:::{warning}
`ase.dft.wannier` needs the **full, unsymmetrized Brillouin zone**. A `.gpw`
written by a symmetry-reduced run carries only the irreducible wedge, and the
failure would surface deep inside ASE as an index error — so Calango compares
the IBZ and BZ k-point counts up front and stops with the remedy: re-run the
baseline single-point with {guilabel}`Symmetry: off`, or let the wizard run
its own SCF, which forces it.
:::

---

## The localization wizard

The baseline combo lists completed Single-Point calculations that saved the
Bloch wavefunctions (`.gpw` with `mode='all'`); the calculator, its
parameters, and the Conda environment are inherited from the selected run, and
the wizard warns when that baseline ran *with* symmetry. Picking
{guilabel}`(none — run a fresh SCF with symmetry off)` converges a ground
state first and writes `wannier.gpw`.

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Initial trial projections` | Seeds the localization: {guilabel}`Automatic (orbitals)`, {guilabel}`Bloch`, {guilabel}`Random`; the atomic sets (s, p, d, sp3, dxy) fall back to ASE's `orbitals` initializer | orbitals |
| {guilabel}`Wannier functions` | How many to localize — typically the number of occupied bands / valence orbitals | 4 |
| {guilabel}`Fixed states` | Which states are frozen (see below) | exactly the Wannier count |
| {guilabel}`Max. minimization iterations` | Cap on `localize()` sweeps (`step=0.25`, tolerance 10⁻⁶); the run exits early once the spread functional Ω stops decreasing | 50 |

### The fixed manifold: one selector, not two toggles

ASE's `Wannier` takes `fixedenergy` **or** `fixedstates` and raises
`RuntimeError('You can not set both …')` when handed both — which is why
Calango presents one three-way choice rather than two independent switches:

- {guilabel}`Exactly the Wannier count (no extra freedom)` — neither argument
  is passed; ASE fixes exactly `nwannier` states per k-point. No
  disentanglement.
- {guilabel}`Every state below an energy window` — ASE's `fixedenergy`. The
  fixed count follows the band structure and may differ between k-points.
  **The reference level is not unconditionally $E_\text{F}$**: ASE's
  `choose_states()` measures the cutoff from the **conduction band minimum**
  whenever the system has a gap (> 0.01 eV) and the value is ≥ 0.01 eV, and
  from the Fermi level otherwise. On silicon, 2.0 eV means 2 eV above the CBM,
  not 2 eV above $E_\text{F}$. That is ASE's rule, not a Calango convention.
- {guilabel}`An explicit number of bands` — ASE's `fixedstates`, the same
  count at every k-point. It must not exceed the Wannier count: ASE derives
  the extra degrees of freedom as `edf_k = nwannier − fixedstates_k` and never
  checks the sign, dying inside the rotation setup with a shape error naming
  neither number — so the generated script wraps the construction and reports
  which two numbers disagree, and by how much to raise the Wannier count.

Only the row belonging to the selected mode is shown; the other is not merely
inapplicable, it is a value ASE would refuse alongside.

### Outputs

The run writes `wannier.json` — centres (Å), per-orbital spreads (Å², via
`get_spreads()` with `get_radii()²` as a version fallback), total spread Ω,
the minimized functional value, the Wannier count, the projection seed, and
**the absolute path of the `.gpw` it used** — plus one `wannier_<n>.cube` per
orbital. Recording the `.gpw` path matters: a run started from a baseline
reads wavefunctions from *another job's* directory and writes none of its own,
and every downstream module resolves the wavefunctions through this record.

### The viewer

The Wannier Functions window lists every orbital with its centre and spread
and the total Ω, and renders the selected `wannier_<n>.cube` as a real-space
isosurface. Wannier amplitudes are signed, so the isovalue slider spans
$[0, \max|\psi|]$ of the loaded cube (opening at 20 % — a low amplitude
reveals the lobes); colormaps are Viridis, Plasma, Coolwarm, Rainbow, and a
slice plane (XY/XZ/YZ with an offset slider) cuts through the volume.

% TODO screenshot: Wannier dialog with the centres/spreads table and an sp3-like orbital isosurface
```{figure} /_static/img/elec_wannier.png
:alt: Wannier functions viewer with centres table and orbital isosurface
:width: 92%
:figclass: screenshot

Centres and spreads on the right, the selected Wannier orbital's signed
isosurface in the viewport.
```

---

## Wannier interpolation

{menuselection}`Wannier Functions --> Wannier Interpolation…` starts from a
**completed** Wannier Functions process. The Wannier count and trial
projections are read back from that run's `wannier.json` — the interpolation
rebuilds the *same* localization from the *same* wavefunctions, so neither is
a setting that could be overridden. Interpolating off a different Wannier
gauge would describe a different (though equally valid) set of bands.

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Band structure — k-path` | Embedded k-path editor, plus {guilabel}`Path samples` | 200 |
| {guilabel}`PDOS — k-mesh` | Monkhorst–Pack grid for the Wannier-projected DOS | 8 × 8 × 8 |
| {guilabel}`Broadening` | Gaussian width of the PDOS | 0.1 eV |
| {guilabel}`Freeze states below an inner window` | ASE's `fixedenergy` — the frozen manifold, reproduced exactly | off (0.0 eV) |
| {guilabel}`Restrict the pool to an outer window` | Converted into ASE's `nbands` — the Bloch states the manifold may be drawn from at all | off (5.0 eV) |

Both disentanglement windows default to **unchecked**: off, every band the
calculator holds is available and nothing is frozen beyond the Wannier count.
The inner window follows the same CBM-referenced `fixedenergy` rule as the
localization wizard. The outer window is resolved at run time: the script
counts, at each k-point, the bands below $E_\text{F} + \text{cutoff}$ and
takes the **maximum** over k-points — `nbands` is a single number for the
whole calculation, and truncating to the smallest k-point's count would
silently drop states inside the window everywhere else. The result is clamped
to at least `nwannier` and at most what the calculator holds, and logged.
Narrowing the pool is what keeps high-lying free-electron-like states out of a
manifold meant to describe a few valence bands.

The job diagonalizes $H(\mathbf{k}) = \sum_\mathbf{R} e^{i\mathbf{k}\cdot\mathbf{R}} H(\mathbf{R})$
along the path and on the mesh, writing `bands.json` (the same schema the
direct band-structure run uses) and `pdos.json`, where the squared eigenvector
amplitudes project the DOS onto each individual Wannier function.

---

## Interpolated versus direct bands

An interpolated band structure is not a cheaper copy of
{doc}`/electronic/bands` — it is a different object with different failure
modes:

- **It covers only the Wannier manifold.** With 4 Wannier functions you get 4
  bands; everything above the manifold simply is not there.
- **It is only as good as the localization.** A poorly localized
  $H(\mathbf{R})$ is long-ranged, and its interpolated bands ring between the
  computed k-points. Check the total spread and the functional value before
  trusting fine features.
- **It is arbitrarily dense for free.** Between the baseline's k-points the
  direct calculation knows nothing; the Wannier Hamiltonian interpolates
  smoothly, which is exactly what a Fermi surface or a dense DOS needs.

:::{note}
The interpolation, Fermi-surface and topology scripts all resolve the
wavefunctions the same way: the path recorded in `wannier.json` first, then
any `.gpw` in the MLWF directory. An MLWF run that predates the recorded path
raises a clear error asking to re-run the localization.
:::
