#!/usr/bin/env bash
# Strip leftover, non-relocatable LC_RPATH entries from every Mach-O in a
# staged .app -- run after macdeployqt, before the final codesign.
#
# macdeployqt correctly rewrites a copied library's own LC_LOAD_DYLIB
# entries to @executable_path-relative, but does not clean up the ORIGINAL
# build machine's LC_RPATH search-path entries it shipped with. calango
# 26.8.36's bundled libopenblas.0.dylib carried three:
#   /opt/homebrew/opt/gcc/lib/gcc/current/gcc/aarch64-apple-darwin25/15
#   /opt/homebrew/opt/gcc/lib/gcc/current/gcc
#   /opt/homebrew/opt/gcc/lib/gcc/current
# On the build machine these happened to be exactly how dyld's chained
# rpath search found libgcc_s.1.1.dylib (a dependency of the also-bundled
# libgfortran.5.dylib, itself never actually copied into the bundle) --
# on a clean Mac without that exact Homebrew GCC install, every one of
# those paths misses and the app aborts at launch before main() runs
# (see CrashReport.md, 2026-08-21). The real fix for THAT specific chain
# was pinning BLA_VENDOR to Accelerate (a system framework needing none
# of this) via a since-removed CALANGO_BLAS option — since superseded by
# removing the native DFT/DFTB engines that were LAPACK's only consumers
# entirely, so there is no chain left to link on any platform. This
# script is defense in depth for every OTHER bundled library macdeployqt
# leaves a leftover build-machine rpath on -- auditing the same .dmg found
# it on libdbus-1.3.dylib, libjasper.7.dylib and libjpeg.8.dylib too, none
# of them BLAS-related.
#
# Deleting a leftover rpath is only safe once nothing actually needs it to
# resolve a real dependency -- which is true for macdeployqt-bundled
# libraries whose own LC_LOAD_DYLIB entries are already @executable_path/
# @loader_path-relative (macdeployqt's job) and simply carry a vestigial
# extra search entry from their original build. packaging/macos/
# audit_macho_deps.py verifies that ALL LC_LOAD_DYLIB entries resolve
# WITHOUT the stripped entries before this bundle is published; if a
# future dependency's resolution genuinely NEEDS a leftover rpath (i.e.
# the actual file it points at was never bundled), the audit fails loudly
# instead of this script silently making that worse.
#
# Excludes the embedded Python payload (Contents/Resources/python,
# Contents/Frameworks/Python.framework) -- its relocatability is a
# separate, already-documented, not-yet-fixed issue (packaging/README.md's
# "BLAS/LAPACK backend" section); this script does not touch it.
#
#   strip_leftover_rpaths.sh path/to/calango.app
set -euo pipefail

app="$1"
[ -d "$app" ] || { echo "error: $app: no such directory" >&2; exit 1; }

count=0
while IFS= read -r -d '' f; do
    case "$f" in
        "$app"/Contents/Resources/python/*) continue ;;
        "$app"/Contents/Frameworks/Python.framework/*) continue ;;
    esac
    # Cheap Mach-O check: first 4 bytes match one of the known magic numbers.
    magic="$(dd if="$f" bs=1 count=4 2>/dev/null | od -An -tx1 | tr -d ' \n')"
    case "$magic" in
        feedface|cefaedfe|feedfacf|cffaedfe|cafebabe|bebafeca) ;;
        *) continue ;;
    esac

    while IFS= read -r rpath; do
        [ -n "$rpath" ] || continue
        case "$rpath" in
            @loader_path|@loader_path/*|@executable_path|@executable_path/*) ;;
            *)
                echo "  stripping leftover RPATH '$rpath' from ${f#"$app"/}"
                install_name_tool -delete_rpath "$rpath" "$f"
                count=$((count + 1))
                ;;
        esac
    done < <(otool -l "$f" | awk '
        /^ *cmd LC_RPATH$/ { want=1; next }
        want && /^ *path / { print $2; want=0 }
        /^Load command/ { want=0 }
    ')
done < <(find "$app" -type f -print0)

echo "==> strip_leftover_rpaths.sh: removed $count leftover LC_RPATH entr$([ "$count" = 1 ] && echo y || echo ies)"
