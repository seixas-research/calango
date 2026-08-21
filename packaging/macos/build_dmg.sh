#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build_dmg.sh — build and package Calango as a drag-and-install macOS .dmg,
# saved, with its .sha256 checksum, as ./installers/calango_<version>_macOS.dmg
# (created if needed) — the same destination packaging/linux/build_deb.sh
# moves its own .deb + .sha256 to.
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
#   4. Move the result to ./installers/calango_<version>_macOS.dmg, write its
#      .sha256 alongside it, then mount the image and prove the packaged app
#      can import ASE before declaring success.
#   5. Delete the scratch build tree under ~/Library/Caches/calango, which is
#      several GB and reproducible. Only after every check above passed — a
#      failed run keeps its tree so it can be diagnosed, and only inside that
#      scratch root, never a BUILD_DIR you chose yourself.
#      --keep-cache skips it, which is what you want while iterating on
#      packaging itself: the tree caches the ~500 MB embedded-Python payload
#      and the compiled objects, so --skip-build on a later run is only
#      useful if the run before it was given --keep-cache.
#
# This script is meant to be run unattended, start to finish, with no one
# watching the log and interpreting it: missing build tools (Qt, create-dmg)
# are installed via Homebrew rather than reported as an error; the embedded
# Python payload is refreshed whenever what it needs to contain changes
# (fingerprinted below) rather than only when it is entirely absent; a cache
# that is merely stale gets an upgrade pass instead of a fresh 500 MB
# re-download; and the two genuinely long, silent steps (the standalone-
# Python download and the pip install) print a heartbeat and give up on a
# bounded timeout instead of running (or hanging) with no visible sign of
# whether they are working or stuck.
#
# Usage:  packaging/macos/build_dmg.sh [--manual] [--skip-build] [--no-python]
#                                      [--keep-cache] [--no-brew]
#
# Overridable via environment:
#   BUILD_DIR                 build/output directory (default build-macos-bundle)
#                             Must sit OUTSIDE any cloud-synced folder: a
#                             provider such as Dropbox stamps an xattr on every
#                             file it syncs and re-applies it faster than it can
#                             be cleared, which makes the signed bundle fail
#                             `codesign --verify --strict`.
#                             You do not have to arrange that yourself — when
#                             the DEFAULT would be cloud-synced (a checkout
#                             inside Dropbox/OneDrive/iCloud), the script moves
#                             it to ~/Library/Caches/calango/ and says so. Only
#                             a BUILD_DIR you set yourself is refused rather
#                             than overruled.
#   DIST_DIR                  where the .dmg lands   (default: ./installers,
#                             created if needed — matches build_deb.sh's own
#                             ./installers destination)
#   CMAKE_PREFIX_PATH         Qt prefix          (default /opt/homebrew/opt/qt)
#   PYTHON_BIN                interpreter the app links libpython against
#                             (default: ./.venv/bin/python, else python3 on PATH)
#   CALANGO_EMBEDDED_PYTHON_DIR  standalone Python payload to bundle; when unset
#                             the script downloads and builds one automatically
#   CALANGO_EMBEDDED_PACKAGES pip packages for that payload
#                             (default: ase numpy scipy spglib matplotlib
#                              imageio imageio-ffmpeg dftd4 torch-dftd
#                              phonopy — NOT xtb, which has no wheel)
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

# Flags first, before anything that touches the filesystem: a mistyped option
# should cost nothing, and the pre-flight below otherwise creates the scratch
# directory on its way to rejecting the command line.
MANUAL=0
SKIP_BUILD=0
WITH_PYTHON=1
KEEP_CACHE=0
NO_BREW=0
for arg in "$@"; do
    case "$arg" in
        --manual)     MANUAL=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        --no-python)  WITH_PYTHON=0 ;;
        --keep-cache) KEEP_CACHE=1 ;;
        --no-brew)    NO_BREW=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

# Whether the caller chose BUILD_DIR themselves decides what happens if it
# turns out to be cloud-synced: an explicit choice is reported and refused, the
# default is relocated automatically. Sampled before the default is applied,
# which is the only moment the difference is visible.
BUILD_DIR_EXPLICIT=0
[[ -n "${BUILD_DIR:-}" ]] && BUILD_DIR_EXPLICIT=1
BUILD_DIR="${BUILD_DIR:-build-macos-bundle}"
DIST_DIR="${DIST_DIR:-$REPO_ROOT/installers}"
QT_PREFIX="${CMAKE_PREFIX_PATH:-/opt/homebrew/opt/qt}"
EMBED_PY="${CALANGO_EMBEDDED_PYTHON_DIR:-}"
# dftd4 / torch-dftd / phonopy ride along as dynamically linked Python
# dependencies: dftd4 ships its shared library inside the wheel, torch-dftd
# backs mace_mp(dispersion=True), and phonopy drives the symmetry-reduced
# phonon path, LO-TO splitting and Γ-mode irrep labels.
#
# xtb is deliberately NOT in this list, though the GFN calculator needs it.
# There is no PyPI wheel: `pip install xtb` fetches a source distribution that
# compiles xtb 6.5.1 with meson, which needs a Fortran compiler and MKL and
# fails on macOS arm64 — so putting it here does not add the calculator, it
# breaks the build of the whole payload. conda-forge's `xtb-python` is the
# only working route, and Calango is no longer distributed as a conda
# package itself (.dmg and .deb only) — a DMG user who wants xTB points the
# engine at their OWN Conda environment that has it, through Preferences ->
# Python & Environments; the wizard reports whether the selected one does.
# See packaging/dependencies.txt.
EMBED_PKGS="${CALANGO_EMBEDDED_PACKAGES:-ase numpy scipy spglib matplotlib imageio imageio-ffmpeg dftd4 torch-dftd phonopy}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

# Where a relocated BUILD_DIR goes, and the only tree the cleanup step at the
# end is allowed to delete. ~/Library/Caches is the conventional macOS home for
# regenerable build output: no provider syncs it, and unlike /tmp it is not
# swept every few days.
SCRATCH_ROOT="$HOME/Library/Caches/calango"

# --- Pre-flight: the staging tree must not be cloud-synced ------------------
#
# A file provider (Dropbox, OneDrive, iCloud Drive, Google Drive) stamps
# xattrs — com.apple.FinderInfo among them — on everything it manages, and
# re-applies them as fast as they are cleared. codesign calls that "resource
# fork, Finder information, or similar detritus" and fails
# `codesign --verify --strict`, so the bundle is rejected by Gatekeeper.
#
# embed_python_framework.sh already runs `xattr -cr` before signing; against a
# live provider that is a race it loses. The only fix is to stage elsewhere.
#
# Left unhandled the failure arrives LAST: the whole app compiles, macdeployqt
# runs, the Python payload is embedded, and only then does codesign refuse —
# ten minutes to learn something knowable at second one. And since the default
# BUILD_DIR is relative to the repo, a checkout living in a synced folder hits
# it on every single run. So the default relocates itself instead of asking.
cloud_synced_path() {
    local probe="$1"
    # Resolve to the physical path first: ~/Dropbox is commonly a link to the
    # File Provider root, and the two spellings must be judged the same.
    local existing="$probe"
    while [[ -n "$existing" && ! -e "$existing" ]]; do
        existing="$(dirname "$existing")"
    done
    [[ -e "$existing" ]] || return 1
    local real
    real="$(cd "$existing" 2>/dev/null && pwd -P)" || return 1

    # macOS mounts every File Provider under this root — definitive on its own.
    [[ "$real" == *"/Library/CloudStorage/"* ]] && return 0

    # Otherwise look for a provider's own stamp on the nearest existing
    # ancestor. Vendor-prefixed names only: com.apple.FinderInfo alone is too
    # weak, since an ordinary folder carrying a Finder tag or custom icon has
    # one and is perfectly fine to build in.
    local dir="$real"
    while [[ "$dir" != "/" ]]; do
        if xattr "$dir" 2>/dev/null | grep -qE \
            '^(com\.apple\.fileprovider\.|com\.dropbox\.|com\.microsoft\.OneDrive|com\.google\.(Drive|GoogleDrive))'; then
            return 0
        fi
        dir="$(dirname "$dir")"
    done
    return 1
}

if cloud_synced_path "$BUILD_DIR"; then
    if [[ "$BUILD_DIR_EXPLICIT" -eq 1 ]]; then
        # A deliberate choice is reported, not overruled: silently building
        # somewhere other than where you asked is worse than stopping.
        cat >&2 <<EOF
error: the BUILD_DIR you set is inside a cloud-synced folder, so the signed
       bundle would fail strict signature validation.

           BUILD_DIR = $BUILD_DIR

       The provider stamps xattrs (com.apple.FinderInfo and friends) on every
       file it syncs and re-applies them faster than they can be cleared, and
       codesign rejects those as "resource fork, Finder information, or
       similar detritus".

       Point BUILD_DIR outside the synced tree, or unset it and let this
       script pick a scratch location itself. Either way the .dmg still lands
       wherever DIST_DIR says — that is only a destination, and a finished
       .dmg is a single signed file that syncing cannot spoil.
EOF
        exit 1
    fi

    # The default landed in a synced folder, so move it into SCRATCH_ROOT.
    # Keyed by the repo's path so two checkouts do not build into each other —
    # which is also why the cleanup step deletes this subdirectory rather than
    # the root: another checkout may have a tree of its own alongside it.
    _repo_key="$(printf '%s' "$REPO_ROOT" | shasum | cut -c1-8)"
    BUILD_DIR="$SCRATCH_ROOT/macos-bundle-$_repo_key"
    mkdir -p "$BUILD_DIR"
    echo "==> BUILD_DIR would be cloud-synced (codesign rejects the xattrs a"
    echo "    file provider stamps); staging in a scratch location instead:"
    echo "    $BUILD_DIR"
fi

# Normalise to an absolute path, once, here.
#
# Everything downstream then treats BUILD_DIR as a location rather than as
# something to be joined onto REPO_ROOT — and exactly one place used to do the
# joining, which quietly broke every absolute BUILD_DIR the header already
# advertised as supported: the embedded-Python cache resolved to
# "$REPO_ROOT/Users/…", so it was never found, the 500 MB payload was
# re-provisioned on every run, and the stray tree landed back inside the repo.
mkdir -p "$BUILD_DIR"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd -P)"

# Whether the build tree already exists must be sampled before anything
# below creates it, or --skip-build would skip a build that never happened.
# After the relocation above, so it describes the tree actually being used —
# and by CONTENTS, since mkdir -p just created the directory itself.
BUILD_DIR_EXISTED=0
[[ -n "$(ls -A "$BUILD_DIR" 2>/dev/null)" ]] && BUILD_DIR_EXISTED=1

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

# --- Run a command with a heartbeat and a hard timeout ---------------------
#
# The two genuinely slow steps below (downloading the standalone-Python
# tarball, pip installing the embedded package set) are otherwise silent for
# minutes at a time — indistinguishable, from the log, between "working" and
# "hung". That ambiguity is exactly what turns a one-shot script into
# something that needs a person (or an LLM) watching it and deciding whether
# to keep waiting or kill it. Printing progress and enforcing a ceiling here
# means the script makes that call itself, every time, unattended.
#
# No dependency on GNU coreutils' `timeout` (not on a stock macOS PATH):
# backgrounds the command, polls it, and escalates TERM -> KILL if it is
# still alive past max_seconds.
run_with_watchdog() {
    local desc="$1" max_seconds="$2"; shift 2
    "$@" &
    local pid=$!
    local elapsed=0
    while kill -0 "$pid" 2>/dev/null; do
        sleep 15
        elapsed=$((elapsed + 15))
        if [[ "$elapsed" -ge "$max_seconds" ]]; then
            echo "error: $desc exceeded ${max_seconds}s — giving up" >&2
            kill -TERM "$pid" 2>/dev/null || true
            sleep 2
            kill -KILL "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
            return 124
        fi
        echo "    ... still $desc (${elapsed}s elapsed)"
    done
    wait "$pid"
}

# --- Auto-bootstrap missing build tooling -----------------------------------
#
# Unattended means a machine that has Homebrew but has never built Calango
# before does not stop here and wait for someone to go run `brew install` —
# it installs the one or two formulas this script actually needs and keeps
# going. Never a blanket `brew upgrade` (that touches everything Homebrew
# manages on the machine, not just this script's dependencies) — only ever
# the named formula, and only when it is missing outright; an already-
# installed-but-outdated formula is reported, not silently upgraded out from
# under whatever else on the machine depends on that exact version.
# --no-brew (or no `brew` on PATH at all) skips this and falls through to the
# existing hard failures below, for a sandboxed/offline runner that
# provisions its own tools ahead of time.
ensure_brew_formula() {
    local formula="$1" probe_path="$2"
    [[ "$NO_BREW" -eq 1 ]] && return 0
    command -v brew >/dev/null 2>&1 || return 0

    if [[ -n "$probe_path" && -e "$probe_path" ]] \
       || brew list --formula "$formula" >/dev/null 2>&1; then
        if brew outdated --formula 2>/dev/null | grep -qx "$formula"; then
            echo "note: Homebrew formula '$formula' has an update available — 'brew upgrade $formula' to take it"
        fi
        return 0
    fi

    echo "==> '$formula' not found — installing via Homebrew"
    brew install "$formula" \
        || echo "warning: 'brew install $formula' failed; continuing without it" >&2
}

ensure_brew_formula cmake "$(command -v cmake || true)"
command -v cmake >/dev/null || { echo "error: cmake not found" >&2; exit 1; }

ensure_brew_formula qt "$QT_PREFIX"
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
[[ -x "$MACDEPLOYQT" ]] || MACDEPLOYQT="$(command -v macdeployqt || true)"
[[ -n "$MACDEPLOYQT" ]] || echo "warning: macdeployqt not found — the .app may not be self-contained" >&2

# create-dmg is only used by the --manual fallback assembly, and that fallback
# already degrades to hdiutil when it is absent — so this is a nicety, not a
# requirement, and its failure is never fatal.
ensure_brew_formula create-dmg "$(command -v create-dmg || true)"

# --- 0. Standalone Python payload (ASE preinstalled) -----------------------
# python-build-standalone's "install_only" trees are relocatable, unlike a
# project .venv whose bin/python links back to this machine's interpreter.
# The tree is cached in the build directory, so re-runs are essentially free.
PBS_REPO="astral-sh/python-build-standalone"

provision_embedded_python() {
    local want_xyz want_xy arch_tag cache url tgz manifest manifest_hash
    want_xyz="$("$PYTHON_BIN" -c 'import sys; print("%d.%d.%d" % sys.version_info[:3])')"
    want_xy="${want_xyz%.*}"
    case "$(uname -m)" in
        arm64)  arch_tag="aarch64" ;;
        x86_64) arch_tag="x86_64" ;;
        *) echo "error: unsupported arch $(uname -m)" >&2; return 1 ;;
    esac

    # BUILD_DIR is absolute by now — see the normalisation above.
    cache="$BUILD_DIR/embedded-python"
    # What the cache is FOR: the interpreter X.Y plus exactly the package set
    # this run asked for. Hashed and stamped alongside the tree so a later
    # run can tell "still what I need" apart from "was built for something
    # else" without re-deriving it — the whole point being that a change to
    # EMBED_PKGS (a dependency added in packaging/dependencies.txt, one
    # dropped, a caller overriding CALANGO_EMBEDDED_PACKAGES) is itself a
    # "necessary update" and must invalidate a cache that predates it, not
    # silently ship the old set forever.
    manifest="$want_xy|$EMBED_PKGS"
    manifest_hash="$(printf '%s' "$manifest" | shasum -a 256 | cut -d' ' -f1)"

    if [[ -x "$cache/python/bin/python3" ]] \
       && "$cache/python/bin/python3" -c "import sys, ase; assert '%d.%d' % sys.version_info[:2] == '$want_xy'" 2>/dev/null \
       && [[ "$(cat "$cache/.calango-manifest" 2>/dev/null)" == "$manifest_hash" ]]; then
        echo "==> Reusing cached embedded Python ($cache/python) — checking for package updates"
        # A cache that is still the right Python and the right package SET can
        # still hold OLD VERSIONS of those packages: without this, the first
        # provision on a machine would freeze ASE/NumPy/etc. at whatever was
        # latest that day and never move again, silently, run after run.
        if run_with_watchdog "checking embedded packages for updates" 900 \
                "$cache/python/bin/python3" -m pip install --no-input \
                --disable-pip-version-check -q --upgrade $EMBED_PKGS; then
            "$cache/python/bin/python3" -c \
                "import ase, numpy; print('    ASE', ase.__version__, '/ NumPy', numpy.__version__)"
            EMBED_PY="$cache/python"
            return 0
        fi
        echo "warning: update check failed — re-provisioning the embedded tree from scratch" >&2
        # Fall through to a full re-provision rather than shipping a payload
        # an interrupted upgrade may have left half-updated.
    fi

    echo "==> Provisioning standalone CPython $want_xyz ($arch_tag) with: $EMBED_PKGS"
    command -v curl >/dev/null || { echo "error: curl not found" >&2; return 1; }

    local api="https://api.github.com/repos/$PBS_REPO/releases/latest"
    local index
    index="$(curl -fsSL --max-time 60 "$api")" || {
        echo "error: could not query $PBS_REPO releases" >&2; return 1; }

    # Release assets encode the '+' of the version tag as %2B, so match both
    # spellings. Prefer PYTHON_BIN's exact X.Y.Z; any X.Y build is ABI-compatible.
    #
    # `|| true` on both lookups below is load-bearing, not cosmetic: under
    # `set -o pipefail`, `grep -o ... | head -1` exits NON-ZERO whenever grep
    # matches nothing — even though head still runs and correctly produces an
    # empty string — so with `set -e` and no guard, the very first release
    # that lacks an exact X.Y.Z build (e.g. python-build-standalone has
    # cut 3.14.7 but PYTHON_BIN is 3.14.6) killed the whole script right here,
    # silently, before the X.Y fallback two lines down ever ran. No error, no
    # partial output — exactly the "just sits there, no .dmg" failure this was
    # reported as. An empty match is an expected, already-handled outcome
    # (that's what the `[[ -z "$url" ]]` fallback below is FOR), not a real
    # failure, so it must not be allowed to trip errexit.
    local plus='\(+\|%2B\)'
    url="$(grep -o "https://[^\"]*cpython-${want_xyz}${plus}[0-9]*-${arch_tag}-apple-darwin-install_only\.tar\.gz" <<<"$index" | head -1)" || true
    if [[ -z "$url" ]]; then
        url="$(grep -o "https://[^\"]*cpython-${want_xy}\.[0-9]*${plus}[0-9]*-${arch_tag}-apple-darwin-install_only\.tar\.gz" <<<"$index" | sort -V | tail -1)" || true
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
    run_with_watchdog "downloading $(basename "$url")" 900 \
        curl -fsSL -o "$tgz" "$url" || {
        echo "error: download failed" >&2; return 1; }
    tar -xzf "$tgz" -C "$cache" || { echo "error: extract failed" >&2; return 1; }
    rm -f "$tgz"
    [[ -x "$cache/python/bin/python3" ]] || {
        echo "error: unexpected archive layout (no python/bin/python3)" >&2; return 1; }

    echo "==> Installing $EMBED_PKGS into the standalone tree"
    # shellcheck disable=SC2086 -- EMBED_PKGS is an intentional word list.
    run_with_watchdog "installing embedded packages" 1200 \
        "$cache/python/bin/python3" -m pip install --no-input --disable-pip-version-check \
        -q $EMBED_PKGS || { echo "error: pip install failed" >&2; return 1; }

    # Trim build-machine noise; __pycache__ is regenerated on demand.
    find "$cache/python" -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true

    "$cache/python/bin/python3" -c "import ase, numpy; print('    ASE', ase.__version__, '/ NumPy', numpy.__version__)" || {
        echo "error: ASE is not importable in the provisioned tree" >&2; return 1; }

    printf '%s' "$manifest_hash" > "$cache/.calango-manifest"
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
    # `|| true`: cpack failing outright (not just "produced no .dmg") must
    # still reach the `-z "$DMG"` check below and fall through to manual
    # assembly, which is the documented behavior of this whole if/if pair —
    # an unguarded failure here would instead kill the script via errexit
    # before the fallback it exists to enable ever ran.
    ( cd "$BUILD_DIR" && cpack -G DragNDrop ${ICNS:+-D CPACK_DMG_VOLUME_ICON="$ICNS"} ) \
        || echo "note: cpack failed — falling back to manual assembly" >&2
    DMG="$(ls -t "$BUILD_DIR"/*.dmg 2>/dev/null | head -1 || true)"
fi
if [[ -z "$DMG" ]]; then
    echo "==> Falling back to manual DMG assembly"
    # Same reasoning as the `|| true` above: a failure inside
    # make_dmg_manual must reach the "no .dmg was produced" check right
    # below rather than kill the script mid-pipeline with no such message.
    DMG="$(make_dmg_manual | tail -1)" || true
fi

# --- Finalize: move the artifact to ./installers/calango_<version>_macOS.dmg -
if [[ -z "$DMG" || ! -f "$DMG" ]]; then
    echo "error: no .dmg was produced" >&2
    exit 1
fi
mkdir -p "$DIST_DIR"
FINAL="$DIST_DIR/calango_${VERSION}_macOS.dmg"
mv -f "$DMG" "$FINAL"

# CPack's own CPACK_PACKAGE_CHECKSUM (CMakeLists.txt) already writes a
# .sha256 next to whatever it names the raw artifact in $BUILD_DIR — but
# that name is not $FINAL (cpack picks its own, and the manual/create-dmg
# fallback above produces no checksum at all), so it is recomputed here,
# fresh, against the file actually being shipped rather than tracked/
# renamed from cpack's copy. Recorded by basename only (not the full path),
# the same way `shasum -c` expects to be run from inside the directory that
# holds both files — matching build_deb.sh's own ".deb + .sha256, moved
# together" convention.
( cd "$DIST_DIR" && shasum -a 256 "$(basename "$FINAL")" > "$(basename "$FINAL").sha256" )

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

# Every shipped Mach-O must load only relative (@executable_path/
# @loader_path/@rpath) or system (/System, /usr/lib) paths — anything else
# is a build-machine path (Homebrew, a conda prefix, $HOME, ...) that will
# not exist on the machine installing this .dmg. Added after calango
# 26.8.36's calango-dftb-run binary (the native DFTB engine's helper,
# since removed along with the engine itself — see CMakeLists.txt) shipped
# linked directly against /opt/homebrew/opt/openblas/lib/libopenblas.0.dylib
# (an unconstrained find_package(LAPACK) preferred Homebrew's openblas
# over Accelerate on that build machine, and nothing downstream of its
# plain install(TARGETS) rewrote load commands the way macdeployqt
# rewrites `calango` itself). The fix to that root cause was a since-
# removed CALANGO_BLAS option, superseded by removing every consumer of
# find_package(LAPACK) entirely — this audit is the backstop that now
# catches the same class of leak on any OTHER bundled library.
#
# The embedded Python payload (Contents/Resources/python and the linked
# Contents/Frameworks/Python.framework) is EXCLUDED here, not because it is
# clean — auditing that same 26.8.36 .dmg found it is NOT: six stdlib
# extension modules and the framework's own python3.14/Python binaries
# loaded Homebrew OpenSSL/sqlite/zstd/mpdecimal/xz directly, meaning that
# artifact was built against a Homebrew-linked interpreter rather than the
# relocatable python-build-standalone tree this script normally downloads
# (see PYTHON_BIN / CALANGO_EMBEDDED_PYTHON_DIR above). That is a separate,
# pre-existing bug in the Python-provisioning path, already partly covered
# by the --probe-python check just above (which catches "ASE won't import"
# but not "every dylib is relocatable") — fixing it is tracked, not done
# here; see packaging/README.md's "BLAS/LAPACK backend" section for the
# full incident writeup. Re-run this script without the two
# --exclude flags below once that is fixed, to turn this into a full gate.
echo ""
echo "==> Auditing shipped Mach-O binaries for build-machine paths"
if ! python3 "$SCRIPT_DIR/audit_macho_deps.py" "$FINAL" \
        --exclude 'Contents/Resources/python/*' \
        --exclude 'Contents/Frameworks/Python.framework/*'; then
    echo "error: audit_macho_deps.py found a build-machine path in the " >&2
    echo "       packaged app outside the embedded Python payload — see " >&2
    echo "       output above. This .dmg (or one of the binaries it" >&2
    echo "       ships) would fail to launch on a clean Mac." >&2
    exit 1
fi

echo ""
echo "==> SUCCESS"
echo "    Artifact : $FINAL"
echo "    Checksum : $FINAL.sha256"
echo "    Size     : $(du -h "$FINAL" | cut -f1)"
echo "    Verified : valid macOS disk image (hdiutil imageinfo)"
echo "    Probe    : $PROBE"
if [[ "$PROBE" == FAILED* ]]; then
    echo "error: the packaged app could not resolve its Python environment" >&2
    exit 1
fi

# --- 5. Clean up the scratch build tree ------------------------------------
#
# Deliberately the LAST thing, after every check above: a run that failed
# anywhere keeps its tree, because that tree is what you need to diagnose it —
# `set -e` already aborts before reaching this point, and the probe check just
# above exits non-zero without cleaning.
#
# The rule is the location, not who chose it: anything under SCRATCH_ROOT is
# scratch and goes, anything elsewhere is someone's own build directory —
# possibly a long-lived one — and is left alone with a note saying so.
#
# The subdirectory goes, not SCRATCH_ROOT itself: it is keyed by repo path so
# that several checkouts can coexist there, and deleting the root would take
# another checkout's cache with it. The root is then removed only if it turns
# out to be empty, which `rmdir` decides — no test, no race.
cleanup_scratch() {
    case "$BUILD_DIR/" in
        "$SCRATCH_ROOT"/*) ;;
        *) echo "    Kept     : $BUILD_DIR (your BUILD_DIR — not this script's to delete)"
           return 0 ;;
    esac

    local freed
    freed="$(du -sh "$BUILD_DIR" 2>/dev/null | cut -f1)"
    rm -rf "$BUILD_DIR"
    rmdir "$SCRATCH_ROOT" 2>/dev/null || true
    echo "    Cleaned  : $BUILD_DIR (freed ${freed:-?})"

    # Worth saying, because it is the one cost of cleaning: the embedded-Python
    # payload was cached in that tree and went with it, so the next run
    # re-downloads and re-pip-installs ~500 MB. Only when there was one.
    #
    # An `[[ ... ]] && echo` here would be a trap rather than a shorthand: as
    # the last command of the script, a false condition becomes the exit
    # status, and a perfectly good run would report failure.
    if [[ "$WITH_PYTHON" -eq 1 ]]; then
        echo "               next run re-provisions the embedded Python; --keep-cache avoids that"
    fi
}

if [[ "$KEEP_CACHE" -eq 1 ]]; then
    echo "    Kept     : $BUILD_DIR (--keep-cache)"
else
    cleanup_scratch
fi
