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
- **Tabbed workspace** — open several structures/trajectories at once;
  each tab keeps its own undo history while sharing one accelerated
  viewport and consistent display settings.
- **Trajectory timeline** — File → Open Trajectory (`.traj`, multi-frame
  XYZ) shows an interactive timeline docked under the viewport:
  transport buttons, tick-marked scrubber, and 0.25×–4× playback speed.
- **ASE input generator** — split view with an *editable*,
  syntax-highlighted script pane (manual edits pause form sync until
  "Regenerate"); calculators: EMT, Lennard-Jones, Quantum ESPRESSO,
  VASP, MACE, **GPAW**, **SIESTA**. A conda-environment selector runs
  jobs inside any local env (its `bin/` is prepended to PATH, so solver
  binaries resolve cleanly).
- **Brillouin zone & k-path builder** (Analysis menu) — Wigner-Seitz
  reciprocal cell rendered in 3D with labeled high-symmetry points;
  click points in order to draw a k-path, preload ASE's suggested path,
  and export to VASP `KPOINTS` (line mode) or Quantum ESPRESSO
  `K_POINTS crystal_b` files.
- **Radial distribution function** (Analysis menu) — total and
  element-pair partial g(r) with exact periodic-image evaluation (PBC
  defaults from the structure, manually overridable), computed on a
  worker thread and plotted with hover readout.
- **Projection toggle** — perspective ↔ orthographic switch in the
  viewport toolbar, scale-preserving across the transition.
- **Database & Preset Browser** (Build → By Examples…) — ready-to-simulate
  benchmarks (diamond, bulk 2H-MoS₂, graphene, 1H-MoS₂ monolayer, benzene,
  naphthalene, coronene) with recommended potentials, plus a **Materials
  Project** tab that fetches structures by mp-id with your API key.
- **Viewport overlays** — corner axes triad (Cartesian or lattice
  vectors), and unit-cell wireframe with configurable color and width.
- **Ray-traced renders** (File menu) — POV-Ray / Tachyon scene export
  matching the on-screen scene, with one-click invocation of the
  installed renderer binary.
- **Stochastic trajectories** (Simulation → Random Noise…) — seeded
  Gaussian/uniform noise as single perturbations or multi-frame
  trajectories (independent or cumulative random-walk modes).
- **Nanomaterial builder** (Build menu) — graphene sheets, zigzag /
  armchair nanoribbons, carbon nanotubes (n, m, length), and MX₂ TMD
  monolayers (1T/2H) via `ase.build`.
- **Random noise tool** (Edit menu) — seeded Gaussian/uniform
  perturbations of positions and/or cell vectors (affine strain) for
  thermal-like disorder and MD warm starts.
- **Trajectory automation** — finished MD/optimization jobs open their
  trajectory in a new tab with the playback timeline pre-loaded.

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
