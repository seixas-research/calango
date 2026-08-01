# Nonlinear optics

{menuselection}`Electronics --> Nonlinear Optics…` evaluates the
*second-order* optical response with GPAW's `gpaw.nlopt` module, within the
independent-particle approximation. Three quantities are offered:

| Response | Quantity | Units |
|---|---|---|
| Second-harmonic generation | $\chi^{(2)}(-2\omega;\, \omega, \omega)$ — two photons of energy $\hbar\omega$ in, one of $2\hbar\omega$ out | pm/V (sheet: $\chi^{(2)} L$ in nm²/V) |
| Shift current | $\sigma^{(2)}(0;\, \omega, -\omega)$ — the DC photocurrent a homogeneous illuminated crystal carries with no junction and no built-in field: the bulk photovoltaic effect | A/V² |
| Linear susceptibility | $\chi^{(1)}(\omega)$, the full 3×3 tensor from the same matrix elements, and $\varepsilon = 1 + \chi^{(1)}$ | dimensionless |

The linear tensor is the independent-particle $\chi^{(1)}$ from `gpaw.nlopt`,
not the local-field-corrected $\varepsilon(\omega)$ the
{doc}`/electronic/optics` module computes — two different approximations to
the same quantity.

:::{warning}
$\chi^{(2)}$ and the shift current are **odd-rank tensors: in a
centrosymmetric crystal every component vanishes identically**, by symmetry
rather than by smallness. GPAW will still return numbers — a finite k-mesh
returns the residue of an incomplete cancellation, and it looks exactly like a
spectrum. The generated script therefore tests the cell for an inversion
centre *before* converging anything and warns, and the viewer repeats the
warning above the plot. This is the single most common way the module is
misused.
:::

---

## Why this wizard converges its own ground state

Unlike every other response workflow ({doc}`/electronic/index`), Nonlinear
Optics does **not** inherit a Single-Point baseline. Three requirements make
an ordinary baseline unusable, and all three are imposed by the script
generator rather than left as settings, because they are requirements of the
method and not preferences:

- `make_nlodata` **asserts that point-group symmetry is off.** The matrix
  elements it builds are not invariant under the point-group folding of the
  k-set, and the assertion arrives as a bare `AssertionError` with no
  message — after the SCF has already been paid for. The generated calculator
  passes `symmetry={'point_group': False, 'time_reversal': True}`.
- The sums run over *intermediate* states, so the band set has to be large.
  The ground state uses `nbands="nao"` — every band the basis can hold.
- Those empty bands must be **converged**. An SCF converges occupied states;
  the unoccupied manifold it leaves behind is noise, and here it is summed
  over. The calculator asks for `convergence={"bands": -10}`.

Time reversal is deliberately *kept*: the weaker `symmetry="off"` would
satisfy the assertion too, at roughly double the k-points, for nothing. The
script also sets `parallel={'domain': 1}`, because `make_nlodata` gathers the
wavefunctions to one rank.

---

## Settings

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Tensor components` | Three letters from `xyz`, several separated by commas (`yyy, xxy`) | `yyy` |
| {guilabel}`SHG gauge` | {guilabel}`Length gauge (lg)` or {guilabel}`Velocity gauge (vg)` | length |
| {guilabel}`Broadening η` | Lorentzian broadening | 0.05 eV |
| {guilabel}`Energy window min/max` | Photon-energy range | 0–6 eV |
| {guilabel}`Number of points` | Samples across the window; the band sum runs at every one | 500 |
| {guilabel}`Scissors shift` | Rigid shift of the empty bands (GPAW's `eshift`) | 0 eV |
| {guilabel}`Band window` | First/last band in the matrix elements (GPAW's `ni`/`nf`; 0 = last, negative counts from the top) | all |
| {guilabel}`Vacuum axis` | Which cell axis carries the vacuum, for a monolayer; seeded from the geometry but never decided by it | none (bulk) |

A few of these deserve more than a row:

- **Components.** There is deliberately no "all 27": most vanish by symmetry,
  each costs a full sum over bands and k-points, and 27 spectra of which 24
  are numerical noise is not a better answer. Invalid entries are named and
  dropped at the wizard, not discovered as a `ValueError` mid-job; with no
  valid component the run falls back to `yyy`.
- **Gauge.** The two are formally equivalent and numerically are not: the
  velocity gauge carries low-frequency divergences that cancel only for a
  complete band set, so a truncated sum leaves it visibly wrong as
  $\omega \to 0$. **Running both and overlaying them is the standard
  convergence test** — their disagreement says more about the band summation
  than any single number does.
- **Broadening.** A second-order spectrum is far more sensitive to η than a
  linear one: the resonances are sharper and the divergences at $\omega$ and
  $2\omega$ are regularized by exactly this number, so a small η on a coarse
  k-mesh produces spikes rather than structure.
- **Scissors.** $\chi^{(2)}$ is more sensitive to the gap than the linear
  response, because the two-photon resonance sits at *half* of it. The applied
  value is recorded in the results and restated by the viewer, so a spectrum
  never silently carries one.
- **Vacuum axis.** A supercell $\chi^{(2)}$ is diluted by whatever vacuum was
  used — double the vacuum and the number halves. Multiplying the thickness
  back in gives the sheet susceptibility $\chi^{(2)} L$ in nm²/V, which is
  what the 2D literature quotes. Bulk numbers are reported either way.

:::{tip}
SHG probes the band structure at $2\omega$ as well as at $\omega$: a window
ending at 6 eV is sampling transitions up to 12 eV, and the band count has to
reach them. This is also why the $\chi^{(1)}$ panel is worth computing — the
$\chi^{(2)}$ features of a semiconductor sit at the absorption edge *and* at
half of it, and a resonance assigned to the wrong one of those is the standard
way to misread an SHG spectrum.
:::

---

## What the run produces

The expensive step is `make_nlodata` — the momentum matrix elements — and it
runs **once**, saved as `mml.npz` beside the run so a later job can reload it
with `NLOData.load()` instead of rebuilding. Every component and every
response reuses the same data: asking for a second tensor component costs a
band sum, not another ground state.

The pipeline is: ground state (`gs.gpw`, written with `mode='all'`) →
`make_nlodata` → one band sum per requested component. Each spectrum is also
saved in GPAW's own layout (`shg_yyy_lg.npy`, `shift_yyy.npy`,
`chi_linear.npy`), and everything lands in `nlopt.json`: the energy grid, η,
gauge, scissors value, component list, the centrosymmetry verdict, and per
component the real/imaginary/absolute $\chi^{(2)}$ in pm/V (plus sheet values
when a vacuum axis was set), $\sigma^{(2)}$ in A/V², and the full $\chi^{(1)}$
with the derived $\varepsilon_1$/$\varepsilon_2$.

Everything GPAW returns is in SI base units (m/V, A/V²); the conversions to
pm/V (×10¹²) and nm²/V are applied in the script's pure post-processing
functions, which `tests/nlopt_parser_test.py` extracts by AST and pins with
arrays of exactly the shape GPAW returns — a factor of 10⁶ in either direction
produces a plot that looks entirely reasonable.

% TODO screenshot: Nonlinear Optics results window, SHG spectrum with the centrosymmetry warning banner visible
```{figure} /_static/img/elec_nlo_viewer.png
:alt: SHG spectrum in the nonlinear optics viewer with warning banner
:width: 92%
:figclass: screenshot

An SHG spectrum with the scissors value restated and — for a centrosymmetric
cell — the warning that what is plotted is cancellation residue, not physics.
```

---

## Limitations

- **GPAW only.** `gpaw.nlopt` has no counterpart in the other engines; a
  non-GPAW selection produces a script that refuses immediately instead of
  failing after the SCF.
- **Independent particles.** No local-field corrections and no excitons; near
  a strongly bound exciton the peak positions and heights are qualitative.
- **No result in centrosymmetric crystals** — by physics, not by
  implementation. Break the symmetry (a surface, a strain, a different
  polymorph) or compute the linear response instead.
