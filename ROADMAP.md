# Project Roadmap: Materials Science & Molecular Modeling Suite

This roadmap outlines the core development phases for building a cross-platform desktop application for materials science using C++20, Qt6, OpenGL, and Python/ASE.

> Status as of v0.11 — checked items are implemented; partial items carry a note.

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
*   [x] Multiple-bond rendering (double/triple) as parallel offset cylinders, with distance-ratio bond-order perception. *(Validation case: `assets/examples/acetic_acid.xyz` — the C=O at 1.25 Å renders as a double bond.)*
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
*   [x] Coordination-number analysis. *(Shipped in Phase 11 as the CN/GCN module; bond-angle distributions still pending below.)*
*   [ ] Bond-angle distribution analysis.

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

## 📅 Phase 11: Topological Analysis, Scalar Color Mapping & Phonons (v0.8)
**Focus:** Atomic-environment descriptors, data-driven viewport coloring, and finite-displacement vibrational analysis.

*   [x] Coordination-number (CN) module: covalent-radius scaling (tolerance-adjustable) or fixed cutoff radius, with exact periodic-image enumeration — a primitive fcc cell correctly reports CN = 12 from self-images. *(Validated: fcc Cu primitive CN = 12; N₂ CN = 1; slab surface layers undercoordinated vs. bulk.)*
*   [x] Generalized coordination numbers (GCN) after Calle-Vallejo et al. (DOI 10.1002/advs.202207644): neighbor CNs summed and normalized by a bulk reference cn_max (12 fcc/hcp, 8 bcc, 4 diamond, or auto = max CN found), ranking terrace/step/edge/vertex sites of nanoparticles and slabs.
*   [x] Analysis → Coordination Numbers dialog: worker-thread computation, per-atom CN/GCN table with summary statistics (min/max/mean, cn_max used), and one-click "color viewport by CN / GCN".
*   [x] Multi-mode atom color mapping in the renderer: Element (CPK) / CN / GCN / custom per-atom property, applied consistently to atoms and bond halves in ball-and-stick, space-filling and wireframe modes; selection highlight tint composes on top.
*   [x] Scientific colormaps (Viridis, Plasma, Turbo) with normalized-range sampling, Display-panel gradient selector, and a live legend-range readout; trajectory playback recomputes CN/GCN per frame so dynamic mapping stays in sync.
*   [x] Scalar-field infrastructure on `core::Structure` (named per-atom arrays, kept index-aligned through atom add/remove) with automatic import of extended-XYZ per-atom columns — 1D arrays directly, (N, 3) arrays as magnitudes (e.g. `|forces|`) — ready for charges, forces and local potentials.
*   [x] Build → Phonon Builder (Finite Displacements): supercell size, displacement δ (default 0.01 Å), and calculator selection (EMT / Lennard-Jones / MACE foundation or custom checkpoints, cpu/cuda/mps), with the same editable syntax-highlighted script preview and conda-environment selector as the calculator dialog.
*   [x] Displaced-structure generation in-app: reference supercell + 6N ±δ configurations opened as a trajectory tab (timeline-scrubbable), ready for export to external DFT codes.
*   [x] Phonon pipeline via ase.phonons: force constants from finite displacements, dynamical matrix with acoustic sum rule, Γ-point frequencies as job-console results, dispersion along the ASE-suggested Brillouin-zone path (`phonon_bands.csv`) and DOS (`phonon_dos.csv`). *(Validated end-to-end: bulk Al/EMT 2×2×2 — three acoustic branches at Γ = 0.)*
*   [x] Molecular branch via ase.vibrations for non-periodic systems: frequency summary (imaginary modes flagged), `vibrations.txt`, and per-mode animation trajectories (`vib.<n>.traj`) openable directly in Calango; zero/imaginary modes excluded from animation (divergent 1/ω amplitude).
*   [ ] Symmetry-inequivalent displacement reduction (spglib/phonopy backend) to cut the 6N force evaluations.
*   [ ] In-app phonon band-structure and DOS plots (LinePlotWidget) fed from the generated CSV files.
*   [ ] Viewport colorbar overlay for the active scalar mapping (numeric legend in exports).
*   [ ] Per-atom scalar editing/import UI (paste charges, load from file) beyond extxyz auto-import.

## 📅 Phase 12: UI Refinement, Interactive Editing & Advanced Exports (v0.9)
**Focus:** Menu/dock reorganization, gradient visuals, bond-level editing, live previews, and multi-code k-path interchange.

*   [x] Menu bar reorganized to the canonical order File · Edit · View · Build · Simulation · Analysis (Help trailing); the projection toggle moved into View.
*   [x] File → Save Trajectory As…: multi-frame export to extended XYZ, plain multi-frame XYZ, ASE `.traj`, and multi-model PDB (`ase.io.write` over the frame list). *(Validated: 4-frame round-trip in all four formats.)*
*   [x] Quit with layout persistence: window geometry and dock arrangement saved via QSettings in closeEvent and restored at launch; the embedded interpreter is finalized cleanly by PythonEngine's destructor after window teardown.
*   [x] Gradient bond coloring: per-instance start/end colors (instance stride 20 → 24 floats, attribute location 7) interpolated along the cylinder axis in mesh.vert — bond halves meet at the midpoint color for one continuous atom-to-atom gradient; wireframe bonds interpolate per-vertex GL colors the same way. Toggleable in the Representation panel.
*   [x] Display settings split into three fully dockable/tabbable/floatable panels: Representation (mode, color mapping, scales, gradient bonds, element settings, background), Unit Cell & Axes (cell wireframe controls + new axes-triad size slider/spinbox), and Lighting (per-light directions and components). Nested and tabbed docking enabled.
*   [x] Edit → Bond Editor: automatic-perception toggle, live covalent cutoff multiplier, and manual add/suppress of individual bonds (by 1-based index pair or the two-atom viewport selection); overrides are stored on `core::Structure` (index-stable across atom deletion, captured by undo) and render regardless of representation mode.
*   [x] Graphical periodic table dialog (Z = 1–118, standard 18-group layout with f-block rows, colored by chemical family) wired into Add Atom and Change Element; element data table extended from Z = 86 to Z = 118 (Pyykkö radii, Jmol colors where defined).
*   [x] Surface slab generator rebuilt around a live preview: debounced `ase.build.surface` rebuilds on every parameter change, embedded 3D preview viewport, and a readout of the surface cell vectors u/v (components, lengths, angle), slab thickness and atom count before insertion.
*   [x] RDF dialog Export Data…: g(r) curves as structured `.csv` or whitespace `.dat` with header comments.
*   [x] Discontinuous k-paths: a Break action splits the path into sections (Γ→X | M→R) honored by the list, the 3D view, and every exporter; ASE's suggested paths now import their "," breaks too.
*   [x] Multi-format k-path export: CASTEP `SPECTRAL_KPOINT_PATH` (with `break`), SIESTA `BandLines` (sections restart at count 1), and a standalone ASE/Python band-path script, alongside the existing VASP/QE exporters (now section-aware). *(Validated: generated ASE script runs against ase 3.29 — BandPath 'GXW,KL', 160 k-points.)*
*   [x] Directional arrowheads along k-path legs in the Brillouin-zone canvas (3D wings in the GL view, filled 2D arrowheads in exports).
*   [x] Brillouin-zone figure export: publication-style rendering (white background, depth-sorted translucent faces, labels, order badges, path arrows) to high-resolution PNG or true-vector SVG via Qt Svg.
*   [x] Materials Project API key auto-loaded from an `.env` file at launch (`~/.env` default, shell environment wins); Edit → Preferences dialog to change the path, reload on demand, and inspect status. The Examples browser falls back to the environment key when none is stored.
*   [ ] Hover highlight and per-atom labels in the viewport.
*   [ ] Per-atom (not just per-element) style overrides and selection-based styling.
*   [ ] Bond-editor visual feedback: highlight the pending pair in the viewport.

## 📅 Phase 13: Workspace Grid, Slab Wizard & Phonon Fix (v0.10)
**Focus:** Fixed-grid workspace default, staged surface construction, and finite-displacement correctness.

*   [x] Phonon displacement bug fix (Build → Normal Modes / Phonon Builder): displaced-structure generation now expands the supercell FIRST and displaces every atom it contains (6·N_supercell + 1 frames), instead of displacing only the original cell atoms; the frame-count readout scales with the supercell multiplicity. *(The ase.phonons job path was already supercell-correct via ASE's own machinery and is unchanged.)*
*   [x] Bond-cylinder lighting parity verified: spheres, bond cylinders and cell tubes all render through the one instanced Blinn-Phong program (per-vertex radial normals, inverse-transpose normal transform, all active key/fill lights with ambient/diffuse/specular) — contract now documented in mesh.frag and the cylinder builder.
*   [x] 8-zone grid workspace (4 columns × 2 rows): Structure | Viewport (span) | Representation on top, Lighting | Job (span) | Unit Cell & Axes below — implemented with dock-area corner ownership so the Job dock spans only the middle columns; all zones splitter-resizable, panels still re-dockable/floatable, and the saved-layout version tag was bumped so the new default applies over stale layouts exactly once.
*   [x] Surface Slab wizard, stage 1 — orientation: Miller spinboxes + axonometric lattice canvas showing the surface parallelogram and in-plane vectors u, v; vector tips drag-snap to lattice points, and since snapped vectors are integer lattice combinations p·a, q·a, the Miller indices come out exactly as the integer cross product p × q (gcd-reduced, positive-leading). Canonical u/v recovery from ASE's rotated slab cell goes through the rotation-invariant metric tensor. *(Validated against ase.build.surface for fcc (111)/(100)/(110)/(211) and diamond (111).)*
*   [x] Surface Slab wizard, stage 2 — cut: orthogonal cross-section canvas with atomic layers clustered along the surface normal (0.1 Å tolerance — resolves the 0.78 Å Si(111) bilayer splitting into separate terminations); clicking layers assigns top/bottom terminations, with synchronized layer-count and Ångström-thickness controls.
*   [x] Surface Slab wizard, stage 3 — vacuum: top/bottom vacuum spinboxes with a symmetric/centered mode, live 3D preview (full viewport renderer), and final slab assembly in C++ (layer slice, re-basing, c = thickness + vacua) inserted into the active tab on Finish.
*   [x] ase.build.surface vacuum ≤ 0 deprecation fixed in the bridge (kwarg omitted for continuous bulk-like stacks).
*   [ ] Multiple termination *chemistries* offered as presets (e.g. Mo- vs S-terminated) rather than raw layer picking.
*   [ ] In-plane supercell repetition (n × m) as a wizard stage.
*   [ ] Wizard-built slab reconstructions (adatoms, missing-row) as an optional stage 4.

## 📅 Phase 14: Visual Fixes, Vector Overlays & Housekeeping (v0.11)
**Focus:** Rendering correctness, per-atom vector visualization, export ergonomics, and project hygiene.

*   [x] Offline-render depth bug fixed: the QPainter overlay at the end of paintGL() resets GL state, so FBO captures (image/GIF/MP4 export) could run with depth testing disabled — bonds then painted over atoms in submission order. renderToImage() now re-enables GL_DEPTH_TEST + depth writes explicitly inside the FBO pass.
*   [x] Default bond radius halved (0.12 → 0.06 Å) for lighter out-of-the-box bonds; the bond-width slider still scales from there.
*   [x] Bond/atom lighting parity re-audited: one shared instanced Blinn-Phong program shades spheres, cylinders and cones with identical ambient/diffuse/specular terms and correct normals — the perceived export discrepancy was the depth bug above.
*   [x] Force and velocity vector overlays: per-atom 3D arrows (lit cylinder shaft + closed cone head through the same shader path) with Representation-panel toggles and a 0.05–20× scale slider/spinbox. `core::Structure` gained per-atom vector fields (index-stable across atom add/remove); the ASE bridge imports every (N, 3) numeric array as vectors + magnitude scalar and derives "velocities" from momenta/masses. Toggles auto-disable when the structure lacks the data.
*   [x] Export resolution presets (720p / 1080p / 4K UHD / Custom) in both the image and animation dialogs; manual size edits flip back to Custom, and the animation size cap was raised to 4096 px for 4K.
*   [x] Surface Slab wizard now inserts the finished slab into a NEW workspace tab, leaving the bulk structure and its undo history untouched.
*   [x] Assets consolidated: `assets/samples` and `assets/samples/examples` merged into a single `assets/examples` (git history preserved via renames; CMake resources, Examples browser paths and docs updated).
*   [x] Dynamic versioning: the user-facing version lives in the plain-text `version` file at the repository root, read at runtime with std::ifstream (binary dir → parent → cwd, compile-time fallback) and shown in the About Calango dialog; CMake stages a copy beside the binary.
*   [ ] Arrow overlays in the wireframe representation (line-based arrows).
*   [ ] Vector-field color mapping (arrow color by magnitude via the scalar gradients).

## 🚀 Future Modules (Post-MVP)
*   **Remote Job Submission:** Connect to HPC clusters using SSH/SFTP and generate SLURM/PBS submission scripts.
*   **Plugin Architecture:** Decouple specific workflows (e.g., NEB path generation, band structure analysis) into modular C++ plugins using `QPluginLoader`.