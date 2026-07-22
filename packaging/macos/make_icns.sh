#!/usr/bin/env bash
# Render a .icns icon from a square source PNG using the stock macOS
# tools (sips + iconutil). Invoked by CMake when the repository does not
# ship a pre-built assets/.internal/calango.icns.
#
#   make_icns.sh <source.png> <output.icns>
set -euo pipefail

src="$1"
out="$2"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

iconset="$tmp/calango.iconset"
mkdir "$iconset"

# The canonical iconset slots: 16/32/128/256/512 pt, each at 1x and 2x.
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$src" \
        --out "$iconset/icon_${size}x${size}.png" >/dev/null
    dbl=$((size * 2))
    sips -z "$dbl" "$dbl" "$src" \
        --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done

iconutil -c icns "$iconset" -o "$out"
