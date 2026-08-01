# Core tasks

Three wizards on the {menuselection}`Simulation` menu cover the bread-and-butter tasks: {menuselection}`Simulation --> Single-point Calculation` ({kbd}`Ctrl+R`), {menuselection}`Simulation --> Geometry Optimization` and {menuselection}`Simulation --> Molecular Dynamics`. All three share the wizard shell and the full engine list — see {doc}`/simulations/wizards` and {doc}`/simulations/calculators`. What happens after {guilabel}`Run (Local)` — job directory, metrics, live streaming — is covered in {doc}`/simulations/jobs`.

---

## Single-point calculation

One energy and force evaluation on the structure as it stands. The run reports the total energy and the largest per-atom force *norm* (not the largest Cartesian component, which under-reports by up to $\sqrt{3}$), and writes:

- `single_point.json` — energy (eV and Hartree), Fermi level where the backend exposes one, `fmax` and the atom carrying it, total and **per-atom magnetic moments** (the total is zero for any antiferromagnet, so per-atom is what a magnetic structure is actually about), the full force array, and the SCF record (iterations, the energy tolerance and step cap the run was held to).
- `single_point.extxyz` — the same geometry with the *converged* results attached as extxyz columns (`forces:R:3`, `magmoms:R:1`), which is what feeds the viewport's vector overlay.
- **GPAW only:** `single_point.gpw` (`mode="all"` — density plus wavefunctions), the baseline file every inheriting wizard on the Electronics menu looks for, plus any volumetric exports selected in {guilabel}`Density Exports` — all-electron or pseudo density, spin density, Hartree potential, **ELF** and kinetic-energy density, each to its own `.cube` (see {doc}`/simulations/calculators` for the file names). Finished cubes register in the Volumetric Data dock.

The Single-point wizard is also the only place that exposes GPAW's {guilabel}`Symmetry: off` toggle — a symmetry-off single point is the required baseline for a Wannier Functions run.

---

## Geometry optimization

ASE's local optimizers drive the relaxation; the calculator only returns forces (for VASP's internal alternative, see {doc}`/simulations/calculators`).

| Setting | Default | Notes |
|---|---|---|
| {guilabel}`Optimizer` | BFGS | also LBFGS (cheaper for large systems), FIRE (no Hessian), GPMin, MDMin |
| {guilabel}`Force convergence (fmax)` | 0.05 eV/Å | per-atom force norm |
| {guilabel}`Max optimization steps` | 200 | cap, not a target |

**Variable-cell relaxation** wraps the atoms in a cell filter — `FrechetCellFilter` (recommended, well-behaved) or the classic `UnitCellFilter` — so the optimizer relaxes positions *and* the cell. Three shapes are offered: full anisotropic stress relaxation, hydrostatic (isotropic strain only), or a custom per-component Voigt mask `[xx, yy, zz, yz, xz, xy]` — the way to relax only the in-plane axes of a 2D material.

**Constraints** freeze degrees of freedom before the optimizer runs. Each rule selects atoms either by explicit index list or by a spatial region (every atom whose x, y or z coordinate falls inside optional bounds — the classic "freeze the bottom two layers, z < 5 Å"), and freezes any subset of the three Cartesian directions: all three emits `FixAtoms`, a partial mask emits `FixCartesian`. **A region rule stores its bounds, not the atoms they selected** — the generated script re-evaluates the selection against the geometry it actually reads, so re-running on a slightly different cell cannot silently freeze the wrong atoms. Constraints bind to the atoms *before* the cell filter wraps them, which is what makes frozen atoms actually frozen in a variable-cell run.

Outputs: `opt.traj` (written by an observer, so every recorded frame comes from an evaluated step — the raw input geometry is not frame 0), `optimized.extxyz`, and `geometry_optimization.json`. Relaxations stream every step into a live viewport tab as they run.

% TODO screenshot: Geometry Optimization wizard's task stage with the cell-relaxation options and a constraints table
```{figure} /_static/img/sim_tasks_geometry.png
:alt: The geometry optimization settings with cell filter and constraint controls
:width: 92%
:figclass: screenshot

Geometry optimization: optimizer, convergence, variable-cell filters and frozen-atom rules.
```

---

## Molecular dynamics

The dynamics page opens with a {guilabel}`Mode` selector — {guilabel}`Constant temperature` (ordinary MD at a fixed setpoint) or {guilabel}`Annealing` (below) — followed by the ensemble and its parameters. Fields enable themselves only for the ensembles that use them.

| Ensemble | Integrator / thermostat | Key parameters |
|---|---|---|
| NVE | Velocity Verlet | timestep |
| NVT | Langevin (default) | temperature, friction (0.01 fs⁻¹) |
| NVT | Andersen | temperature, collision probability (0.05) |
| NVT | Berendsen | temperature, $\tau_T$ |
| NVT | Nosé–Hoover chain | temperature, $\tau_T$ |
| NPT | Berendsen | temperature, pressure, $\tau_T$, $\tau_P$ |
| NPT | Nosé–Hoover / Parrinello–Rahman (`ase.md.npt.NPT`) | temperature, pressure, $\tau_T$, $\tau_P$ |

Shared defaults: **300 K**, timestep **1 fs**, **1000** MD steps, thermostat coupling $\tau_T$ = **100 fs**, barostat coupling $\tau_P$ = **1000 fs**, pressure **0 GPa**. The NPT-Berendsen script carries a water-like compressibility and the Parrinello–Rahman script a solid-like 100 GPa bulk modulus in its `pfactor`, both marked `EDIT ME` — set your material's values for meaningful cell dynamics.

:::{note}
Every MD run initializes velocities from a Maxwell–Boltzmann distribution and then removes the net center-of-mass momentum (`Stationary`), so the system does not drift as a whole; for isolated, non-periodic systems the net angular momentum is removed as well (`ZeroRotation`). Constraints are applied *before* the velocities are drawn, so frozen atoms start at rest instead of receiving thermal velocities that must then be projected out every step.
:::

Sampling: metrics and trajectory frames are recorded every $N$ steps, with $N$ chosen automatically so a run streams at most about **400 frames** (settable explicitly). The complete record is written three ways — `md.traj` (ASE binary), `md.extxyz` (portable, with forces and velocities written into every frame for the vector overlay) and `md_final.extxyz`. Thermostatted runs print their setpoint so the Temperature plot draws a dashed reference line; barostatted runs do the same for pressure, computed as $P = -\mathrm{tr}(\sigma)/3$ in GPa.

---

## Simulated annealing

{guilabel}`Annealing` sweeps the thermostat target from an {guilabel}`Initial temperature` (default **1000 K**) to a {guilabel}`Final temperature` (default **300 K**) over the run. Everything else is unchanged — the same integrator, constraints, sampling and streamed trajectory. Only the target moves, and it is **retargeted on every step**, not once per sampling interval: a setpoint that jumps in sampling-sized increments is a staircase, and every riser is a thermal shock that shows up as sawtooth ringing on the temperature trace.

Three schedules, written in the run fraction $x = n/N$:

| Schedule | $T(x)$ |
|---|---|
| Linear | $T_0 + (T_1 - T_0)\,x$ |
| Exponential | $T_1 + (T_0 - T_1)\dfrac{e^{-kx} - e^{-k}}{1 - e^{-k}}$ |
| Logarithmic | $T_0 + (T_1 - T_0)\dfrac{\ln(1 + kx)}{\ln(1 + k)}$ |

**All three are endpoint-exact: $T(0) = T_0$ and $T(1) = T_1$ whatever the coefficient.** A schedule that only approaches its target asymptotically ends the run at a temperature nobody asked for, and the discrepancy is invisible in a plot that has been cooling for 50 ps. The {guilabel}`Ramp curvature k` (default **3.0**) is a curvature, not a rate constant with units: it says how far the ramp bends away from a straight line, and both non-linear laws degenerate to Linear as $k \to 0$ (Linear itself has no coefficient, so the field is disabled there). Exponential moves most of the way early — the shape for quenching a melt; Logarithmic is slowest near the target, the practical, endpoint-exact relative of the Geman–Geman $\sim 1/\ln n$ schedule.

The page restates the chosen schedule as the temperatures it actually produces at 0/25/50/75/100 % of the run, with the elapsed time the ramp spans — "1000 → 631 → 419 → 315 → 300 K over 20 ps" is a number you can check; a named curve is not.

:::{important}
Annealing needs a thermostat to retarget, so **NVE is withdrawn** from the ensemble list while the mode is selected (the ensemble moves to Langevin if NVE was chosen). The initial Maxwell–Boltzmann velocities are drawn at the *initial* temperature, not the constant-temperature field, so a 1000 K quench starts where the schedule says instead of spending its first picosecond catching up. For the Nosé–Hoover chain, the thermostat's fictitious masses are proportional to $k_BT$, so the generated script rescales them with every retarget — leaving them behind gives a chain tuned for a temperature it is no longer aiming at, visible as a thermostat that lags further behind the ramp the longer the run.
:::

The moving setpoint is logged as its own series: the MD Viewer plots it underneath the measured temperature, and the CSV export carries it as a `target_temperature_K` column. That second trace answers the only question an annealing run is asking — is the system following the schedule? A constant-temperature run has no such series; it shows its single setpoint as a dashed reference line instead.

% TODO screenshot: MD wizard in Annealing mode with the schedule selector, curvature field and the 0/25/50/75/100 % temperature summary
```{figure} /_static/img/sim_tasks_annealing.png
:alt: The annealing controls with the schedule restated as concrete temperatures
:width: 92%
:figclass: screenshot

Simulated annealing: the schedule is restated as the temperatures it actually produces, at five points along the run.
```
