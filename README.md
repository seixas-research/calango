# Calango

**Calango** is a modern, high-performance, cross-platform desktop application designed for materials science, crystallography, and atomistic modeling. By combining a raw C++20/OpenGL core with the flexibility of Python's Atomic Simulation Environment (ASE), Calango provides the speed needed for real-time 3D visualization and the extensibility of state-of-the-art simulation tools.

[![License: MIT](https://img.shields.io/badge/License-MIT-red?style=for-the-badge)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![CMake 3.21+](https://img.shields.io/badge/CMake-3.21%2B-064F8C?style=for-the-badge&logo=cmake&logoColor=white)](https://cmake.org/)
[![Qt 6.4+](https://img.shields.io/badge/Qt-6.4%2B-41CD52?style=for-the-badge&logo=qt&logoColor=white)](https://www.qt.io/)
[![Python](https://img.shields.io/badge/Python-3.14%2B-fcbc2c?style=for-the-badge&logo=python&logoColor=white)](https://www.python.org/)

---

## Key Features

### 3D Visualization & Aesthetics
- **High-Performance Viewport:** Fully accelerated `QOpenGLWidget` canvas leveraging OpenGL 3.3/4.1 instanced rendering to fluidly display thousands of atoms (spheres) and bonds (cylinders) in a single draw call.
- **Lighting & Camera Control:** Comprehensive turntable camera with double-click reframing, perspective/orthographic projections, and a multi-light studio setup (warm key light + cool fill light) customizable via a real-time lighting panel.
- **Per-Element Styling:** Dynamic configuration of CPK colors (with QColorDialog overrides) and covalent radii scaling.
- **Viewport Furniture:** Axes triad overlays showing Cartesian (XYZ) or Bravais lattice vectors (a1, a2, a3), alongside tube-rendered unit cell boundaries.

### Structure Builder & File I/O
- **Broad File Format Support:** Read and write any format supported by ASE (including `.cif`, `POSCAR`, `.xyz`, `.pdb`, Quantum ESPRESSO, CASTEP, LAMMPS, Gaussian, and SHELX).
- **Crystal & Nanomaterial Wizard:** Generate supercells, cleave slabs along specific Miller indices, or build graphene sheets, nanoribbons, carbon nanotubes, and MX₂ TMD monolayers.
- **Deformation & Randomization:** Add uniform/Gaussian noise to atomic positions, or apply affine strains directly to unit cells.
- **Interactive Timeline:** A playback scrubber below the viewport for exploring Molecular Dynamics (MD) trajectories or optimization steps (0.1 to 120 FPS).

### Simulations & ML Potentials
- **Calculators Supported:** Empirical solvers (EMT, Lennard-Jones) and state-of-the-art Machine Learning Interatomic Potentials (MACE-MP-0 and MACE-OFF foundation models or custom `.pt` checkpoints) runnable on CPU, CUDA, or MPS.
- **Interactive Script Editor:** Synthesize your GUI setup into standard Python/ASE scripts with a live, syntax-highlighted editor that pauses sync upon manual edits.
- **Subprocess Isolation:** Long-running simulations are spawned as separate subprocesses. This keeps the GUI responsive and ensures every simulation run is self-contained and reproducible.

### Reciprocal Space & Analytics
- **Brillouin Zone Viewer:** Render Wigner-Seitz cells of the reciprocal lattice with high-symmetry labels (Γ, X, W, K, L, U).
- **k-Path Builder:** Interactive node-by-node path definition with export capabilities to VASP `KPOINTS` and Quantum ESPRESSO card files.
- **Radial Distribution Function $g(r)$:** Multithreaded computation of total and element-pair partial RDFs with automatic periodic boundary conditions (triclinic-safe) and interactive plotting.

---

## Architecture Philosophy

Calango enforces a strict **Model-View-Controller (MVC)** split to ensure stability, performance, and easy extension points:

```
            ┌──────────────────────────────────────────────┐
            │                  gui/  (Qt Widgets)          │
            │   MainWindow = Controller                    │
            │   ViewportWidget / docks / dialogs = Views   │
            └──────┬────────────────┬─────────────┬────────┘
                   │ observes       │ uses        │ uses
            ┌──────▼──────┐  ┌──────▼───────┐  ┌──▼───────────┐
            │   render/   │  │python_bridge/│  │    jobs/     │
            │ OpenGL View │  │ embedded ASE │  │   QProcess   │
            └──────┬──────┘  └──────┬───────┘  └──────────────┘
                   │ reads          │ converts
            ┌──────▼────────────────▼───────┐
            │            core/              │
            │  Model: Structure, Atom,      │
            │  UnitCell, CalculatorConfig,  │
            │  AseScriptGenerator           │
            └───────────────────────────────┘
```

1. **`core/` (Model):** Represents the physics and geometry of the system. Written in pure C++ with zero external GUI or Python dependencies.
2. **`python_bridge/` (Stateless Translator):** Converts memory layouts between `core::Structure` and `ase.Atoms` objects via pybind11. Python types never escape this boundary.
3. **`render/` (OpenGL View):** Reads model data to construct instanced GPU buffers. It never mutates the state.
4. **`jobs/` (Subprocess Engine):** Spawns simulations using `QProcess`. It parses stdout streams for progress markers (`CALANGO_PROGRESS`, `CALANGO_ENERGY`) to trigger real-time GUI updates.
5. **`gui/` (Controller & Widgets):** Dispatches user actions, coordinates the views, and handles UI events.

---

## Compilation & Setup

### Prerequisites
- A C++20 compliant compiler (GCC 11+, Clang 13+, MSVC 2022+).
- **CMake** (version ≥ 3.21).
- **Qt 6.4+** (including the `Widgets` and `OpenGLWidgets` modules).
- **Python 3.14+** interpreter with development headers.

### Step-by-Step Build Guide

1. **Set up the embedded Python environment:**
   We recommend isolating the embedded interpreter and its libraries inside a virtual environment:
   ```bash
   # Create a virtual environment in the project root
   python3 -m venv .venv
   
   # Activate and install required scientific & export packages
   .venv/bin/pip install ase numpy pillow imageio imageio-ffmpeg
   
   # (Optional) Install MACE for Machine Learning potentials
   .venv/bin/pip install mace-torch
   ```

2. **Configure with CMake:**
   Point CMake to the virtual environment's Python interpreter and your Qt installation:
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
         -DPython3_EXECUTABLE=$PWD/.venv/bin/python \
         -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<platform>
   ```

3. **Build and Run:**
   Compile the source code using all available cores and run the application:
   ```bash
   cmake --build build -j
   
   # Run with a sample structure
   ./build/calango assets/samples/Si_diamond.vasp
   ```

---

## Python Environment Resolution

Because embedded Python runtimes do not automatically inherit shell virtual environments, Calango resolves the target Python interpreter using the following priority queue:

1. **`CALANGO_PYTHON` Environment Variable:** Explicit path to a Python executable (highest priority).
2. **`VIRTUAL_ENV` Environment Variable:** Detects currently activated virtual environments.
3. **Configure-time Interpreter:** Fallback interpreter detected when CMake configured the build.

### Diagnostic Tools
If your structure fails to load or logs show an `ASE` import error, query Calango's interpreter settings from the terminal:
```bash
./build/calango --probe-python
```
To force Calango to use a specific interpreter, export the variable beforehand:
```bash
export CALANGO_PYTHON=/path/to/.venv/bin/python
./build/calango
```

---

## Repository Layout

- [CMakeLists.txt](file:///Users/leseixas/Dropbox/Repositories/seixas-research/calango/CMakeLists.txt) — Global build configuration.
- [assets/](file:///Users/leseixas/Dropbox/Repositories/seixas-research/calango/assets/)
  - `shaders/` — GLSL shaders (compiled as Qt resource files).
  - `samples/` — Standard structure files (`.xyz`, `.vasp`) for debugging and benchmarking.
- [docs/](file:///Users/leseixas/Dropbox/Repositories/seixas-research/calango/docs/) — Documentation, architecture guides, and design notes.
- [src/](file:///Users/leseixas/Dropbox/Repositories/seixas-research/calango/src/)
  - [main.cpp](file:///Users/leseixas/Dropbox/Repositories/seixas-research/calango/src/main.cpp) — Application entry point and initialization loop.
  - [core/](file:///Users/leseixas/Dropbox/Repositories/seixas-research/calango/src/core/) — Physical models, cell properties, and ASE script generation routines.
  - [python_bridge/](file:///Users/leseixas/Dropbox/Repositories/seixas-research/calango/src/python_bridge/) — Embedded interpreter control and coordinate-translation wrappers.
  - [render/](file:///Users/leseixas/Dropbox/Repositories/seixas-research/calango/src/render/) — Custom shaders, instanced renderers, and OpenGL canvas widgets.
  - [jobs/](file:///Users/leseixas/Dropbox/Repositories/seixas-research/calango/src/jobs/) — Asynchronous simulation run wrappers and progress monitors.
  - [gui/](file:///Users/leseixas/Dropbox/Repositories/seixas-research/calango/src/gui/) — Dock widgets, settings panels, dialog boxes, and the main window controller.
