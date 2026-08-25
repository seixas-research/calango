"""Qt APIs used in src/ must exist at the Qt version this project targets.

WHY THIS EXISTS. Calango declares its Qt floor once, in CMakeLists.txt:

    find_package(Qt6 6.4 REQUIRED COMPONENTS ...)

6.4 because that is what Ubuntu 24.04's own qt6-base-dev ships, and the
.deb has to build against the system Qt -- "install a newer Qt" does not
travel to a machine that only has the system one. But development happens
against whatever Qt the developer's machine has (Homebrew ships 6.9), and
a call that only exists above the floor compiles there and fails only when
the .deb is built. That has now happened twice:

  * QKeySequenceEdit::setMaximumSequenceLength -- Qt 6.7 only, fixed with
    a QT_VERSION_CHECK guard;
  * QString::arg(std::string) -- an overload Qt 6.9 accepts and 6.4 does
    not, which broke a .deb build with

        error: no matching function for call to 'QString::arg(std::string)'

    from a preflight added on a machine running Qt 6.9.

The second one is the shape this test catches, because it is the one that
recurs: core:: returns std::string, Qt wants a QString, and a new-enough Qt
papers over the gap. The rule is simply that the conversion is written
out -- QString::fromStdString(...) -- which compiles on every Qt.

WHAT IT DOES. Reads the std::string-returning free functions out of the
core headers, then checks that none of them is passed straight into a Qt
string sink in src/gui. Pure text, no Qt, no build: it runs in
milliseconds on any machine, which is the point -- the developer's machine
is the one that cannot otherwise see this.

Run directly, or through ctest as `qt_api_floor`.
"""

import os
import re
import sys

failures = 0

# The Qt string sinks that take a QString and will NOT take a std::string on
# an older Qt. `.arg(` is the one that broke; the others are the same shape.
SINKS = ("arg", "setText", "setToolTip", "setWindowTitle", "setPlaceholderText",
         "setStatusTip", "setWhatsThis", "addItem", "setItemText", "setTitle")


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1
    return condition


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _std_string_functions(core_dir):
    """Free functions in core/*.hpp declared to return std::string."""
    names = set()
    for entry in sorted(os.listdir(core_dir)):
        if not entry.endswith(".hpp"):
            continue
        with open(os.path.join(core_dir, entry), errors="replace") as fh:
            text = fh.read()
        # `std::string foo(` and `inline std::string foo(`, declaration or
        # definition, at any indentation.
        for m in re.finditer(r"^\s*(?:inline\s+|static\s+)*std::string\s+"
                             r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", text, re.M):
            names.add(m.group(1))
    return names


def _offenders(gui_dir, names):
    """(file, line, text) for a std::string function fed straight to a sink."""
    if not names:
        return []
    joined = "|".join(sorted(re.escape(n) for n in names))
    sinks = "|".join(SINKS)
    # The call must be a DIRECT argument of the sink: `.arg(toString(x)` with
    # nothing converting it in between. A fromStdString anywhere between the
    # sink and the call means the conversion is written out.
    pattern = re.compile(r"\.(?:%s)\(\s*(?:[A-Za-z_][A-Za-z0-9_]*::)?(?:%s)\s*\("
                         % (sinks, joined))
    found = []
    for root, _dirs, files in os.walk(gui_dir):
        for name in sorted(files):
            if not name.endswith((".cpp", ".hpp")):
                continue
            path = os.path.join(root, name)
            with open(path, errors="replace") as fh:
                lines = fh.read().splitlines()
            # Join continuations so a call split across lines is still seen.
            for i, line in enumerate(lines):
                window = " ".join(lines[i:i + 3])
                if pattern.search(window) and "fromStdString" not in window:
                    found.append((os.path.relpath(path, _repo_root()),
                                  i + 1, line.strip()))
    return found


def main():
    root = _repo_root()
    with open(os.path.join(root, "CMakeLists.txt")) as fh:
        cmake = fh.read()

    print("The declared Qt floor:")
    floor = re.search(r"find_package\(Qt6 (\d+\.\d+)", cmake)
    check(floor is not None, "CMakeLists.txt declares a Qt6 minimum")
    if floor:
        print(f"       Qt {floor.group(1)} — Ubuntu's own qt6-base-dev, which "
              f"is what the .deb builds against")

    print("\nstd::string must not be handed straight to a Qt string API:")
    names = _std_string_functions(os.path.join(root, "src", "core"))
    check(len(names) > 5,
          f"found {len(names)} std::string-returning core functions to check "
          f"for (toString and friends)")

    offenders = _offenders(os.path.join(root, "src", "gui"), names)
    if offenders:
        for path, line, text in offenders:
            print(f"       {path}:{line}: {text[:110]}")
    check(not offenders,
          "none of them is passed to .arg()/setText()/... without "
          "QString::fromStdString() — the conversion Qt 6.4 requires and a "
          "newer Qt papers over")

    print("\n" + (f"{failures} check(s) FAILED." if failures
                  else "All Qt API floor checks passed."))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
