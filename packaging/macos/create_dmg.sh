#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# create_dmg.sh — build and package Calango as a drag-and-install macOS .dmg,
# saved as packaging/macos/dist/Calango-<version>-macOS.dmg.
#
# Pipeline:
#   1. Configure + build the .app bundle (-DCALANGO_MACOS_BUNDLE=ON).
#   2. Package with CPack's DragNDrop generator, which runs macdeployqt
#      (two-pass, see CMakeLists), embeds the Python.framework, adds the
#      /Applications symlink and the volume icon, and compresses the image.
#   3. Fall back to a manual `cmake --install` + create-dmg/hdiutil assembly
#      when cpack is unavailable or `--manual` is passed.
#   4. Move the result to dist/Calango-<version>-macOS.dmg and verify it.
#
# Usage:  packaging/macos/create_dmg.sh [--manual] [--skip-build]
#
# Overridable via environment:
#   BUILD_DIR                 build/output directory (default build-macos-bundle)
#   CMAKE_PREFIX_PATH         Qt prefix          (default /opt/homebrew/opt/qt)
#   PYTHON_BIN                embedded interpreter (default ./.venv/bin/python)
#   CALANGO_EMBEDDED_PYTHON_DIR  standalone Python payload to bundle (optional)
#   JOBS                      parallel build jobs (default: CPU count)
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
DIST_DIR="${DIST_DIR:-$SCRIPT_DIR/dist}"
QT_PREFIX="${CMAKE_PREFIX_PATH:-/opt/homebrew/opt/qt}"
PYTHON_BIN="${PYTHON_BIN:-$REPO_ROOT/.venv/bin/python}"
EMBED_PY="${CALANGO_EMBEDDED_PYTHON_DIR:-}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

MANUAL=0
SKIP_BUILD=0
for arg in "$@"; do
    case "$arg" in
        --manual)     MANUAL=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

command -v cmake >/dev/null || { echo "error: cmake not found" >&2; exit 1; }
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
[[ -x "$MACDEPLOYQT" ]] || MACDEPLOYQT="$(command -v macdeployqt || true)"
[[ -n "$MACDEPLOYQT" ]] || echo "warning: macdeployqt not found — the .app may not be self-contained" >&2

# --- 1. Configure + build --------------------------------------------------
if [[ "$SKIP_BUILD" -eq 0 || ! -d "$BUILD_DIR" ]]; then
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
    "$REPO_ROOT/assets/.internal/calango.icns" \
    "$REPO_ROOT/assets/.internal/icon.icns" \
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
if hdiutil imageinfo "$FINAL" >/dev/null 2>&1; then
    echo ""
    echo "==> SUCCESS"
    echo "    Artifact : $FINAL"
    echo "    Size     : $(du -h "$FINAL" | cut -f1)"
    echo "    Verified : valid macOS disk image (hdiutil imageinfo)"
else
    echo "error: produced $FINAL but it is not a valid disk image" >&2
    exit 1
fi
