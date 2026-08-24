# Calango packaging

Two distribution formats are supported. Both are built from the repository
root; substitute the Qt prefix / Python interpreter paths for your machine.

The user-facing version is the single source of truth in `CMakeLists.txt`
(`project(... VERSION ...)`) and is baked into the binary as `CALANGO_VERSION`.

---

## Dependency audit

The packaged `calango` binary links no BLAS/LAPACK implementation at all: the
native DFT and DFTB engines that were LAPACK's only consumers in this codebase
have been removed, and nothing calls `find_package(LAPACK)` any more. There is
no numerical-library vendor here to choose, or to get wrong.

The audit below is not about that and does not go away with it. It guards a
general class of defect — *a library from the build machine leaking into a
shipped artifact without being declared* — which applies to every library the
package touches: Qt, HDF5, the Python framework's extension modules, the
compiler runtime. That is why it runs on every build rather than on the ones
somebody thought were risky.

### Load paths and rpaths on macOS

The macOS audit checks **two** things, and the second is the one that matters.
Every `LC_RPATH` entry on a bundled Mach-O must be
`@loader_path`/`@executable_path`-relative; and every relative dependency must
actually **resolve** to a file present in the bundle. A bundle-relative load
path is proof of *form*, not of *resolution*: a library can carry a
correct-looking `@rpath/...` dependency and still be found only through a
leftover absolute rpath pointing at the build machine, which no clean Mac has.

`packaging/macos/strip_leftover_rpaths.sh` enforces the first half before the
audit checks it. It is wired into `CMakeLists.txt`'s install step — after
`macdeployqt`'s two passes, before the final `codesign` — and deletes any
non-relocatable `LC_RPATH` entry from every bundled Mach-O.

### What the scripts check

`packaging/linux/audit_elf_deps.py` and `packaging/macos/audit_macho_deps.py`
run automatically at the end of `build_deb.sh`/`build_dmg.sh` respectively —
after `cpack` produces the artifact, before it is moved/finalized — and fail
the packaging step (not just warn) if any shipped ELF/Mach-O needs a library
that isn't declared in the `.deb`'s own `Depends`, bundled in the package
itself, or part of the base OS. Intel MKL and the Intel OpenMP runtime
(`libmkl_*`, `libiomp5`, `libimf`, `libirc`, `libsvml`, `libintlc`) are
hard-banned in the Linux audit regardless of `Depends` — a packaged build
must never need them, full stop. The macOS audit additionally checks (see
above) that every LC_RPATH entry on every bundled Mach-O is
`@loader_path`/`@executable_path`-relative, and that every relative
dependency actually resolves to a file present in the bundle — not just
that its load-path string has the right shape.

Run either standalone against an already-built artifact:

```sh
python3 packaging/linux/audit_elf_deps.py installers/calango_<ver>_<arch>.deb
python3 packaging/macos/audit_macho_deps.py installers/calango_<ver>_macOS.dmg
```

The Linux script needs `dpkg` for an authoritative package-ownership check
(the actual gate, run on the Linux packaging machine); without it, it falls
back to a smaller soname-family heuristic (still catches an unrecognized
soname, just can't confirm exact package ownership) — useful for exercising
the script's logic on a non-Debian machine, not a substitute for the real
gate. The macOS script needs no such fallback: `otool`, always present, is
enough to tell a relative/system load path from a build-machine one.

**Known gap, not silently swept under the audit:** the macOS audit excludes
`Contents/Resources/python/*` and `Contents/Frameworks/Python.framework/*`
(`build_dmg.sh`'s two `--exclude` flags), so **that tree is not audited**. The
exclusion is a concession, not a statement that the tree is clean: an
interpreter built against Homebrew's OpenSSL, sqlite, zstd, mpdecimal or xz
carries exactly the same class of undeclared dependency in its stdlib
extension modules, and the `--probe-python` launch check catches "ASE won't
import", not "every dylib is relocatable".

Build the framework via the documented default flow — the relocatable
`python-build-standalone` tree, with no `PYTHON_BIN` /
`CALANGO_EMBEDDED_PYTHON_DIR` override — and re-run `audit_macho_deps.py`
**without** the two `--exclude` flags to confirm a given build is actually
clean there. Removing the two flags is what closes this gap for good.

---

## macOS `.dmg` (drag-and-install)

Automated end-to-end by the helper script (provision standalone Python →
configure → build → macdeployqt → embed Python.framework → CPack DragNDrop →
verify). No further steps:

```sh
packaging/macos/build_dmg.sh
# → installers/calango_<version>_macOS.dmg (created if needed)
# → installers/calango_<version>_macOS.dmg.sha256
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
Right after that, it runs `packaging/macos/audit_macho_deps.py` over the same
mounted image and fails the build if `calango` or any bundled binary or Qt
framework loads a build-machine path (Homebrew, a conda prefix, ...) — see
[Dependency audit](#dependency-audit) above for what it checks and the one
known gap (`--exclude`d) it doesn't yet cover.

The image is **ad-hoc signed and not notarized**; Gatekeeper will warn on first
launch elsewhere unless you sign with a Developer ID and notarize.

### File associations

`packaging/macos/Info.plist.in` declares `CFBundleDocumentTypes` +
`UTExportedTypeDeclarations` for every structure/trajectory/volumetric format
Calango reads that has a real, associable extension — 13 types in total, in
two tiers:

| Tier | `LSHandlerRank` | What |
| --- | --- | --- |
| Owner | `Owner` | `.calproj` — Calango's own project format |
| Alternate | `Default` / `Alternate` | Everything else: `.extxyz` (kept at `Default`, its original rank), `.xyz`, `.cif`, `.vasp`, `.traj`, `.pwi`, `.lammpstrj`, `.gjf`, `.res`, `.cube`, `.xsf`, `.h5`/`.hdf5` |

`Alternate` adds Calango to Finder's "Open With" menu for a format without
taking over the user's existing default handler — these are all
community/scientific formats other applications (VESTA, OVITO, Avogadro,
Mercury, ...) also read. `.calproj` is the only format Calango defines, so it
alone claims `Owner`.

Two things are deliberately **not** declared:

- **Extension-less filenames.** Launch Services associates by filename
  extension (or UTI), never by exact basename, so `POSCAR`/`CONTCAR` and the
  VASP volumetric family `CHGCAR`/`LOCPOT`/`PARCHG`/`ELFCAR` cannot be
  double-click-associated at all — `.vasp` and `.cube` cover the extension
  cases of the same formats. These still open fine through Calango's Open
  dialog, a CLI argument, or drag-and-drop.
- **Extensions too generic to claim system-wide**, even though Calango reads
  them (via `ase.io.read`'s own format sniffing or an explicit format hint —
  see `src/gui/FileOpenRouting.hpp`): `.in`, `.out`, `.data`, `.dump`,
  `.cell`, `.com`, `.json`, `.dat`, `.top`. Declaring these would put Calango
  in "Open With" for large numbers of files that are not Calango's concern
  (build templates, core dumps, arbitrary JSON/data files, ...).

No well-established system or community UTI was found to *import* for any of
these formats (checked specifically for HDF5, since `.h5`/`.hdf5` are generic
containers Calango does not own) — see `org.seixasresearch.calango.*` in
`UTExportedTypeDeclarations`. Where a real chemical-MIME-type convention
exists (`chemical/x-xyz`, `chemical/x-cif`, `chemical/x-gaussian-input`,
`chemical/x-gaussian-cube`), the declaration reuses it as `public.mime-type`;
everything else gets an `application/x-*` string of Calango's own.

Installing by dragging the `.app` out of the `.dmg` registers the bundle with
Launch Services automatically — no separate `lsregister` step is needed, and
this was confirmed against exactly this ad-hoc-signed, unnotarized build:
`codesign --force --deep --sign -` followed by
`lsregister -f calango.app` registers all 13 UTIs correctly (`lsregister
-dump` shows the full `claimed UTIs:` list with the right extension/MIME
tags). **Caveat:** a freshly registered, unnotarized bundle carries a
`launch-disabled` flag until Gatekeeper approves it — the same first-launch
warning already noted above for running the app directly. Practically, that
means the document *associations* appear in Finder's "Open With" menu
immediately after install, but double-clicking a document to launch Calango
for the first time may still need the one-time right-click → Open approval
(or a first ordinary launch of the app) before Gatekeeper lets it run.

On the application side, a file opened this way — double-click, "Open With",
a Dock drop, or several files selected together (each arrives as its own
`QFileOpenEvent`) — is handled by `calango::CalangoApplication`
(`src/CalangoApplication.hpp`): `MainWindow::loadFile()` for a still-starting
app, one call per event, so each opens in its own tab exactly as multiple CLI
arguments do. An event that arrives before `MainWindow` exists (the normal
case on a cold Finder launch) is queued (`src/FileOpenQueue.hpp`) and
replayed once the window is attached. `loadFile()` itself decides project vs.
structure and picks the ASE format hint (`src/gui/FileOpenRouting.hpp`) —
the same routing a `.vasp`/`.cif`/... opened via the Open dialog or a CLI
argument goes through, so format detection, error dialogs and recent-files
behave identically regardless of how the file arrived.

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
`libpython`, runs CPack, and then audits the resulting `.deb` (see
[Dependency audit](#dependency-audit) above) before publishing it.
Flags: `-p` (interpreter), `-b` (build directory), `-j` (jobs).

**Never run it under `sudo`** — only the closing `apt install` needs root. A
`sudo` run leaves `build-deb/` owned by root, and since the script begins by
deleting that directory, the next ordinary run fails at `rm -rf: Permission
denied` before doing anything. Undo with `sudo rm -rf build-deb`.

Ships the `.desktop` launcher, the app icon, and — in parity with the macOS
`.app`'s `Info.plist` (see [File associations](#file-associations) above) —
`packaging/linux/calango-mime.xml`: `application/x-calango-project` for
`.calproj`, plus the same 12-format Alternate-tier list (`.extxyz`, `.xyz`,
`.cif`, `.vasp`, `.traj`, `.pwi`, `.lammpstrj`, `.gjf`, `.res`, `.cube`,
`.xsf`, `.h5`/`.hdf5`), declared self-contained rather than assuming the
optional `chemical-mime-data` package is installed. `dpkg-shlibdeps` derives
the Qt6/OpenGL/libpython runtime dependencies from the linked shared
libraries — `apt install ./calango_*.deb` resolves all of it, no separate
step for the user. `packaging/linux/audit_elf_deps.py` (wired into
`build_deb.sh`, see [Dependency audit](#dependency-audit) above)
verifies that derivation actually covered everything shipped, rather than
trusting it silently.

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
