<h1 align="center" style="margin-top:20px; margin-bottom:50px;">

<a href="https://github.com/seixas-research/calango" target="_blank" rel="noopener noreferrer">
  <picture>
    <source srcset="https://raw.githubusercontent.com/seixas-research/calango/refs/heads/main/assets/calango/logo_dark.png" media="(prefers-color-scheme: dark)">
    <source srcset="https://raw.githubusercontent.com/seixas-research/calango/refs/heads/main/assets/calango/logo_light.png" media="(prefers-color-scheme: light)">
    <img src="https://raw.githubusercontent.com/seixas-research/calango/refs/heads/main/assets/calango/logo_light.png" alt="Calango logo" style="height: auto; width: auto; max-height: 100px;">
  </picture>
</a>
</h1>

**Calango** is a desktop application for computational materials science. It brings three things together in one window: an interactive viewer for building, editing and inspecting atomic structures; a calculator-agnostic environment for setting up, orchestrating and running simulations; and a set of viewers that turn the output of those simulations back into figures you can read.

[![License: MIT](https://img.shields.io/badge/License-MIT-red?style=for-the-badge)](LICENSE)
[![macOS .dmg](https://img.shields.io/badge/macOS-.dmg-000000?style=for-the-badge&logo=apple&logoColor=white)](docs/tex/packaging/packaging.pdf)
[![Debian](https://img.shields.io/badge/Debian%2FUbuntu-.deb-A81D33?style=for-the-badge&logo=debian&logoColor=white)](docs/tex/packaging/packaging.pdf)
[![ASE](https://img.shields.io/badge/ASE-3.2x-4B8BBE?style=for-the-badge)](https://wiki.fysik.dtu.dk/ase/)

<!-- screenshot placeholder: assets/calango/screenshot_main.png — main window with a structure loaded -->

---

## Atomistic structure viewer

Molecules, crystals, surfaces and disordered systems are shown on a 3D canvas that stays fluid at tens of thousands of atoms, and every view on it is **ready for publication**.

- **Representations and color.** Ball-and-stick, space-filling and wireframe, with coloring by element, coordination number, generalized coordination number or any per-atom scalar field, plus live legends, gradient bond coloring, force and velocity arrows, and magnetic-moment overlays.
- **Direct manipulation.** Six mouse modes — rotate, pan, select, insert, distance, angle — with single-key shortcuts, ray-cast picking, rubber-band selection, snapshot undo and redo, an interactive bond editor, and a periodic-table element picker.
- **Builders for real materials problems.** Surface slabs with an interactive Miller-index canvas; liquid and gas interfaces and ionic solutions packed to a target density; dislocations, stacking faults, twins, bicrystals and Voronoi polycrystals; nanotubes, nanoribbons, monolayers and nanoparticles; special quasirandom structures; polymers, water and ice, and adsorbate coverages.
- **Molecule and crystal libraries.** Common molecules and crystal prototypes are one dialog away, ordered by how often they are what you need, and editable the moment they appear; the Materials Project, PubChem and the Computational 2D Materials Database (C2DB) are one search away too.
- **Studio lighting and depth.** Up to four directional lights with a three-light studio default, ambient occlusion, depth of field and distance fog, all tuned live.
- **File formats without friction.** Everything the Atomic Simulation Environment reads and writes — CIF, POSCAR, extended XYZ, Quantum ESPRESSO, LAMMPS, Gaussian, SHELX and more — plus project files that restore a whole multi-tab session.
- **Publication output.** Off-screen renders up to 8192 px with transparency, turntable and trajectory animations, ray-traced scenes matching the live viewport, and Alembic export for Blender, Houdini and Maya.

## Atomistic simulation executor

A staged wizard collects the physics, generates a complete and editable Python script, and hands it to a runner that executes it locally, queues it behind other jobs, or submits it to an HPC cluster. The script is **always visible**: read it, edit it in place, or export it and run it unchanged on any machine.

- **Calculator-agnostic.** DFT and quantum chemistry (GPAW, Quantum ESPRESSO, VASP, ABINIT, CP2K, FHI-aims, SIESTA, OpenMX, FLEUR, NWChem, ORCA), semi-empirical tight binding (xTB, DFTB+), classical potentials and molecular mechanics (LAMMPS, GROMACS, Amber, EMT, Lennard-Jones, ASAP), and machine-learning potentials (MACE, CHGNet, MatterSim, FAIRChem, NequIP, Allegro, DeepMD-kit) — a continually expanding library, in which every engine offers the parameters it actually has rather than a lowest-common-denominator form.
- **The full task matrix.** Single-point energies, geometry optimization, molecular dynamics with the complete thermostat set and simulated-annealing schedules, phonons, absolute free energies by thermodynamic integration, Monte Carlo, nudged elastic band, cluster expansion and alloy thermodynamics, parameter convergence sweeps, and machine-learning potential training.
- **Orchestration.** Chain calculations on a node-graph canvas — relax, then converge, then compute a spectrum — passing geometries and converged ground states from node to node, with per-node status, batch fan-out over many structures, and resume from the point of failure.
- **Remote HPC execution.** Connect over SSH and Calango stages the script and structure, writes a SLURM, PBS or SGE wrapper, submits it, polls the queue, streams the remote log, and downloads the results when the run finishes.
- **Live monitoring.** Energy, temperature, force and pressure stream into plots while a job runs, molecular-dynamics trajectories appear in the viewport frame by frame as they are computed, and new jobs queue instead of being refused.

## Simulation data viewer

Finished runs are read back into the application, so results arrive as **interactive figures** rather than as output files waiting to be post-processed.

- **Electronic structure.** Band structures with projected density of states, fatbands and irreducible-representation labels; band unfolding for supercells; Fermi surfaces and topological invariants; local density of states, energy-level diagrams and real-space wavefunctions for finite systems; Wannier functions, with Boltzmann transport, Berry-phase polarization and Bethe-Salpeter excitons built on them; G₀W₀ quasiparticle corrections; Hubbard U by linear response; X-ray absorption spectra; elastic and piezoelectric tensors, with area-normalized 2D variants for monolayers.
- **Optical and vibrational spectra.** Linear, 2D and nonlinear optics, including second-harmonic generation, shift current and the intraband Drude response of metals; phonon dispersions and densities of states; Raman and infrared spectra with mode symmetry labels.
- **Thermodynamic properties.** Phonon free energy, entropy and heat capacity; absolute free energies from thermodynamic integration, with autocorrelation-corrected error bars; configurational entropy and order-disorder temperatures for alloys, alongside dilute-solution and cluster-ensemble mixing-enthalpy models; formation-energy convex hulls.
- **Structure and chemistry.** Radial distribution functions, structure factor, simulated X-ray diffraction, bond statistics, coordination numbers, short-range order, local entropy, velocity autocorrelation, partial charges, charge-density differences, and charged-defect formation energies.
- **Volumetric fields.** Isosurfaces, slice planes and dual-field potential maps from cube, CHGCAR, LOCPOT, PARCHG, ELFCAR and XSF grids, with mesh export for external renderers.
- **Convergence and run artifacts.** Parameter sweeps plotted against the quantity being converged, and every run's log, metrics and output files one click away.

---

## Installation

Prebuilt installers are published for Linux and macOS, each accompanied by a `.sha256` checksum file.

### Linux (Debian and Ubuntu)

Install the downloaded package with `apt` rather than `dpkg -i`, so that its dependencies are resolved for you:

```bash
sudo apt install ./calango_*_amd64.deb
```

This provides the `calango` command, a desktop launcher, and a file association for `.calproj` project files, so double-clicking a saved project opens the whole session.

### macOS

Open the downloaded `.dmg` and drag `calango.app` onto the Applications shortcut shown beside it. The image mounts without a license prompt, and the application runs on Apple Silicon without any further setup.

Distribution builds are signed ad hoc rather than notarized, so the first launch needs right-click, then Open, to get past Gatekeeper. Subsequent launches are normal.

## Documentation

| Resource | Covers |
|---|---|
| [On-line manual](docs/sphinx/) *(ReadTheDocs)* | The full manual: builders, wizards, calculators, analysis, orchestration, remote execution, gallery, and building from source |
| [User Guide (PDF)](docs/tex/user_guide/calango_user_guide.pdf) | The complete user guide, including a start-to-finish silicon tutorial |
| [Packaging Guide (PDF)](docs/tex/packaging/packaging.pdf) | How the installers are produced and what each one bundles |

## License

Calango is released under the [MIT License](LICENSE). Copyright © 2026 Leandro Seixas Rocha.

## Acknowledgements

We thank financial support from INCT Materials Informatics (Grant No. 406447/2022-5).
