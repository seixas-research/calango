# Simulations

Everything on the {menuselection}`Simulation` menu — and the simulation-shaped tools under {menuselection}`Modules` — follows one pattern: a staged wizard collects the physics, generates a **complete, standalone ASE script** you can read and edit, and hands it to a job runner that executes it in its own directory and process. This section walks that pipeline end to end: the shared {doc}`/simulations/wizards` shell every setup dialog is built on, the expanding library of {doc}`/simulations/calculators` those wizards can drive (from EMT and xTB to GPAW, VASP, Quantum ESPRESSO, FHI-aims, SIESTA, ORCA, DFTB+, LAMMPS, GROMACS and the machine-learning potentials), the three core {doc}`/simulations/tasks` (single point, relaxation, molecular dynamics with simulated annealing), the finite-displacement {doc}`/simulations/phonons` workflow and the {doc}`/simulations/electron_phonon` coupling built on it, absolute free energies by {doc}`/simulations/thermodynamic_integration`, {doc}`/simulations/monte_carlo` sampling, the alloy toolchain — the {doc}`/simulations/cluster_expansion` builder and calculation, the {doc}`/simulations/eci_fit` that turns its energies into a model, the {doc}`/simulations/cluster_variation` solver that turns that model into configurational entropy and an order-disorder temperature, {doc}`/simulations/egqca`, which instead solves the explicit cluster ensemble directly for the Gibbs mixing free energy and any composition-dependent property, and {doc}`/simulations/dsim`, a third route that needs no cluster-expansion batch at all — just four calculations at a binary alloy's two dilute-solution limits — transition states with {doc}`/simulations/neb`, the parameter {doc}`/simulations/convergence` sweeps, and the {doc}`/simulations/mlip` trainer and dataset manager. The remaining pages cover what happens after you press {guilabel}`Run (Local)`: local {doc}`/simulations/jobs`, {doc}`/simulations/remote` execution, chaining runs on the {doc}`/simulations/orchestration` canvas, and the anatomy of the generated {doc}`/simulations/scripts`.

```{toctree}
:maxdepth: 1

wizards
calculators
tasks
phonons
electron_phonon
thermodynamic_integration
monte_carlo
cluster_expansion
eci_fit
cluster_variation
egqca
dsim
neb
convergence
mlip
jobs
remote
orchestration
scripts
```
