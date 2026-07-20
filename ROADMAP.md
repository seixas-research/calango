# Project Roadmap: Materials Science & Molecular Modeling Suite

This roadmap outlines the core development phases for building a cross-platform desktop application for materials science using C++20, Qt6, OpenGL, and Python/ASE.

> Status as of v0.2 — checked items are implemented; partial items carry a note.

---

## 📅 Phase 1: Infrastructure & Python/C++ Integration
**Focus:** Setting up the environment, build systems, and cross-language communication.

*   [x] Configure the **CMake** build ecosystem to correctly link **Qt6**, **OpenGL**, and **pybind11**.
*   [x] Implement the initialization and teardown of the embedded Python interpreter within the C++ binary lifecycle. *(Virtualenv-aware: resolves `CALANGO_PYTHON` → `VIRTUAL_ENV` → build-time interpreter via `PyConfig.executable`.)*
*   [x] Create a robust bridge layer: Verify data exchange by passing atom coordinates matrices from C++ to a Python `ase.Atoms` object and retrieving physical properties back. *(`AseBridge`: read/write/repeat/surface round-trips, verified with EMT jobs.)*
*   [x] Setup basic logging and error handling for the embedded Python runtime. *(Python tracebacks surfaced as C++ exceptions/dialogs; `calango --probe-python` for headless diagnosis.)*

---

## 📅 Phase 2: 3D Graphics & Visualization Engine
**Focus:** Building a high-performance 3D viewport for molecular systems.

*   [x] Implement a custom `QOpenGLWidget` subclass to act as the primary 3D canvas.
*   [x] Develop optimized OpenGL shaders using **Instanced Rendering** to smoothly draw thousands of atoms (spheres) and bonds (cylinders). *(One instanced draw per mesh type; GLSL 3.30 core.)*
*   [x] Implement robust camera controls including Arcball/Trackball rotation, panning, and zooming. *(Orbit/turntable; double-click reframes.)*
*   [x] Add rendering support for unit cell boundaries (lattice vectors) and periodic boundary conditions visualization. *(Cell wireframe + minimum-image bonds drawn as boundary stubs.)*
*   [x] Implement color-coding and scaling systems based on chemical elements and covalent radii. *(Jmol CPK colors, Cordero radii, Z ≤ 86.)*

---

## 📅 Phase 3: The Structure Builder & File I/O
**Focus:** Enabling structure manipulation, creation, and importing/exporting files.

*   [x] Integrate ASE's I/O modules to read and write common file formats (`.cif`, `POSCAR`, `.xyz`, `.pdb`). *(Everything `ase.io` knows, one code path.)*
*   [x] Implement mouse-based interaction using **Raycasting** for atom selection, hovering, and multi-selection. *(Click / Ctrl+click with highlight rendering; hover feedback still pending.)*
*   [x] Create UI manipulation tools: Adding/deleting atoms, modifying bond lengths, altering chemical species, and translating selections. *(Plus snapshot undo/redo; direct bond-length editing still pending.)*
*   [x] Build Crystal Wizard modules: Interface with ASE to generate supercells $(n \times m \times k)$ and cleave surfaces (Slabs). *(`atoms.repeat` + `ase.build.surface` with Miller indices, layers, vacuum.)*

---

## 📅 Phase 4: Simulation Setup & Calculator Modules
**Focus:** Designing the GUI frontend for setting up atomistic simulations.

*   [x] Design dynamic Qt forms for configuring ASE Calculators (starting with empirical ones like `EMT` or `Asap`, planning layouts for `Quantum ESPRESSO` and `VASP`). *(EMT + Lennard-Jones runnable; QE/VASP emit editable script templates.)*
*   [x] Implement simulation task parameter selectors: Geometry Optimization, Molecular Dynamics (NVE, NVT, NPT), and Energy/Force calculations. *(BFGS, Langevin NVT, Velocity-Verlet NVE; NPT pending.)*
*   [x] Develop an automation backend that translates GUI configurations into a clean, self-contained `.py` script ready to run via ASE. *(Live preview + Save Script…; scripts run unmodified on clusters.)*

---

## 📅 Phase 5: Local Job Runner & Post-Processing Analysis
**Focus:** Executing simulations locally and visualizing the results.

*   [x] Create a local job queue manager using `QProcess` or asynchronous C++ threads (`std::jthread`) to prevent UI freezing during calculations. *(Single job at a time; a multi-job queue with history browser is the next step here.)*
*   [x] Implement a real-time log viewer window capturing stdout and stderr from the running Python process. *(Line-buffered, colored stderr, progress bar from `CALANGO_PROGRESS` markers, kill button.)*
*   [x] Build an analysis dashboard: Integrate a plotting library (e.g., *QCustomPlot*) to graph energy convergence curves or temperature fluctuations over time. *(Built-in QPainter energy-vs-step plot fed by `CALANGO_ENERGY` markers — zero extra dependencies; can swap in QCustomPlot later.)*
*   [x] Implement trajectory playback controls (Play, Pause, Frame-by-Frame) for Molecular Dynamics or Optimization paths. *(File → Open Trajectory; play/pause + frame slider dock.)*

---

## 🚀 Future Modules (Post-MVP)
*   **Remote Job Submission:** Connect to HPC clusters using SSH/SFTP and generate SLURM/PBS submission scripts.
*   **Plugin Architecture:** Decouple specific workflows (e.g., NEB path generation, band structure analysis) into modular C++ plugins using `QPluginLoader`.