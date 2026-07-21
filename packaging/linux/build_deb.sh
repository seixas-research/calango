#!/usr/bin/env bash
#
# Build the Calango Linux .deb installer from a clean, Linux-native build
# directory. Encapsulates the procedure and guardrails documented in
# docs/packaging.tex (the "Linux: building the .deb" section):
#
#   * a FRESH build directory (never a tree configured on another OS — a
#     Dropbox-synced macOS build/ bakes in /opt/homebrew paths and makes
#     cpack fail looking for its templates there);
#   * the SYSTEM python3, so the binary links the distribution's
#     libpython (owned by the libpython3.x package) and dpkg-shlibdeps can
#     resolve it into a proper, portable Depends: line.
#
# Usage:
#   packaging/linux/build_deb.sh [-p PYTHON] [-b BUILD_DIR] [-j JOBS]
#
#   -p PYTHON      Python interpreter to embed/link (default: /usr/bin/python3)
#   -b BUILD_DIR   build directory (default: build-deb)
#   -j JOBS        parallel build jobs (default: nproc)
#
# The resulting .deb and its .sha256 are left in BUILD_DIR and their paths
# printed at the end. Install with:  sudo apt install ./<path>.deb

set -euo pipefail

PYTHON=/usr/bin/python3
BUILD_DIR=build-deb
JOBS=$(nproc 2>/dev/null || echo 4)

while getopts ":p:b:j:h" opt; do
    case "$opt" in
        p) PYTHON=$OPTARG ;;
        b) BUILD_DIR=$OPTARG ;;
        j) JOBS=$OPTARG ;;
        h) sed -n '2,25p' "$0"; exit 0 ;;
        *) echo "Unknown option: -$OPTARG (use -h)" >&2; exit 2 ;;
    esac
done

# Run from the repository root regardless of where the script is invoked.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
cd "$REPO_ROOT"

# --- Preflight -------------------------------------------------------------
for tool in cmake cpack dpkg-shlibdeps; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: '$tool' not found. Install: sudo apt install cmake dpkg-dev" >&2
        exit 1
    }
done

if [ ! -x "$PYTHON" ]; then
    echo "error: python interpreter '$PYTHON' not found or not executable." >&2
    echo "       Install the system interpreter with: sudo apt install python3-dev" >&2
    exit 1
fi

# Guard against a non-system libpython (Conda/Homebrew): its libpython
# lives under the interpreter's prefix, not in a dpkg-owned system path, so
# dpkg-shlibdeps cannot resolve it and cpack fails.
PY_PREFIX=$("$PYTHON" -c 'import sys; print(sys.base_prefix)')
case "$PY_PREFIX" in
    /usr|/usr/local) ;;  # system interpreters — fine
    *)
        echo "warning: '$PYTHON' has prefix '$PY_PREFIX', which does not look" >&2
        echo "         like a system path. If it is a Conda/Homebrew Python the" >&2
        echo "         binary will link a private libpython and dpkg-shlibdeps" >&2
        echo "         will fail. Prefer -p /usr/bin/python3 (apt install python3-dev)." >&2
        ;;
esac

echo ">> Python interpreter : $PYTHON  (prefix: $PY_PREFIX)"
echo ">> Build directory     : $BUILD_DIR  (recreated clean)"
echo ">> Parallel jobs       : $JOBS"

# --- Configure (fresh) -----------------------------------------------------
rm -rf "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE="$PYTHON" \
    -G Ninja

# --- Build -----------------------------------------------------------------
cmake --build "$BUILD_DIR" -j "$JOBS"

# --- Sanity check: must link a SYSTEM libpython ----------------------------
# A libpython path under $HOME means a Conda/Homebrew lib slipped in and the
# package would be non-portable / break shlibdeps. Fail loudly before cpack.
if command -v readelf >/dev/null 2>&1; then
    if readelf -d "$BUILD_DIR/calango" 2>/dev/null \
        | grep -iE 'RUNPATH|RPATH' | grep -q "$HOME"; then
        echo "error: the binary carries an RUNPATH into \$HOME — a non-system" >&2
        echo "       libpython was linked. Re-run with -p /usr/bin/python3." >&2
        exit 1
    fi
fi

# --- Package ---------------------------------------------------------------
( cd "$BUILD_DIR" && cpack )

DEB=$(ls -1 "$BUILD_DIR"/calango_*_*.deb 2>/dev/null | head -n1 || true)
if [ -z "$DEB" ]; then
    echo "error: cpack finished but no .deb was produced." >&2
    exit 1
fi

echo
echo ">> Package : $DEB"
echo ">> Checksum: ${DEB}.sha256"
echo
echo "Install with:"
echo "    sudo apt install ./$DEB"
