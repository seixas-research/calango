# Parameters convergence

{menuselection}`Modules --> Parameters Convergence` holds two sweep wizards that answer "is this setting tight enough?" with a curve instead of folklore: {guilabel}`Plane-wave Cutoff Convergence…` and {guilabel}`K-points Convergence…`. Both run the same fixed-geometry single point over an ascending parameter list, difference every run against the best member of the set, and open a shared results window with three convergence panels.

**The reference is the best run in the sweep — the highest cutoff or densest mesh — not an absolute number.** A PAW total energy has no meaningful absolute value, and totals are not comparable between codes or datasets; only the differences are.

Both wizards are three-stage: the sweep definition, then {guilabel}`Calculator & Convergence Settings` (the standard engine page — see {doc}`/simulations/calculators`), then the script review. The engine list is restricted to **GPAW and VASP**, the two plane-wave engines with a first-class sweep script. Every non-swept setting — XC, convergence targets, spin — is held fixed across the runs, as a convergence study requires.

---

## The cutoff sweep

| Setting | Default | Notes |
|---|---|---|
| {guilabel}`Minimum cutoff` | 300 eV | start well below the production candidate — that is what makes the trend visible |
| {guilabel}`Maximum cutoff` | 800 eV | the convergence reference |
| {guilabel}`Stride` | 100 eV | the maximum is always included, even when the stride does not land on it |

The calculator page shows **no single-cutoff field** — this sweep *is* the cutoff, and a second control would be one the generated script ignores. GPAW is forced into PW mode (any other discretization would hold the sweep variable constant and measure noise). Each point binds a **fresh calculator**: a shared one would carry the previous cutoff's converged density into the next SCF and quietly smooth out the very behavior being measured. For VASP the script sets `ENCUT` per point, runs each in its own `ecut_*` directory with `istart=0`, and switches `LWAVE`/`LCHARG` off — one cutoff's files must not leak into the next.

## The k-point sweep

The sibling wizard sweeps Monkhorst–Pack meshes, in one of two modes:

- **Isotropic** (default): one subdivision count swept $n \times n \times n$ — minimum **2**, maximum **10**, stride **2**.
- **Anisotropic**: independent start and stride per axis, advanced together over a fixed number of steps (default **5**) — for layered and chain-like materials whose reciprocal cell is far from cubic, e.g. 4×4×2 → 8×8×3 → 12×12×4. **A stride of 0 pins that axis** for the whole sweep — a slab's vacuum direction samples once, always.

{guilabel}`Gamma-centered Grid` lives on the sweep stage, because centering is part of the mesh definition and is held fixed across the sweep; the calculator page's k-grid row and BZ toggles are hidden here for the same reason the cutoff wizard hides its cutoff.

---

## What is measured

Every run records the total energy, forces and (best-effort) the k-averaged band energies; each is then differenced against the reference run:

| Metric | Definition | Why this form |
|---|---|---|
| $\Delta E/\text{atom}$ | $(E - E_\text{ref})/N$ | per atom, so the criterion transfers between cells |
| Force error | $\max_i \lVert \mathbf{F}_i - \mathbf{F}_{i,\text{ref}} \rVert$ | vector-wise per atom — comparing max\|F\| scalars would miss two wrong forces that share a magnitude; not divided by N, a force is already per-atom |
| $\Delta f_\text{max}$ | $f_\text{max} - f_\text{max,ref}$ | the scalar most convergence criteria quote |
| Eigenvalue MAD | $\text{mean}_n\,\lvert \langle\varepsilon_n\rangle - \langle\varepsilon_n\rangle_\text{ref} \rvert$ | spectral convergence: a per-band fingerprint of the k-averaged band energies |

A failing point (a diverging low-cutoff SCF, say) is recorded with its error and the sweep continues — one bad member must not lose the rest of the curve. If a run yields no eigenvalues, the third panel simply has one point fewer.

---

## Results

The script writes a JSON file in the job directory — `cutoff_convergence.json` or `kpoints_convergence.json` — with a `summary` block (formula, atom count, evaluated/failed counts, the reference run's cutoff or mesh, energy and `fmax`) and a `points` array carrying the raw and differenced values per member. That file is what the viewer reads, so it survives the session and reopens from the job's directory later.

Both sweeps share one **Convergence Results** window: three panels against the swept parameter — ΔE/atom, the force error and the eigenvalue MAD, all in **meV** so the numbers on screen match the units convergence criteria are quoted in. Each evaluated point gets a marker (the curve is an interpolation between a handful of expensive SCFs, not a dense signal), and:

- **Threshold corridors** (on by default) hatch the band within the energy and force tolerances of the reference, so "converged" is the first marker inside the corridor — a number you read off the plot.
- **σ×threshold zoom** clamps each panel's y-axis to ±σ times its own criterion: the far-from-converged early points would otherwise set the scale and flatten the interesting tail into a line.
- Each panel exports its own CSV and image, styled through {guilabel}`Customize Appearance`.

% TODO screenshot: the Convergence Results window with three panels, threshold corridors hatched and markers on each evaluated point
```{figure} /_static/img/sim_convergence_results.png
:alt: Three convergence panels with hatched threshold corridors
:width: 92%
:figclass: screenshot

The shared results window: energy, force and eigenvalue convergence against the swept parameter, with the acceptance corridor hatched.
```

:::{tip}
Converge the k-mesh and the cutoff separately, at a fixed value of the other, and quote the criterion you used — "ΔE/atom below 1 meV and force error below 10 meV/Å against an 800 eV reference" is a statement a referee can check. Jobs run and stream like any other — see {doc}`/simulations/jobs`.
:::
