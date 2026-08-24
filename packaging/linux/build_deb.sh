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
#   packaging/linux/build_deb.sh [-p PYTHON] [-b BUILD_DIR] [-j JOBS] [-q QT_PREFIX]
#
#   -p PYTHON      Python interpreter to embed/link (default: /usr/bin/python3)
#   -b BUILD_DIR   build directory (default: build-deb)
#   -j JOBS        parallel build jobs (default: nproc)
#   -q QT_PREFIX   CMAKE_PREFIX_PATH entry for a non-system Qt6 (e.g. a
#                  Qt maintenance-tool/aqtinstall tree such as
#                  ~/Qt/6.8.3/gcc_64). Default: unset, so CMake resolves
#                  Qt6 off the system's own search path (apt's qt6-base-dev).
#                  Needed only when the distro's packaged Qt6 is older than
#                  the source tree's actual minimum (CMakeLists.txt states
#                  6.4, but the source may since have grown a call that
#                  needs newer Qt — check the compiler error, not just the
#                  find_package version, before reaching for this).
#                  IMPORTANT: a QT_PREFIX outside a system package path
#                  (anything dpkg does not own, e.g. under $HOME) breaks the
#                  "portable Depends:" promise this script otherwise makes —
#                  dpkg-shlibdeps cannot attribute those .so files to an
#                  installable package, and the resulting .deb will not run
#                  on a machine that lacks that same private Qt tree. Use
#                  this to validate that the code builds against a newer
#                  Qt, not to ship the resulting .deb to end users, unless
#                  you also arrange to bundle/deploy the Qt libraries (not
#                  done by this script).
#
# The resulting .deb and its .sha256 are moved to ./installers (created if
# needed) and BUILD_DIR is deleted — only the package matters afterward
# (e.g. for uploading to the site); the build tree is a disposable
# intermediate. Their final paths are printed at the end.
# Install with:  sudo apt install ./installers/<name>.deb

set -euo pipefail

PYTHON=/usr/bin/python3
BUILD_DIR=build-deb
JOBS=$(nproc 2>/dev/null || echo 4)
QT_PREFIX=""

while getopts ":p:b:j:q:h" opt; do
    case "$opt" in
        p) PYTHON=$OPTARG ;;
        b) BUILD_DIR=$OPTARG ;;
        j) JOBS=$OPTARG ;;
        q) QT_PREFIX=$OPTARG ;;
        h) sed -n '2,38p' "$0"; exit 0 ;;
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
if [ -n "$QT_PREFIX" ]; then
    echo ">> Qt prefix           : $QT_PREFIX  (non-default — see -q in -h)"
fi

# --- Configure (fresh) -----------------------------------------------------
rm -rf "$BUILD_DIR"
CMAKE_ARGS=(
    -S . -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DPython3_EXECUTABLE="$PYTHON"
    -G Ninja
)
[ -n "$QT_PREFIX" ] && CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$QT_PREFIX")
cmake "${CMAKE_ARGS[@]}"

# --- Build -----------------------------------------------------------------
cmake --build "$BUILD_DIR" -j "$JOBS"

# --- Sanity check: the binary must not point into $HOME --------------------
# ANY RUNPATH/RPATH entry under $HOME (Conda/Homebrew libpython, a private
# aqtinstall Qt tree, anything else linked from a user prefix in the future)
# means dpkg-shlibdeps cannot attribute that library to an installable
# package and the resulting .deb will not run on a machine that lacks that
# exact private tree — the opposite of what this script promises. Checked
# generically (not "just python", not "just Qt") so a future dependency that
# happens to resolve to a $HOME path is caught here too, not discovered by
# whoever installs the package.
#
# -q QT_PREFIX is the one deliberate, opted-in exception: it exists
# specifically to validate a build against a newer Qt than the system has,
# and its own warning above already says the result isn't for distribution.
# Without -q (the normal, "produce a shippable .deb" invocation) this is a
# hard failure, not a warning — silently downgrading it here would recreate
# by mistake the exact non-portable package -q exists to opt into on purpose.
if command -v readelf >/dev/null 2>&1; then
    HOME_RUNPATH=$(readelf -d "$BUILD_DIR/calango" 2>/dev/null \
        | grep -iE 'RUNPATH|RPATH' | grep -F "$HOME" || true)
    if [ -n "$HOME_RUNPATH" ]; then
        if [ -n "$QT_PREFIX" ]; then
            echo "warning: binary carries a \$HOME RUNPATH (expected: -q was" >&2
            echo "         given). This .deb is NOT portable — see -h." >&2
            echo "         $HOME_RUNPATH" >&2
        else
            echo "error: the binary carries a RUNPATH into \$HOME — a private," >&2
            echo "       non-system library was linked (Conda/Homebrew python," >&2
            echo "       or similar). The resulting .deb would not run on a" >&2
            echo "       machine that lacks that same private path." >&2
            echo "       $HOME_RUNPATH" >&2
            echo "       Re-run with -p /usr/bin/python3, or if this is a" >&2
            echo "       deliberate -q Qt build, pass -q again to acknowledge it." >&2
            exit 1
        fi
    fi
fi

# --- Package ---------------------------------------------------------------
( cd "$BUILD_DIR" && cpack )

DEB=$(ls -1 "$BUILD_DIR"/calango_*_*.deb 2>/dev/null | head -n1 || true)
if [ -z "$DEB" ]; then
    echo "error: cpack finished but no .deb was produced." >&2
    exit 1
fi

# --- Audit: every NEEDED library must be declared, bundled, or base-system -
# dpkg-shlibdeps (CPACK_DEBIAN_PACKAGE_SHLIBDEPS, CMakeLists.txt) derives
# Depends: from whatever the binary actually links — but a library it
# cannot attribute to any installed package, it silently DROPS rather than
# failing the build on. A .deb can therefore install cleanly and die at
# launch on any machine that lacks a library the build machine happened to
# have — with nothing in the control file to say so. This is the backstop
# that catches such a leak here, not on an end user's machine.
echo ">> Auditing shipped ELF binaries for undeclared library dependencies"
if ! python3 "$SCRIPT_DIR/audit_elf_deps.py" "$DEB"; then
    echo "error: audit_elf_deps.py found a NEEDED library that is not" >&2
    echo "       declared in the .deb's own Depends — see output above." >&2
    echo "       This package would fail to run (not just fail to" >&2
    echo "       install) on a clean machine. Fix whatever pulled the" >&2
    echo "       library in (check find_package()/find_library() calls" >&2
    echo "       and the build machine's library search path in" >&2
    echo "       CMakeLists.txt) or, if it is a genuine new runtime" >&2
    echo "       dependency, add it to CPACK_DEBIAN_PACKAGE_DEPENDS." >&2
    exit 1
fi

# --- Publish: only the .deb (+ its checksum) is the deliverable -------------
# Everything else in BUILD_DIR is a disposable intermediate — object files,
# CMake cache, the unpacked staging tree cpack built the .deb from. The site
# upload only ever wants the package, so move it out to a stable, version-
# agnostic location and throw the rest away rather than leaving a stale
# multi-GB build tree (on a Dropbox-synced repo, see CLAUDE.md) lying around
# from every run.
INSTALL_DIR="$REPO_ROOT/installers"
mkdir -p "$INSTALL_DIR"
mv "$DEB" "${DEB}.sha256" "$INSTALL_DIR/"
DEB="$INSTALL_DIR/$(basename "$DEB")"
rm -rf "$BUILD_DIR"

echo
echo ">> Package : $DEB"
echo ">> Checksum: ${DEB}.sha256"
echo
echo "Install with:"
echo "    sudo apt install $DEB"
