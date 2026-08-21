# Dependencies by feature

Calango itself is C++20 + Qt 6 (≥ 6.4) + OpenGL 3.3 core; everything
scientific runs through an embedded CPython (≥ 3.9). Which interpreter that
is — and how job environments can differ from it — is explained in
{doc}`/python_environment`. This page maps features to the packages and
binaries they need.

---

## Python packages

| Package | Needed for | Constraint |
|---|---|---|
| `ase` | Everything — structure I/O, builders, calculators, band paths | ≥ 3.22 (tested 3.29) |
| `numpy` | Everything | ≥ 1.23 |
| `spglib` | Symmetry detection, Raman/IR activity, Magnetic Space Group (needs its magnetic tables) | ≥ 2.0 |
| `scipy` | Interpolation and signal helpers | ≥ 1.9, optional |
| `matplotlib` | Offline figure export | ≥ 3.6, optional |
| `paramiko` | HPC panel (SSH/SFTP) | ≥ 3.0, optional |
| `pillow` | GIF animation export | optional |
| `imageio` + `imageio-ffmpeg` | MP4 / MOV / WebM animation export (bundled ffmpeg) | ≥ 2.28 / ≥ 0.4 |
| `mace-torch` | MACE machine-learning potentials and the MLIP Trainer (pulls `torch` ≥ 2.0, `e3nn`, `torch-geometric`) | ≥ 0.3 |
| `gpaw` + `gpaw-data` | PAW/PW DFT: single points, optimization, bands, PDOS, and every GPAW post-process | ≥ 24.1 (tested 25.7) |
| `dftd4` | Grimme D4 dispersion, attached to any calculator via ASE's `SumCalculator` (conda-forge: `dftd4-python`) | ≥ 3.6 |
| `torch-dftd` | The D3 dispersion head behind `mace_mp(dispersion=True)` | ≥ 0.4 |
| `phonopy` | Symmetry-reduced phonon displacements, LO–TO (NAC), Γ-mode irrep labels | ≥ 2.20 |
| `icet` | Preferred SQS backend | optional |
| `pymatgen` | Materials Project interop; structure conversion | optional |
| `pymatgen-analysis-defects` | FNV finite-size correction for charged defects on VASP/QE — without it those runs report **uncorrected** energies, loudly | optional |
| `mp-api` | From Database → Materials Project (needs an API key) | optional |
| `pubchempy` (or plain REST) | PubChem molecule import | optional |

:::{note}
**GPAW on Apple Silicon**: conda-forge has no `osx-arm64` GPAW build, so on
an arm64 macOS environment install it with `pip install gpaw` (the
`gpaw-data` PAW datasets install from conda-forge on all platforms).
:::

---

## External binaries

These are engines and renderers Calango *drives*, resolved from the job
environment's `PATH` (the selected interpreter's `bin/` is prepended, so
solvers installed beside the interpreter win):

| Binary | Feature |
|---|---|
| `pw.x` | Quantum ESPRESSO calculations |
| `pp.x` | QE density export (partial charges, CDD) — see `CALANGO_PP_X` below |
| `ph.x` | QE DFPT: Born charges, Raman/IR |
| `siesta` | SIESTA LCAO DFT |
| VASP (`vasp_std` …) | VASP calculations (licensed; needs `VASP_PP_PATH` for POTCARs) |
| `orca` | ORCA quantum chemistry |
| `yambo`, `p2y` | GW on the Yambo route |
| `povray` / `tachyon` | Ray-traced rendering ({doc}`/output`) |
| `lammps` | LAMMPS (via `lammpsrun`; the library route uses the `lammps` Python module) |

---

## Environment variables

| Variable | Effect |
|---|---|
| `CALANGO_PYTHON` | First choice for the embedded interpreter — point it at a venv/conda `python` that has ASE |
| `VIRTUAL_ENV` | Second choice — an activated virtualenv is picked up automatically |
| `MP_API_KEY` | Materials Project API key for the From Database browser |
| `CALANGO_PP_X` | Path to QE's `pp.x` when it is not on `PATH` (partial charges, CDD) |
| `VASP_PP_PATH` | ASE's pseudopotential root for the VASP calculator |
| `ASE_ESPRESSO_COMMAND` | Injected per-run by Calango for QE jobs; set it yourself only for hand-run scripts |

Verify the interpreter resolution any time with:

```bash
calango --probe-python
# interpreter: /path/to/.venv/bin/python
# python:      3.14.x
# ase:         3.29.0
```

---

## Version-pin quick reference

The constraints Calango is validated against (from
`packaging/dependencies.txt`):

```text
CMake >= 3.21 · C++20 · Qt6 >= 6.4 (System theme needs 6.5+)
pybind11 >= 2.12 · Python >= 3.9 · OpenGL 3.3 core
ase >= 3.22 · numpy >= 1.23 · scipy >= 1.9 · spglib >= 2.0
matplotlib >= 3.6 · paramiko >= 3.0 · imageio >= 2.28 · imageio-ffmpeg >= 0.4
gpaw >= 24.1 · dftd4 >= 3.6 · torch-dftd >= 0.4 · phonopy >= 2.20
mace-torch >= 0.3 · torch >= 2.0 · torch-geometric >= 2.4 · e3nn >= 0.4
```

Two package stacks exist side by side: the **embedded interpreter** the GUI
itself uses (structure I/O, builders, symmetry), and the **job
environments** the wizards can point at per run — a GPAW conda env, a MACE
env with CUDA, a QE toolchain. A package listed above only needs to exist
in the environment whose feature you are using; the wizards' environment
selectors and the {guilabel}`Execution Environment` status line show which
one that is.
