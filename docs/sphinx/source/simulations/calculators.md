# Calculation engines

The {guilabel}`Calculation engine` dropdown at the top of every wizard's Calculator Settings stage selects which of ASE's calculators the generated script builds. Sixteen engines are offered; each brings its own settings group, and the page shows only the groups that apply to the current selection.

| Engine | Kind | Needs in the job environment |
|---|---|---|
| MACE | ML potential | `mace-torch` |
| Quantum ESPRESSO | DFT | `pw.x` + pseudopotential library |
| SIESTA | DFT | `siesta` binary + pseudopotentials |
| ORCA | Quantum chemistry | the ORCA binaries |
| GPAW | DFT (Python package) | `gpaw` |
| VASP | DFT | licensed binary + POTCAR datasets |
| EMT | Classical (test) | nothing — ships with ASE |
| ASAP | Classical | `asap3` (fast C++ EMT / OpenKIM) |
| Lennard-Jones | Classical | nothing — ships with ASE |
| DeepMD-kit | ML potential | `deepmd-kit` |
| NequIP | ML potential | `nequip` |
| Allegro | ML potential | `nequip` + `allegro` |
| CHGNet | Universal ML potential | `chgnet` |
| MatterSim | Universal ML potential | `mattersim` |
| FAIRChem / OCP | ML potential | `fairchem` |
| LAMMPS | Classical MD engine | LAMMPS (library or binary) |

**EMT and Lennard-Jones run out of the box** and are the right way to test a workflow before committing compute. ASAP is the same physics as EMT in fast C++. The DFT engines other than GPAW generate script skeletons with clearly marked `EDIT ME` lines where a binary path or launch command must be supplied.

The Python environment each engine runs in is resolved per engine from {menuselection}`Preferences --> Python & Environments` — see {doc}`/simulations/wizards`.

---

## The shared DFT layout

The four DFT engines share one set of thematic group boxes, so each decision lives in the same place whichever engine is selected:

- {guilabel}`Mode & Basis Set` — GPAW's mode, the shared plane-wave cutoff row (with VASP's XC beside it), GPAW's XC and the corrections to it (Hubbard U, DFTD4).
- {guilabel}`Brillouin Zone & k-Points` — the Monkhorst–Pack grid (default **7×7×7**), {guilabel}`Gamma-centered Grid`, and (Single-point only) {guilabel}`Symmetry: off`.
- {guilabel}`Electronic Convergence & Smearing` — smearing method and width, eigensolver and SCF step cap, density mixer, convergence tolerances.
- {guilabel}`Spin Configurations` — the polarization mode and initial moments.
- {guilabel}`Density Exports` — GPAW only, on the Single-point wizard only.

The plane-wave cutoff row (default **500 eV**, range 100–2000 eV) serves exactly the two engines that share the concept: GPAW's `PW(ecut)` and VASP's `ENCUT`. **Quantum ESPRESSO and SIESTA never read it** — QE's cutoff is a *pair* and lives in its own group; SIESTA has no plane-wave cutoff at all.

{guilabel}`Gamma-centered Grid` shifts the mesh so it includes Γ (`kpts={'size': …, 'gamma': True}` for GPAW, Gamma-centered `KPOINTS` for VASP). An even-numbered Monkhorst–Pack mesh misses Γ in every plane-wave code; a hexagonal cell, a Wannier interpolation or any downstream step that needs Γ in the set wants this on.

---

## GPAW

GPAW takes exactly one of three discretizations, selected by {guilabel}`Mode` — and the page shows only the basis control that is in play:

| Mode | Basis parameter | Default |
|---|---|---|
| PW — plane waves | {guilabel}`Plane-wave cutoff` | 500 eV |
| FD — finite difference | {guilabel}`Grid spacing h` | 0.20 Å (0.18–0.20 Å is typical) |
| LCAO — atomic orbitals | {guilabel}`LCAO basis` | `dzp` (also `dz`, `sz`, `szp`) |

**The cutoff is a property of the plane-wave basis, so it appears only in PW mode.** In FD the basis is the real-space grid, in LCAO the orbital set — a cutoff there converges nothing, which is a trap the UI refuses to lay.

{guilabel}`XC functional` (editable, default `PBE`) offers the GGAs, the hybrids (`HSE06`, `B3LYP`), the meta-GGAs (`SCAN`, `r2SCAN`) and the van der Waals functionals (`vdW-DF`, `vdW-DF2`, `vdW-DF-cx`, `optPBE-vdW`, `optB88-vdW`, `BEEF-vdW`, `VV10`, `rVV10`). The hybrids and meta-GGAs need a GPAW build with libxc; **the vdW functionals need GPAW compiled against libvdwxc** — the generated script selects them as `xc={'name': …, 'backend': 'libvdwxc'}` — and carry their own non-local correlation, so no D4 correction is needed on top.

Electronic convergence:

- {guilabel}`Eigensolver` — Davidson (robust default), RMM-DIIS (cheapest per step for large metallic systems), CG (very stable when the SCF oscillates), Direct (exact diagonalization for LCAO / small systems); beside it, {guilabel}`max SCF steps` (default **500** — a runaway guard, not a target).
- {guilabel}`Density mixer` — `Mixer` (non-magnetic), `MixerSum` (spin-polarized, mixes the total density), `MixerDif` (also mixes the magnetization separately); parameters `beta` **0.05**, `nmaxold` **5**, `weight` **50**. Metals and magnetic systems often want β = 0.02–0.05.
- {guilabel}`Convergence tolerances` — energy **1e-4 eV**, eigenstates **4e-8** eV²/electron, density **1e-4** electrons/valence electron, entered in scientific notation.

Smearing (shared row, GPAW's full menu): None, Gaussian, Fermi–Dirac (default, width **0.1 eV**), Methfessel–Paxton (order default **1** — order 0 *is* Gaussian), Marzari–Vanderbilt, Tetrahedron method, Improved tetrahedron (Blöchl), Orbital-free, and Fixed occupations. The tetrahedron schemes, orbital-free and fixed occupations take no width — the field disappears rather than offering a knob GPAW rejects. Fixed occupations require an explicit list per spin channel; the wizard refuses to generate until it is filled.

Brillouin-zone extras (Single-point and Geometry Optimization only): {guilabel}`Symmetry: off` emits `symmetry="off"` — no point-group reduction of the k-set. A symmetry-off Single-point is the required baseline for a Wannier Functions run.

**Density exports** (Single-point only): after the SCF, any of six volumetric fields is written to its own `.cube` in the job directory, each costing one grid evaluation on the already-converged calculation. All-electron density is on by default.

| Field | File | Source |
|---|---|---|
| All-electron density | `density_all_electron.cube` | `get_all_electron_density(gridrefinement=2)` |
| Pseudodensity | `density_pseudo.cube` | `get_pseudo_density()` |
| Spin density | `density_spin.cube` | n(↑) − n(↓); skipped with a note in a spin-restricted run |
| Hartree potential | `potential_hartree.cube` | `get_electrostatic_potential()`, eV |
| ELF | `elf.cube` | `gpaw.elf.elf_from_dft_calculation` |
| Kinetic energy density | `kinetic_energy_density.cube` | τ(r) after `update_ked()` |

Finished exports register in the Volumetric Data dock automatically. A GPAW Single-point also always writes `single_point.gpw` (`mode="all"`), the baseline every inheriting wizard looks for.

---

## Quantum ESPRESSO

QE is a plane-wave code with a *dual* grid, and its group is built around that fact.

- {guilabel}`ecutwfc` — the wavefunction cutoff, default **60 Ry**, in Rydberg because that is what `pw.x` reads; nothing is converted behind your back. Converge it against energy *differences*, not the absolute total.
- {guilabel}`ecutrho` — the charge-density cutoff on QE's second grid. Default {guilabel}`auto`, meaning QE's own 4 × ecutwfc. The note under the row restates the effective dual live.
- {guilabel}`XC functional` — `input_dft` (editable; `pbe`, `pbesol`, `pz`, `blyp`, `scan`, `vdw-df2`, `hse`). Leave it matching the pseudopotential library: a PBE pseudo with an LDA `input_dft` is not an LDA calculation, it is an inconsistent one.
- {guilabel}`Occupations` — `smearing` (metals), `fixed` (insulators with a known gap), `tetrahedra` (Blöchl) or `tetrahedra_opt`. The smearing function and width appear only for `smearing` — the others take no width, and writing `degauss` beside them is how a QE input gets silently ignored. The tetrahedron methods give no forces, so they cannot drive a relaxation.
- {guilabel}`Smearing` — Marzari–Vanderbilt ("cold", QE's recommended default), Gaussian, Methfessel–Paxton, Fermi–Dirac (a physical electronic temperature, not a convergence aid). Width `degauss` default **0.01 Ry**.
- {guilabel}`conv_thr` — the SCF threshold, default **1e-8 Ry**. A phonon or Born-charge run wants 1e-10 or tighter, because it differentiates this quantity.

The pseudopotential library (`pseudo_dir`) comes from {menuselection}`Preferences --> External Files`; unset, the script carries a placeholder.

:::{warning}
The 4× default for `ecutrho` is correct for **norm-conserving** pseudopotentials and badly under-converged for ultrasoft or PAW, whose augmentation charges want **8–12 ×** ecutwfc. Leaving it on auto with a USPP library is the single most common way a QE run comes out quietly wrong.
:::

---

## VASP

The {guilabel}`VASP settings` group exposes the primary INCAR tags; everything with a counterpart in another engine sits in the shared rows instead (XC beside the cutoff, ALGO and NELM on the eigensolver row, EDIFF on the tolerance row, Γ-centering with the k-grid).

- **PAW datasets** — `VASP_PP_PATH` is set once in {menuselection}`Preferences --> External Files` and only *reported* here; it describes the installation, not a run. Both layouts work: the canonical one with a `potpaw_PBE/` level inside, and the flat one with element folders at the top — for the latter the generated script builds a symlink shim, because ASE cannot be told to look anywhere else.
- {guilabel}`XC functional` — ASE's `xc` (`PBE`, `PBEsol`, `RPBE`, `LDA`, `SCAN`, `r2SCAN`, `HSE06`), expanding to the matching `GGA`/`METAGGA` tags.
- {guilabel}`PREC` — default **Accurate**; Normal's coarser grid puts egg-box error into the forces, exactly what a relaxation is most sensitive to.
- {guilabel}`ALGO` — Normal (blocked Davidson, default), Fast, VeryFast, All (CG), Damped; All/Damped are required for meta-GGA and hybrid functionals.
- {guilabel}`NELM` / {guilabel}`EDIFF` — SCF iteration cap **500** and energy threshold **1e-6 eV**.
- {guilabel}`LREAL` — **Auto** (a large speed-up above roughly 20 atoms) or False (exact, right for small cells).
- {guilabel}`Relaxation driver` — who takes the ionic steps. **Exactly one side may relax**: with both enabled, every force evaluation ASE asked for would run a complete VASP relaxation.
  - *ASE optimizer* (default) pins VASP to `IBRION = -1`, `NSW = 0`. This is the mode the rest of the application is built around — constraints, cell filters, live streaming and per-step metrics all come from ASE driving.
  - *VASP internal relaxation* creates no ASE optimizer; the {guilabel}`IBRION` (2 = CG, 1 = quasi-Newton, 3 = damped MD) / {guilabel}`ISIF` (2 ions, 3 ions + cell + volume, 4 ions + shape) / {guilabel}`EDIFFG` (default **−0.02**; negative = force criterion in eV/Å) row appears only in this mode. Much faster per step — VASP keeps the wavefunction and density between ionic steps — but the steps happen inside one call and cannot be streamed or constrained. The ionic path is recovered from `OUTCAR` afterwards, so the trajectory tab and the Geometry Optimization Viewer work either way.
- {guilabel}`Write` — `LCHARG` (**on**; CHGCAR is what every density analysis reads), `LWAVE` (off; WAVECAR is large), `LAECHG` (the AECCARs a Bader analysis needs), `LORBIT = 11` (site/l-projected DOS). Finished CHGCAR/AECCAR/LOCPOT/ELFCAR files register in the Volumetric Data dock; an {menuselection}`Electronics --> Electronic Structure` run with a VASP baseline copies the CHGCAR in and diagonalizes along the band path with `ICHARG = 11`, running no SCF of its own.
- {guilabel}`NCORE` / {guilabel}`KPAR` — default **auto** (0): a wrong value is a performance cliff, not an error.
- {guilabel}`Extra INCAR tags` — one `TAG = value` per line, applied verbatim on top of everything above. No dialog can cover 300 INCAR flags, and one that tries becomes a ceiling.

:::{note}
`ISMEAR` and `SIGMA` come from the shared smearing row, so one control drives every engine: Fermi–Dirac maps to `ISMEAR = -1`, Gaussian to 0, Methfessel–Paxton to 1. *None* maps to a very narrow Gaussian rather than the tetrahedron `ISMEAR = -5`, which VASP refuses on the Γ-only meshes small test cells use. The smearing menu itself is engine-filtered — methods VASP has no ISMEAR for (Marzari–Vanderbilt, orbital-free, fixed occupations) are withdrawn while VASP is selected. `MAGMOM` is not set here either: it comes from the structure's initial magnetic moments.
:::

---

## SIESTA

**SIESTA has no plane-wave cutoff, and the option is gone.** Its basis is a finite set of numerical atomic orbitals; there is no plane-wave expansion to truncate. What a cutoff field used to do here was silently set `MeshCutoff` — so raising it to "converge the basis" refined a real-space grid while the basis stayed exactly as small.

- {guilabel}`XC functional` — resolved by ASE into `XC.functional` / `XC.authors` (`PBE`, `PBEsol`, `revPBE`, `RPBE`, `BLYP`, `PW91`; `PZ`, `CA`, `PW92` are the LDA parametrizations; `DRSLL` and `VV` are van der Waals functionals).
- {guilabel}`Basis type` — `PAO.BasisType`: *how* the multiple-ζ orbitals are generated. `split` is the standard scheme and what almost every published SIESTA calculation uses (also `splitgauss`, `nodes`, `nonodes`, `filteret`).
- {guilabel}`Basis size` — `SZ`, `SZP`, `DZ`, `DZP` (default; the standard production basis), `TZP`. **This is the parameter that plays the role a plane-wave cutoff plays elsewhere: it is what you converge.**
- {guilabel}`Energy shift` — `PAO.EnergyShift`, default **0.27 eV** (SIESTA's 0.02 Ry). The confinement energy that fixes each orbital's cutoff radius: *smaller* means longer-ranged orbitals, a better basis and a more expensive run — the second knob after the basis size.
- {guilabel}`Mesh cutoff` — `MeshCutoff`, default **300 eV** (200–400 eV typical). The fineness of the real-space grid the Hartree and XC terms are integrated on — *not* a basis parameter. An under-converged mesh shows up as an "egg-box" force error as atoms cross grid points.

The pseudopotential library (`SIESTA_PP_PATH`) comes from {menuselection}`Preferences --> External Files`.

---

## ORCA

- {guilabel}`Method / functional` — editable: `B3LYP`, `PBE0`, `r2SCAN`, `HF`, or any ORCA keyword you type.
- {guilabel}`Basis set` — editable: `def2-SVP`, `def2-TZVP`, `cc-pVDZ`, ….
- {guilabel}`Charge` — −10 to 10; {guilabel}`Multiplicity` — 2S+1, 1 to 11.
- Solvation — none (gas phase), CPCM or SMD, with the solvent named (water, acetonitrile, toluene, …).

ASE writes the `.inp` file into the job directory and parses the output. The generated script contains an `EDIT ME` line for the path to your `orca` binary.

---

## LAMMPS

:::{warning}
**LAMMPS is an engine, not a force field.** What it computes is decided entirely by the pair style and coefficients you supply, and nothing validates them — a `pair_coeff` that does not match the style, or a potential file for the wrong elements, is a physics error LAMMPS will run without complaint.
:::

- {guilabel}`Interface` — which of ASE's two LAMMPS interfaces the script drives; the right choice depends on how LAMMPS is installed, not on the physics.
  - *Library* (`ase.calculators.lammpslib.LAMMPSlib`, default): in-process through the LAMMPS Python module, no file I/O per step — the right choice for MD and relaxation. Needs LAMMPS built as a shared library with its Python package (conda-forge's `lammps` provides both).
  - *Executable* (`ase.calculators.lammpsrun.LAMMPS`): spawns the `lmp` binary per force evaluation, exchanging data files. Works with any build, including a plain distro package, at the cost of process startup on every step.
- {guilabel}`Pair style` — the `pair_style` line without the keyword, default `lj/cut 10.0`; everything after the style name is passed through verbatim.
- {guilabel}`Pair coefficients` — one `pair_coeff` line per row, without the keyword (`* * Cu_u3.eam.alloy Cu`, `1 1 0.0103 3.4`). Where a line names elements, they must follow the type order the generated script prints as *LAMMPS species order* — getting it wrong computes a different compound rather than failing.
- {guilabel}`Potential files` — EAM tables, Tersoff files, one per line, **absolute paths**: the executable interface runs in a scratch directory and the library interface inherits the process's working directory, so a relative path resolves against neither reliably.
- {guilabel}`Extra commands` — appended after the pair setup (neighbor lists, `pair_modify`, per-style `fix`). Only the library interface can apply them directly; with the executable interface they are emitted as comments.
- {guilabel}`LAMMPS binary` — executable interface only; blank falls back to `$ASE_LAMMPSRUN_COMMAND`, then `lmp` on `$PATH`.
- {guilabel}`Keep the LAMMPS log` — on by default; when a pair style rejects its coefficients, `lammps.log` is the only place the reason appears.

The generated script pins `units metal` (eV, Å, ps) — the only units style that matches what ASE expects.

---

## MACE

- {guilabel}`Model` — *MACE-MP-0* (universal, materials), *MACE-OFF* (universal, organic molecules), or *Custom trained model*. Foundation models download automatically on first use and are cached in `~/.cache/mace`.
- {guilabel}`Model size` — `small`, `medium` (default), `large`. Shown for the foundation families only; a custom checkpoint carries its own architecture.
- {guilabel}`Dispersion` — MACE-MP-0 only: `mace_mp(dispersion=True)` adds the D3(BJ) van der Waals head the foundation model ships (needs `torch-dftd` in the job environment). The bare model has no long-range dispersion, so layered and molecular-crystal systems come out under-bound without it.
- {guilabel}`Model file` — custom models only: a dropdown over the ML potentials directory configured in Preferences, editable, with {guilabel}`Browse…` for a `.model` / `.pt` checkpoint elsewhere.
- {guilabel}`Precision` — `float64` (default; reproduces the training checkpoint exactly, what tight force convergence and vibrational analysis need) or `float32` (roughly twice as fast, ~1e-4 eV/Å noise in the forces).
- {guilabel}`Device / GPU` — `cpu`, `cuda`, `mps`. **PyTorch's MPS backend implements no float64** — selecting `mps` with float64 turns the precision control red and the tooltip says to pick float32 or the CPU, rather than letting the job die on its first forward pass.

---

## The other machine-learning potentials

DeepMD, NequIP, Allegro, CHGNet, MatterSim and FAIRChem share one {guilabel}`Machine-Learning Potential` group — a model file, a device, and per-engine rows shown only for the selected engine. One device selector serves them all (`cpu` / `cuda` / `mps`); each engine needs its own package in the job environment, which the per-engine conda preset in {menuselection}`Preferences --> Python & Environments` supplies.

| Engine | Model file | Engine-specific rows |
|---|---|---|
| DeepMD-kit | frozen `.pb` graph (or `.pth` for the PyTorch backend) | none — **no MPS backend exists**; the device combo warns |
| NequIP / Allegro | *deployed* TorchScript `.pth` — the output of `nequip-deploy build`, not a training checkpoint | training units (energy: eV, kcal/mol, Hartree, meV; length: Angstrom, Bohr, nm) — ASE works in eV/Å, so a kcal/mol model left at eV silently reports wrong energies |
| CHGNet | none — ships its own weights | pretrained weights `0.3.0` (published checkpoint, default) or `latest`; {guilabel}`Evaluate stress tensor` (on — required for variable-cell relaxation) |
| MatterSim | none — ships its own weights | model `MatterSim-v1.0.0-1M` (fast, default) or `-5M` (more accurate); optional thermodynamic state T (300 K) / P (0 GPa) for the finite-temperature head |
| FAIRChem / OCP | `.pt` checkpoint — **required, there is no default model** | architecture `EquiformerV2` or `eSCN`; it must match the checkpoint or loading fails |

---

## Dispersion corrections

Semilocal functionals carry no long-range correlation, so layered and molecular systems come out under-bound. Calango offers three routes:

- {guilabel}`van der Waals Correction (DFTD4)` — wraps the calculator in ASE's `SumCalculator([DFTD4(method=xc), calc])`: each computes independently and energies and forces add (DFTD4 is not a wrapper — passing it another calculator is not how it composes). Needs the `dftd4` package; the damping parameters follow the calculator's own functional. The checkbox appears only in the wizards whose answer depends on forces or energy *differences* — Geometry Optimization, Phonon, MD, Monte Carlo, NEB — because a single-point total energy gains only a constant shift.
- **GPAW's vdW functionals** (vdW-DF family, VV10, rVV10) carry non-local correlation in the functional itself, evaluated through libvdwxc — do not stack D4 on top.
- **MACE-MP-0's dispersion head** — see above.

---

## Spin and DFT+U

{guilabel}`Spin Configurations` selects the treatment: **Unpolarized** (spin-restricted), **Collinear** (↑/↓ densities, scalar moments) or **Non-collinear** (spinor, vector moments). Only the *mode* is chosen here — the per-atom initial moments are a property of the structure, set in {menuselection}`Edit --> Edit Structure`, and travel with the staged geometry into every calculation. When the structure carries no moments at all, a uniform fallback of **1.0 μB** per atom seeds the SCF — a spin-polarized run starting from all zeros converges straight back to the non-magnetic solution.

{guilabel}`Hubbard parameters…` (GPAW only, next to the XC combo it corrects) opens the DFT+U editor: one row per correction — element, orbital shell (s/p/d/f, default d), U in eV, and GPAW's optional *scale* flag (off by default). Emitted as GPAW's `setups`, e.g. `setups={"Fe": ":d,3.5"}` — the leading colon keeps the default PAW dataset and appends the correction. The element completer is seeded from the structure, because a U on an element the cell does not contain is silently inert. Nothing validates the numbers; the same Fe 3d takes different values in an oxide and in a metal, and they are yours to justify.

% TODO screenshot: Calculator Settings stage with GPAW selected, showing Mode & Basis Set, Brillouin Zone & k-Points and Electronic Convergence & Smearing groups
```{figure} /_static/img/sim_calculators_gpaw.png
:alt: The GPAW calculator page with its thematic settings groups
:width: 92%
:figclass: screenshot

GPAW's calculator page. Switching the engine swaps only the groups that differ; shared decisions stay in the same place.
```

% TODO screenshot: the same stage with VASP selected, showing the VASP settings group with the relaxation driver row and extra INCAR tags field
```{figure} /_static/img/sim_calculators_vasp.png
:alt: The VASP settings group with INCAR controls
:width: 92%
:figclass: screenshot

VASP's settings group: primary INCAR tags, the relaxation-driver choice, and the free-form extra-tags escape hatch.
```
