# Structural analysis

Five tools that answer "where are the atoms, statistically?": the radial
distribution function $g(r)$, bond length and angle histograms, the static
structure factor $S(q)$, a simulated powder XRD pattern, and the velocity
autocorrelation function of an MD trajectory. All five open from the
{guilabel}`Analysis` menu, compute on a worker thread, and export CSV or
`.dat` chosen by the extension you type.

---

## Radial distribution function

{menuselection}`Analysis --> Radial Distribution Function…` computes $g(r)$ —
the probability of finding an atom at distance $r$, relative to a uniform
density of the same composition.

| Control | Meaning | Default |
|---|---|---|
| {guilabel}`Element pair` | Two combos, each {guilabel}`Any` or a specific element | Any–Any (total RDF) |
| {guilabel}`r max` | Upper bound of the histogram | 10 Å |
| {guilabel}`Bins` | Histogram resolution | 200 |
| {guilabel}`Periodic boundary conditions` | Enabled and pre-checked when the structure has a cell | on |
| {guilabel}`Frames` | Start / end / stride over a trajectory | all frames |

Choosing a specific pair gives the partial RDF $g_{AB}(r)$; the frame range
gives a frame-averaged $g(r)$ over any sub-interval of a trajectory — the
production window of an MD run, with the equilibration excluded.

:::{note}
With periodicity enabled, **all periodic images within $r_\text{max}$ are
enumerated explicitly** rather than folded through the minimum-image
convention. The result is therefore valid beyond $L/2$ and correct for
triclinic cells — both places where a naive implementation quietly produces
artefacts. This is why the default $r_\text{max}$ of 10 Å is usable even on
a small primitive cell.
:::

% TODO screenshot: the RDF dialog showing a partial g(r) of a liquid with the frame-range controls visible
```{figure} /_static/img/analysis_rdf.png
:alt: Radial distribution function dialog with a computed partial g(r) curve
:width: 92%
:figclass: screenshot

A partial $g_{AB}(r)$ averaged over an MD production window. The curve
recomputes on a worker thread whenever a control changes.
```

---

## Bond length and angle distributions

{menuselection}`Analysis --> Bond Length / Angle Distributions…` histograms
either pair distances or three-body angles $j$–$i$–$k$ within a cutoff.

| Control | Meaning | Default |
|---|---|---|
| {guilabel}`Distribution` | Bond length, or bond angle ($j$–$i$–$k$) | bond length |
| {guilabel}`Species` | Two element filters; for angles the first is the *central* atom | Any |
| {guilabel}`Cutoff` | What counts as bonded | 3 Å |
| {guilabel}`Bins` | Histogram resolution | 90 |

Lengths are reported in Å, angles in degrees, and both handle periodic
images exactly. The angle histogram is the quick diagnostic for
coordination geometry — a tetrahedral network peaks at 109.5°, an octahedral
one at 90°, and a distorted phase shows as the peak's width.

---

## Static structure factor

{menuselection}`Analysis --> Structure Factor S(q)…` computes $S(q)$ from
the (frame-averaged) pair distribution via a **Lorch-windowed Fourier
transform** — the window suppresses the truncation ripples a finite
$r_\text{max}$ would otherwise print onto the curve.

| Control | Meaning | Default |
|---|---|---|
| {guilabel}`q range` | Momentum-transfer window | 0.3 – 12 Å⁻¹ |
| {guilabel}`q points` | Sampling of that window | 400 |
| {guilabel}`g(r) r max` | Upper bound of the Fourier integral | 10 Å |
| {guilabel}`g(r) bins` | Resolution of the underlying $g(r)$ | 400 |
| {guilabel}`Frames` | Start / end / stride | all frames |

Larger $r_\text{max}$ improves low-$q$ fidelity — the long-wavelength limit
of $S(q)$ is exactly the part of $g(r)$ farthest from the origin.

---

## X-ray diffraction

{menuselection}`Analysis --> X-Ray Diffraction (XRD)…` simulates a powder
pattern with the **Debye scattering equation**, evaluated through ASE's
`ase.utils.xrdebye` module.

| Control | Meaning | Default |
|---|---|---|
| {guilabel}`Wavelength` | Cu Kα (1.54056 Å), Co Kα, Mo Kα, Cr Kα, or {guilabel}`Custom` | Cu Kα |
| {guilabel}`2θ range` | Angular window | 10 – 90° |
| {guilabel}`Points` | Curve sampling | 800 |
| {guilabel}`Supercell repeat` | Periodic structures are repeated before the Debye sum | 3 |

Unlike the other analysis dialogs, this one waits for {guilabel}`Simulate`:
the Debye sum grows as $N^2$ in atoms, and more supercell repeats sharpen
the Bragg peaks at exactly that cost.

The status line reports which atomic form factors were used —
**Waasmaier–Kirfel factors when every species is tabulated, otherwise a
constant $f = Z$ approximation**. In the fallback, peak *positions* remain
exact but high-angle *intensities* are approximate, and the dialog says so
rather than letting the two cases look alike.

Two exports: {guilabel}`Export Curve…` writes the $2\theta$–intensity
pattern, and {guilabel}`Export Peaks…` writes the detected peaks (those
above 2 % of the maximum) with their $d$-spacings from
$d = \lambda / (2\sin\theta)$.

% TODO screenshot: the XRD dialog after Simulate, with labeled Bragg peaks and the form-factor status line
```{figure} /_static/img/analysis_xrd.png
:alt: Simulated powder XRD pattern with the peak list and d-spacings
:width: 92%
:figclass: screenshot

A simulated Cu Kα powder pattern. The status line names the form-factor
table used; the peak list carries the $d$-spacings.
```

---

## Velocity autocorrelation function

{menuselection}`Analysis --> Velocity Autocorrelation Function (VACF)…`
turns an MD trajectory into its normalized velocity autocorrelation

$$
C_v(t) \;=\; \frac{\langle \mathbf{v}(0)\cdot\mathbf{v}(t)\rangle}
                  {\langle \mathbf{v}(0)\cdot\mathbf{v}(0)\rangle},
$$

averaged over **all atoms and all time origins**, plus the two transport
quantities and the spectrum that follow from it.

**Input requirements.** The active tab must hold a trajectory of at least
two frames in which *every* frame carries per-atom velocities (the
`velocities` vector field). Trajectories written by Calango's own
{menuselection}`Simulation --> Molecular Dynamics…` runs record them; a
positions-only file is refused with an explanatory message. Velocities are
assumed to be in Å/fs — the convention of Calango's extended-XYZ export
({doc}`/data_io`).

| Control | Meaning | Default |
|---|---|---|
| {guilabel}`Frame timestep` | Time between *stored* frames, in fs | 1.0 fs |
| {guilabel}`Max correlation lag` | Longest lag, in frames; 0 = half the trajectory | half |
| {guilabel}`Start / End frame` | Sub-interval of the trajectory (inclusive, 0-based) | all |
| {guilabel}`Step / stride` | Use every *N*-th frame; the effective timestep grows with it | 1 |

Every control recomputes immediately; {guilabel}`Recompute` forces a refresh.

**What it reports:**

- **Normalized VACF** $C_v(t)$ against lag time in fs.
- **Vibrational DOS** — the cosine transform of the Hann-windowed $C_v(t)$,
  on a frequency grid running from 0 to the Nyquist limit $1/(2\,\Delta t)$,
  reported in THz. The Hann window suppresses truncation ringing.
- **Self-diffusion coefficient** from the Green–Kubo relation
  $D = \tfrac{1}{3}\int_0^\infty \langle\mathbf{v}(0)\cdot\mathbf{v}(t)\rangle\,dt$,
  integrated by the trapezoid rule and displayed in cm²/s
  (1 Å²/fs = 0.1 cm²/s).
- **Momentum relaxation time** $\tau = \int_0^\infty C_v(t)\,dt$, displayed
  in ps — the area under the normalized VACF.

{guilabel}`Export CSV…` writes both curves in one file — a header comment
carrying $D$ (Å²/fs) and $\tau$ (fs), then a `time_fs,Cv` block and a
`frequency_THz,VDOS` block. {guilabel}`Export Image…` saves the two plots
stacked as a PNG.

:::{warning}
The Green–Kubo integral only converges when $C_v(t)$ has actually decayed
inside the correlation window. For a solid, $C_v$ oscillates and $D$ should
come out near zero; for a liquid, make the trajectory long enough that the
tail is flat — a $D$ read off a still-decaying VACF is an underestimate with
no error bar. Raising the stride coarsens the frequency grid: the VDOS
Nyquist limit is set by the *effective* timestep, stride included.
:::
