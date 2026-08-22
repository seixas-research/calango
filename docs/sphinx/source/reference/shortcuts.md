# Shortcuts and menu map

Everything on this page is generated from the current sources. On macOS,
{kbd}`Ctrl` in the tables below is {kbd}`Cmd`.

---

## Viewport keys

Single letters, no modifier, active while the 3D viewport has focus. **They
yield to text fields while you are typing** — a plain-letter shortcut never
steals a character from a search box.

| Key | Action |
|---|---|
| {kbd}`R` | Rotation mode — drag orbits the camera |
| {kbd}`T` | Translation mode — drag pans the scene |
| {kbd}`S` | Selection mode — drag a box to select atoms |
| {kbd}`I` | Insertion mode — click empty space to add the active element; drag atom-to-atom to bond |
| {kbd}`D` | Distance measurement — click two atoms |
| {kbd}`A` | Angle measurement — click three atoms, vertex second |
| {kbd}`O` | Toggle orthographic / perspective projection |
| {kbd}`F` | Reset camera — restore the saved default point of view, or frame the structure |
| {kbd}`Delete` / {kbd}`Backspace` | Delete the selection (Selection mode) |

The XY / XZ / YZ plane alignments, the fixed-angle axis rotations (X±, Y±,
Z± by an editable step), the field-of-view popup, and the display toggles
(element symbols, atomic indices, hydrogens, gradient bonds) are
**toolbar buttons on the viewport header with no key bindings** — the
toolbar is documented in {doc}`/viewport`.

---

## Application shortcuts

| Shortcut | Action | Menu |
|---|---|---|
| {kbd}`Ctrl+N` | New workspace | File |
| {kbd}`Ctrl+O` | Open structure | File → Open |
| {kbd}`Ctrl+T` | Open trajectory | File → Open |
| {kbd}`Ctrl+Shift+S` | Save structure as | File → Save |
| {kbd}`Ctrl+Shift+T` | Save trajectory as | File → Save |
| {kbd}`Ctrl+E` | Export image | File → Import / Export |
| {kbd}`Ctrl+Shift+O` | Open project | File → Project Workspace |
| {kbd}`Ctrl+S` | Save project | File → Project Workspace |
| {kbd}`Ctrl+W` | Close tab | File |
| {kbd}`Ctrl+Q` | Quit | File |
| {kbd}`Ctrl+Z` | Undo | Edit |
| {kbd}`Ctrl+Shift+Z` (or {kbd}`Ctrl+Y`) | Redo — the platform's standard Redo binding | Edit |
| {kbd}`Ctrl+Shift+A` | Add atom | Edit |
| {kbd}`Delete` | Delete selected atoms | Edit |
| {kbd}`Ctrl+B` | Bond editor | Edit |
| {kbd}`Ctrl+P` | Preferences — application-wide, works from any window | Edit |
| {kbd}`Ctrl+R` | Single-point calculation | Simulation |

---

## Mouse

| Gesture | Action |
|---|---|
| Left drag | Depends on the interaction mode (R/T/S/I/D/A) |
| Middle drag | Pan, in any mode |
| {kbd}`Shift`+left drag | Pan, in any mode |
| Wheel | Zoom |
| Left click | Pick atom |
| {kbd}`Ctrl`/{kbd}`Cmd`+click | Toggle atom in the selection |
| Double click | Frame the structure |

---

## Menu map

The nine menus, with every item, as built in v26.8.

**{guilabel}`File`** — New Workspace · Open (Structure…, Trajectory…, Open
Recent) · Save (Structure As…, Trajectory As…) · Import / Export (Export
Image…, Export Animation…, Export to Alembic…, Ray-Traced Render…) ·
Project Workspace (Open Project…, Save Project, Save Project As…) · Close
Tab · Quit

**{guilabel}`Edit`** — Undo · Redo · Add Atom… · Selection (Change Element
of Selection…, Translate Selection…) · Delete Selected Atoms · Bond
Editor… · Preferences…

**{guilabel}`View`** — one toggle per dock, in layout order: Calango
(branding) · Structure · Volumetric Data · Additional Overlays · Processes
· Representation · Visual Effects · Spatial References · Orchestration · Remote
Access · Results — then Status Bar and Reset Layout. Camera alignment and
projection live on the viewport toolbar, not here.

**{guilabel}`Build`** — From Database… · Nanoparticle Builder… · Surface
Slab… · Nanomaterials… · Add adsorbate… · Macromolecules… · Water & Ice… ·
Liquid / Gas Interface… · Dislocation… · Solid Interface… · Brillouin Zone
Builder…

**{guilabel}`Simulation`** — Single-point Calculation… ({kbd}`Ctrl+R`) ·
Geometry Optimization… · Molecular Dynamics… · Phonon… · Monte Carlo
Simulation… · Random Noise Setup… · Nudged Elastic Band (NEB)…

**{guilabel}`Electronics`** — Electronic Structure… · Effective Bands
(Unfolding)… · Optics… · Nonlinear Optics… · GW Calculations… · Wannier
Functions… · Wannier Interpolation… · Fermi Surface… · Topological
Invariants… · Charged defects… · Hubbard Parameter Calculation… · Born
Effective Charges… · X-ray Absorption Spectroscopy (XAS)… · Raman and IR
Spectroscopy…

**{guilabel}`Analysis`** — Symmetry, Raman & IR Activity… · Magnetic Space
Group… · Structure Factor S(q)… · X-Ray Diffraction (XRD)… · Radial
Distribution Function… · Bond Length / Angle Distributions… · Coordination
Numbers (CN / GCN)… · Local Entropy Analysis… · Partial Charge Analysis… ·
Velocity Autocorrelation Function (VACF)… · Charge Density Difference
(CDD)… · Adsorption & Catalysis…

**{guilabel}`Modules`** — MLIP (Trainer…, Dataset Manager…) · 2D Materials
(2D Bands…, 2D Optics…, 2D Workfunction…, Charged Defects in 2D Materials…)
· Graphene Oxide (Graphene Oxide Builder…, GO-MDMC…, GO Functional Group
Analysis…, GO Pair Correlation…) · Alloys (Cluster Expansion Builder…,
Cluster Expansion Calculation…, Special Quasirandom Structure (SQS)…,
Warren-Cowley Analysis…) · Parameters Convergence (Plane-wave Cutoff
Convergence…, K-points Convergence…)

**{guilabel}`Help`** — Documentation · GitHub Repository · About Calango

:::{note}
Items that moved, relative to older documentation: **Warren–Cowley** is
under {menuselection}`Modules --> Alloys` (not Analysis); the **Brillouin
Zone Builder** is under {guilabel}`Build` (not Analysis); **Volumetric
Data** is a dock toggled from {guilabel}`View` (not an Analysis dialog);
**Supercell** creation moved to the Structure panel's action row; the
standalone *New Remote Calculation* entry is gone — remote execution is
chosen inside each wizard and monitored in the HPC dock; and the
*Results* menu is gone — viewers open from the Processes panel entry of the
run that produced them.
:::
