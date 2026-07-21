# Calango

Cross-platform desktop application for materials science and atomistic
modeling.

[![License: MIT](https://img.shields.io/badge/License-MIT-red?style=for-the-badge)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![CMake 3.21+](https://img.shields.io/badge/CMake-3.21%2B-064F8C?style=for-the-badge&logo=cmake&logoColor=white)](https://cmake.org/)
[![Qt 6.4+](https://img.shields.io/badge/Qt-6.4%2B-41CD52?style=for-the-badge&logo=qt&logoColor=white)](https://www.qt.io/)
[![Python](https://img.shields.io/badge/Python-3.14%2B-fcbc2c?style=for-the-badge&logo=python&logoColor=white)](https://www.python.org/)

## Building

Prerequisites:

- CMake ≥ 3.21 and a C++20 compiler
- Qt 6.4+ (Widgets + OpenGLWidgets)
- Python 3.14+ with **ASE and NumPy installed in the interpreter you build
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
