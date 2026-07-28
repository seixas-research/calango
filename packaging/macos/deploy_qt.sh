#!/usr/bin/env bash
# Run macdeployqt over the staged .app, quietly.
#
# Two passes: frameworks copied into the bundle lose the @loader_path context
# their @rpath dependencies resolve against, so transitive deps (QtGui ->
# QtDBus, plugin deps) are missed on the first scan. The second pass re-scans
# the deployed copies — with -libpath naming the copy source — and completes
# the set.
#
# On the way there macdeployqt prints "ERROR: Cannot resolve rpath ..." for
# every dependency it cannot see on a given pass, recovers, and still exits 0.
# Dumping dozens of those lines makes a healthy build look broken, so keep the
# full output in a log and print a single summary line. A real failure (non-zero
# exit) is still fatal and shows the tail of the log.
#
#   deploy_qt.sh <macdeployqt> <path/to/App.app> <qt-lib-dir> <logfile>
set -euo pipefail

mdq="$1"
app="$2"
libpath="$3"
log="$4"

: > "$log"

for pass in 1 2; do
    if ! "$mdq" "$app" -always-overwrite "-libpath=$libpath" >>"$log" 2>&1; then
        echo "error: macdeployqt failed on pass $pass (exit $?) — full log: $log" >&2
        echo "--- last 20 lines ---" >&2
        tail -20 "$log" >&2
        exit 1
    fi
done

# grep -c exits 1 when nothing matches, which set -e would treat as fatal.
warnings="$(grep -c '^ERROR' "$log" || true)"
echo "Qt deployed — macdeployqt reported ${warnings} non-fatal rpath warnings" \
     "over 2 passes (expected); full log: $log"
