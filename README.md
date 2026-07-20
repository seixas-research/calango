# Calango 🦎

Cross-platform desktop application for materials science and molecular
modeling — a Qt6/OpenGL front-end over the
[Atomic Simulation Environment (ASE)](https://wiki.fysik.dtu.dk/ase/),
in the spirit of QuantumATK and Schrödinger Maestro.

Development follows [ROADMAP.md](ROADMAP.md).

## Features (v0.2)

- **3D visualizer** — atoms as instanced spheres (CPK colors), bonds as
  per-atom-colored cylinders (minimum-image bonds across periodic
  boundaries; double/triple bonds as parallel offset cylinders),
  unit-cell wireframe; orbit/pan/zoom camera (OpenGL 3.3 core via
  `QOpenGLWidget`).
- **Display panel** — Ball-and-Stick / Space-filling (CPK) / Wireframe
  representations, global atom-radius and bond-width sliders, per-element
  color *and* radius overrides (Element Settings dialog), viewport
  background color picker, and up to 4 configurable directional lights
  (two-light key/fill studio default; direction +
  ambient/diffuse/specular per light).
- **Image & animation export** — high-resolution off-screen captures
  (8× MSAA, up to 8192 px) to PNG/JPEG with white or transparent
  background; animated GIFs (Pillow) and H.264 MP4 videos
  (imageio + bundled ffmpeg) from turntable rotations or trajectory
  frames, with resolution, framerate and background-color options.
- **MACE ML potentials** — simulation setup supports MACE foundation
  models (MACE-MP-0 for materials, MACE-OFF for organics;
  small/medium/large, auto-downloaded on first use) and custom
  user-trained checkpoints (`.model`/`.pt`) on cpu/cuda/mps
  (`pip install mace-torch` to run the generated scripts).
- **Picking & editing** — click / Ctrl+click ray-cast selection with
  highlight; add atoms, change element, translate or delete selections;
  snapshot undo/redo (Ctrl+Z / Ctrl+Shift+Z).
- **Structure I/O through ASE** — one embedded-Python code path reads and
  writes every format ASE knows: XYZ, extended XYZ, CIF, POSCAR/CONTCAR,
  trajectories, …
- **Builder** — supercells via `ase.Atoms.repeat`; surface slabs via
  `ase.build.surface` (Miller indices, layers, vacuum).
- **ASE input generator** — a dialog that maps a form onto ASE calculators
  (EMT, Lennard-Jones out of the box; Quantum ESPRESSO / VASP templates)
  and tasks (single point, BFGS optimization, Langevin NVT / Velocity-
  Verlet NVE MD), with a live preview of the generated Python script.
- **Local job runner** — generated scripts run as `python run.py`
  subprocesses (`QProcess`); stdout/stderr stream into a dockable console,
  `CALANGO_PROGRESS` markers drive a progress bar, `CALANGO_ENERGY`
  markers feed a live energy-vs-step plot, and finished jobs offer to
  load their result structure.
- **Trajectory timeline** — File → Open Trajectory (`.traj`, multi-frame
  XYZ) shows an interactive timeline docked under the viewport:
  transport buttons, tick-marked scrubber, and 0.25×–4× playback speed.

## Building

Prerequisites:

- CMake ≥ 3.21 and a C++20 compiler
- Qt 6.4+ (Widgets + OpenGLWidgets)
- Python 3.9+ with **ASE and NumPy installed in the interpreter you build
  against** (pybind11 is fetched automatically if not found)

```bash
# 1. Python environment the app will embed
python3 -m venv .venv
.venv/bin/pip install ase numpy pillow imageio imageio-ffmpeg
# pillow: GIF export · imageio(-ffmpeg): MP4 export
# optional, for MACE ML potentials:  .venv/bin/pip install mace-torch

# 2. Configure — point CMake at that interpreter (and at Qt if needed)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DPython3_EXECUTABLE=$PWD/.venv/bin/python \
      -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<platform>

# 3. Build & run
cmake --build build -j
./build/calango assets/samples/Si_diamond.vasp
```

## Python environment resolution

An embedded interpreter does not inherit a virtualenv by itself, so
Calango picks its interpreter explicitly, in this order:

1. `CALANGO_PYTHON` — path to an interpreter (highest priority)
2. `VIRTUAL_ENV` — an activated virtualenv
3. the interpreter CMake found at configure time (baked into the binary)

The same interpreter also launches simulation jobs, so ASE must be
installed in it. If structure loading fails with an ASE import error,
diagnose from a terminal:

```bash
./build/calango --probe-python   # prints interpreter, Python and ASE versions
export CALANGO_PYTHON=/path/to/.venv/bin/python   # to override
```

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

See [ROADMAP.md](ROADMAP.md) for the phase-by-phase plan and current status.
