# Optical properties

{menuselection}`Electronics --> Optics…` computes the frequency-dependent
dielectric function $\varepsilon(\omega)$ and every spectrum derived from it —
absorption, reflectivity, refractive index and the energy-loss function. The
2D variant, {menuselection}`Modules --> 2D Materials --> 2D Optics…`, adds the
sheet observables of a monolayer: absorbance, sheet conductivity and sheet
polarizability.

The wizard is a two-stage dialog — **Optical Response Settings**, then the
editable script review — because it inherits its ground state instead of
configuring one: the response is evaluated at the **fixed density of a
completed Single-Point Calculation**. Its SCF is never re-run. Re-converging
here would produce a spectrum from a different ground state than the one you
validated.

% TODO screenshot: Optics wizard stage 1 with the GPAW baseline picker and response form
```{figure} /_static/img/elec_optics_wizard.png
:alt: The Optical Response Settings stage with baseline selection and response parameters
:width: 92%
:figclass: screenshot

Stage 1 — the inherited baseline and the response parameters.
```

---

## Two engines

| Engine | Ground state | Method |
|---|---|---|
| **GPAW** (default) | inherits a `.gpw` baseline | `gpaw.response.df.DielectricFunction` at fixed density |
| **VASP** | self-contained | SCF, then an exact-diagonalization restart (`ICHARG=11`) with `LOPTICS=.TRUE.` |

The GPAW entry is disabled — with a tooltip saying to run one first — when no
completed GPAW single point with saved wavefunctions exists. For VASP the
wizard shows its own small ground-state group ({guilabel}`ENCUT` 500 eV,
7×7×7 SCF k-grid, XC from PBE/PBEsol/LDA/SCAN/HSE06/PBE0); POTCARs come from
{menuselection}`Edit --> Preferences --> External Files`.

:::{note}
When the baseline's `calculator.json` records the interpreter it ran under,
the optics job binds to **that** Python environment — the response is
evaluated by the same GPAW build that produced the density.
:::

---

## Response settings

| Control | Default | Meaning |
|---|---|---|
| {guilabel}`Broadening η` | 0.1 eV | Lorentzian broadening of $\varepsilon(\omega)$ |
| {guilabel}`Energy window` | 0 – 20 eV | photon-energy range |
| {guilabel}`Response k-mesh` | auto, auto, auto | per-axis Monkhorst–Pack divisions for the response step; *auto* keeps the baseline's value on that axis |
| {guilabel}`Include IBZ points` | off | reduce to the irreducible zone with symmetry weights |
| {guilabel}`Tetrahedron integration` | off | linear tetrahedron BZ integration instead of point sampling |
| {guilabel}`Number of points` | 500 | frequency samples, spread linearly over the window |
| {guilabel}`Additional empty bands` | 200 % | empty states as a percentage of the occupied count (floor of 12) |
| {guilabel}`Polarization directions` | xx, yy, zz | tensor components to evaluate |

The dielectric function is a Brillouin-zone integral over interband
transitions and converges **far more slowly with k-points than the total
energy does** — the grid that converged the SCF is routinely too coarse for
the spectrum. The per-axis *auto* is what lets a 2D sheet take a dense
in-plane mesh (24, 24, auto) while the vacuum axis stays where the ground
state put it.

Reducing to the irreducible zone is close to free accuracy-wise: on bulk Si
at 6×6×6 it evaluates 28 irreducible points instead of 216, with
$\varepsilon_2$ agreeing to 0.6 %.

:::{admonition} Tetrahedron integration has a hard grid requirement
:class: caution

The tetrahedron method resolves van Hove features that Lorentzian broadening
smears — but it requires the **baseline's** k-grid to contain every vertex of
the irreducible Brillouin zone
(`gpaw.bztools.find_high_symmetry_monkhorst_pack`). An ordinary
Monkhorst–Pack grid usually does not, and the run **stops with that message**
rather than quietly switching back to point integration. Either re-run the
single-point baseline on such a grid, or turn the checkbox off.
:::

---

## What the run produces

The generated script recovers the baseline (`GPAW(r"….gpw")`), re-diagonalizes
at fixed density with the enlarged empty-band count
(`fixed_density(nbands=…, kpts=…)` → `gs_nscf.gpw`), evaluates
`DielectricFunction` on the explicit frequency grid, and derives, per
direction:

$$
N(\omega) = \sqrt{\varepsilon(\omega)} = n + ik, \qquad
\alpha(\omega) = \frac{2\omega}{\hbar c}\,k \ \ [\mathrm{cm}^{-1}], \qquad
R = \frac{(n-1)^2 + k^2}{(n+1)^2 + k^2}, \qquad
L = -\,\mathrm{Im}\frac{1}{\varepsilon}.
$$

Everything lands in one `optics.json`: the energy grid, per-direction blocks
(`eps1`, `eps2`, `absorption`, `reflectivity`, `n`, `k`, `loss`), the
integration mode actually used, and a `sampling` record of the k-meshes. A
direction that fails to evaluate is logged and skipped rather than sinking
the run; only when *no* direction survives does the script raise.

---

## 2D optics: properties of the sheet, not the padding

A supercell dielectric function is diluted by whatever vacuum was used —
double the vacuum and $\varepsilon_{3D}$ moves, so it is not a property of
the sheet. The 2D workflow divides that thickness back out. The
{guilabel}`Vacuum axis` is seeded from the cell (the axis whose atoms leave
the most empty space) but should be confirmed: getting it wrong rescales
every 2D quantity by the wrong length, *silently*.

With $L_z$ the vacuum-direction cell length and $k = \omega/\hbar c$:

$$
\alpha_{2D}(\omega) = \frac{L_z}{4\pi}\,\bigl(\varepsilon(\omega) - 1\bigr),
\qquad
A(\omega) = k\,L_z\,\varepsilon_2(\omega),
\qquad
\sigma_{2D}(\omega) = -\,i\,\omega\,\alpha_{2D}(\omega)\ \ [e^2/h].
$$

The $-1$ in $\alpha_{2D}$ removes the vacuum's own contribution — that is
what makes the result a property of the **sheet**. All three observables are
invariant under a change of vacuum thickness; $\varepsilon_{3D}$ itself is
not, and the test suite checks both directions of that statement.

:::{important}
**Validation.** For graphene the absorbance returns the universal
$A = \pi\alpha = 2.29\,\%$ and the sheet conductivity
$\mathrm{Re}\,\sigma_{2D} = \pi/2$ in units of $e^2/h$. The 2D formulas are
pinned to machine precision by `optics_2d_test.py` (which extracts
`twod_observables` from a freshly generated script by AST, so the code under
test is the code that ships), and end-to-end by a real GPAW tetrahedron run
on graphene that lands within 20 % of $\pi\alpha$ at a 36×36 mesh.
:::

---

## The viewer

The results window opens automatically when `optics.json` appears in a
finished job directory ({doc}`/simulations/jobs`).

% TODO screenshot: Optics viewer with the dielectric function of silicon and the quantity/direction/axis combos
```{figure} /_static/img/elec_optics_viewer.png
:alt: The optics viewer plotting eps1 and eps2 with quantity, direction and x-axis selectors
:width: 92%
:figclass: screenshot

The viewer — quantity, direction and x-axis unit; the vertical scale follows
the visible window.
```

- {guilabel}`Quantity` — dielectric function ($\varepsilon_1$ &
  $\varepsilon_2$), absorption, reflectivity, refractive index ($n$ & $k$),
  energy loss; a 2D job appends absorbance, polarizability and conductivity
  and **opens on the absorbance** — the quantity the user came for.
- {guilabel}`Direction` — only the tensor components actually present in the
  file.
- {guilabel}`X axis` — photon energy (eV) or wavelength (nm), converted with
  $\lambda = hc/E$, $hc = 1239.84197$ eV·nm. Samples at $E \le 0$ have no
  finite wavelength and are omitted from the wavelength view.
- {guilabel}`Range` — display window; the y scale re-fits to what is visible
  rather than to an off-screen peak.
- {guilabel}`Export CSV…` / {guilabel}`Export Image…` — a tidy per-direction
  table (always on the energy grid; 2D columns appended only for a sheet
  job), or a 3× print-resolution raster. {guilabel}`Customize Appearance…`
  applies styling live.

---

## Current limitations

- The GPAW path computes the **interband** response of a semiconductor:
  `intraband=False`, no Drude term — a metal's free-carrier response is not
  included.
- Local-field effects enter through GPAW's `eps_lfc`; excitonic effects do
  not — this is RPA linear response, not BSE.
- The CSV always exports on the energy grid, whichever x-axis unit the plot
  shows.
