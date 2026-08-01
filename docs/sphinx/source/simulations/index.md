# Simulations

Everything on the {menuselection}`Simulation` menu — and the simulation-shaped tools under {menuselection}`Modules` — follows one pattern: a staged wizard collects the physics, generates a **complete, standalone ASE script** you can read and edit, and hands it to a job runner that executes it in its own directory and process. This section walks that pipeline end to end: the shared {doc}`/simulations/wizards` shell every setup dialog is built on, the sixteen {doc}`/simulations/calculators` those wizards can drive (from EMT to GPAW, VASP, Quantum ESPRESSO, SIESTA, ORCA, LAMMPS and the machine-learning potentials), the three core {doc}`/simulations/tasks` (single point, relaxation, molecular dynamics with simulated annealing), the finite-displacement {doc}`/simulations/phonons` workflow, {doc}`/simulations/monte_carlo` sampling, the alloy {doc}`/simulations/cluster_expansion` toolchain, transition states with {doc}`/simulations/neb`, the parameter {doc}`/simulations/convergence` sweeps, and the {doc}`/simulations/mlip` trainer and dataset manager. The remaining pages cover what happens after you press {guilabel}`Run (Local)`: local {doc}`/simulations/jobs`, {doc}`/simulations/remote` execution, chaining runs on the {doc}`/simulations/workflows` canvas, and the anatomy of the generated {doc}`/simulations/scripts`.

```{toctree}
:maxdepth: 1

wizards
calculators
tasks
phonons
monte_carlo
cluster_expansion
neb
convergence
mlip
jobs
remote
workflows
scripts
```
