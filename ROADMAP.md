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

## 📅 Phase 6: Advanced Visualization & Export (v0.3)
**Focus:** Presentation-quality rendering and publication/media output.

*   [x] Customizable rendering modes: Space-filling (CPK), Ball-and-Stick, and Wireframe.
*   [x] Dynamic global scale controls for atom sphere radii and bond cylinder widths (Display panel sliders → shader instance data).
*   [x] Multiple-bond rendering (double/triple) as parallel offset cylinders, with distance-ratio bond-order perception. *(Validation case: `assets/samples/acetic_acid.xyz` — the C=O at 1.25 Å renders as a double bond.)*
*   [x] Atom Color Editor: per-element overrides of the CPK palette via `QColorDialog`, with per-element and global reset.
*   [x] Lighting panel: up to 4 independent directional lights with editable direction vectors and ambient/diffuse/specular components (multi-light Blinn-Phong fragment shader).
*   [x] High-resolution viewport capture via `QOpenGLFramebufferObject` (8× MSAA) up to 8192 px.
*   [x] Static image export: PNG with solid-white or fully transparent background; JPEG (auto-flattened over white).
*   [x] Animated GIF export: turntable rotation or loaded trajectory frames, with FPS control and background transparency (Pillow via the embedded Python bridge).
*   [ ] Hover highlight and per-atom labels in the viewport.
*   [ ] MP4/WebM video export (ffmpeg) for longer trajectories.

## 📅 Phase 7: Per-Element Styling, Timeline & ML Potentials (v0.4)
**Focus:** Fine-grained visual control, trajectory workflow polish, and machine-learning potentials.

*   [x] Per-element visual customization: individual color overrides *and* individual radius scales per chemical element (Element Settings dialog).
*   [x] Two-light studio default: warm key light + soft cool fill light (both fully editable in the Lighting panel).
*   [x] Viewport background color picker (Display panel), also usable as image/animation export background.
*   [x] Interactive playback timeline embedded below the 3D viewport and above the job console: first/prev/play-pause/next/last transport, tick-marked scrubber, playback-speed selector (0.25×–4×).
*   [x] MP4 (H.264) trajectory/turntable export with resolution, framerate and background-color options (imageio + bundled ffmpeg via the embedded Python bridge), alongside the existing transparent-GIF path.
*   [x] MACE machine-learning interatomic potentials in the simulation setup: universal foundation models (MACE-MP-0 and MACE-OFF, small/medium/large — auto-downloaded and cached by mace-torch) and custom user-trained checkpoints (`.model` / `.pt`) via a file browser, with cpu/cuda/mps device selection. *(Runtime requires `pip install mace-torch` in the job environment.)*
*   [ ] Per-atom (not just per-element) overrides and selection-based styling.
*   [ ] NPT molecular dynamics and MACE dispersion-correction toggle.

## 📅 Phase 8: Workspace, Solvers, Environments & Reciprocal Space (v0.5)
**Focus:** Multi-document workflow, expanded electronic-structure support, isolated execution environments, and k-space analytics.

*   [x] Tabbed multi-file workspace: every opened structure/trajectory gets its own tab (with per-document undo history); all tabs share the single accelerated viewport so display settings stay consistent.
*   [x] Precise UI controls: atom-radius and bond-width sliders paired with synced `QDoubleSpinBox` fields for exact typed values.
*   [x] Interactive ASE script editor: form on the left, live script on the right — now syntax-highlighted and manually editable, with edit detection that pauses form sync and an explicit "Regenerate" action.
*   [x] GPAW and SIESTA calculator modules in the ASE input generator (plane-wave/mesh cutoff + k-grid mapped into both).
*   [x] Conda environment selector: browse to an env folder or python executable; jobs run inside it with the env's `bin/` prepended to PATH (and `CONDA_PREFIX` set), so pw.x / siesta / gpaw / mace binaries resolve without global conflicts. Persisted across sessions.
*   [x] Brillouin zone viewer: Wigner-Seitz cell of the reciprocal lattice (exact half-space intersection, validated against sc cube and fcc truncated octahedron), translucent 3D polyhedron with edges.
*   [x] High-symmetry points from ASE's Bravais-lattice detection, rendered with labels (Γ, X, W, K, L, U, …); click points sequentially to draw the k-path (with order badges, undo/clear, and ASE's suggested path preload).
*   [x] k-path export to VASP `KPOINTS` (line mode) and Quantum ESPRESSO `K_POINTS crystal_b` card files.
*   [ ] Band-structure job template that consumes the drawn k-path directly.
*   [ ] Segmented (discontinuous) k-paths ("," breaks) in the path builder and exports.

## 📅 Phase 9: Projections, Analysis, Workflow & Builders (v0.6)
**Focus:** Camera projections, structural analysis, automated trajectory UX, benchmark library, and nanomaterial generators.

*   [x] Perspective ↔ Orthographic camera toggle in the viewport toolbar; the orthographic frustum is matched to the perspective FOV at the target distance so the toggle preserves apparent scale and alignment (picking and exports follow automatically).
*   [x] Radial distribution function g(r): total and element-pair partial RDFs, exact periodic-image evaluation (valid beyond L/2, triclinic-safe) with PBC defaulting from the structure and manual override; computed on a worker thread (QtConcurrent) and plotted in an interactive hover-readout chart. *(Validated: sc lattice shell positions and first-shell coordination = 6.000; CsCl-type partials.)*
*   [x] Automated trajectory workflow: finished MD/optimization jobs auto-open their `md.traj`/`opt.traj` in a new tab with the timeline pre-loaded; any multi-frame file (traj/XYZ) loaded by any path now activates the timeline automatically.
*   [x] Pre-built benchmark Examples menu (embedded as Qt resources, with recommended potentials): Diamond, bulk 2H-MoS₂, graphene monolayer, 1H-MoS₂ monolayer, benzene, naphthalene, coronene.
*   [x] Nanomaterial Builder dialog (ase.build): graphene sheets, zigzag/armchair nanoribbons (optional H termination), carbon nanotubes with chiral indices (n, m) and length, and MX₂ TMD monolayers (formula, 1T/2H phase, lattice parameters, vacuum).
*   [x] Random noise generator: Gaussian or uniform displacements with amplitude (Å) and reproducible seed; targets positions, cell vectors (affine strain — atoms follow fractionally), or both. Undoable.
*   [ ] RDF averaging over trajectory frames.
*   [ ] Coordination-number and bond-angle distribution analyses.

## 📅 Phase 10: Overlays, Databases, Stochastic Trajectories & Ray Tracing (v0.7)
**Focus:** Viewport furniture, external structure databases, noise trajectories, publication renders, and broad file-format coverage.

*   [x] Axes triad overlay in the viewport corner, toggleable, switchable between Cartesian X/Y/Z and Bravais lattice vectors a1/a2/a3 (Display panel).
*   [x] Unit cell wireframe customization: visibility toggle, QColorDialog color, and line width — widths > 1 render as lit tubes (core-profile GL clamps `glLineWidth`, tubes are the portable equivalent).
*   [x] Examples moved to Build → By Examples… as a "Database & Preset Browser" with a Presets tab and a Materials Project tab: fetch any structure by mp-id via the documented REST API (urllib through the embedded Python — no mp-api/pymatgen dependency; API key persisted in QSettings). *(JSON→Atoms conversion validated against an mp-149-shaped payload: Si-Si = 2.351 Å.)*
*   [x] Random Noise moved to the Simulation menu and extended to multi-frame stochastic trajectories with Independent (fresh noise from the original per frame) and Cumulative (random-walk) accumulation modes; trajectories open in a new tab with the timeline active.
*   [x] Timeline playback speed as a direct numeric FPS field (0.1–120 fps) instead of fixed multipliers.
*   [x] POV-Ray and Tachyon integration (File → Ray-Traced Render…): scene files generated from the *active viewport scene* (camera pose incl. orthographic mode, lights, per-element styling, multi-bonds, cell), plus QProcess invocation of the installed renderer binary with live log capture.
*   [x] Expanded ASE-backed I/O: Quantum ESPRESSO input/output (.in/.pwi/.pwo/.out), CASTEP .cell, LAMMPS data + dump trajectories, Gaussian .gjf/.com, SHELX .res, CIF — extension→format hints on read, explicit format mapping on save (QE export auto-fills pseudopotential placeholders). VASP: POSCAR I/O plus KPOINTS generation (Phase 8); INCAR templates remain part of calculator scripts.
*   [ ] MP database tab: formula/keyword search with result list (currently by-ID fetch).
*   [ ] Tachyon/POV-Ray trajectory batch rendering (frame sequences).

## 🚀 Future Modules (Post-MVP)
*   **Remote Job Submission:** Connect to HPC clusters using SSH/SFTP and generate SLURM/PBS submission scripts.
*   **Plugin Architecture:** Decouple specific workflows (e.g., NEB path generation, band structure analysis) into modular C++ plugins using `QPluginLoader`.