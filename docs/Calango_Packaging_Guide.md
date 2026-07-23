# Calango — Packaging Guide

How to turn a source checkout into a distributable artifact: a macOS `.dmg`,
a Conda package, or a Debian/Ubuntu `.deb`.

Every path below is relative to the repository root. Script names are given
exactly as they exist in the tree — check `packaging/` if a command is not
found rather than guessing a variant.

> **Dropbox note.** This repository is commonly synced through Dropbox, which
> strips the executable bit from shell scripts. Every script here is therefore
> invoked as `bash <script>` rather than `./<script>`, and the CMake install
> hooks do the same. Do not rely on `chmod +x` surviving a sync.

---

## 1. Prerequisites common to all targets

| Requirement | Version | Notes |
|---|---|---|
| CMake | ≥ 3.21 | needs `qt_add_executable`, `qt_add_resources` |
| C++ compiler | C++20 | clang 14+, gcc 11+, AppleClang 14+ |
| Qt | ≥ 6.4 | components `Widgets OpenGLWidgets Concurrent Svg` |
| Python | ≥ 3.9 | with `Development.Embed`; must have ASE installed |
| pybind11 | ≥ 2.12 | auto-fetched if not found on the system |

The interpreter CMake finds at configure time is baked in as the *fallback*
runtime interpreter. Point it at an environment that actually has ASE:

```bash
cmake -S . -B build -DPython3_EXECUTABLE=$PWD/.venv/bin/python
```

Verify the embedded interpreter resolves before packaging anything:

```bash
./build/calango --probe-python     # exit 0 means ASE is importable
```

---

## 2. macOS `.dmg`

### 2.1 One-shot script

```bash
bash packaging/macos/create_dmg.sh
```

This drives the whole pipeline: configures `-DCALANGO_MACOS_BUNDLE=ON` into
`build-macos-bundle/`, builds, and runs `cpack -G DragNDrop`. The result is
`calango-<version>-macos-arm64.dmg` containing `Calango.app` and an
`Applications` symlink for drag-and-drop install.

Overridable environment variables:

| Variable | Purpose |
|---|---|
| `BUILD_DIR` | build directory (default `build-macos-bundle`) |
| `CMAKE_PREFIX_PATH` | Qt location (default `/opt/homebrew/opt/qt`) |
| `PYTHON_BIN` | interpreter baked in as the fallback |
| `CALANGO_EMBEDDED_PYTHON_DIR` | relocatable Python tree to bundle (see 2.3) |
| `JOBS` | parallel build jobs |

### 2.2 Manual equivalent

```bash
cmake -S . -B build-macos-bundle \
    -DCMAKE_BUILD_TYPE=Release \
    -DCALANGO_MACOS_BUNDLE=ON \
    -DPython3_EXECUTABLE=$PWD/.venv/bin/python \
    -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build-macos-bundle -j 8
cd build-macos-bundle && cpack -G DragNDrop
```

### 2.3 Bundling a Python interpreter

Without `CALANGO_EMBEDDED_PYTHON_DIR` the installed app falls back to the
runtime resolution order (`$CALANGO_PYTHON` → `$VIRTUAL_ENV` → system
`python3`), which means the target machine must already have ASE.

To ship a self-contained app, point the variable at a **relocatable** Python
tree that has ASE installed — for example a
[python-build-standalone](https://github.com/astral-sh/python-build-standalone)
distribution. It must contain `bin/python3`:

```bash
CALANGO_EMBEDDED_PYTHON_DIR=/path/to/relocatable-python \
    bash packaging/macos/create_dmg.sh
```

The interpreter lands at `Calango.app/Contents/Resources/python/bin/python3`,
which is the first location `PythonEngine` checks inside a bundle.

> A plain project `.venv` is **not** relocatable — its `bin/python` symlinks
> back to the build machine's base interpreter. Bundling one produces an app
> that works only on the machine that built it.

### 2.4 Known-good behaviours that look like errors

These lines are expected during a successful build; judge success by the
final bundle, not by the log:

* `macdeployqt` is run **twice**, with `-libpath=/opt/homebrew/opt/qt/lib`.
  The first pass emits `ERROR: Cannot resolve rpath` lines — transitive
  dependencies of freshly copied frameworks (QtGui → QtDBus) are not visible
  until the second scan.
* `ERROR: no file at .../python@3.14/lib/.../Python` is the Python.framework
  case; `packaging/macos/embed_python_framework.sh` handles Python *after*
  `macdeployqt` runs.

### 2.5 Verifying the artifact

```bash
codesign --verify --deep --strict build-macos-bundle/_CPack_Packages/*/Calango.app
build-macos-bundle/_CPack_Packages/*/Calango.app/Contents/MacOS/calango --probe-python
```

`embed_python_framework.sh` ad-hoc-signs the bundle with `--deep`, because
`install_name_tool` and `macdeployqt` both invalidate existing signatures and
arm64 refuses to load unsigned/invalid binaries.

Clean staging trees with `cmake -E rm -rf build-macos-bundle/_CPack_Packages`.

---

## 3. Conda package

```bash
bash packaging/conda/create_conda_osx-arm64.sh
```

The recipe lives in `packaging/conda/meta.yaml`, with the build steps in
`packaging/conda/build.sh`; output lands in `packaging/conda/dist/`.

To build with `conda-build` directly:

```bash
conda install -n base conda-build
conda build packaging/conda --output-folder packaging/conda/dist
```

Install and smoke-test the result:

```bash
conda create -n calango-test -c ./packaging/conda/dist calango
conda activate calango-test
calango --probe-python
```

The Conda route differs from the `.dmg` in one important way: Python and ASE
come from the **environment**, not from a bundled interpreter, so `meta.yaml`
declares them as real runtime dependencies. Keep that dependency list in step
with `packaging/dependencies.txt` (section 5).

---

## 4. Debian / Ubuntu `.deb`

```bash
bash packaging/linux/build_deb.sh
```

Or via CPack directly:

```bash
cmake -S . -B build-deb -DCMAKE_BUILD_TYPE=Release
cmake --build build-deb -j 8
cd build-deb && cpack -G DEB
```

The package name follows `DEB-DEFAULT` (`calango_<version>_<arch>.deb`) and
installs:

* the `calango` binary and its Qt resources;
* `packaging/linux/calango.desktop` — the application menu entry;
* `packaging/linux/calango-mime.xml` — registers
  `application/x-calango-project` so `.calproj` files open in Calango;
* the icon rendered from `assets/.internal/icon.png`;
* `postinst` / `postrm` maintainer scripts that refresh the MIME and desktop
  databases.

With `CALANGO_EMBEDDED_PYTHON_DIR` set, the interpreter installs to
`../lib/calango/python/bin/python3` relative to the binary, which is where
`PythonEngine` looks on Linux.

Inspect before publishing:

```bash
dpkg -c build-deb/calango_*.deb        # file list
dpkg -I build-deb/calango_*.deb        # control metadata + dependencies
sudo apt install ./build-deb/calango_*.deb
```

> `dpkg-shlibdeps` derives the dependency list from what the binary actually
> links. This is why `PYBIND11_FINDPYTHON=ON` matters: without it pybind11's
> legacy search can link a Conda/Homebrew `libpython`, which makes the
> resulting `.deb` non-portable.

---

## 5. Managing Python dependencies

`packaging/dependencies.txt` is the authoritative inventory. It is a
documentation file, not a `pip` requirements file — it records every build-
and run-time dependency with its version constraint and the feature it
enables, grouped by layer and tagged:

```
[required] / [optional]   — is the feature subset lost without it?
[build]    / [runtime]    — when is it needed?
```

### Core (required at runtime)

| Package | Enables |
|---|---|
| `ase` | everything — file I/O, builders, calculators, trajectories |
| `numpy` | array interchange across the pybind11 bridge |

### Optional (feature subsets)

| Package | Enables | Degrades to |
|---|---|---|
| `mace-torch` | MACE ML potential | engine unavailable in wizards |
| `gpaw` | DFT bands, PDOS, band unfolding | other backends still usable |
| `spglib` | symmetry detection, cell standardization | those tools report why |
| `pillow` | GIF export | error naming the missing package |
| `imageio` + `imageio-ffmpeg` | MP4 export | same |
| `paramiko` | Remote Access (SSH/SFTP) | remote panel disabled |

Every optional dependency is guarded at its call site with an actionable
message naming the `pip install` line — a missing package must never surface
as a bare `ImportError` traceback in the job log.

### When adding a dependency

1. Add the entry to `packaging/dependencies.txt` with its tags and constraint.
2. Add it to `packaging/conda/meta.yaml` if it is required at runtime.
3. Guard the import at its call site with a message naming the install command.
4. Note whether the `.dmg`'s bundled interpreter must carry it — anything in
   the *core* table must be present in `CALANGO_EMBEDDED_PYTHON_DIR`.
