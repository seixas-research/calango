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

# Ad-hoc re-sign the whole bundle: install_name_tool invalidated the main
# binary's signature, and macdeployqt leaves the Qt plugins/frameworks it
# rewrote with stale signatures (arm64 refuses to load either).
codesign --force --deep --sign - "$app"
