# File formats

Three families: structure/trajectory files (delegated to `ase.io`),
volumetric grids, and Calango's own artifacts — the `.calproj` project
document and the files a job directory accumulates. The narrative version
of the I/O story, including why extended XYZ is the default everywhere, is
{doc}`/data_io`; this page is the lookup table.

---

## Structure and trajectory formats

Everything goes through `ase.io.read` / `ase.io.write` — **whatever ASE can
parse, Calango can open** — except PDBx/mmCIF, which has a native C++
reader/writer detected by content sniffing.

| Format | Extensions | Read | Write |
|---|---|---|---|
| Extended XYZ | `.extxyz`, `.xyz` | ✓ | ✓ (full round-trip: cell, PBC flags, magmoms, charges, forces, velocities, custom columns) |
| XYZ (plain) | `.xyz` | ✓ | ✓ |
| CIF | `.cif` | ✓ | ✓ |
| PDBx / mmCIF | `.cif` | ✓ | ✓ (native C++) |
| PDB | `.pdb` | ✓ | trajectory export only (multi-model) |
| VASP | `POSCAR`, `CONTCAR`, `.vasp` | ✓ | ✓ (POSCAR) |
| ASE trajectory | `.traj` | ✓ | trajectory export only |
| Quantum ESPRESSO | `.in`, `.pwi`, `.pwo`, `.out` | ✓ | input (`espresso-in`) only |
| CASTEP | `.cell` | ✓ | ✓ |
| LAMMPS data | `.data` | ✓ | ✓ |
| LAMMPS dump | `.dump`, `.lammpstrj` | ✓ | — |
| Gaussian input | `.gjf`, `.com` | ✓ | ✓ |
| SHELX | `.res` | ✓ | ✓ |

Trajectory saves offer four multi-frame containers: `extxyz`, `xyz`,
`traj`, and multi-model `pdb`.

---

## Volumetric formats

Read by the Volumetric Data dock ({doc}`/analysis/volumetric`):

| Format | Files | Notes |
|---|---|---|
| Gaussian cube | `.cube` | Including cubes written by QE's `pp.x` and by Calango's own exports |
| VASP grids | `CHGCAR`, `LOCPOT`, `PARCHG`, `ELFCAR` (and `AECCAR*` via the charge workflows) | Prefix-matched — `CHGCAR-relax2` still opens |
| XCrySDen | `.xsf` | 3D grids |

---

## Project files and MIME types

A `.calproj` is **one self-describing JSON document** storing every open
tab (structures verbatim, trajectories with all frames), per-atom fields
and bond overrides, the viewport colour state, and the job console +
metric series. Details in {doc}`/data_io`.

The installers register two MIME types so the desktop opens these files in
Calango:

| Extension | MIME type | Registered by |
|---|---|---|
| `.calproj` | `application/x-calango-project` | macOS `Info.plist`, Linux `calango-mime.xml` + `.desktop` |
| `.extxyz` | `application/x-extxyz` | same |

---

## Job artifacts

Every job runs in its own directory (`.calango_tmp/proc_<id>` beside the
project file, or the simulations directory from Preferences). What
accumulates there, who writes it, and who reads it:

**Staged inputs and infrastructure**

| File | Produced by | Consumed by |
|---|---|---|
| `run.py` | The wizard's script generator (hand-editable) | The job runner — a shell launches it with the chosen interpreter |
| `structure.extxyz` | Staging — the input geometry | `run.py` reads it back with `ase.io.read` |
| `configs.extxyz` | Staging of a noise-ensemble run | `run.py` (one single point per frame) |
| `calculator.json` | Staging, from simulation wizards | Baseline-inheriting wizards (Optics, GW, Wannier, Born, Raman/IR) — see {doc}`/reference/job_protocol` |
| `job.sh`, `calango_job.out`, `calango_job.err` | Remote submission — scheduler wrapper and captured stdout/stderr | The HPC panel |

**Live-state files (rewritten during the run)**

| File | Produced by | Consumed by |
|---|---|---|
| `metrics.json` | The script's embedded logger — metric samples + progress | Results panel (polled live: plots + progress bar) |
| `log.json` | Embedded logger — structured events | Results panel Log tab |
| `warnings.log` | Python `warnings` routed to file | Read on demand; kept out of stdout so the log stays legible |

**Results (what the viewers dispatch on)**

| File | Produced by | Consumed by |
|---|---|---|
| `opt.traj`, `optimized.extxyz`, `geometry_optimization.json` | Geometry optimization | Trajectory loader; optimization viewer |
| `md.traj`, `md.extxyz` | Molecular dynamics | Trajectory loader — `md.extxyz` preferred: it carries forces/velocities for overlays and the VACF |
| `single_point.json` | Single-point calculation | Single-point viewer |
| `single_point.extxyz` | Single-point calculation — the converged geometry with the calculator's results (energy, forces, ...) still attached | Overlay vectors (forces/magmoms); a downstream Orchestration Dump Trajectory node reads this file directly, once per fan-out pass |
| `*.gpw` | GPAW single point (restart file) | Baseline-inheriting wizards; partial-charge and CDD workflows |
| `density_*.cube`, `elf.cube`, `potential_hartree.cube`, `kinetic_energy_density.cube` | Single-point density exports | Volumetric Data dock (auto-registered) |
| `bands.json`, `pdos.json` | Electronic Structure run | Band structure / PDOS viewer |
| `effective_bands.json` | Effective Bands (unfolding) | Unfolding viewer |
| `bands_2d.json` | 2D Bands | 2D band-surface viewer |
| `phonon_band.json`, `phonon_bands.csv`, `phonon_dos.csv` | Phonon run | Phonon plot window; CSV for external plotting |
| `optics.json` | Optics / 2D Optics | Optics viewer |
| `gw.json` | GW Calculations | GW viewer |
| `wannier.json` (+ orbital cubes) | Wannier Functions | Wannier viewer; Interpolation / Fermi Surface / Topology modules |
| `fermi_surface.json`, `topology.json` | Wannier post-processes | Their viewers |
| `raman_ir.json` | Raman and IR Spectroscopy (any engine) | Raman/IR viewer — one viewer serves GPAW, VASP, and QE |
| `born_charges.json` | Born Effective Charges (any engine) | Born viewer; Raman/IR wizard (IR intensities); phonon LO–TO |
| `cdd.cube`, `cdd.json` | Charge Density Difference | Volumetric dock (auto-loaded); status-bar summary |
| `xas.json` | XAS | XAS viewer |
| `charged_defects.json` | Charged defects | Defect-diagram viewer |
| `cluster_expansion.json` | Cluster expansion | Hull viewer |
| `cutoff_convergence.json`, `kpoints_convergence.json` | Parameters Convergence sweeps | Shared convergence viewer |
| `random_noise.json`, `noise_singlepoint.extxyz` | Random-noise ensemble (legacy — Random Noise Setup no longer writes these; a job directory from before that change still opens with the ensemble viewer) | Ensemble viewer / dataset manager |
| `dump_summary.json` | Orchestration Dump Trajectory node — frame counts, excluded reasons, the output path | Informational only; the training set itself is the extxyz file the node's *Output file* field names, written wherever chosen rather than into the job directory |
| `dump_densities_summary.json` | Orchestration Dump Charge Densities node — the chosen product, files written/missing, the missing reasons | Informational only; the enumerated `density_*.cube`/`.h5` files themselves land in the node's *Destination folder*, not the job directory |

**The dispatch rule**: when you ask a completed process for its results,
Calango checks which of these files the directory actually holds and opens
the matching viewer — there is no separate bookkeeping to go stale. Delete
a job directory and its viewers simply stop being offered.
