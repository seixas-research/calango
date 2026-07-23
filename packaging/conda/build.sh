#!/usr/bin/env bash
# Conda build script for Calango: configure with CMake + Ninja against the
# conda host environment, compile, and install the binary into $PREFIX/bin.
set -euo pipefail

# conda-build's `path:` source copies the whole working tree — including any
# local build*/ directories with a CMakeCache.txt pinned to the original source
# path. Configure into a fresh, uniquely-named directory (removed first) so the
# stale cache never collides with this out-of-tree build.
BUILD_DIR=build_conda
rm -rf "$BUILD_DIR"

# The interpreter Calango embeds at runtime is the one conda provides ($PYTHON),
# so the packaged app finds ASE/GPAW/MACE from the same environment.
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DPython3_EXECUTABLE="$PYTHON" \
    -DCALANGO_DEFAULT_PYTHON="$PREFIX/bin/python"

cmake --build "$BUILD_DIR" -j "${CPU_COUNT:-4}"

# Install. The CMake install rules are platform-specific (a .app bundle on
# macOS-bundle builds, $BINDIR on Linux), so copy the binary into $PREFIX/bin
# directly — robust on every conda platform.
mkdir -p "$PREFIX/bin"
install -m 0755 "$BUILD_DIR/calango" "$PREFIX/bin/calango"

# Desktop integration on Linux (icon + .desktop + MIME for .calproj).
if [[ "$(uname)" == "Linux" ]]; then
    install -Dm644 "$SRC_DIR/packaging/linux/calango.desktop" \
        "$PREFIX/share/applications/calango.desktop"
    install -Dm644 "$SRC_DIR/packaging/linux/calango-mime.xml" \
        "$PREFIX/share/mime/packages/calango-mime.xml"
    install -Dm644 "$SRC_DIR/assets/.internal/icon.png" \
        "$PREFIX/share/pixmaps/calango.png"
fi
