"""README.md and the Sphinx landing page must carry the SAME badges.

Two files, one badge row, kept in step by hand -- which is exactly the kind
of pair that drifts. Before this they had drifted: the README had four
badges (License, macOS, Debian, ASE) and the Sphinx index five (License,
GitHub, C++, Qt, ASE), with only two in common and no documentation badge
anywhere.

WHAT IS ASSERTED:
  * both files list the same shields.io image URLs, in the same ORDER;
  * every badge uses the same for-the-badge style;
  * the version-bearing badges tell the truth -- the C++ standard against
    CMAKE_CXX_STANDARD and the Qt version against the find_package(Qt6 ...)
    minimum, both read out of CMakeLists.txt rather than trusted;
  * the documentation badge points at the on-line manual and says "Manual",
    with no "sphinx" in any visible text.

Pure text: no network, no build, so it runs everywhere and in milliseconds.
The URLs themselves resolving is a separate question, checked by hand when
the row changes -- a test that fetched them would fail on an outage rather
than on a defect.

Run directly, or through ctest as `docs_badge_parity`.
"""

import os
import re
import sys

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1
    return condition


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _readme_badges(text):
    """shields.io image URLs from the README's markdown badge row."""
    return [m.group(1) for m in
            re.finditer(r"\[!\[[^\]]*\]\((https://img\.shields\.io/[^)]+)\)\]",
                        text)]


def _sphinx_badges(text):
    """The same, from the landing page's raw-HTML badge row."""
    return [m.group(1).replace("&amp;", "&") for m in
            re.finditer(r'<img src="(https://img\.shields\.io/[^"]+)"', text)]


def main():
    root = _repo_root()
    with open(os.path.join(root, "README.md")) as fh:
        readme = fh.read()
    with open(os.path.join(root, "docs/sphinx/source/index.rst")) as fh:
        sphinx = fh.read()
    with open(os.path.join(root, "CMakeLists.txt")) as fh:
        cmake = fh.read()

    print("Badge parity:")
    a = _readme_badges(readme)
    b = _sphinx_badges(sphinx)
    check(bool(a), f"README.md has a badge row ({len(a)} badges)")
    check(bool(b), f"the Sphinx landing page has one ({len(b)} badges)")
    check(a == b,
          "and the two are IDENTICAL — same badges, same order"
          + ("" if a == b else f"\n       README: {a}\n       Sphinx: {b}"))

    print("\nStyle:")
    check(all("style=for-the-badge" in u for u in a),
          "every badge uses the for-the-badge style")

    print("\nThe version-bearing badges are truthful:")
    std = re.search(r"set\(CMAKE_CXX_STANDARD (\d+)\)", cmake)
    check(std is not None, "CMakeLists.txt states a C++ standard")
    if std:
        want = std.group(1)
        cxx = [u for u in a if "C%2B%2B" in u or "/C++-" in u]
        check(len(cxx) == 1, "there is exactly one C++ badge")
        if cxx:
            check(f"-{want}-" in cxx[0],
                  f"and it says C++{want}, which is what the build sets")

    qt = re.search(r"find_package\(Qt6 (\d+\.\d+)", cmake)
    check(qt is not None, "CMakeLists.txt states a Qt6 minimum")
    if qt:
        want = qt.group(1)
        badges = [u for u in a if "/Qt-" in u]
        check(len(badges) == 1, "there is exactly one Qt badge")
        if badges:
            check(want in badges[0],
                  f"and it says Qt {want}+, the minimum find_package requires")

    print("\nThe documentation badge:")
    docs = [u for u in a if "readthedocs" in u]
    check(len(docs) == 1, "exactly one documentation badge")
    if docs:
        check("label=Manual" in docs[0],
              'labelled "Manual" — the "On-line manual" naming, not "Sphinx"')
    # No visible "sphinx" anywhere in the badge row, in either file. The
    # word is an implementation detail of how the manual is built.
    for name, text, badges in (("README.md", readme, a),
                               ("index.rst", sphinx, b)):
        row = " ".join(badges)
        # `logo=readthedocs` is the host, not the generator, and is fine.
        check("sphinx" not in row.lower(),
              f"and no \"sphinx\" in {name}'s badge row")

    print("\nLink targets:")
    # A relative link works from the README and not from the docs site, so
    # anything shared has to be absolute.
    for m in re.finditer(r"\[!\[[^\]]*\]\(https://img\.shields\.io/[^)]+\)\]\(([^)]+)\)",
                         readme):
        target = m.group(1)
        check(target.startswith("http") or target == "LICENSE",
              f"README badge links to {target} — absolute, or the repo's "
              f"own LICENSE")

    print("\n" + (f"{failures} check(s) FAILED." if failures
                  else "All badge parity checks passed."))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
