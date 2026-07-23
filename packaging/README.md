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

**MACE ships as the conda-forge `pymace` package** (pulling pytorch, e3nn,
cuequivariance-torch, torch-ema, torchmetrics) and is a normal `run` dep, so it
is installed automatically.

**GPAW is conda-forge on x86_64 only** (linux-64 / osx-64), not yet on
osx-arm64 (Apple Silicon). The recipe gates it with a `# [x86_64]` selector, so
on x86_64 it is installed automatically, and `gpaw-data` (the PAW datasets)
ships on every platform. On osx-arm64, add GPAW with pip afterwards:

```sh
pip install gpaw        # osx-arm64 only — GPAW has no conda-forge arm64 build yet
```

(Calango runs each job in a user-selected environment, so GPAW only needs to be
importable in whatever env you point a run at — e.g. a dedicated `gpaw_env`.)

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
- Run deps (conda): `qt6-main`, `numpy`, `ase`, `scipy`, `spglib`, `pymatgen`,
  `paramiko`, `pymace` (MACE + backend), `gpaw` (x86_64 only), `gpaw-data`.
- osx-arm64 only: `pip install gpaw` (no conda-forge arm64 build yet).
  Full annotated list + version constraints: `packaging/dependencies.txt`.
- `qcustomplot` and `nlohmann_json` from the original spec were dropped:
  Calango uses custom QPainter plot widgets and Qt's own QJson, so neither is a
  real dependency — and `qcustomplot` isn't packaged on conda-forge for
  osx-arm64, which made the solve unsatisfiable.
