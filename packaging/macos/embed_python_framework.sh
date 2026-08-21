#!/usr/bin/env bash
# Make the .app independent of the build machine's Python installation:
# copy the Python.framework version the binary links against (typically
# Homebrew's) into Contents/Frameworks, retarget the load command, and
# re-apply an ad-hoc code signature (install_name_tool invalidates the
# old one, and unsigned/broken binaries are killed on arm64).
#
# Runs at install/package time, after macdeployqt (which handles the Qt
# frameworks but skips Python).
#
#   embed_python_framework.sh <path/to/Calango.app>
set -euo pipefail

app="$1"
bin="$app/Contents/MacOS/calango"

link="$(otool -L "$bin" | awk '/Python\.framework/ {print $1; exit}')"
if [[ -z "$link" ]]; then
    exit 0 # not linked against a Python framework — nothing to do
fi
case "$link" in
@*|/System/*)
    exit 0 ;; # already bundle-relative, or provided by the OS
esac

# link looks like .../Python.framework/Versions/3.14/Python
ver_dir="$(dirname "$link")"
ver="$(basename "$ver_dir")"
dst="$app/Contents/Frameworks/Python.framework/Versions/$ver"

echo "Embedding Python.framework $ver from $ver_dir"
mkdir -p "$dst"
ditto "$ver_dir" "$dst"

# Homebrew keeps site-packages outside the Cellar behind a relative
# symlink, which dangles once the framework is copied — codesign then
# refuses the bundle. Replace it with a real (empty) directory; packages
# come from the embedded environment or the user's interpreter instead.
sp="$dst/lib/python$ver/site-packages"
if [[ -L "$sp" ]]; then
    rm "$sp"
    mkdir "$sp"
fi

# PythonEngine runs an in-process interpreter against THIS framework's
# libpython, so sys.path comes from the framework's own prefix — a framework
# build derives it from the framework location, not from config.executable.
# The bundled environment in Resources/python is therefore invisible to it.
# Bridge the two with a .pth: paths inside a .pth are resolved relative to the
# directory holding it, so this stays valid wherever the .app is dragged.
embedded_sp="$app/Contents/Resources/python/lib/python$ver/site-packages"
if [[ -d "$embedded_sp" ]]; then
    echo "Pointing framework site-packages at the embedded environment"
    mkdir -p "$sp"
    # site-packages -> ... -> Contents is seven levels up; see the layout
    # Contents/Frameworks/Python.framework/Versions/$ver/lib/python$ver/site-packages
    printf '%s\n' \
        "../../../../../../../Resources/python/lib/python$ver/site-packages" \
        > "$sp/calango-embedded.pth"
fi

# codesign validates nested frameworks and insists on the canonical
# framework layout — recreate the top-level symlinks ditto did not copy.
fw="$app/Contents/Frameworks/Python.framework"
ln -sfn "$ver" "$fw/Versions/Current"
ln -sfn "Versions/Current/Python" "$fw/Python"
ln -sfn "Versions/Current/Resources" "$fw/Resources"

install_name_tool -change "$link" \
    "@executable_path/../Frameworks/Python.framework/Versions/$ver/Python" \
    "$bin"

# Strip extended attributes from the whole bundle before signing.
#
# codesign treats any xattr as "resource fork, Finder information, or similar
# detritus": the bundle signs, then fails `codesign --verify --strict` and is
# rejected by spctl. hdiutil copies xattrs into the .dmg, so a shipped app
# inherits the problem.
#
# Must run BEFORE codesign: clearing xattrs on a signed bundle can strip the
# signature's own storage.
#
# Best-effort, because it CANNOT succeed everywhere. A cloud file provider
# (Dropbox, iCloud Drive) tags every file it syncs — `com.dropbox.attrs` — and
# denies the removal with EPERM, so a staging tree inside a synced folder can
# never be cleaned in place. The verify below is what actually enforces the
# result; the fix when it fails is to stage outside the synced folder, which
# is what BUILD_DIR is for (see build_dmg.sh).
xattr -cr "$app" 2>/dev/null || true

# Ad-hoc re-sign the whole bundle: install_name_tool invalidated the main
# binary's signature, and macdeployqt leaves the Qt plugins/frameworks it
# rewrote with stale signatures (arm64 refuses to load either).
codesign --force --deep --sign - "$app"

# Verified here rather than left to the caller: every prior step in this
# script (ditto, symlink surgery, install_name_tool) can produce a bundle that
# signs without complaint and still fails strict validation, and the failure
# only surfaces on a user's machine.
if ! codesign --verify --deep --strict "$app"; then
    echo "error: $app fails strict signature validation" >&2
    echo "hint: if the message mentions 'resource fork, Finder information, or" >&2
    echo "      similar detritus', the staging tree is inside a cloud-synced" >&2
    echo "      folder whose provider stamps xattrs that cannot be removed." >&2
    echo "      Re-run with BUILD_DIR outside it, e.g." >&2
    echo "      BUILD_DIR=/tmp/calango-build packaging/macos/build_dmg.sh" >&2
    exit 1
fi
