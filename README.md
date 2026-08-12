<h1 align="center" style="margin-top:20px; margin-bottom:50px;">

<a href="https://github.com/seixas-research/calango" target="_blank" rel="noopener noreferrer">
  <picture>
    <source srcset="https://raw.githubusercontent.com/seixas-research/calango/refs/heads/main/assets/calango/logo_dark.png" media="(prefers-color-scheme: dark)">
    <source srcset="https://raw.githubusercontent.com/seixas-research/calango/refs/heads/main/assets/calango/logo_light.png" media="(prefers-color-scheme: light)">
    <img src="https://raw.githubusercontent.com/seixas-research/calango/refs/heads/main/assets/calango/logo_light.png" alt="Calango logo" style="height: auto; width: auto; max-height: 100px;">
  </picture>
</a>
</h1>

**Calango** is a desktop application for computational materials science that does two things exceptionally well: it **runs, orchestrates, and analyzes atomistic simulations** across nineteen calculation engines, and it **renders and edits atomic structures in real time** on a GPU-accelerated 3D canvas. A C++20/OpenGL core provides the speed; an embedded Python interpreter running the Atomic Simulation Environment (ASE) provides the science — so every structure on screen is one click away from a real calculation.

[![License: MIT](https://img.shields.io/badge/License-MIT-red?style=for-the-badge)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Qt 6.4+](https://img.shields.io/badge/Qt-6.4%2B-41CD52?style=for-the-badge&logo=qt&logoColor=white)](https://www.qt.io/)
[![Python](https://img.shields.io/badge/Python-3.9%2B-fcbc2c?style=for-the-badge&logo=python&logoColor=white)](https://www.python.org/)
[![ASE](https://img.shields.io/badge/ASE-3.2x-4B8BBE?style=for-the-badge)](https://wiki.fysik.dtu.dk/ase/)
[![macOS .dmg](https://img.shields.io/badge/macOS-.dmg-000000?style=for-the-badge&logo=apple&logoColor=white)](docs/tex/packaging/packaging.pdf)
[![Debian](https://img.shields.io/badge/Debian%2FUbuntu-.deb-A81D33?style=for-the-badge&logo=debian&logoColor=white)](docs/tex/packaging/packaging.pdf)
[![Conda](https://img.shields.io/badge/Conda-package-44A833?style=for-the-badge&logo=anaconda&logoColor=white)](docs/tex/packaging/packaging.pdf)

<!-- screenshot placeholder: assets/calango/screenshot_main.png — main window with a structure loaded -->

---

## A simulation engine with a cockpit

Calango turns a GUI configuration into a **standalone, editable Python/ASE script**, then runs it — locally as an isolated subprocess, queued behind other jobs, or submitted to an HPC cluster over SSH. The script is always visible and always yours: edit it in the built-in editor, or export it and run it unmodified on any machine with ASE.

- **Nineteen calculators, one workflow.** DFT (GPAW, Quantum ESPRESSO, VASP, SIESTA), quantum chemistry (ORCA), semi-empirical tight binding (xTB/GFN2, DFTB+), classical potentials (LAMMPS, GROMACS, EMT, Lennard-Jones, ASAP), and machine-learning potentials (MACE, DeepMD-kit, NequIP, Allegro, CHGNet, MatterSim, FAIRChem). Every wizard shares the same staged flow — calculator, settings, task, script review — and each engine exposes the parameters it actually has, not a lowest-common-denominator form.
- **The full task matrix.** Single-point, geometry optimization, molecular dynamics (NVE/NVT/NPT with the complete ASE thermostat set, plus endpoint-exact simulated-annealing schedules), phonons with spglib symmetry reduction, **electron–phonon coupling** (`gpaw.elph` supercell finite differences → gₘₙᵛ(k,q), then the Eliashberg function α²F(ω), λ, and the relaxation time τ that feeds the Drude model — with both Fermi-surface δ-functions integrated by the **linear tetrahedron method**, so there is no smearing parameter to converge), Monte Carlo, NEB, and cluster expansion.
- **Advanced electronic structure.** Band structures with PDOS, fatbands, and first-principles irreducible-representation labels; band unfolding for supercells; linear, 2D, and nonlinear optics (SHG, shift current) — including the **intraband Drude term** for metals, with the free-carrier relaxation time set from τ or tied to the broadening, and tetrahedron Brillouin-zone integration; G₀W₀ quasiparticle corrections (GPAW or Yambo); Wannier functions and interpolation; Fermi surfaces; topological invariants; XAS; Hubbard U by linear response; Born effective charges; Raman/IR spectra on three engines.
- **Orchestration.** The Orchestration dock chains calculations into a DAG on a node-graph canvas — relax, then converge, then compute a spectrum — passing geometries and ground states between nodes, with per-node status and failure propagation.
- **Live monitoring.** Energy, temperature, force, and pressure stream into plots as the job runs; MD trajectories stream frame-by-frame into the viewport while they are being computed. Jobs queue instead of refusing; a process manager keeps every run's logs, metrics, and artifacts one click away.
- **Remote HPC execution.** Connect over SSH, and Calango stages the same script and structure, generates a SLURM/PBS/SGE wrapper, uploads, submits, polls the queue, streams remote logs, and downloads results automatically when the job finishes.
- **A deep analysis toolbox.** RDF, structure factor, XRD, bond statistics, coordination (CN/GCN), Warren–Cowley short-range order, local entropy, VACF, Bader/Voronoi/Hirshfeld partial charges, charge-density differences, charged-defect formation energies, magnetic space groups (all 1651, BNS), Γ-point Raman/IR activity, convex hulls, and adsorption-site detection.

## A 3D visualization and modeling studio

The viewport is a fully accelerated OpenGL 3.3 canvas — instanced rendering keeps tens of thousands of atoms fluid — and everything you see is publication-ready.

- **Representations and color.** Ball-and-stick, space-filling, wireframe; coloring by element (CPK), coordination number, generalized coordination number, or any per-atom scalar field, through ten scientific colormaps with live legends. Gradient bond coloring, force/velocity arrows, and magnetic-moment overlays.
- **Studio lighting and depth.** Up to four directional Blinn-Phong lights with a three-light studio default, screen-space ambient occlusion, depth of field, and distance fog — all tuned live.
- **Direct manipulation.** Six mouse modes (rotate, pan, select, insert, distance, angle) with single-key shortcuts; ray-cast picking, rubber-band selection, snapshot undo/redo, an interactive bond editor with trajectory-wide rules, and a periodic-table element picker.
- **Builders for real materials problems.** Surface slabs with an interactive Miller-index canvas; liquid/gas interfaces and ionic solutions packed to a target density; dislocations (edge, screw, dipoles, anisotropic Stroh); stacking faults, twins, bicrystals, and Voronoi polycrystals; nanotubes, nanoribbons, TMD monolayers, and graphene oxide; Wulff-shape nanoparticles; special quasirandom structures; polymers, water/ice, and adsorbate coverages.
- **Volumetric data.** Isosurfaces, slice planes, and dual-field potential maps from `.cube`, CHGCAR/LOCPOT/PARCHG/ELFCAR, and `.xsf` grids, with OBJ mesh export.
- **Reciprocal space.** An interactive Brillouin-zone viewer with click-to-build k-paths, exportable to VASP, Quantum ESPRESSO, CASTEP, SIESTA, or an ASE script.
- **Publication output.** Off-screen renders up to 8192 px with transparency, turntable and trajectory animations (MP4/GIF and more), POV-Ray/Tachyon ray-traced scenes matching the live viewport, and Alembic export for Blender/Houdini/Maya.
- **File I/O without friction.** Every format ASE reads and writes — CIF, POSCAR, extended XYZ, Quantum ESPRESSO, LAMMPS, Gaussian, SHELX, and more — plus `.calproj` project files that restore your whole multi-tab session.

---

## Install

Prebuilt installers are described in the [Packaging Guide](docs/tex/packaging/packaging.pdf):

- **macOS** — drag-and-drop `.dmg` (Apple Silicon)
- **Debian/Ubuntu** — `sudo apt install ./calango_<version>_amd64.deb`
- **Conda** — `packaging/conda/create_conda_osx-arm64.sh` builds a local package

### Build from source

Requirements: a C++20 compiler, CMake 3.21+, Qt 6.4+ (Widgets, OpenGLWidgets, Concurrent, Svg), Python 3.9+ with development headers.

```bash
# 1. Python environment for the embedded interpreter
python3 -m venv .venv
.venv/bin/pip install ase numpy spglib pillow imageio imageio-ffmpeg paramiko

# 2. Configure and build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DPython3_EXECUTABLE=$PWD/.venv/bin/python \
      -DCMAKE_PREFIX_PATH=/path/to/Qt   # e.g. /opt/homebrew/opt/qt
cmake --build build -j

# 3. Run
./build/calango examples/Si_diamond.vasp
./build/calango --probe-python   # verify the embedded interpreter finds ASE
```

The embedded interpreter is resolved as `$CALANGO_PYTHON` → `$VIRTUAL_ENV` → bundled interpreter → configure-time default. Simulation jobs are independent of it: every wizard has an execution-environment selector for conda environments or explicit interpreters.

### Tests

Configure with `-DCALANGO_BUILD_TESTS=ON`, then `ctest --test-dir build`. The suite covers the pure C++ physics (builders, unfolding, magnetic groups, phonon thermodynamics), the generated Python scripts (parsed back by AST and exercised directly), and live GPAW benchmarks that self-skip when the response stack is absent.

## Documentation

| Resource | Covers |
|---|---|
| [Sphinx manual](docs/sphinx/) *(ReadTheDocs)* | The full manual: builders, wizards, calculators, analysis, orchestration, remote HPC, gallery |
| [User Guide (PDF)](docs/tex/user_guide/calango_user_guide.pdf) | The LaTeX user guide, including a start-to-finish silicon tutorial |
| [Packaging Guide (PDF)](docs/tex/packaging/packaging.pdf) | Installers, the embedded Python environment, dependency management |

## Architecture

```
            ┌──────────────────────────────────────────────┐
            │                  gui/  (Qt Widgets)          │
            │   MainWindow = Controller                    │
            │   ViewportWidget / docks / dialogs = Views   │
            └──────┬────────────────┬─────────────┬────────┘
                   │ observes       │ uses        │ uses
            ┌──────▼──────┐  ┌──────▼───────┐  ┌──▼───────────┐
            │   render/   │  │python_bridge/│  │ jobs/ remote/│
            │ OpenGL View │  │ embedded ASE │  │  QProcess    │
            └──────┬──────┘  └──────┬───────┘  └──────────────┘
                   │ reads          │ converts
            ┌──────▼────────────────▼───────┐
            │            core/              │
            │   Structure, UnitCell,        │
            │   CalculatorConfig,           │
            │   script generators, physics  │
            └───────────────────────────────┘
```

- **`core/`** — Qt-free data model, geometry, analysis algorithms, and the ASE script generators.
- **`python_bridge/`** — converts `core::Structure` ↔ `ase.Atoms` via pybind11; Python types never escape it.
- **`render/`** — instanced OpenGL renderer; reads the model, never mutates it.
- **`jobs/` + `remote/`** — subprocess and SSH job execution with a stdout marker protocol for live updates.
- **`gui/`** — main window, viewport, docks, wizards, and viewers.
- **`ui/`** — SVG icon theming.

Simulations never run inside the GUI process: local jobs are isolated `QProcess` subprocesses, remote I/O lives in a paramiko helper process, so a crashed calculation never takes the application down.

## License

Calango is released under the [MIT License](LICENSE). Copyright © 2026 Leandro Seixas Rocha.

## Acknowledgements

We thank financial support from INCT Materials Informatics (Grant No. 406447/2022-5).
