# HDF5 density containers

Calango can store a charge-density (or any other volumetric scalar field —
ELF, an electrostatic potential, a kinetic-energy density, …) as a
compressed HDF5 container instead of a plain-text `.cube` or a VASP
`CHGCAR`. The container is self-contained: `core::VolumetricData::load()`
reads it back exactly like the format it was converted from, so nothing
downstream — the isosurface extractor, the Volumetric Data dock, the slice
viewer — has to know `.h5` exists as a special case.

Everything on this page lives in `src/core/VolumetricData.{hpp,cpp}`, all
native C++ against the HDF5 **C** API (not the separate `libhdf5_cpp`).
That is a deliberate choice, not a default: reading `.cube`/`CHGCAR`/`.xsf`
was already 100% native C++ with no Python involved (`VolumetricData::load`
and its private `loadCube`/`loadChgcar`/`loadXsf`), so the natural place for
a fourth format — reading OR writing — is the same class, using the same
library family. The alternative (an `h5py` step in the generated ASE
scripts) would need a second implementation to stay in sync with the first,
and would only cover writing during a fresh calculation, not the "convert a
file that already exists on disk" case the Dump Charge Densities node
needs. See {doc}`/simulations/orchestration` and
{doc}`/simulations/scripts` for where each of Calango's two Python surfaces
(generated ASE scripts vs. nothing at all in this case) actually sits.

---

## Layout

An HDF5 file conceptually nests **groups** (like directories) holding
**datasets** (arrays) and **attributes** (small named values on any group
or dataset) — this section states, and pins with `calango_hdf5_layout_version`,
exactly which of those Calango's own container uses.

**Root attributes** (on `/`):

| Attribute | Type | Meaning |
|---|---|---|
| `calango_hdf5_layout_version` | `int32`, scalar | `1` today. A reader can refuse or adapt if this is ever bumped; nothing currently reads it as anything but a sanity check. |
| `source_format` | fixed-length C string | `"cube"`, `"chgcar"`, `"xsf"`, or empty. The format the file was **converted from** — a reader that loads the `.h5` gets this back in `VolumetricData::sourceFormat`, not `"hdf5"` itself, so "what was this originally" survives the round trip. |
| `label` | fixed-length C string | `VolumetricData::label` — the field name (file stem or block title) shown in the Volumetric Data dock. |
| `origin` | `float64[3]` | Cartesian origin of grid node (0,0,0), Å. |
| `span_a`, `span_b`, `span_c` | `float64[3]` each | The three full spanning vectors of the grid box, Å — see `VolumetricData`'s own doc comment for how a grid node's position is built from these. |

**Dataset `/density`**: shape `(nx, ny, nz)`, `float64`, **chunked**
(chunk size clamped to `min(dim, 64)` per axis), **byte-shuffled**, then
**gzip**-compressed (level 6). Row-major C order over `(nx, ny, nz)` already
puts `z` fastest — the same layout `VolumetricData::values` uses in memory
— so there is no permutation on either side of the round trip, unlike the
Fortran-order transpose `loadChgcar()` has to do for a plain CHGCAR.
Byte-shuffle runs before gzip: it regroups each `float64`'s eight bytes by
significance across a chunk, which is what lets gzip find real repetition
in a smoothly-varying density instead of the near-random mantissa bytes it
would see un-shuffled. On a realistic smooth field this reliably beats the
raw `nx*ny*nz*8` byte count several-fold (a synthetic Gaussian blob in the
test suite compresses ~5.6x); a field with no smooth structure at all (pure
noise) would not compress much, if any — this is an ordinary lossless-codec
property, not a Calango-specific guarantee.

**Group `/atoms`**: always present, even when there are no atoms to record,
so a reader never branches on the group's existence — only on its datasets
being empty:

| Dataset | Type | Shape |
|---|---|---|
| `atomic_numbers` | `int32` | `(natoms,)` |
| `positions` | `float64` | `(natoms, 3)`, Cartesian Å |

`VolumetricData::atoms` exists **only** so a converted `.h5` stays
self-contained enough to reconstruct the file it came from — nothing in the
grid-sampling or isosurface API reads it. `loadCube()` and `loadChgcar()`
populate it (an atom whose species could not be resolved — a VASP4 CHGCAR
with no symbol line — is recorded with `atomic_numbers[i] == 0`);
`loadXsf()` leaves it empty, since that loader never parses XSF's separate
atoms block, only its `DATAGRID_3D` section.

---

## The one conversion path

```cpp
// src/core/VolumetricData.hpp
void saveHdf5(const std::string& path) const;
static VolumetricData loadHdf5(const std::string& path);  // via load()
static bool convertToHdf5(const std::string& sourcePath,
                          const std::string& destPath, std::string* error);
```

`convertToHdf5()` is `load(sourcePath)` followed by `saveHdf5(destPath)` —
nothing more — but it is the **one** call site both features that produce
`.h5` files go through:

- The **calculator setup pages'** "Compress to HDF5" option (GPAW's Density
  Exports group, VASP's CHGCAR/AECCAR checkboxes) — converts each density
  file a finished run wrote, in the GUI process, right after the run's own
  files are discovered (see {doc}`/simulations/orchestration`'s Dump
  Charge Densities section and the calculator-setup pages themselves).
- The **Dump Charge Densities** Orchestration node's own HDF5 option, when
  collecting a fan-out's per-item density files into one destination
  folder.

Neither writes its own HDF5 code. `saveHdf5()` never leaves a partially
written file at its destination on failure or interruption: it writes to a
`.tmp` sibling and renames it into place only once every dataset and
attribute has been written successfully.

:::{note}
The calculator setup pages' checkbox reaches only **standalone** wizard
launches (the main menu's Single-Point / Geometry Optimization / Molecular
Dynamics, and anything built on `SimulationWizardBase`): the flag travels
through `calculator.json`, which `MainWindow::stageJob()` writes from the
wizard's full `CalculatorConfig`. An **Orchestration** Simulation node
writes its own, deliberately minimal `calculator.json` (engine name only —
see the comment beside its write in `OrchestrationWindow.cpp`), so checking
the same box in a node's wizard does not (yet) reach that node's own run.
The Dump Charge Densities node's HDF5 option is unaffected by this: it calls
`convertToHdf5()` directly on the files it collects, with no dependency on
`calculator.json` at all.
:::

`load()` dispatches to `loadHdf5()` for any path ending `.h5` or `.hdf5`,
alongside its existing `.cube`/`.xsf`/CHGCAR-name-sniffing rules — every
existing caller of `VolumetricData::load()` (the Volumetric Data dock,
`MlwfViewer`, `WannierDialog`, the isosurface extractor) already goes
through this one entry point, so none of them needed to change to gain
`.h5` support.

---

## Inspecting a container by hand

Any standard HDF5 tool works — the layout is not a Calango-specific
serialization on top of HDF5, just an ordinary use of it:

```bash
h5dump -H density.h5      # header: groups, datasets, attributes, no data
h5dump -a source_format density.h5
python3 -c "import h5py; f = h5py.File('density.h5'); print(f['/density'].shape, dict(f.attrs))"
```
