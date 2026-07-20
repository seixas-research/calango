# Calango 🦎

Cross-platform desktop application for materials science and molecular
modeling — a Qt6/OpenGL front-end over the
[Atomic Simulation Environment (ASE)](https://wiki.fysik.dtu.dk/ase/),
in the spirit of QuantumATK and Schrödinger Maestro.

## Features (v0.1 skeleton)

- **3D visualizer** — atoms as instanced spheres (CPK colors), bonds as
  per-atom-colored cylinders, unit-cell wireframe; orbit/pan/zoom camera
  (OpenGL 3.3 core via `QOpenGLWidget`).
- **Structure I/O through ASE** — one embedded-Python code path reads and
  writes every format ASE knows: XYZ, extended XYZ, CIF, POSCAR/CONTCAR,
  trajectories, …
- **Builder** — supercell creation via `ase.Atoms.repeat` (more editing
  tools on the roadmap).
- **ASE input generator** — a dialog that maps a form onto ASE calculators
  (EMT, Lennard-Jones out of the box; Quantum ESPRESSO / VASP templates)
  and tasks (single point, BFGS optimization, Langevin MD), with a live
  preview of the generated Python script.
- **Local job runner** — generated scripts run as `python run.py`
  subprocesses (`QProcess`); stdout/stderr stream into a dockable console
  and `CALANGO_PROGRESS` markers drive a progress bar.

## Building

Prerequisites:

- CMake ≥ 3.21 and a C++20 compiler
- Qt 6.4+ (Widgets + OpenGLWidgets)
- Python 3.9+ with **ASE and NumPy installed in the interpreter you build
  against** (pybind11 is fetched automatically if not found)

```bash
# 1. Python environment the app will embed
python3 -m venv .venv
.venv/bin/pip install ase numpy

# 2. Configure — point CMake at that interpreter (and at Qt if needed)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DPython3_EXECUTABLE=$PWD/.venv/bin/python \
      -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<platform>

# 3. Build & run
cmake --build build -j
./build/calango assets/samples/Si_diamond.vasp
```

The interpreter found at configure time is the one embedded at runtime and
also the one used to launch simulation jobs — keep ASE installed there.

## Layout

```
├── CMakeLists.txt
├── assets/
│   ├── shaders/          # GLSL 3.30 (compiled into Qt resources)
│   └── samples/          # small test structures
├── docs/ARCHITECTURE.md  # design notes: MVC split, threading, extension points
└── src/
    ├── main.cpp
    ├── core/             # Model: atoms, cells, bonds, calculator configs (no Qt/GL/Python)
    ├── python_bridge/    # embedded CPython + ase.Atoms <-> core::Structure
    ├── render/           # View: camera + instanced OpenGL renderer
    ├── jobs/             # QProcess job runner with live output parsing
    └── gui/              # View/Controller: main window, viewport, dialogs, docks
```

Headers live beside their sources: Calango is an application, not a
library. If a public SDK is ever split out, an `include/` tree can be
introduced for the exported surface.

## Roadmap

- Atom picking/selection and interactive editing (add/delete atoms, drag)
- Bonds across periodic boundaries (minimum-image) and cell-list bond
  detection for large systems
- Polyhedra rendering and alternative representations (van der Waals,
  wireframe, licorice)
- Trajectory playback from `.traj` files; auto-load `optimized.extxyz`
  after a job finishes
- Job queue with multiple concurrent jobs and remote (SSH/SLURM) submission
- Undo/redo via command pattern on the Structure model
