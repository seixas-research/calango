#!/usr/bin/env python3
"""The Sphinx site still builds.

The documentation is the half of this project that no compiler checks. A
broken cross-reference, a MyST fence nested one level wrong, a toctree entry
pointing at a page that was renamed — all of them build a site that is subtly
missing content, and none of them shows up until someone reads the page that
lost it.

Builds into the CMake binary directory rather than docs/sphinx/build, so a
test run never leaves artefacts in the source tree.

SELF-SKIPS (exit 0) when sphinx-build is not available. Sphinx lives in a venv
under docs/sphinx/.venv that a fresh clone does not have, and a missing
documentation toolchain is an environment fact, not a defect in the docs —
exactly the convention shader_compilation and translucent_render follow for a
missing GL context.

Warnings are reported but do not fail the run: the site carries ~90 deliberate
`image.not_readable` warnings from screenshot placeholders for captures that
have not been taken yet, and failing on those would mean the test could only
ever be green after every screenshot exists.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "docs/sphinx/source"


def find_sphinx() -> list[str] | None:
    """The venv's sphinx-build first — a bare Homebrew one cannot see myst/furo."""
    venv = ROOT / "docs/sphinx/.venv/bin/sphinx-build"
    if venv.exists():
        return [str(venv)]
    found = shutil.which("sphinx-build")
    if found:
        return [found]
    for python in (ROOT / "docs/sphinx/.venv/bin/python", Path(sys.executable)):
        if python.exists():
            probe = subprocess.run([str(python), "-c", "import sphinx, myst_parser, furo"],
                                   capture_output=True)
            if probe.returncode == 0:
                return [str(python), "-m", "sphinx"]
    return None


def main() -> int:
    sphinx = find_sphinx()
    if sphinx is None:
        print("sphinx-build (with myst_parser and furo) is not available — "
              "skipping.\nThis is an environment limitation, not a "
              "documentation error.")
        return 0

    out = Path(os.environ.get("CALANGO_DOC_BUILD_DIR",
                              ROOT / "build" / "docs-test")) / "html"
    out.parent.mkdir(parents=True, exist_ok=True)
    command = [*sphinx, "-b", "html", str(SOURCE), str(out)]
    print("$ " + " ".join(command))
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    output = result.stdout + result.stderr

    # Errors are fatal; the screenshot placeholders are not (see the docstring).
    errors = [l for l in output.splitlines() if "ERROR" in l or "SEVERE" in l]
    warnings = [l for l in output.splitlines() if "WARNING" in l]
    placeholders = [l for l in warnings if "image.not_readable" in l]
    other = [l for l in warnings if "image.not_readable" not in l]

    print(f"  {len(warnings)} warning(s): {len(placeholders)} screenshot "
          f"placeholder(s), {len(other)} other")
    for line in other[:20]:
        print(f"    {line}")
    for line in errors[:20]:
        print(f"    {line}")

    ok = result.returncode == 0 and not errors
    print(f"  {'ok  ' if ok else 'FAIL'} the Sphinx site builds")
    print("\nDocumentation build passed." if ok
          else f"\nsphinx-build exited {result.returncode}.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
