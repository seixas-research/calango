#!/usr/bin/env python3
"""Version injection: every version string a reader can see agrees with the binary.

The application's version lives in exactly one place — ``project(calango
VERSION x.y.z)`` in the top-level CMakeLists.txt — and three other artefacts
have to carry it:

  * the LaTeX guides' covers, via ``calango_version.tex``, which docs/tex's
    Makefile GENERATES by grepping CMakeLists;
  * the Sphinx site's ``release``, which nothing generates and which is
    therefore the one that drifts;
  * the About box / project files, via the CALANGO_VERSION compile definition,
    which CMake derives from the same ``project()`` call.

What this pins is the two mechanisms that can silently stop working — the
Makefile's grep, and the hand-maintained Sphinx string. It found the Sphinx
one reading ``26.8.2`` against a ``26.8.20`` build: a dropped final digit,
which is exactly the kind of thing that survives every review because both
strings look like versions.

The generated ``calango_version.tex`` files are deliberately NOT compared:
they are untracked build products, absent on a fresh clone and legitimately
stale until someone runs ``make -C docs/tex``. Checking the recipe that writes
them is the durable test; checking their contents would fail for a reason that
is not a defect.

Run from anywhere: paths are resolved against this file's location.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

failures = 0


def check(condition: bool, what: str) -> None:
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def cmake_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text()
    match = re.search(r"^project\(calango\s*\n\s*VERSION\s+([0-9]+(?:\.[0-9]+)*)",
                      text, re.M)
    if not match:
        raise SystemExit("could not find project(calango VERSION ...) — the "
                         "declaration this whole check hangs off has moved")
    return match.group(1)


def main() -> int:
    version = cmake_version()
    print(f"CMakeLists.txt declares version {version}\n")

    print("Sphinx site:")
    conf = (ROOT / "docs/sphinx/source/conf.py").read_text()
    match = re.search(r"^release\s*=\s*['\"]([^'\"]+)['\"]", conf, re.M)
    check(match is not None, "conf.py sets `release`")
    if match:
        print(f"       conf.py release = {match.group(1)}")
        check(match.group(1) == version,
              "and it matches the version the binary reports")

    print("LaTeX guides:")
    # The Makefile's grep is the injection mechanism. Running it here is what
    # catches a CMakeLists reformat that leaves the recipe matching nothing —
    # which would silently stamp every cover with an EMPTY version rather than
    # fail, since the recipe cannot tell "no match" from "no version".
    makefile = (ROOT / "docs/tex/Makefile").read_text()
    recipe = re.search(r"^VERSION\s*:=\s*\$\(shell\s+(.*)\)\s*$", makefile, re.M)
    check(recipe is not None, "the Makefile still derives VERSION by a shell grep")
    if recipe:
        command = recipe.group(1).replace("$(ROOT)/CMakeLists.txt",
                                          str(ROOT / "CMakeLists.txt"))
        extracted = subprocess.run(["sh", "-c", command], capture_output=True,
                                   text=True, check=False).stdout.strip()
        print(f"       the recipe extracts = {extracted!r}")
        check(extracted == version,
              "and it still extracts exactly that version from CMakeLists")

    print("\nAll version-sync checks passed." if failures == 0
          else f"\n{failures} version-sync check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
