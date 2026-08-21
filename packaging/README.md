# Calango packaging

Two distribution formats are supported. Both are built from the repository
root; substitute the Qt prefix / Python interpreter paths for your machine.

The user-facing version is the single source of truth in `CMakeLists.txt`
(`project(... VERSION ...)`) and is baked into the binary as `CALANGO_VERSION`.

---

## macOS `.dmg` (drag-and-install)

Automated end-to-end by the helper script (provision standalone Python →
configure → build → macdeployqt → embed Python.framework → CPack DragNDrop →
verify). No further steps:

```sh
packaging/macos/build_dmg.sh
# → installers/calango_<version>_macOS.dmg (created if needed)
```

It downloads a relocatable CPython (python-build-standalone) matching
`PYTHON_BIN`'s version, installs
`ase numpy scipy spglib matplotlib imageio imageio-ffmpeg dftd4 torch-dftd
phonopy xtb` into it,
and ships it at `calango.app/Contents/Resources/python`, so the installed app
needs no Python on the target machine. The tree is cached under
`$BUILD_DIR/embedded-python`, so only the first run needs network access.

Flags: `--no-python` (skip the payload), `--skip-build` (repackage only),
`--manual` (bypass CPack). Environment: `DIST_DIR`, `PYTHON_BIN`,
`CALANGO_EMBEDDED_PACKAGES`, `CALANGO_EMBEDDED_PYTHON_DIR` (supply your own
tree), `JOBS`, `CMAKE_PREFIX_PATH`. On a 16 GB machine prefer `JOBS=4`; the
default is the CPU count and the Qt translation units are memory-hungry.

Three non-obvious constraints, all of which have already caused broken
installers — please do not "simplify" them away:

- **The bundled Python must match `PYTHON_BIN`'s X.Y.** `PythonEngine` runs an
  *in-process* interpreter against the libpython the binary links, so the
  bundled tree only supplies modules; its extension modules must match that
  ABI. `build_dmg.sh` derives the version automatically.
- **A framework libpython derives `sys.path` from the framework, not from
  `config.executable`**, so `Resources/python` alone is invisible to it.
  `embed_python_framework.sh` bridges this with a `calango-embedded.pth`
  written into the embedded framework's `site-packages`.
- **The Python payload is staged after `macdeployqt`** (see the install-rule
  order in `CMakeLists.txt`). macdeployqt rewrites every Mach-O file it finds;
  with the payload already in place it mangles the standalone interpreter's
  stdlib extensions and fails on `_tkinter`, which has no headerpad to grow
  into.

`macdeployqt` prints `ERROR: Cannot resolve rpath ...` for dependencies it
cannot see on a given pass, recovers on the next one, and still exits 0.
`deploy_qt.sh` runs the two passes, keeps that output in
`$BUILD_DIR/macdeployqt.log`, and prints one summary line — a non-zero exit is
still fatal. A healthy build should be judged by the script's exit code and its
final `Probe` line, not by the absence of the word ERROR.

The script finishes by mounting the image and running `calango --probe-python`
against it, failing the build if the packaged app cannot import ASE:
`hdiutil imageinfo` alone passes happily on a bundle whose Python is broken.

The image is **ad-hoc signed and not notarized**; Gatekeeper will warn on first
launch elsewhere unless you sign with a Developer ID and notarize.

### App icon

The three brand PNGs in `assets/calango/` are not interchangeable:

| File | Used for |
| --- | --- |
| `icon_osx.png` | macOS `.icns` (Finder/Dock) and the macOS window icon — squircle **with** the macOS margin |
| `icon_linux.png` | `/usr/share/pixmaps` + the hicolor theme in the `.deb`, and the window icon everywhere except macOS — full-bleed circular badge |
| `logo.png` | in-app branding (the About dialog banner) — the bare mark, no plate |

`make_icns.sh` scales `assets/calango/icon_osx.png` straight into every
iconset slot, so **that source PNG must already carry the macOS icon margin**:
on a 1024 px canvas the icon body is 824 px (~80.5%) centred, the rest
transparent. A full-bleed source renders visibly larger than every other app in
the Dock. `icon_linux.png` is the opposite case — freedesktop launchers apply
their own padding, so it is full-bleed on purpose and must not be swapped in
here. Check a candidate before committing it:

```sh
python3 -c "import sys;import matplotlib.image as m,numpy as np;a=m.imread(sys.argv[1]);y,x=np.nonzero(a[:,:,3]>0.02);print('%.1f%% wide'%(100*(x.max()-x.min()+1)/a.shape[1]))" assets/calango/icon_osx.png
# expect ~80% for icon_osx.png, ~100% for icon_linux.png
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
packaging/linux/build_deb.sh        # → installers/calango_<version>_<arch>.deb
```

The script recreates the build directory from scratch, configures against
`/usr/bin/python3`, aborts if the binary ends up linking a non-system
`libpython`, and runs CPack. Flags: `-p` (interpreter), `-b` (build directory),
`-j` (jobs).

**Never run it under `sudo`** — only the closing `apt install` needs root. A
`sudo` run leaves `build-deb/` owned by root, and since the script begins by
deleting that directory, the next ordinary run fails at `rm -rf: Permission
denied` before doing anything. Undo with `sudo rm -rf build-deb`.

Ships the `.desktop` launcher, the `application/x-calango-project` MIME type for
`.calproj` files, `application/x-extxyz` for `.extxyz` structures, and the app
icon. `dpkg-shlibdeps` derives the Qt6/OpenGL/libpython runtime dependencies
from the linked shared libraries.

The `.deb` build is normally the first time the tree meets GCC/libstdc++ rather
than macOS Clang/libc++, so it is where a missing `#include <cstdint>` shows up
— `std::uint32_t` compiles on libc++ via transitive includes and fails on
libstdc++ 13+ with `'uint32_t' in namespace 'std' does not name a type`. The
error names the header holding the declaration, not the file you changed, and
one missing include cascades into unrelated-looking `no member named` errors.
See the packaging guide (`docs/tex/packaging/`) for the full note.

Manual equivalent:

```sh
cmake -S . -B build-deb -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build-deb -j"$(nproc)"
( cd build-deb && cpack -G DEB )     # → calango_<version>_<arch>.deb
```

The icon is `assets/calango/icon_linux.png` (never the margined `icon_osx.png`
— see [App icon](#app-icon) above). It always lands in
`/usr/share/pixmaps/calango.png`, which is size-agnostic and resolves the
`.desktop` `Icon=calango` lookup on its own. If ImageMagick (`magick` or
`convert`) is on `PATH` at configure time, CMake additionally scales it into
the exact-size `hicolor` slots (32–512 px) that modern desktops prefer;
without it the configure step logs that it is installing the pixmaps icon
only, and the launcher still works.
