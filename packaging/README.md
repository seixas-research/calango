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
# 1. Get conda-build (and, ideally, mamba's faster solver) in the base env.
conda install -n base -c conda-forge conda-build

# 2. Build the package from the repo root. -c conda-forge provides qt6-main,
#    gpaw, mace-torch, pytorch, etc.
conda build packaging/conda/ -c conda-forge

# 3. Install the freshly built package into a new environment to test it.
conda create -n calango -c conda-forge --use-local calango
conda activate calango
calango --probe-python        # exit 0 == embedded ASE import works
calango                        # launch the GUI
```

Notes:
- The version is read from the `CALANGO_VERSION` env var if set, else the
  default pinned in `meta.yaml` — keep it in sync with `CMakeLists.txt`, e.g.
  `CALANGO_VERSION=$(grep -m1 VERSION CMakeLists.txt | grep -oE '[0-9.]+') \
   conda build packaging/conda/ -c conda-forge`.
- Build deps: `cmake`, `ninja`, a C++20 `cxx-compiler`, `qt6-main`,
  `qcustomplot`, `pybind11`, `nlohmann_json`.
- Run deps: `ase`, `gpaw`, `mace-torch`, `pytorch`, `spglib`, `pymatgen`,
  `nlohmann_json`, `scipy`, `paramiko` (see `packaging/dependencies.txt` for the
  complete annotated list and version constraints).
