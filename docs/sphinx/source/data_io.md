# Data input and output

Calango reads and writes structures through the embedded Python interpreter, delegating every format to `ase.io.read` / `ase.io.write` — **whatever ASE can parse, Calango can open**. The one exception is PDBx/mmCIF, which is handled natively in C++ and detected by content sniffing before ASE ever sees the file. Structures from online databases (Materials Project, COD, PubChem, …) arrive through a separate route — see {doc}`/builders/database`.

% TODO screenshot: the Open Structure dialog with the format filter dropdown expanded, Extended XYZ pre-selected at the top
```{figure} /_static/img/data_open_dialog.png
:alt: Open Structure dialog showing the file-format filter list with Extended XYZ pre-selected
:width: 92%
:figclass: screenshot

The structure open dialog. Extended XYZ leads every filter list and is therefore the pre-selected default.
```

---

## Supported formats

| Format | Extensions | Read | Write | Notes |
|---|---|---|---|---|
| Extended XYZ | `.extxyz`, `.xyz` | ✓ | ✓ | The default everywhere; full round-trip (see below) |
| XYZ (plain) | `.xyz` | ✓ | ✓ | Coordinates only; multi-frame on trajectory export |
| CIF | `.cif` | ✓ | ✓ | Crystallographic Information File |
| PDBx / mmCIF | `.cif` | ✓ | ✓ | Native C++ reader/writer, auto-detected by content |
| Protein Data Bank | `.pdb` | ✓ | ✓* | *Write via trajectory export (multi-model) only |
| VASP | `POSCAR`, `CONTCAR`, `.vasp` | ✓ | ✓ | Writes POSCAR format |
| ASE trajectory | `.traj` | ✓ | ✓* | *Write via trajectory export only |
| Quantum ESPRESSO | `.in`, `.pwi`, `.pwo`, `.out` | ✓ | ✓* | *Writes input (`espresso-in`) only; `.pwo`/`.out` are read-only |
| CASTEP | `.cell` | ✓ | ✓ | |
| LAMMPS data | `.data` | ✓ | ✓ | |
| LAMMPS dump | `.dump`, `.lammpstrj` | ✓ | — | Read-only (`lammps-dump-text`) |
| Gaussian input | `.gjf`, `.com` | ✓ | ✓ | |
| SHELX | `.res` | ✓ | ✓ | |

For extensions ASE cannot sniff on its own, Calango passes an explicit format hint — `.data` → `lammps-data`, `.dump`/`.lammpstrj` → `lammps-dump-text`, `.pwi`/`.in` → `espresso-in`, `.pwo` → `espresso-out`, `.gjf`/`.com` → `gaussian-in`, `.cell` → `castep-cell`, `.res` → `res`. Everything else, including `.out`, is left to ASE's content detection.

:::{note}
The Quantum ESPRESSO input writer automatically emits `<El>.upf` pseudopotential placeholders for every species, so the exported `.pwi` file is syntactically complete — swap in your actual pseudopotential names before running.
:::

---

## Why Extended XYZ is the default

Extended XYZ is *deliberately* the first — and therefore the pre-selected — filter in every open and save dialog, and `structure.extxyz` is the suggested name whenever a fresh file name is needed. The reason is fidelity: **Extended XYZ is the only plain-text format that round-trips everything Calango knows about a structure** —

- the cell and per-axis periodic-boundary flags,
- per-atom magnetic moments and charges,
- forces and velocities,
- arbitrary extra per-atom columns.

On write, scalar and vector fields become named columns (`forces:R:3`, `magmoms:R:1`, …). Velocities are written twice — as ASE-native momenta *and* as a plain `velocities` column — so OVITO, VMD, and i-PI read them too. On read, Calango scans not only `atoms.arrays` but also the attached calculator results, because ASE's extxyz reader parks `forces`, `energies`, and `magmoms` in a `SinglePointCalculator` — exactly the columns a finished calculation produces would otherwise be dropped.

If you type a bare file name in a save dialog, the selected filter's primary extension is appended automatically; bare-name filters such as VASP's `POSCAR` are left untouched.

### Per-atom columns become color-mappable fields

Every numeric per-atom column in an extxyz file is imported as a named field on the structure: 1-D columns become scalar fields, and $N \times 3$ columns become vector fields *plus* a derived magnitude scalar named `|name|`. Scalar fields appear in the {guilabel}`Custom property` color mode of the Representation panel ({doc}`/representation`); vector fields such as `forces` additionally drive arrow overlays. This is the simplest route for visualizing per-atom data computed elsewhere — write it as an extxyz column and open the file. Non-numeric columns are skipped silently.

---

## Opening structures and trajectories

| Action | Menu | Shortcut |
|---|---|---|
| Open structure(s) | {menuselection}`File --> Open --> Structure...` | {kbd}`Ctrl+O` |
| Open trajectory | {menuselection}`File --> Open --> Trajectory...` | {kbd}`Ctrl+T` |
| Recent files | {menuselection}`File --> Open --> Open Recent` | — |

Both entries call the same loader, which always reads *every* frame (`ase.io.read(path, index=":")`). A one-frame file opens as a structure tab; a multi-frame file opens as a trajectory with the playback timeline. **The two menu entries differ only in their filter lists** — the structure dialog supports multi-select, so you can open several files into separate tabs at once. {guilabel}`Open Recent` keeps the last 10 files, with Alt-number mnemonics.

---

## Saving

{menuselection}`File --> Save --> Structure As...` ({kbd}`Ctrl+Shift+S`) writes the current frame in any of the ten save formats in the table above, suggesting the document's own name with an `.extxyz` suffix.

{menuselection}`File --> Save --> Trajectory As...` ({kbd}`Ctrl+Shift+T`) exports all frames in one of four multi-frame formats:

| Filter | ASE format |
|---|---|
| Extended XYZ trajectory (`*.extxyz`) | `extxyz` |
| XYZ multi-frame (`*.xyz`) | `xyz` |
| ASE trajectory (`*.traj`) | `traj` |
| PDB multi-model (`*.pdb`) | `proteindatabank` |

Trajectory export requires at least 2 frames — a single-frame tab produces the message *"The current tab has no multi-frame trajectory."* The suggested name is `trajectory.extxyz`.

Image, animation, Alembic, and ray-traced exports live under {menuselection}`File --> Import / Export` and are covered in {doc}`/output`.

---

## Volumetric grids

Volumetric data (charge densities, ELF, wavefunctions) is loaded through the Volumetric Data dock, which accepts `.cube`, `.xsf`, and VASP `CHGCAR*` / `LOCPOT*` / `PARCHG*` / `ELFCAR*` files; `.cube` files produced by Calango's own calculations are registered automatically with friendly labels. See {doc}`/analysis/volumetric`.

---

## Project files (.calproj)

A Calango project is **one self-describing JSON document** — not an archive — written atomically so a crash mid-save never corrupts an existing file. Restoring a project needs no Python round-trip and no sidecar files. A `.calproj` stores:

- **Every open tab** — single structures verbatim, trajectories with every frame embedded plus the current frame index; per-atom scalar/vector fields (including analysis overlays such as CN, GCN, and charges) and manual bond overrides ride along.
- **The active tab** and the viewport's color mode, gradient, custom field, and background.
- **Job context** — the last job directory, the console log, and the recorded energy / temperature / max-force / pressure metric series.

Window geometry and dock layouts are *not* part of the project — they persist separately in application settings.

| Action | Menu | Shortcut |
|---|---|---|
| Open project | {menuselection}`File --> Project Workspace --> Open Project...` | {kbd}`Ctrl+Shift+O` |
| Save project | {menuselection}`File --> Project Workspace --> Save Project` | {kbd}`Ctrl+S` |
| Save project as | {menuselection}`File --> Project Workspace --> Save Project As...` | — |

`.calproj` files also open through *any* other route — the command line, a file-manager double-click (the installers register the MIME type), even the structure open dialog — because the loader intercepts the extension before format detection. Opening a project replaces the current workspace after a confirmation, and is refused while a job is running. Files written by a newer Calango are refused with an explicit *update Calango* error rather than a partial load (the current format version is 1).

:::{admonition} Projects stay self-contained
:class: caution
Jobs launched from a saved project are staged in a `.calango_tmp/` folder beside the `.calproj` file (one `proc_<id>` directory per job, never clobbered on reopen), so the project and its job artifacts move together when you copy the folder. Unsaved sessions stage jobs in the simulations directory from Preferences instead.
:::

% TODO screenshot: a saved project folder in the file manager showing workspace.calproj next to its .calango_tmp job directory
```{figure} /_static/img/data_calproj_layout.png
:alt: File manager view of a project folder containing workspace.calproj and the .calango_tmp job staging directory
:width: 92%
:figclass: screenshot

A self-contained project: the `.calproj` JSON document and its `.calango_tmp/` job staging folder travel as a unit.
```
