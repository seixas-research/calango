# X-ray absorption spectroscopy

{menuselection}`Electronics --> X-ray Absorption Spectroscopy (XAS)…`
computes a core-level absorption spectrum with GPAW, following the GPAW XAS
tutorial.

An XAS transition starts in a *core* level of one atom, so it needs a PAW
dataset with a hole in that level — and none ships with GPAW. The generated
script therefore runs **three stages rather than one**:

1. **Build the core-hole dataset** for the absorbing element, written into the
   job directory and found through `setup_paths` — nothing is installed into
   your GPAW distribution, and the run reproduces elsewhere.
2. **Converge a ground state** that uses the core-hole setup on the absorbing
   *atom* (and the ordinary setup on every other atom of the same element).
3. **Evaluate the spectrum** from those wavefunctions with `gpaw.xas`.

:::{note}
`gpaw.xas` runs on GPAW's **legacy engine only**. This is the one script
Calango generates that clears `GPAW_NEW` and asks for `legacy_gpaw=True` —
without it the run stops with "New-GPAW not supported".
:::

The three-stage design is what makes the job self-contained: a core-hole
dataset is not a property of the element alone but of the element *and* the
chosen hole, so it is generated per run rather than assumed to exist, and
keeping it in the job directory means the whole calculation — dataset, ground
state, spectrum — can be reproduced from that one folder on another machine.

---

## Settings at a glance

| Setting | Meaning | Default |
|---|---|---|
| {guilabel}`Element` / {guilabel}`Absorbing atom` | The species, then the one atom that carries the core hole | — |
| {guilabel}`Edge` | K (1s), L₁ (2s), L₂,₃ (2p) | K |
| {guilabel}`Core hole` | half / full / no hole | half hole |
| {guilabel}`Unoccupied bands` | States above the occupied ones converged for the sum | 30 |
| {guilabel}`Broadening (FWHM)` | Uniform Gaussian on every transition | 0.5 eV |
| {guilabel}`Broaden more at higher energy` | Linear width ramp above the edge | tutorial's ramp |
| {guilabel}`Compute the absolute edge position` | ΔKS shift — two extra total-energy runs | off |
| {guilabel}`Edge energy` | Manual shift of the whole spectrum; 0 keeps the relative scale | 0 eV |

---

## Absorbing site and edge

Pick the {guilabel}`Element` and then the {guilabel}`Absorbing atom` — **one
atom, not all atoms of that species**. Giving the core-hole setup to every one
of them would model a solid in which all are excited simultaneously, which is
not what an absorption measurement does. Inequivalent sites each need their
own run, and the measured spectrum is their sum; the wizard says so when the
structure has more than one candidate.

{guilabel}`Edge` selects the level:

| Edge | Core level | When it matters |
|---|---|---|
| K | 1s | what almost every XAS measurement means |
| L₁ | 2s | deeper L structure |
| L₂,₃ | 2p | transition metals |

---

## The core hole

| Choice | What it models |
|---|---|
| {guilabel}`Half hole (transition potential, 0.5 e)` | The transition-potential approximation: one calculation gives the whole spectrum, because the final-state relaxation is averaged between initial and final states. The tutorial's choice, and what most published XAS is. |
| {guilabel}`Full hole (excited final state, 1.0 e)` | The excited final state proper. Better for the first resonance, worse for the rest of the spectrum, and it needs the ΔKS shift to sit on an absolute energy scale. |
| {guilabel}`No hole (unperturbed ground state)` | What the spectrum would be if the core hole did not pull the excited states down — worth computing to see how large that effect is. |

{guilabel}`Unoccupied bands` (default 30) sets how many states above the
occupied ones are converged. **The spectrum is a sum over them, so too few
truncates it** — visibly, as a spectrum that stops rather than decays.

---

## Broadening and the energy scale

{guilabel}`Broadening (FWHM)` (default 0.5 eV) is a uniform Gaussian on every
transition. {guilabel}`Broaden more at higher energy` ramps the width linearly
above the edge, which is the physical behaviour — core-hole lifetime plus
final-state broadening — and what makes the result comparable with a
measurement whose peaks wash out with energy. The default ramp is the
tutorial's, chosen for the oxygen K edge; move it to your own system.

GPAW produces the spectrum on a **relative** scale whose zero is the first
unoccupied state, which is not where a measurement puts it. Two ways to fix
that:

- {guilabel}`Compute the absolute edge position` adds two total-energy
  calculations (delta-Kohn–Sham) that give the shift — roughly doubling the
  cost of the run.
- Type a known {guilabel}`Edge energy` directly (default 0, i.e. leave the
  relative scale).

Which to use depends on the question. Comparing peak *spacings* and shapes
against a measurement needs no absolute scale at all; assigning an observed
edge position, or comparing two different absorbing sites on one axis, needs
the ΔKS shift or a known reference energy. Mixing the two — one spectrum
shifted, another not — is the error the viewer's energy-scale header exists to
prevent.

---

## Results

The viewer opens automatically when the run finishes and plots the
**isotropic spectrum** — the average over the three Cartesian polarizations,
which is what a powder or solution measurement sees.
{guilabel}`Show the x, y and z polarizations` adds them individually; for an
oriented sample the difference between them *is* the result.
{guilabel}`Show individual transitions` overlays the discrete lines the
broadened curve is built from — which is how you tell a genuine shoulder from
two peaks the broadening merged.

**The header states which energy scale the axis is on**, because a relative
spectrum plotted as though it were absolute is wrong by hundreds of eV and
nothing about the curve reveals it.

% TODO screenshot: XAS viewer with isotropic spectrum, per-polarization curves and transition sticks
```{figure} /_static/img/elec_xas.png
:alt: XAS viewer showing the isotropic spectrum with individual transitions
:width: 92%
:figclass: screenshot

The isotropic K-edge spectrum with the per-polarization components and the
discrete transitions the broadened curve is built from.
```

---

## Limitations

- **GPAW's legacy engine only** — the script arranges this itself, but it
  means the run cannot share an environment pinned to new-GPAW-only builds.
- **One absorbing site per run.** A structure with inequivalent sites needs
  one run each, summed afterwards; the module does not do the summation for
  you.
- **No core-hole spin-orbit structure.** The L₂,₃ edges are computed from the
  2p level as one channel; the experimental L₂/L₃ splitting is not
  reproduced.
- The half-hole spectrum is on the right *shape* scale but not an absolute
  cross-section; compare shapes and peak spacings, not absolute intensities.

XAS complements the valence-side tools: where {doc}`/electronic/bands` and
{doc}`/electronic/optics` probe the states around the gap, the core-hole
spectrum probes the *unoccupied* manifold as seen from one chosen atom — an
element- and site-selective conduction-band fingerprint.
