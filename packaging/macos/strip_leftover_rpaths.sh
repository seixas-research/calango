#!/usr/bin/env bash
# Strip leftover, non-relocatable LC_RPATH entries from every Mach-O in a
# staged .app -- run after macdeployqt, before the final codesign.
#
# macdeployqt correctly rewrites a copied library's own LC_LOAD_DYLIB
# entries to @executable_path-relative, but does NOT clean up the original
# build machine's LC_RPATH search-path entries the library shipped with --
# absolute paths like /opt/homebrew/opt/<formula>/lib.
#
# Why that matters even when every load path looks right: dyld's chained
# rpath search will happily resolve a dependency THROUGH one of those
# leftover paths on the build machine, so the bundle appears complete
# there while the file was never actually copied into it. On a clean Mac
# none of those paths exist, nothing resolves, and the app aborts at
# launch before main() runs. A bundle-relative load path is proof of
# form, not of resolution.
#
# So this runs over EVERY bundled Mach-O, not a chosen few: any library
# macdeployqt copies can carry one, and the ones that do are not
# predictable from what the library is for.
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
# "Dependency audit" section); this script does not touch it.
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
