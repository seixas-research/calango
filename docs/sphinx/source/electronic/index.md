# Electronic structure

The {guilabel}`Electronics` menu collects everything that reads out the
electronic state of a converged calculation: band structures with symmetry
labels and orbital projections, supercell unfolding, linear and nonlinear
optical response, G₀W₀ quasiparticle corrections, core-level spectroscopy,
first-principles Hubbard parameters, and four charge-analysis workflows.

The localized-orbital workflows have a menu of their own, {guilabel}`Wannier
Functions`, immediately to the right of {guilabel}`Electronics`. They are
documented in this section because that is where their physics belongs, but
they are a *chain* rather than four independent readouts: one entry builds the
Wannier basis and the other three consume the $H(\mathbf{R})$ it produces.

% TODO screenshot: the Electronics menu fully expanded over the main window
```{figure} /_static/img/elec_menu.png
:alt: The Electronics menu with its entries
:width: 92%
:figclass: screenshot

The Electronics menu — band structure and unfolding at the top, response
functions in the middle, spectroscopies and charge analysis below.
```

---

## One design rule

Almost every workflow in this section is a *post-process*: it interrogates a
ground state rather than producing one. Calango's rule is that **the ground
state is inherited, not recomputed** — Optics, 2D Optics, GW, and Wannier
Functions all load the `single_point.gpw` written by a completed
{menuselection}`Simulation --> Single-point Calculation…` run and refuse to
open without one. Because the baseline arrives whole, these wizards show no
Calculator Settings stage: the functional, cutoff, and k-grid come back from
the run you already inspected, and every spectrum is attributable to that
specific SCF solution.

:::{note}
Nonlinear Optics is the single deliberate exception — `gpaw.nlopt` needs
point-group symmetry off and a *converged* empty-band manifold, neither of
which a general baseline has, so that one wizard converges its own ground
state. {doc}`/electronic/nonlinear_optics` explains why.
:::

---

## The workflows

| Page | Menu entry | Needs |
|---|---|---|
| {doc}`/electronic/bands` | {menuselection}`Electronics --> Electronic Structure…` | GPAW baseline (DFT route) |
| {doc}`/electronic/twod_bands` | {menuselection}`Modules --> 2D Materials --> 2D Bands…` | GPAW baseline `.gpw` |
| {doc}`/electronic/unfolding` | {menuselection}`Electronics --> Effective Bands (Unfolding)…` | supercell + primitive cell |
| {doc}`/electronic/optics` | {menuselection}`Electronics --> Optics…` | GPAW baseline `.gpw` |
| {doc}`/electronic/workfunction` | {menuselection}`Modules --> 2D Materials --> 2D Workfunction…` | GPAW baseline `.gpw` |
| {doc}`/electronic/nonlinear_optics` | {menuselection}`Electronics --> Nonlinear Optics…` | converges its own ground state |
| {doc}`/electronic/gw` | {menuselection}`Electronics --> GW Calculations…` | GPAW `.gpw` or QE `.save` |
| {doc}`/electronic/wannier` | {menuselection}`Wannier Functions --> Wannierization…` / {menuselection}`Wannier Functions --> Wannier Interpolation…` | GPAW baseline `.gpw` |
| {doc}`/electronic/fermi_topology` | {menuselection}`Wannier Functions --> Fermi Surface…` / {menuselection}`Wannier Functions --> Topological Invariants…` | completed Wannier Functions run |
| {doc}`/electronic/wannier_transport_berry` | {menuselection}`Wannier Functions --> Boltzmann Transport…` / {menuselection}`Wannier Functions --> Berry Phase…` | a Wannier H(R) — computed natively, no job launched |
| {doc}`/electronic/xas` | {menuselection}`Electronics --> X-ray Absorption Spectroscopy (XAS)…` | GPAW (legacy engine) |
| {doc}`/electronic/hubbard` | {menuselection}`Electronics --> Hubbard Parameter Calculation…` | VASP or Quantum ESPRESSO |
| {doc}`/electronic/charges` | {menuselection}`Electronics --> Born Effective Charges…` / {menuselection}`Charged defects…` and related | VASP / QE / GPAW |
| {doc}`/electronic/raman_ir` | {menuselection}`Electronics --> Raman and IR Spectroscopy…` | GPAW / VASP / QE |

Every entry follows the same pipeline as the rest of Calango
({doc}`/simulations/index`): the wizard writes a standalone Python/ASE
script, the script runs as an isolated job with live progress markers, the
results land as JSON in the job directory, and a dedicated viewer opens on
completion — reopenable any time through {guilabel}`Load Result` in the
process manager.

---

```{toctree}
:maxdepth: 1

bands
twod_bands
unfolding
optics
workfunction
nonlinear_optics
gw
wannier
fermi_topology
wannier_transport_berry
xas
hubbard
charges
raman_ir
```
