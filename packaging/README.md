# Calango packaging

Three distribution formats are supported. All are built from the repository
root; substitute the Qt prefix / Python interpreter paths for your machine.

The user-facing version is the single source of truth in `CMakeLists.txt`
(`project(... VERSION ...)`) and is baked into the binary as `CALANGO_VERSION`.

---

## macOS `.dmg` (drag-and-install)

Automated end-to-end by the helper script (configure → build → macdeployqt →
embed Python.framework → CPack DragNDrop → verify):

```sh
packaging/macos/create_dmg.sh
# → build-macos-bundle/calango-<version>-macos-<arch>.dmg  (+ .sha256)
```

Manual equivalent:

```sh
cmake -S . -B build-macos -DCALANGO_MACOS_BUNDLE=ON \
    -DPython3_EXECUTABLE="$PWD/.venv/bin/python" \
    -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
( cd build-macos && cpack -G DragNDrop )
```

---

## Linux `.deb`

```sh
cmake -S . -B build-deb -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build-deb -j"$(nproc)"
( cd build-deb && cpack -G DEB )     # → calango_<version>_<arch>.deb
```

Ships the `.desktop` launcher, the `application/x-calango-project` MIME type for
`.calproj` files, and the app icon. `dpkg-shlibdeps` derives the Qt6/OpenGL/
libpython runtime dependencies from the linked shared libraries.

---

## Conda package (`packaging/conda/`)

The recipe (`meta.yaml` + `build.sh`) builds Calango against a conda toolchain
and installs the binary into `$PREFIX/bin/calango`. Because the embedded
interpreter is `$PREFIX/bin/python`, the packaged app finds ASE, GPAW and MACE
from the same environment automatically.

Build it:

```sh
# 1. Get conda-build AND the libmamba solver in the build env.
conda install -n base -c conda-forge conda-build conda-libmamba-solver

# 2. Build the package from the repo root, using the libmamba solver.
#    -c conda-forge provides qt6-main, gpaw, mace-torch, pytorch, etc.
CONDA_SOLVER=libmamba conda build packaging/conda/ -c conda-forge
#    (equivalently:  conda build packaging/conda/ -c conda-forge --solver=libmamba)

# 3. Install the freshly built package into a new environment to test it.
conda create -n calango -c conda-forge --use-local calango
conda activate calango
calango --probe-python        # exit 0 == embedded ASE import works
calango                        # launch the GUI
```

**Use the libmamba solver.** The classic solver in conda 26.x fails this recipe
with `Unsatisfiable dependencies … {'__conda', '__osx', '__unix', '__archspec'}`
— it does not inject the platform virtual packages into the build/host solve.
libmamba resolves it. Set it per-invocation as above, or make it the default
with `conda config --set solver libmamba`.

Notes:
- The version is read from the `CALANGO_VERSION` env var if set, else the
  default pinned in `meta.yaml` — keep it in sync with `CMakeLists.txt`, e.g.
  `CALANGO_VERSION=$(grep -m1 VERSION CMakeLists.txt | grep -oE '[0-9.]+') \
   CONDA_SOLVER=libmamba conda build packaging/conda/ -c conda-forge`.
- Build/host deps: `cmake`, `ninja`, a C++20 `cxx-compiler`, `pkg-config`,
  `qt6-main`, `pybind11`, `python`.
- Run deps: `qt6-main`, `numpy`, `ase`, `gpaw`, `mace-torch`, `pytorch`,
  `spglib`, `pymatgen`, `scipy`, `paramiko` (see `packaging/dependencies.txt`
  for the complete annotated list and version constraints).
- `qcustomplot` and `nlohmann_json` from the original spec were dropped:
  Calango uses custom QPainter plot widgets and Qt's own QJson, so neither is a
  real dependency — and `qcustomplot` isn't packaged on conda-forge for
  osx-arm64, which made the solve unsatisfiable.
