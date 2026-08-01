# Installation

Calango ships as native installers for macOS and Debian/Ubuntu, as a Conda package, and as source. The prebuilt installers are the fastest route to a working setup; **building from source is a standard CMake flow** and takes a few minutes on a laptop. Every installer artifact is accompanied by a `.sha256` checksum file, and the version in the file name — currently 26.7.83 — comes from a single source of truth, the `project(calango VERSION …)` line in `CMakeLists.txt`.

---

## Prebuilt installers

::::{tab-set}

:::{tab-item} macOS
A drag-and-drop disk image for Apple Silicon:

1. Download `calango-26.7.83-macos-arm64.dmg` and open it.
2. Drag `calango.app` onto the *Applications* shortcut shown next to it.

The image mounts without a license click-through, and the bundle embeds the `Python.framework` it links against, so the app launches on machines without Homebrew. The bundle is signed *ad hoc* — distribution builds are not notarized, so on first launch macOS may require right-click ▸ {guilabel}`Open` to bypass Gatekeeper.
:::

:::{tab-item} Debian / Ubuntu
A Debian package. Install it with `apt` — *not* `dpkg -i` — so the dependencies resolve automatically:

```bash
sudo apt install ./calango_26.7.83_amd64.deb
```

The package installs `/usr/bin/calango`, a desktop launcher, the application icon, and a MIME type (`application/x-calango-project`) for `.calproj` files — double-clicking a saved project in your file manager opens the whole workspace. It declares `Depends: python3 (>= 3.9)` and `Recommends: python3-ase`; without ASE the GUI starts but structure I/O and job submission are disabled (see {doc}`/python_environment`).
:::

:::{tab-item} Conda
The Conda recipe lives in `packaging/conda/` and differs from the two installers in kind: **it does not bundle an interpreter** — Conda *is* the environment, so the Python stack (`ase`, `numpy`, `scipy`, `spglib`, `pymatgen`, `paramiko`, plus `pymace` for ML potentials) is declared as ordinary run dependencies and the solver provides them. Build a local package from the repository root:

```bash
bash packaging/conda/create_conda_osx-arm64.sh
# Result: packaging/conda/dist/calango-<version>-<build>.conda
```

The script picks whichever build tool is available (`conda-build`, `boa`, or `rattler-build`) and defaults to the `libmamba` solver — the classic solver fails this recipe on the platform virtual packages. The recipe's built-in test is `calango --probe-python`, a cheap end-to-end check that the packaged binary and the packaged Python stack agree.

:::{admonition} GPAW on Apple Silicon
:class: caution
conda-forge does not build `gpaw` for `osx-arm64`, so on Apple Silicon it must be added separately with `pip install gpaw`. The PAW datasets (`gpaw-data`) *are* available on every platform and are always pulled in. Every DFT workflow depends on this — check it before concluding that a wizard is broken.
:::
:::

::::

---

## Build from source

### Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| C++ compiler | C++20 | clang 14+, gcc 11+, AppleClang 14+ |
| CMake | ≥ 3.21 | `qt_add_executable`, `qt_add_resources` |
| Qt 6 | ≥ 6.4 | Components: Widgets, OpenGLWidgets, Concurrent, Svg |
| Python | ≥ 3.9 | With development headers (`Development.Embed`) |
| pybind11 | ≥ 2.12 | Used from the system if found, otherwise fetched automatically (pinned v2.13.6) |
| OpenGL | 3.3 core | macOS provides 4.1 core; Linux needs `libGL` |

:::{note}
The *System* appearance theme (following the OS dark/light preference) needs Qt ≥ 6.5; with Qt 6.4 Calango still builds, with that one feature guarded off.
:::

On Ubuntu/Debian the whole toolchain is one line:

```bash
sudo apt install build-essential cmake ninja-build \
  qt6-base-dev libqt6svg6-dev libqt6opengl6-dev \
  libgl1-mesa-dev python3-dev python3-venv
```

On macOS, `brew install qt cmake` provides Qt at `/opt/homebrew/opt/qt`.

### Python environment for the embedded interpreter

Calango embeds CPython and drives ASE through it, so create a virtualenv *before* configuring and point CMake at it:

```bash
python3 -m venv .venv
.venv/bin/pip install ase numpy spglib pillow imageio imageio-ffmpeg paramiko
```

`ase` and `numpy` are the hard floor; the rest enable feature subsets ({doc}`/python_environment` has the full per-feature table).

### Configure, build, run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DPython3_EXECUTABLE=$PWD/.venv/bin/python \
      -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt   # your Qt prefix
cmake --build build -j

./build/calango examples/Si_diamond.vasp
./build/calango --probe-python   # verify the embedded interpreter finds ASE
```

The interpreter the build was configured against becomes the *fallback* for the embedded runtime — at run time it can always be overridden with `CALANGO_PYTHON` or an activated `VIRTUAL_ENV`, and simulation jobs select their own environments independently ({doc}`/python_environment`).

:::{admonition} Building the installers yourself
:class: caution
The `.dmg` and `.deb` come out of the same tree via CPack: configure with `-DCALANGO_MACOS_BUNDLE=ON` on macOS (then `cpack` in the build directory), or configure a *fresh* Linux build directory against `/usr/bin/python3` and run `cpack` there. The Debian binary must link the distribution's `libpython` — pointing CMake at a Conda or Homebrew interpreter makes `dpkg-shlibdeps` fail and would produce a package that is not installable elsewhere. A fully self-contained installer additionally needs a *relocatable* Python passed as `-DCALANGO_EMBEDDED_PYTHON_DIR` (a `python-build-standalone` distribution; a plain project `.venv` is not relocatable).
:::

---

## Tests

Configure with the test option, build, and run `ctest`:

```bash
cmake -S . -B build -DCALANGO_BUILD_TESTS=ON \
      -DCMAKE_BUILD_TYPE=Release \
      -DPython3_EXECUTABLE=$PWD/.venv/bin/python \
      -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build -j
ctest --test-dir build
```

The suite is GUI-free — it exercises the core physics and the embedded Python bridge. It covers three layers: the pure C++ algorithms (builders, band unfolding, magnetic space groups, phonon thermodynamics), the generated Python scripts (parsed back by AST and executed directly), and live GPAW benchmarks that *self-skip* when the response stack is absent, so a bare `ase`-only environment still yields a green run.

---

## Next steps

With Calango launching and `--probe-python` reporting an ASE version, continue with the {doc}`/quickstart` for a first calculation, or read {doc}`/python_environment` to understand how interpreters and job environments are resolved.
