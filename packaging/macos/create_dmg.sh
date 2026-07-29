#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# create_dmg.sh — build and package Calango as a drag-and-install macOS .dmg,
# saved as packaging/macos/dist/Calango-<version>-macOS.dmg.
#
# Pipeline:
#   0. Provision a relocatable CPython (python-build-standalone) with ASE and
#      friends preinstalled, so the shipped .app needs no Python on the target
#      machine. Skipped with --no-python or when you supply your own tree.
#   1. Configure + build the .app bundle (-DCALANGO_MACOS_BUNDLE=ON).
#   2. Package with CPack's DragNDrop generator, which runs macdeployqt
#      (two-pass, see CMakeLists), embeds the Python.framework, adds the
#      /Applications symlink and the volume icon, and compresses the image.
#   3. Fall back to a manual `cmake --install` + create-dmg/hdiutil assembly
#      when cpack is unavailable or `--manual` is passed.
#   4. Move the result to Calango-<version>-macOS.dmg, then mount it and prove
#      the packaged app can import ASE before declaring success.
#
# Usage:  packaging/macos/create_dmg.sh [--manual] [--skip-build] [--no-python]
#
# Overridable via environment:
#   BUILD_DIR                 build/output directory (default build-macos-bundle)
#                             Point this OUTSIDE any cloud-synced folder. A
#                             provider such as Dropbox stamps an xattr on every
#                             file it syncs and refuses to let it be removed,
#                             which makes the signed bundle fail
#                             `codesign --verify --strict`. Absolute paths work;
#                             prefer a scratch location such as
#                             /tmp/calango-build over the home directory, which
#                             this override should never clutter.
#   DIST_DIR                  where the .dmg lands   (default: repo root)
#   CMAKE_PREFIX_PATH         Qt prefix          (default /opt/homebrew/opt/qt)
#   PYTHON_BIN                interpreter the app links libpython against
#                             (default: ./.venv/bin/python, else python3 on PATH)
#   CALANGO_EMBEDDED_PYTHON_DIR  standalone Python payload to bundle; when unset
#                             the script downloads and builds one automatically
#   CALANGO_EMBEDDED_PACKAGES pip packages for that payload
#                             (default: ase numpy scipy spglib matplotlib
#                              imageio imageio-ffmpeg)
#   JOBS                      parallel build jobs (default: CPU count)
#
# NOTE on the Python version: PythonEngine runs an *in-process* interpreter
# against the libpython the binary links (PYTHON_BIN's), and only points
# config.executable at the bundled tree. The bundled standalone CPython must
# therefore be the same X.Y as PYTHON_BIN or its extension modules (numpy,
# scipy, ...) will not load. The script enforces this automatically.
# ---------------------------------------------------------------------------
set -euo pipefail

[[ "$(uname)" == "Darwin" ]] || {
    echo "error: this script builds a macOS .dmg and must run on macOS." >&2
    exit 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-macos-bundle}"
DIST_DIR="${DIST_DIR:-$REPO_ROOT}"
QT_PREFIX="${CMAKE_PREFIX_PATH:-/opt/homebrew/opt/qt}"
EMBED_PY="${CALANGO_EMBEDDED_PYTHON_DIR:-}"
EMBED_PKGS="${CALANGO_EMBEDDED_PACKAGES:-ase numpy scipy spglib matplotlib imageio imageio-ffmpeg}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

# Whether the build tree already exists must be sampled before anything
# below creates it, or --skip-build would skip a build that never happened.
BUILD_DIR_EXISTED=0
[[ -d "$BUILD_DIR" ]] && BUILD_DIR_EXISTED=1

# The interpreter the app links libpython against. A missing project .venv
# must not break a plain run, so fall back to python3 on PATH.
if [[ -z "${PYTHON_BIN:-}" ]]; then
    if [[ -x "$REPO_ROOT/.venv/bin/python" ]]; then
        PYTHON_BIN="$REPO_ROOT/.venv/bin/python"
    else
        PYTHON_BIN="$(command -v python3 || true)"
    fi
fi
[[ -x "$PYTHON_BIN" ]] || {
    echo "error: no usable Python interpreter (set PYTHON_BIN)" >&2; exit 1; }

MANUAL=0
SKIP_BUILD=0
WITH_PYTHON=1
for arg in "$@"; do
    case "$arg" in
        --manual)     MANUAL=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        --no-python)  WITH_PYTHON=0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

command -v cmake >/dev/null || { echo "error: cmake not found" >&2; exit 1; }
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
[[ -x "$MACDEPLOYQT" ]] || MACDEPLOYQT="$(command -v macdeployqt || true)"
[[ -n "$MACDEPLOYQT" ]] || echo "warning: macdeployqt not found — the .app may not be self-contained" >&2

# --- 0. Standalone Python payload (ASE preinstalled) -----------------------
# python-build-standalone's "install_only" trees are relocatable, unlike a
# project .venv whose bin/python links back to this machine's interpreter.
# The tree is cached in the build directory, so re-runs are essentially free.
PBS_REPO="astral-sh/python-build-standalone"

provision_embedded_python() {
    local want_xyz want_xy arch_tag cache url tgz
    want_xyz="$("$PYTHON_BIN" -c 'import sys; print("%d.%d.%d" % sys.version_info[:3])')"
    want_xy="${want_xyz%.*}"
    case "$(uname -m)" in
        arm64)  arch_tag="aarch64" ;;
        x86_64) arch_tag="x86_64" ;;
        *) echo "error: unsupported arch $(uname -m)" >&2; return 1 ;;
    esac

    cache="$REPO_ROOT/$BUILD_DIR/embedded-python"
    # Reuse only a cache that matches this interpreter's X.Y and still imports
    # ASE — a stale tree from a different Python would fail at runtime.
    if [[ -x "$cache/python/bin/python3" ]] \
       && "$cache/python/bin/python3" -c "import sys, ase; assert '%d.%d' % sys.version_info[:2] == '$want_xy'" 2>/dev/null; then
        echo "==> Reusing cached embedded Python ($cache/python)"
        EMBED_PY="$cache/python"
        return 0
    fi

    echo "==> Provisioning standalone CPython $want_xyz ($arch_tag) with: $EMBED_PKGS"
    command -v curl >/dev/null || { echo "error: curl not found" >&2; return 1; }

    local api="https://api.github.com/repos/$PBS_REPO/releases/latest"
    local index
    index="$(curl -fsSL --max-time 60 "$api")" || {
        echo "error: could not query $PBS_REPO releases" >&2; return 1; }

    # Release assets encode the '+' of the version tag as %2B, so match both
    # spellings. Prefer PYTHON_BIN's exact X.Y.Z; any X.Y build is ABI-compatible.
    local plus='\(+\|%2B\)'
    url="$(grep -o "https://[^\"]*cpython-${want_xyz}${plus}[0-9]*-${arch_tag}-apple-darwin-install_only\.tar\.gz" <<<"$index" | head -1)"
    if [[ -z "$url" ]]; then
        url="$(grep -o "https://[^\"]*cpython-${want_xy}\.[0-9]*${plus}[0-9]*-${arch_tag}-apple-darwin-install_only\.tar\.gz" <<<"$index" | sort -V | tail -1)"
        if [[ -n "$url" ]]; then
            echo "note: no exact $want_xyz build; using $(basename "$url")"
        fi
    fi
    [[ -n "$url" ]] || {
        echo "error: no python-build-standalone $want_xy build for $arch_tag." >&2
        echo "       Set CALANGO_EMBEDDED_PYTHON_DIR, or pass --no-python." >&2
        return 1; }

    rm -rf "$cache"; mkdir -p "$cache"
    tgz="$cache/cpython.tar.gz"
    echo "==> Downloading $(basename "$url")"
    curl -fsSL --max-time 900 -o "$tgz" "$url" || {
        echo "error: download failed" >&2; return 1; }
    tar -xzf "$tgz" -C "$cache" || { echo "error: extract failed" >&2; return 1; }
    rm -f "$tgz"
    [[ -x "$cache/python/bin/python3" ]] || {
        echo "error: unexpected archive layout (no python/bin/python3)" >&2; return 1; }

    echo "==> Installing $EMBED_PKGS into the standalone tree"
    # shellcheck disable=SC2086 -- EMBED_PKGS is an intentional word list.
    "$cache/python/bin/python3" -m pip install --no-input --disable-pip-version-check \
        -q $EMBED_PKGS || { echo "error: pip install failed" >&2; return 1; }

    # Trim build-machine noise; __pycache__ is regenerated on demand.
    find "$cache/python" -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true

    "$cache/python/bin/python3" -c "import ase, numpy; print('    ASE', ase.__version__, '/ NumPy', numpy.__version__)" || {
        echo "error: ASE is not importable in the provisioned tree" >&2; return 1; }

    EMBED_PY="$cache/python"
}

if [[ "$WITH_PYTHON" -eq 1 && -z "$EMBED_PY" ]]; then
    provision_embedded_python
elif [[ -n "$EMBED_PY" ]]; then
    echo "==> Using supplied embedded Python: $EMBED_PY"
else
    echo "==> --no-python: the .app will resolve Python at runtime"
fi

# --- 1. Configure + build --------------------------------------------------
if [[ "$SKIP_BUILD" -eq 0 || "$BUILD_DIR_EXISTED" -eq 0 ]]; then
    echo "==> Configuring bundle build in $BUILD_DIR"
    cmake -S . -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCALANGO_MACOS_BUNDLE=ON \
        -DPython3_EXECUTABLE="$PYTHON_BIN" \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        ${EMBED_PY:+-DCALANGO_EMBEDDED_PYTHON_DIR="$EMBED_PY"}

    echo "==> Building Calango.app (-j $JOBS)"
    cmake --build "$BUILD_DIR" -j "$JOBS"
fi

# App bundle artifact check.
APP="$(/usr/bin/find "$BUILD_DIR" -maxdepth 2 -name 'calango.app' -o -maxdepth 2 -name 'Calango.app' 2>/dev/null | head -1)"
[[ -n "$APP" ]] || echo "note: an installed calango.app is produced by the packaging step below."

VERSION="$(grep -E '^CMAKE_PROJECT_VERSION:' "$BUILD_DIR/CMakeCache.txt" | cut -d= -f2)"
ARCH="$(uname -m)"
ICNS=""
for candidate in \
    "$REPO_ROOT/assets/calango/calango.icns" \
    "$REPO_ROOT/assets/calango/icon.icns" \
    "$BUILD_DIR/calango.icns"; do
    [[ -f "$candidate" ]] && { ICNS="$candidate"; break; }
done
[[ -z "$ICNS" ]] || echo "==> Volume icon: $ICNS"

# --- 3b. Manual assembly ---------------------------------------------------
make_dmg_manual() {
    local stage="$BUILD_DIR/_dmg_stage"
    rm -rf "$stage"; mkdir -p "$stage"
    echo "==> Installing bundle into staging tree (runs macdeployqt + embeds Python)"
    cmake --install "$BUILD_DIR" --prefix "$stage"
    ln -sf /Applications "$stage/Applications"

    local dmg="$BUILD_DIR/calango-${VERSION}-macos-${ARCH}.dmg"
    rm -f "$dmg"
    local appbundle
    appbundle="$(/usr/bin/find "$stage" -maxdepth 1 -iname 'calango.app' | head -1)"
    if command -v create-dmg >/dev/null; then
        echo "==> Packaging with create-dmg"
        create-dmg --volname "Calango $VERSION" ${ICNS:+--volicon "$ICNS"} \
            --app-drop-link 600 185 "$dmg" "$appbundle" || \
            hdiutil create -volname "Calango $VERSION" -srcfolder "$stage" \
                -ov -format UDZO "$dmg"
    else
        echo "==> Packaging with hdiutil"
        hdiutil create -volname "Calango $VERSION" -srcfolder "$stage" \
            -ov -format UDZO "$dmg"
    fi
    echo "$dmg"
}

# --- 3a. CPack (preferred) -------------------------------------------------
echo "==> Packaging Calango $VERSION ($ARCH)"
DMG=""
if [[ "$MANUAL" -eq 0 ]] && command -v cpack >/dev/null; then
    ( cd "$BUILD_DIR" && cpack -G DragNDrop ${ICNS:+-D CPACK_DMG_VOLUME_ICON="$ICNS"} )
    DMG="$(ls -t "$BUILD_DIR"/*.dmg 2>/dev/null | head -1 || true)"
fi
if [[ -z "$DMG" ]]; then
    echo "==> Falling back to manual DMG assembly"
    DMG="$(make_dmg_manual | tail -1)"
fi

# --- Finalize: place the artifact at dist/Calango-<version>-macOS.dmg -------
if [[ -z "$DMG" || ! -f "$DMG" ]]; then
    echo "error: no .dmg was produced" >&2
    exit 1
fi
mkdir -p "$DIST_DIR"
FINAL="$DIST_DIR/Calango-${VERSION}-macOS.dmg"
mv -f "$DMG" "$FINAL"

# --- Verify ----------------------------------------------------------------
if ! hdiutil imageinfo "$FINAL" >/dev/null 2>&1; then
    echo "error: produced $FINAL but it is not a valid disk image" >&2
    exit 1
fi

# Mount the finished image and ask the shipped binary to resolve its own
# interpreter. This is the check that actually matters: a bundled ASE that
# the in-process interpreter cannot import looks identical to a good build
# from the outside, and `hdiutil imageinfo` passes happily either way.
PROBE="not run"
MNT="$(mktemp -d)"
PROBE_LOG="$(mktemp)"
if hdiutil attach -nobrowse -quiet -readonly -mountpoint "$MNT" "$FINAL" 2>/dev/null; then
    APP_IN_DMG="$(/usr/bin/find "$MNT" -maxdepth 1 -iname 'calango.app' | head -1)"
    if [[ -n "$APP_IN_DMG" ]] && env -u VIRTUAL_ENV -u CALANGO_PYTHON \
            -u PYTHONHOME -u PYTHONPATH \
            "$APP_IN_DMG/Contents/MacOS/calango" --probe-python >"$PROBE_LOG" 2>&1; then
        PROBE="$(grep -E '^(python|ase):' "$PROBE_LOG" | tr '\n' ' ')"
    else
        PROBE="FAILED — $(tail -2 "$PROBE_LOG" 2>/dev/null | tr '\n' ' ')"
    fi
    hdiutil detach "$MNT" -quiet || true
fi
rm -rf "$MNT"; rm -f "$PROBE_LOG"

echo ""
echo "==> SUCCESS"
echo "    Artifact : $FINAL"
echo "    Size     : $(du -h "$FINAL" | cut -f1)"
echo "    Verified : valid macOS disk image (hdiutil imageinfo)"
echo "    Probe    : $PROBE"
if [[ "$PROBE" == FAILED* ]]; then
    echo "error: the packaged app could not resolve its Python environment" >&2
    exit 1
fi
