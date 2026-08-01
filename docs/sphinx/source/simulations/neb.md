# Nudged elastic band

{menuselection}`Simulation --> Nudged Elastic Band (NEB)` finds minimum-energy paths and barriers between two structures. Unlike the wizards it is a single, **non-modal** dialog: you pick endpoints, interpolate an initial band, preview it as a scrubbable trajectory in the main viewport, adjust, and launch — without losing the 3D view.

---

## Endpoints and interpolation

The reactant and product come from the open structure tabs or from files ({guilabel}`Browse…`). **Both endpoints must have the same atoms in the same order** — the dialog rejects a count mismatch up front, and the interpolation error message says so when the ordering is the problem.

| Setting | Default | Notes |
|---|---|---|
| {guilabel}`Intermediate images` | 5 | 1–50 between the endpoints |
| {guilabel}`Method` | Linear | or IDPP — the image-dependent pair potential, a smoother, less-overlapping initial guess |

{guilabel}`Preview` interpolates immediately (in the embedded interpreter, via `ase.mep.NEB`) and loads the band as a multi-frame document you can scrub — the cheapest possible sanity check before any force is computed. When you launch, the band is staged into the job directory as **`band.extxyz`** and the generated script reads it back.

---

## The solver

| Setting | Default | Notes |
|---|---|---|
| {guilabel}`Variant` | CI-NEB | Standard NEB, **CI-NEB (climbing image)**, or AutoNEB |
| {guilabel}`Spring constant k` | 0.1 eV/Å² | the band's inter-image springs |
| {guilabel}`Force convergence (fmax)` | 0.05 eV/Å | on the band forces |
| {guilabel}`Max optimizer steps` | 200 | — |
| {guilabel}`Optimizer` | FIRE | also BFGS and the other ASE optimizers |

Standard and climbing-image runs use ASE's `NEB` with the *improved tangent* method; CI-NEB lets the highest image climb against the springs so it converges onto the saddle point — the variant to use when the barrier height is the answer. **AutoNEB** grows and climbs the band adaptively instead: the interpolated images seed `autoneb*.traj` prefix files and ASE's `AutoNEB` inserts up to four additional images (`n_max = n_images + 4`) where the path needs resolution, always climbing.

---

## Calculator

The band relaxation runs one force evaluation per image per optimizer step, so the calculator group offers the engines that make sense for that budget: EMT, Lennard-Jones, MACE (size + device), GPAW, Quantum ESPRESSO, and the shared ML potentials (DeepMD, NequIP, Allegro, CHGNet, MatterSim, FAIRChem — one model path + device row). The plane-wave cutoff and k-grid rows serve the DFT engines; the {guilabel}`van der Waals Correction (DFTD4)` toggle is offered because a barrier is an energy *difference* — exactly where dispersion changes the answer.

Selecting GPAW reveals the same electronic-structure group the simulation wizards present — XC functional, smearing, eigensolver + SCF step cap, the three convergence tolerances, spin rows and the {guilabel}`Hubbard parameters…` editor (its element completer is seeded from *both* endpoints). An NEB converges one SCF per image per step, so these knobs matter here exactly as much as anywhere.

The dialog has its own execution-environment field ({guilabel}`Env Folder…` / a direct interpreter path) with a live status line; VASP's POTCAR directory is shared with the wizards through {menuselection}`Preferences --> External Files`.

:::{note}
There is deliberately **no density-export group** here: a `.cube` export belongs after *one* converged SCF, and a band converges one per image per step — asking for an export would produce either hundreds of files or an arbitrary pick. Run the converged saddle-point geometry through Single-point Calculation when you want its density.
:::

---

## Running and outputs

{guilabel}`Run` interpolates if you did not preview first, generates the script, and launches it as a standard streaming job (see {doc}`/simulations/jobs`). Every optimizer step streams the whole band — all images — into the live tab, so you watch the path relax as a trajectory whose frames are the images.

On completion:

- `neb_final.extxyz` — the converged band, endpoints included.
- The Job panel's Energy plot shows the **barrier profile**: each image's energy relative to the first image, one point per image.
- `CALANGO_RESULT barrier_eV=…` — the barrier, $\max_i E_i - E_0$.

:::{warning}
The reported barrier is the highest *image*, which for Standard NEB can sit beside the true saddle rather than on it — that is what the climbing image exists for. If the barrier matters, use CI-NEB (the default) and check that the profile's maximum is interior, not an endpoint: a maximum at an endpoint means the "reactant" or "product" was not a minimum of the calculator you ran.
:::

% TODO screenshot: the NEB dialog with endpoints selected and an interpolated band previewed in the viewport behind it
```{figure} /_static/img/sim_neb.png
:alt: The NEB dialog over a scrubbable preview of the interpolated band
:width: 92%
:figclass: screenshot

The non-modal NEB dialog: endpoints, interpolation and solver on the left, the previewed band scrubbable in the viewport.
```
