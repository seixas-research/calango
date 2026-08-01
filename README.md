<h1 align="center" style="margin-top:20px; margin-bottom:50px;">

<a href="https://github.com/seixas-research/calango" target="_blank" rel="noopener noreferrer">
  <picture>
    <source srcset="https://raw.githubusercontent.com/seixas-research/calango/refs/heads/main/assets/calango/logo_dark.png" media="(prefers-color-scheme: dark)">
    <source srcset="https://raw.githubusercontent.com/seixas-research/calango/refs/heads/main/assets/calango/logo_light.png" media="(prefers-color-scheme: light)">
    <img src="https://raw.githubusercontent.com/seixas-research/carcara/refs/heads/main/assets/.internal/logo_light.png" alt="Carcará logo" style="height: auto; width: auto; max-height: 100px;">
  </picture>
</a>
</h1> 

**Calango** is a modern, high-performance, cross-platform desktop application for materials science, crystallography, and atomistic modeling. By combining a raw C++20/OpenGL core with the flexibility of Python's Atomic Simulation Environment (ASE), Calango provides the speed needed for real-time 3D visualization together with the extensibility of state-of-the-art simulation tools.

[![License: MIT](https://img.shields.io/badge/License-MIT-red?style=for-the-badge)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![CMake 3.21+](https://img.shields.io/badge/CMake-3.21%2B-064F8C?style=for-the-badge&logo=cmake&logoColor=white)](https://cmake.org/)
[![Qt 6.4+](https://img.shields.io/badge/Qt-6.4%2B-41CD52?style=for-the-badge&logo=qt&logoColor=white)](https://www.qt.io/)
[![Python](https://img.shields.io/badge/Python-3.9%2B-fcbc2c?style=for-the-badge&logo=python&logoColor=white)](https://www.python.org/)
[![macOS .dmg](https://img.shields.io/badge/macOS-.dmg-000000?style=for-the-badge&logo=apple&logoColor=white)](docs/tex/packaging/packaging.pdf)
[![Conda](https://img.shields.io/badge/Conda-package-44A833?style=for-the-badge&logo=anaconda&logoColor=white)](docs/tex/packaging/packaging.pdf)
[![Debian](https://img.shields.io/badge/Debian%2FUbuntu-.deb-A81D33?style=for-the-badge&logo=debian&logoColor=white)](docs/tex/packaging/packaging.pdf)
[![ASE](https://img.shields.io/badge/ASE-3.2x-4B8BBE?style=for-the-badge)](https://wiki.fysik.dtu.dk/ase/)

---

## Documentation

| Guide | Covers |
|---|---|
| [User Guide](docs/tex/user_guide/calango_user_guide.pdf) | a step-by-step silicon tutorial (build → relax → phonons → bands), the staged wizards, interactive Brillouin zone / k-paths, optics and GW, cluster expansion and convex hulls, slab and nanoparticle builders, effective band structure (unfolding), run monitoring |
| [Packaging Guide](docs/tex/packaging/packaging.pdf) | macOS `.dmg`, Conda package, Debian/Ubuntu `.deb`, the embedded Python environment, and managing core vs optional dependencies |

Both are LaTeX sources under `docs/tex/`; rebuild either with
`pdflatex <name>.tex` (twice, for the table of contents).

---

## Key Features

### 3D Visualization and Aesthetics
- **High-Performance Viewport:** Fully accelerated `QOpenGLWidget` canvas leveraging OpenGL 3.3 core-profile instanced rendering to fluidly display thousands of atoms (spheres) and bonds (cylinders) in a single draw call per mesh type.
- **Representation Modes:** Ball-and-stick, space-filling (CPK), and wireframe. Bond orders (single/double/triple) are assigned manually per atom pair and render as parallel cylinders; automatic perception never guesses multiplicities.
- **Gradient Bond Coloring:** Each bond blends smoothly from one atom's color to the other's (Gouraud-interpolated along the cylinder axis in the instanced shader), toggleable back to the classic half-and-half split.
- **Modular Dock Panels:** Every panel is an independent dock widget that can sit side-by-side, stack as tabs, or float; the layout persists across sessions.
- **Two-Column Workspace:** The default layout is two full-height side columns flanking the 3D viewport — Calango branding, Structure, Volumetric Data and Processes on the left; Representation, Cell/Axes/Vectors and Visual Effects on the right — with a short Results and Remote Access row between them. Every panel is resizable, re-dockable, tabbable and floatable, the arrangement persists across sessions, and View → Reset Layout restores the default.
- **Dynamic Atom Color Mapping:** Four coloring modes selectable from the Display panel, applied to atoms and their bond halves in every representation:
  - *Element (CPK):* the Jmol palette with per-element user overrides.
  - *Coordination Number (CN):* discrete CN values mapped along a continuous gradient.
  - *Generalized Coordination Number (GCN):* continuous GCN values, ideal for highlighting terraces, steps, edges, and vertices of nanoparticles and slabs.
  - *Custom Property:* any per-atom scalar field carried by the structure (charges, force magnitudes, extended-XYZ columns) overlaid on the atomic spheres.
- **Scientific Gradients:** Viridis, Plasma, Turbo, Inferno, Magma, Cividis, Hot, and Afmhot colormaps with an invert-palette toggle and a live legend-range readout; trajectory playback re-colors every frame, so CN/GCN maps stay in sync during animations.
- **Force and Velocity Arrows:** Per-atom force and velocity vectors render as lit 3D arrows (cylinder shaft plus cone head) from each atom center, with a scale slider/spinbox in the Representation panel; data imports automatically from extended-XYZ force columns and trajectory momenta.
- **Lighting and Camera Control:** Turntable camera with double-click reframing, perspective/orthographic projections preserving apparent scale, and a three-light studio default (warm key, cool fill, back/rim) editable in a real-time lighting panel (up to four directional lights, Blinn-Phong).
- **Interaction Modes and Hotkeys:** Six exclusive mouse modes in the frame toolbar — Rotate (R), Pan (T), box Selection (S), Insertion (I), Distance measurement (D), and Angle measurement (A) — plus O for the projection toggle. Insertion mode places atoms of the toolbar-selected element on click and bonds atom pairs on drag; Selection mode supports rubber-band multi-select and Delete/Backspace removal with automatic bond-network rebuilding.
- **Measurement Overlays:** Distance (two atoms, in angstroms) and angle (three atoms, in degrees) readouts drawn directly on the 3D canvas and echoed to the status console.
- **Fixed-Angle Axis Rotations:** X/Y/Z clockwise and counter-clockwise buttons with an editable degree step rotate the scene smoothly about the world axes by exact increments.
- **Depth and Occlusion Effects:** Fog (linear or exponential, blending into the background color), a depth-of-field pass (circle-of-confusion Poisson blur around the focal plane), and real-time screen-space ambient occlusion (MRT G-buffer, hemisphere-kernel sampling, bilateral blur) — all tuned live in the Visual Effects dock's Lighting / Shadow / Fog / Blur / Occlusion tabs.
- **Per-Element Styling:** CPK color overrides via `QColorDialog` and per-element radius scaling, plus global atom-radius and bond-width sliders with synced spin boxes.
- **Scene Overlays (Cell, Axes & Vectors dock):** Unit cell boundaries as thin lines or lit tubes with configurable color and width, optional duplication of atoms and bonds across periodic boundaries, an axes triad showing Cartesian (X, Y, Z) or Bravais lattice vectors (a1, a2, a3), and the per-atom vector overlay with arrowhead and magnitude-threshold filtering.

### Structure Building and File I/O
- **Broad File Format Support:** Read and write any format supported by ASE, including `.cif`, `POSCAR`/`CONTCAR`, `.xyz`/`.extxyz`, Quantum ESPRESSO input/output, CASTEP `.cell`, LAMMPS data and dump files, Gaussian `.gjf`/`.com`, and SHELX `.res`. Per-atom arrays in extended-XYZ files (charges, forces) are imported as color-mappable scalar fields. Extended XYZ is the pre-selected default in every open and save dialog — structures, trajectories, NEB endpoints, dataset imports — because it is the only format in the list that round-trips everything a document carries (cell and pbc, moments and charges, forces and velocities, arbitrary extra columns); every other format is one entry down in the same list.
- **Liquid/Gas Interface Builder:** Open a fluid region of an exact requested thickness on any periodic structure — the cell is grown *or shrunk* so the gap between the slab and its own periodic image is what you asked for, whatever vacuum the input already carried — with a lateral supercell, and pack it with a liquid, a gas, a mixture given as mole fractions, or an ionic solution. Salts are inserted by formula unit and expand into their ions, so `(NH4)2SO4` gives two ammonium and one sulfate and the cell stays neutral by construction; ions are placed before the solvent and count against the density target, so a brine comes out at the density of brine. Hard-sphere rejection packing with an atom-level floor against interlocking and an atom-by-atom clearance against the fixed surface, seeded and reproducible. The generated cell is a starting geometry, not an equilibrated liquid — the wizard, the structure label and the docs all say so.
- **Dislocation Builder:** Insert a Volterra line defect into any periodic crystal by displacing its atoms with a closed-form elastic field — edge, screw, a conservative *glide* dipole (two opposite cores in one glide plane, the trace of a dislocation that has glided a given distance), a non-conservative *climb* dipole (a collapsed vacancy platelet one Burgers vector thick, the one construction that changes the atom count), or the fully anisotropic solution from Stroh's sextic formalism, which is what a mixed edge+screw Burgers vector actually needs. Cubic/hexagonal elastic tensors with room-temperature presets for Cu, Al, α-Fe, Ni, W and Si. Dipoles carry the compensating uniform distortion that keeps the cell periodic — without it the construction looks normal and hides a |b|-sized discontinuity at the boundary — while single dislocations report honestly that they are periodic only along the line. Implemented natively in C++; no external dislocation code is linked or vendored.
- **Solid Interface Builder:** Stacking faults (rigid in-plane shift by a fraction of a lattice vector), coherent twin boundaries (the half above the plane replaced by the mirror of the half below, with the boundary layer shared rather than doubled), bicrystals at an arbitrary twist misorientation, and Voronoi polycrystals — single-phase or multi-phase, drawing each grain's lattice from another open tab. Grains are carved under the *periodic* minimum image so they wrap through the cell faces instead of stopping at them, orientations are uniformly random (Shoemake quaternions, not three Euler angles), and every atom carries its grain and phase index as a scalar field so the tessellation can be seen rather than assumed. Reports what it cannot fix: the two interfaces a periodic cell always has, and how far a non-CSL rotation misses the box periods.
- **Crystal and Nanomaterial Wizard:** Generate supercells, cleave slabs along Miller indices, or build graphene sheets, nanoribbons, carbon nanotubes, and MX2 TMD monolayers.
- **Special Quasirandom Structures (SQS):** Decorate a chosen sublattice of any crystal with a target alloy composition so the Warren-Cowley short-range order of the selected pair shells approaches the ideal random alloy — via icet when installed, otherwise a built-in simulated-annealing backend (numpy plus ASE neighbor lists only).
- **Metallic Nanoparticles:** Wulff-construction equilibrium shapes from per-facet surface-energy ratios (fcc/bcc/sc, target atom count, ase.cluster) or spherical clusters carved from the bulk lattice (fcc/bcc/hcp, cutoff radius).
- **Adsorption & Catalysis:** Automatic detection of high-symmetry adsorption sites on slabs (top, bridge, fcc/hcp threefold hollows classified by the subsurface layer), placement of common or custom adsorbates (OH, O, CO, CHO, H2O, ...) anchored by the chemically sensible atom at a chosen height, and systematic coverage series (0.25-1.00 ML over one site family).
- **Editing Tools:** Add/delete atoms, change chemical species, translate selections, all with snapshot undo/redo; ray-cast picking with click and Ctrl/Cmd+click multi-selection.
- **Atomic Configurations Table:** The Edit Structure dialog shows one coordinate triple with a **Fractional coordinates** toggle (Cartesian by default) that converts in place and relabels the columns — both representations stay editable, and a fractional edit recombines all three components of the row so changing *u* alone does not discard *v* and *w*. Every extended per-atom array the structure carries gets its own column beside the magnetic moments — partial charges, velocities, forces, anything an extended-XYZ file put in `atoms.arrays` — shown read-only, because those are results of a calculation rather than geometry and typing over them would leave a frame whose arrays no longer match anything that was computed.
- **Periodic Table Selector:** Adding an atom or changing a selection's element opens a graphical periodic table (Z = 1 to 118, colored by chemical family) for single-click selection.
- **Interactive Bond Editor:** Toggle automatic bond perception, tune the covalent cutoff multiplier live, and manually add or suppress individual bonds between atom pairs (by index or from the two-atom viewport selection); overrides live on the structure and are undoable. Rules apply to the **whole trajectory**, not just the displayed frame — bonding is a statement about a system's chemistry, and one that held on frame 0 and vanished on frame 1 was an artifact of which frame was on screen. The two rule kinds propagate differently on purpose: an index pair names atoms and is copied to every frame, while an element/distance window names a *geometric condition* and is re-matched against each frame's own coordinates, so a bond that dissociates stops being drawn instead of being frozen in from the start. Bond orders are assigned from the Representation panel with a two-atom selection (Single/Double/Triple) and persist in project files.
- **Unsaved-Changes Guard:** Workspace mutations set a dirty flag; quitting or closing the window with unsaved changes offers Save / Discard / Cancel.
- **Three-Stage Surface Slab Wizard:** (1) pick the orientation with Miller spinboxes or by dragging the in-plane cell vectors on a lattice canvas — snapped vectors recompute the nearest integer (h k l) exactly; (2) choose top/bottom termination layers and thickness on an orthogonal cross-section view; (3) configure top/bottom or centered vacuum with a full 3D preview before insertion.
- **Trajectory Export:** Save multi-frame datasets as extended XYZ, multi-frame XYZ, ASE .traj, or multi-model PDB.
- **Databases and Examples:** Curated benchmark presets (diamond, MoS2 phases, graphene, aromatic molecules) plus Materials Project fetch-by-id through the documented REST API.
- **Deformation and Randomization:** Gaussian or uniform noise on positions and/or cell vectors (affine strain), single-shot or as multi-frame stochastic trajectories.
- **Tabbed Workspace and Timeline:** Every structure or trajectory opens in its own tab with per-document undo history; a playback timeline (0.1 to 120 fps) scrubs MD runs, optimization paths, and generated displacement sets.

### Simulation and Machine-Learning Potentials
- **Four-Stage Setup Wizards:** Every workflow (Single-point, Geometry Optimization, Molecular Dynamics, Phonon, Electronic Structure, Monte Carlo, Cluster Expansion, Effective Bands) runs through one stepper shell — Calculator & Execution Environment, Calculator Settings, the workflow's own task stage, and an editable ASE Script Review that doubles as the run/export launcher. The k-path workflows place their task stage after the engine choice, since a path is only meaningful once the calculator is known.
- **Machine-Learning Interatomic Potentials:** Beyond MACE, first-class calculator blocks for DeepMD-kit, NequIP, Allegro, CHGNet, MatterSim and FAIRChem, each with its own model-source, device and precision controls, and a per-engine Conda environment bound from Preferences.
- **MACE Machine-Learning Potentials:** MACE-MP-0 and MACE-OFF foundation models by size keyword, or a pinned `.model`/`.pt` checkpoint; selectable `float32`/`float64` precision and `cpu`/`cuda`/`mps` device, with a guard against the float64-on-MPS combination PyTorch cannot execute.
- **Full GPAW Parameter Set:** Mode (PW / FD / LCAO), XC functional (PBE, LDA, revPBE, RPBE, PBEsol, HSE06, B3LYP, SCAN, r2SCAN), eigensolver (Davidson, CG, RMM-DIIS, direct), density mixer class with beta / nmaxold / weight, SCF convergence thresholds, smearing and Monkhorst-Pack grid — the identical control set across Single-point, Geometry Optimization, Electronic Structure and Band Unfolding.
- **Effective Band Structure (Band Unfolding):** Popescu-Zunger unfolding of defect, dopant and alloy supercells back onto the primitive Brillouin zone, with automatic supercell-matrix deduction (and a commensurability check that refuses incommensurate cell pairs), spectral weights P_Km(k), and a Gaussian-broadened A(k, E) intensity heatmap in the Results panel.
- **Live System Metrics:** A permanent status bar reports CPU, GPU and memory load plus the ASE thread count, so a long DFT run's resource use is visible without leaving the app.
- **Calculators Supported:** Empirical potentials (EMT, Lennard-Jones), machine-learning interatomic potentials (MACE-MP-0 and MACE-OFF foundation models or custom `.model`/`.pt` checkpoints on CPU, CUDA, or MPS), DFT script templates for Quantum ESPRESSO, VASP, GPAW, and SIESTA, and ORCA quantum chemistry (method/functional, basis set, charge, multiplicity, CPCM/SMD solvation — ASE writes the `.inp` and parses the results).
- **Engine-Native Calculator Pages:** Each DFT backend exposes the parameters it actually has, not a shared approximation of them. **SIESTA has no plane-wave cutoff option** — it is a numerical-atomic-orbital code, and the field used to map silently onto `MeshCutoff`, so raising it to "converge the basis" refined a real-space grid while the basis stayed exactly as small; it is replaced by basis type (`PAO.BasisType`), basis size, `PAO.EnergyShift` and `MeshCutoff`. **Quantum ESPRESSO** gets its dual cutoff (`ecutwfc` + `ecutrho`, with a live note on the effective ratio, since the 4× default is right for norm-conserving and badly soft for ultrasoft/PAW), `input_dft`, occupations with QE's own smearing vocabulary in Ry, and `conv_thr` — the smearing rows hiding for `fixed` and the tetrahedron methods, which take no width.
- **Tasks:** Single-point energies/forces, BFGS geometry optimization, and molecular dynamics across the full ASE ensemble matrix — NVE (velocity Verlet), NVT (Langevin, Andersen, Berendsen, Nose-Hoover chains), and NPT (Berendsen, Nose-Hoover/Parrinello-Rahman) — with thermostat/barostat coupling and pressure controls.
- **Simulated Annealing:** MD with a *moving* setpoint — Linear, Exponential or Logarithmic ramps between an initial and a final temperature, retargeted every step rather than once per sampling interval (a staircase setpoint is a thermal shock per riser). All three laws are endpoint-exact, so the run finishes at the temperature that was asked for whatever the curvature coefficient, and every one degenerates to a straight line as that coefficient goes to zero. Velocities are seeded at the start of the ramp; NVE is withdrawn from the ensemble list, since a schedule needs a thermostat to retarget; the Nose-Hoover chain's fictitious masses move with kT rather than being left tuned for the temperature the run started at. The wizard restates the schedule as the temperatures it actually produces, at 0/25/50/75/100% of the run.
- **Live MD Monitoring:** The Job panel tracks the log, energy convergence, and ionic temperature vs. step, with a dashed thermostat-setpoint reference line for constant-temperature ensembles. An annealing run logs its moving setpoint as its own series instead, plotted under the measured temperature and exported as its own CSV column — "is the system following the schedule?" is the only question such a run asks, and it cannot be answered from the temperature trace alone.
- **Live Trajectory Streaming:** MD runs and geometry relaxations open their trajectory tab at process start and stream freshly computed frames into it in real time (geometry blocks over the subprocess pipe, parsed into structures on arrival); the playback timeline grows as frames land and follows the newest frame unless the user scrubs away.
- **Result Viewers:** Dedicated read-outs on the Results menu for single-point summaries, geometry-optimization convergence, molecular dynamics (T/E/P/V time series, RDF, frame player), Wannier Functions centres and orbitals, and GW quasiparticle energies. Each opens automatically when the matching artifact appears in a finished job directory.
- **Process Manager:** A compact dock between the branding and Structure panels lists every background task (local jobs, remote submissions, band-structure runs) with live status. Jobs of a saved project stage into a managed `.calango_tmp/` folder next to the `.calproj` — checkpoints, trajectory dumps and logs stay linked in the panel for one-click post-processing without recomputation.
- **Electronic Bands / PDOS:** Band-structure workflows along the ASE-suggested (or custom) high-symmetry k-path with three backends — free-electron reference (always available), GPAW (DFT bands plus element/orbital-projected DOS), and a Quantum ESPRESSO scaffold. Results open in a hand-painted two-pane viewer: bands (E − E_F vs. k-distance, Gamma/X/... tick labels, adjustable Fermi reference line) beside the PDOS sharing the energy axis, with per-projection visibility toggles and CSV/.dat export.
- **Band Symmetry (Irreducible Representations):** Optional classification of every band at the high-symmetry points of the k-path by the irrep of its little group. The characters are evaluated from the Kohn-Sham states themselves — the operation is a permutation of the plane-wave coefficients times a phase, exact on any grid and for nonsymmorphic operations — and reduced against a character table computed numerically from the little co-group's own class-sum algebra, not looked up. The symmetry centre is located rather than assumed, so the labels at a zone boundary are convention-free. Symmetry *lines* are classified alongside the points, which is what makes the compatibility relations readable; a projective (nonsymmorphic zone-boundary) representation is detected from its factor system and reported as such instead of being given a label it cannot have. Validated on graphene against Kogan & Nazarov, Phys. Rev. B **85**, 115418 (2012).
- **Orbital-Projected Bands (Fatbands):** Per-band, per-k orbital weights carried alongside the energies, drawn as line thickness, colour, or both. Channels select any set of atoms — element symbol, index list such as `0, 2, 5-8`, or all — against a shell (s, p, d, f) or a single magnetic sub-level (p_z, d_z2, ...); several channels overlay at once on one shared normalization, which is how hybridization becomes visible. A PDOS says which orbitals contribute at an energy; a fatband says which contribute to a *band*, at a *k-point*. Each channel gets its own sequential colormap (Greens, Blues, Reds, Oranges, Greys, Purples — the ColorBrewer maps matplotlib ships) **with the alpha channel ramped so the lowest value is fully transparent rather than white**, which is what lets overlapping projections superimpose instead of the last one painted hiding the rest; the ramp's lightness mirrors on a dark plot background so maximum weight stays visible either way. Weights export to CSV/`.dat` as a tidy table — one row per (k-point, spin, band) with the state's energy beside its weights, which is the shape a plotting script actually consumes.
- **Optical Properties:** The frequency-dependent dielectric function from GPAW's linear-response module, with eps1/eps2, absorption, reflectivity, refractive index (n, k) and the energy-loss function. Selectable point or **linear tetrahedron** Brillouin-zone integration (the latter resolving van Hove features that a Lorentzian broadening would smear), and an x-axis that switches between photon energy (eV) and wavelength (nm).
- **2D Optics (Modules, 2D Materials):** Sheet observables for a monolayer — absorbance A(w), 2D conductivity sigma_2D in e^2/h, and sheet polarizability alpha_2D — obtained by dividing the supercell's arbitrary vacuum thickness back out, so the result is a property of the sheet rather than of the padding. Validated against graphene's universal absorbance, A = pi\*alpha = 2.29%.
- **Nonlinear Optics (GPAW `gpaw.nlopt`):** Second-order response — second-harmonic generation chi^(2)(-2w; w, w) in pm/V, the shift current sigma^(2)(0; w, -w) in A/V^2 (the bulk photovoltaic effect: a DC photocurrent with no junction and no built-in field), and the full linear chi^(1) tensor from the same matrix elements. Both second-order responses are **odd-rank tensors and vanish identically in a centrosymmetric crystal**, so the run tests the cell for an inversion centre *before* converging anything and the viewer repeats the warning — what a finite k-mesh returns there is the residue of an incomplete cancellation, and it looks exactly like a spectrum. The expensive step (`make_nlodata`) runs once and is reused by every component and every response, so a second tensor component costs a band sum rather than another ground state. This is the one response module that converges its **own** ground state: `gpaw.nlopt` asserts point-group symmetry is off and its sums need a converged empty manifold, neither of which a general baseline has.
- **GW Quasiparticle Corrections:** One-shot G0W0 through two engines — GPAW (`gpaw.response.g0w0`) correcting a `.gpw`, or Yambo (`p2y` + `yambo`) correcting a Quantum ESPRESSO `.save` — with plasmon-pole or real-axis frequency treatment. Both write the same schema, and the viewer reports the DFT gap, the quasiparticle gap and the renormalization between them, flagging the near-zero or negative values that indicate an unconverged run.
- **Baseline Inheritance:** Optics, 2D Optics, GW, ELF and Wannier Functions all load a completed Single-Point ground state and evaluate at fixed density rather than re-converging their own — so a spectrum is always attributable to a specific, inspected SCF solution, and the calculator stage is dropped from those wizards entirely Nonlinear Optics is the deliberate exception, and says why: its method requires a ground state no ordinary baseline provides.
- **Interactive Script Editor:** Every GUI configuration is synthesized into a standalone, syntax-highlighted Python/ASE script that remains fully editable — manual edits pause form synchronization until an explicit regenerate. Scripts run unmodified on clusters.
- **Subprocess Isolation:** Jobs run as separate processes in per-job directories with live log capture, progress markers, an energy-convergence plot, and automatic trajectory loading on completion. A conda-environment selector routes jobs to any interpreter.
- **MLIP Dataset Manager:** Assembles machine-learning training datasets from heterogeneous multi-frame trajectories: deterministic train/validation/test splits (percentages plus seed), Query-by-Committee sub-datasets (independent splits or bootstrap resampling), and export to Extended XYZ (MACE-ready) or an ASE SQLite database with energies, forces and stresses preserved.

### Vibrational Analysis: Phonon Builder
- **Finite Displacements:** Applies plus/minus displacements (default 0.01 angstrom) along x, y and z to every atom of the supercell — 6N + 1 configurations. **Spglib symmetry reduction** (through phonopy) displaces only along the symmetry-irreducible directions and rebuilds the full force-constant matrix by symmetry, cutting the count by an order of magnitude for a high-symmetry cell; the script falls back to the full 6N set when phonopy is absent. **Residual-force removal** subtracts the forces on the un-displaced geometry, which is what keeps the acoustic branches at zero when the relaxation stopped at a finite fmax.
- **Two Workflows:**
  - *Generate displaced structures:* the displacement set opens as a trajectory tab, ready for inspection or export to external DFT codes.
  - *Run calculation:* a generated ASE script computes forces with EMT, Lennard-Jones, or MACE, assembles force constants and the dynamical matrix (acoustic sum rule enforced), and reports vibrational frequencies.
- **Outputs:** Gamma-point mode frequencies in the job console, phonon dispersion along the ASE-suggested Brillouin-zone path (`phonon_bands.csv`), and phonon density of states (`phonon_dos.csv`). Isolated molecules run through `ase.vibrations` instead, producing a frequency summary and per-mode animation trajectories that Calango opens directly.

### Analysis and Reciprocal Space
- **Partial Charges (GPAW / VASP / Quantum ESPRESSO):** Bader, Voronoi and Hirshfeld partitioning, all native and all running on one standardized density grid — only the *acquisition* changes per engine: GPAW's all-electron density from the `.gpw`, VASP's `AECCAR0`+`AECCAR2` (falling back to `CHGCAR` with a warning, because Bader on the pseudo-valence density alone follows the wrong topology and returns systematically small charges), or a `pp.x` export for QE. A scope selector partitions the displayed frame or every frame of the trajectory — and the trajectory mode is not a loop over one density: it requires one converged density *per frame* and says so when the source produced only one. Results are reported as net charge, range and per-element means (a net charge above 0.05 e on a neutral cell is flagged as lost density), and can be written back onto the document as `initial_charges` so an `.extxyz` save carries them as a column.
- **Born Effective Charges by DFPT:** VASP (`LEPSILON`) and Quantum ESPRESSO (`ph.x` with `epsil`) compute Z\* in a **single linear-response run** rather than 6N displaced SCFs — an analytic derivative, so there is no displacement amplitude to trade against SCF noise and no linearity assumption left to check; the dielectric tensor comes free with it. All three engines write one `born_charges.json` schema, so the viewer and the phonon LO-TO block need no second reader.
- **Raman and IR Spectra on three engines:** One physics core — the mass-weighted diagonalization, the Z\* and dalpha/du contractions, the Stokes prefactor — behind three very different front ends, all writing one `raman_ir.json`. GPAW displaces the ions throughout (6N force evaluations plus 6N dielectric runs) and inherits Z\* from a Born Charges job; VASP gets the force constants, every Z\* and eps_inf from a **single** `IBRION=8` + `LEPSILON` run and needs the 6N displaced sweep only for the Raman half; Quantum ESPRESSO gets all of it — the Raman tensor included, as an analytic **third-order** response — from one `ph.x` run. The parsers are pinned against realistic OUTCAR and `.dyn` fragments, because every failure mode there is silent: VASP prints dF/du (minus the Hessian) and three different dielectric tensors, and QE stores force constants in Ry/bohr^2 unweighted by the masses.
- **Charged Defects and CDD across engines:** Formation-energy diagrams read energies and band edges from `vasprun.xml` or `pw.x` output and set the charge state through `NELECT` (an *absolute* electron count, so q=+1 is one electron fewer) or `tot_charge`. The FNV correction is delegated to pymatgen where it is available and degrades to clearly-labelled **uncorrected** energies where it is not — uncorrected is a legitimate mode for a supercell-convergence study, whereas an unvalidated hand-rolled correction is a plausible number of the right magnitude and the wrong value. Charge-density differences pin the fragment FFT grid explicitly to the parent's (`NGXF…`, `nr1…`), because both codes choose that grid from the cell *contents* and a fragment can otherwise land on a different one — two densities on different grids cannot be subtracted at all.
- **Coordination Analysis (CN/GCN):** Per-atom coordination numbers from covalent-radius scaling or a fixed cutoff, with exact periodic-image enumeration (correct even for primitive cells), and generalized coordination numbers following Calle-Vallejo and co-workers with a configurable bulk reference (12 fcc, 8 bcc, 4 diamond, or auto). Results appear in a sortable table with summary statistics and can be pushed onto the viewport colors in one click.
- **Radial Distribution Function g(r):** Total and element-pair partial RDFs with exact periodic-image evaluation (valid beyond L/2, triclinic-safe), computed on a worker thread, plotted interactively, and exportable as `.csv` or `.dat` data files — including frame-averaged RDFs over a selectable trajectory range (start, end, stride).
- **Bond Length and Angle Distributions:** Histogram statistics of pair distances and three-body angles within a configurable cutoff, with species filters, periodic-image handling, and CSV export.
- **Static Structure Factor S(q):** Computed from the (frame-averaged) pair distribution via a Lorch-windowed Fourier transform, with configurable q range and CSV export.
- **XRD Simulation:** Debye-equation powder diffraction patterns (via ASE) with Cu/Co/Mo/Cr K-alpha or custom wavelengths, supercell sharpening for crystals, and export of both the 2-theta intensity curve and the detected peak list with d-spacings.
- **Warren-Cowley Analysis:** Short-range order parameters alpha_ij for every ordered species pair of a multicomponent alloy, evaluated on one or two coordination shells with exact periodic-image enumeration, displayed as a matrix and exportable as CSV.
- **Local Entropy Analysis:** The per-atom pair-entropy fingerprint of Piaggi and Parrinello (units of k_B), stored as a color-mappable scalar field and plotted as a distribution histogram — crystalline environments sit lowest, disordered ones higher.
- **Raman Modes:** Gamma-point factor-group analysis of the vibrational modes. The character table of the crystal's point group is computed numerically from the class-sum algebra (no hardcoded tables), the mechanical representation is reduced into Mulliken-labeled irreps, and each optical mode set is classified as Raman-active, IR-active, or silent.
- **Magnetic Space Group:** Determination of which of the 1651 magnetic space groups a structure realizes, from its coordinates together with its magnetic moments (converged `magmoms`, seeded `initial_magmoms`, or typed straight into the editable moment table). Reports the BNS label `S.L`, the Belov-Neronova-Smirnova type I-IV, the Opechowski-Guccione label, the parent space group, and — beside it — the crystallographic space group the same structure would have with the moments ignored, so what the magnetic order broke is visible rather than inferred. Type IV names its anti-translation, the vector by which the magnetic cell exceeds the crystallographic one. Collinear and non-collinear (axial-vector) moments, with a moment tolerance separate from the positional one. Follows Watanabe, Po & Vishwanath, Sci. Adv. **4**, eaat8685 (2018).
- **Volumetric Data:** Reads Gaussian .cube, VASP CHGCAR/LOCPOT/PARCHG/ELFCAR and .xsf grids; live-isovalue isosurfaces (marching-cubes family, tetrahedral variant, gradient normals), color-mapped slice planes (axis-aligned or custom normal), and dual-field electrostatic potential maps where Field A shapes the surface and Field B colors it. Exports OBJ meshes and CSV slices.
- **Crystallographic Info:** The Structure panel reports a, b, c, alpha, beta, gamma, volume, periodicity, and the spglib-detected space group, point group, and crystal system.
- **Brillouin Zone Viewer:** Wigner-Seitz cells of the reciprocal lattice with high-symmetry labels (Gamma, X, W, K, L, U, ...) from ASE's Bravais-lattice detection, with directional arrows drawn along the k-path and high-resolution PNG/SVG figure export.
- **k-Path Builder:** Click-to-build k-paths with discontinuous sections (Gamma to X | M to R); a single "Export k-Path" action offers VASP `KPOINTS` (line mode), Quantum ESPRESSO `K_POINTS crystal_b`, CASTEP `SPECTRAL_KPOINT_PATH`, SIESTA `BandLines`, and standalone ASE/Python scripts.

### Remote HPC Execution (SSH/SFTP)
- **Cluster Connection:** The Remote Access panel manages host, port, user, and authentication (SSH key with optional passphrase, agent/default keys, or password — credentials stay in memory and travel over stdin, never argv or disk).
- **One-Click Submission:** "New Remote Calculation" stages the same run.py plus structure files as local jobs, generates a SLURM (#SBATCH), PBS (#PBS), or SGE (#$) wrapper from the panel's resource settings (partition/queue, tasks, walltime, environment prologue), uploads everything over SFTP, and submits with sbatch/qsub.
- **Live Monitoring:** Queue state polling (squeue/qstat) with incremental streaming of the remote stdout/stderr into the panel console, job cancellation (scancel/qdel), and automatic download of results (structures, trajectories, logs, CSV metrics) when the job leaves the queue — trajectories open directly in a new tab.
- **Out-of-Process SSH:** All network I/O runs in a paramiko helper subprocess driven over JSON, mirroring the local job runner's isolation philosophy: the GUI never blocks, and a wedged connection dies with the helper.

### Publication Output
- **Static Images:** High-resolution off-screen capture (8x MSAA, up to 8192 px) as PNG (transparent or solid) or JPEG, with 720p/1080p/4K resolution presets.
- **Animations:** Turntable or trajectory export to animated GIF (with transparency) or MP4 (H.264) up to 4K, with resolution presets, framerate, and background options.
- **Ray Tracing:** POV-Ray and Tachyon scene export reproducing the active viewport (camera, lights, styling, multi-bonds, cell), with in-app renderer invocation and log capture.

---

## Technology Stack

| Layer | Technology |
| --- | --- |
| Language | C++20 (GCC 11+, Clang 13+, MSVC 2022+) |
| GUI toolkit | Qt 6.4+ (Widgets, OpenGLWidgets, Concurrent) |
| Rendering | OpenGL 3.3 core profile, GLSL 330, instanced draw calls |
| Embedded scripting | CPython 3.9+ via pybind11 (2.12+, fetched automatically if absent) |
| Atomistic engine | ASE (Atomic Simulation Environment) |
| Symmetry | spglib (space groups, factor-group analysis) |
| Remote execution | paramiko (SSH/SFTP helper subprocess) |
| ML potentials | MACE (mace-torch), optional |
| Build system | CMake 3.21+, CPack installers (macOS DMG, Linux DEB) |

## Architecture Philosophy

Calango enforces a strict Model-View-Controller (MVC) split to ensure stability, performance, and easy extension:

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

1. **`core/` (Model):** Physics and geometry — structures, cells, bond perception, coordination and RDF analysis, and the ASE/phonon script generators. Pure C++ with zero GUI or Python dependencies.
2. **`python_bridge/` (Stateless Translator):** Converts between `core::Structure` and `ase.Atoms` via pybind11, including per-atom scalar arrays. Python types never escape this boundary.
3. **`render/` (OpenGL View):** Builds instanced GPU buffers from model data, including the scalar-to-gradient color mapping (Viridis/Plasma/Turbo). Never mutates state.
4. **`jobs/` (Subprocess Engine):** Spawns simulations with `QProcess` and parses stdout markers (`CALANGO_PROGRESS`, `CALANGO_ENERGY`, `CALANGO_RESULT`) for real-time GUI updates.
5. **`gui/` (Controller and Widgets):** Dispatches user actions and coordinates the views.

---

## Compilation and Setup

### Prerequisites
- A C++20 compliant compiler (GCC 11+, Clang 13+, MSVC 2022+).
- **CMake** 3.21 or newer.
- **Qt 6.4+** with the `Widgets`, `OpenGLWidgets`, `Concurrent`, and `Svg` modules.
- **Python 3.9+** with development headers (tested up to 3.14).

### Step-by-Step Build Guide

1. **Set up the embedded Python environment:**
   ```bash
   # Create a virtual environment in the project root
   python3 -m venv .venv

   # Install the scientific, export and remote-access packages
   .venv/bin/pip install ase numpy spglib pillow imageio imageio-ffmpeg paramiko

   # (Optional) Install MACE for machine-learning potentials
   .venv/bin/pip install mace-torch
   ```

2. **Configure with CMake:**
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
         -DPython3_EXECUTABLE=$PWD/.venv/bin/python \
         -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<platform>
   ```
   On macOS with Homebrew Qt, `-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt`.

3. **Build and run:**
   ```bash
   cmake --build build -j

   # Run with a sample structure
   ./build/calango examples/diamond.vasp
   ```

### Integration Tests

Configure with `-DCALANGO_BUILD_TESTS=ON` and run `ctest --test-dir build`.
They fall into three groups:

- **Pure C++ / physics** — SQS generation and its Warren-Cowley order
  parameter, cluster expansion, phonon thermodynamics against the Einstein
  oscillator, ice-rule satisfaction, polymer geometry, band unfolding, the
  magnetic space group (one cell, three magnetic configurations, three BNS
  types — the crystallography is identical in all three, so nothing but the
  moments can produce the difference), and interface packing (region height,
  composition, charge neutrality and every exclusion distance re-derived from
  the returned coordinates, because a packer that fused two molecules or
  dropped an ion produces a cell that looks entirely normal until a
  calculation is run on it). Dislocations are checked by the one property
  that defines them — carry a circuit around the line and the displacement
  fails to close by exactly **b** — with the anisotropic Stroh solver pinned
  against the isotropic closed form it must reduce to, a test a sextic
  eigenvalue solve with a sign error in it cannot pass by accident. Solid
  interfaces are checked structurally: atom counts against what the geometry
  demands, grains that must wrap through the cell faces rather than stop at
  them, and closest approach under the minimum image, since every way these
  constructions go wrong looks fine in a viewer.
- **Generated-script checks** — the emitted Python is asserted on and dumped
  for `python -m py_compile`; the Yambo `.qp` parser, the 2D-optics
  observables, the VASP/QE Raman-IR parsers and the nonlinear-optics unit
  conversions are extracted from a freshly generated script *by AST* and
  exercised directly, so the code under test is the code that ships. The
  parser tests are driven with realistic OUTCAR and `.dyn` fragments whose
  right answers are known by construction; the nonlinear-optics test also
  checks the emitted `gpaw.nlopt` call surface against the keyword names GPAW
  actually declares, since a misspelling there surfaces as a `TypeError` only
  after a ground state has been converged.
- **Live engine benchmarks** — real GPAW runs that self-skip when the
  response stack is unavailable: silicon and diamond optics, ELF, Wannier
  interpolation, G0W0 on silicon, graphene tetrahedron integration checked
  against the universal absorbance (the slowest test, ~4 min), and the
  graphene band-symmetry classification checked against the published irrep
  table of Kogan & Nazarov, PRB **85**, 115418 (2012) — with the π manifold
  identified by its p_z fatband weight rather than by its energy, so the
  symmetry labels and the orbital projections are validated together.

GUI tests run under the offscreen platform and need no display.

### Installers

CPack builds native installers. One-shot scripts:

```bash
bash packaging/macos/create_dmg.sh              # drag-and-drop macOS .dmg
bash packaging/conda/create_conda_osx-arm64.sh  # Conda package
bash packaging/linux/build_deb.sh               # Debian/Ubuntu .deb
```

The `.deb` registers a desktop entry, the `application/x-calango-project`
MIME type for `.calproj`, and icons. Full instructions — including bundling a
relocatable Python interpreter so the app ships self-contained — are in the
[Packaging Guide](docs/tex/packaging/packaging.pdf).

---

## Python Environment Resolution

Embedded Python runtimes do not automatically inherit shell virtual environments, so Calango resolves its interpreter in this priority order:

1. **`CALANGO_PYTHON`** environment variable — explicit path to a Python executable (highest priority).
2. **`VIRTUAL_ENV`** environment variable — a currently activated virtual environment.
3. **Bundled interpreter** — a Python shipped by the installers (`Contents/Resources/python` in the macOS bundle, `lib/calango/python` on Linux).
4. **Configure-time interpreter** — the one CMake found when the build was configured.

Simulation jobs are independent of the embedded interpreter: the calculator and phonon dialogs include an execution-environment selector (conda environment folder or interpreter path) that is persisted across sessions.

### Environment File (.env)

At launch Calango reads an environment file — `~/.env` by default, overridable in Edit, Preferences — and exports `MP_API_KEY` (the Materials Project API key) into the process environment. A key already present in the shell environment takes precedence at startup; the Preferences dialog can reload the file unconditionally.

### Diagnostics
```bash
# Print the resolved interpreter, Python version, and ASE availability
./build/calango --probe-python

# Force a specific interpreter
export CALANGO_PYTHON=/path/to/.venv/bin/python
./build/calango
```

---

## Repository Layout

- `CMakeLists.txt` — global build configuration, installers (CPack), and tests.
- `assets/`
  - `shaders/` — GLSL shaders (compiled in as Qt resources).
  - `icons/` — the RemixIcon SVG set, tinted at runtime per theme (embedded as Qt resources).
  - `calango/` — brand assets (application icon, logos) and helper scripts; all embedded as Qt resources.
  - `remote/` — the paramiko SSH/SFTP helper (embedded as a Qt resource).
- `examples/` — benchmark structure files for the Database browser.
- `docs/tex/` — LaTeX sources and built PDFs for the User Guide and the Packaging Guide.
- `packaging/` — installer support files (macOS scripts, Linux desktop/MIME assets).
- `src/`
  - `main.cpp` — application entry point and CLI (`--probe-python`).
  - `core/` — data model, geometry, analysis engines (coordination, RDF, Warren-Cowley, local entropy), script generators.
  - `python_bridge/` — embedded interpreter control, ASE interop, SQS builder, Raman analysis, Materials Project client.
  - `render/` — instanced OpenGL renderer, colormaps, ray-trace exporter.
  - `jobs/` — subprocess job runner and progress parsing.
  - `remote/` — SSH/SFTP client driving the paramiko helper.
  - `gui/` — main window, viewport, docks, and dialogs.
- `tests/` — GUI-free integration tests (`-DCALANGO_BUILD_TESTS=ON`).

## License

Calango is released under the MIT License. See `LICENSE` for details.
