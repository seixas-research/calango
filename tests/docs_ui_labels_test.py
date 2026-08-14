#!/usr/bin/env python3
"""Docs / UI string consistency: the manual may not name a control that is not there.

A renamed widget is the cheapest possible source of wrong documentation. The
label changes in one `tr("…")`, every screenshot and every prose reference goes
on saying the old thing, and nothing anywhere fails — the docs still build, the
app still runs, and the only symptom is a reader hunting for a tab that no
longer exists.

This checks the one family of labels the documentation names most often and
most specifically: the tabs of the Visual Effects dock. Both directions matter
and both have been wrong in this repository:

  * docs naming a tab the panel does not have (the panel's tabs were renamed
    and the pages kept the old names);
  * the panel growing a tab the docs never mention (a new effect that is
    undocumented is, for a reader, one that does not exist).

The Visual Effects tabs rather than every label in the application, because a
whole-UI check would be a grep with a permanent backlog. This one is a closed
set, it is named in prose in several files, and it is small enough that the
failure message can say exactly what to do about it.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PANEL = ROOT / "src/gui/VisualEffectsPanel.cpp"
DOC_FILES = [
    ROOT / "docs/sphinx/source/viewport.md",
    ROOT / "docs/sphinx/source/representation.md",
    ROOT / "docs/tex/user_guide/sections/02_workspace.tex",
    ROOT / "docs/tex/user_guide/sections/07_appearance.tex",
]

failures = 0


def check(condition: bool, what: str) -> None:
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def panel_tabs() -> list[str]:
    """The tab labels the panel actually creates, in order."""
    text = PANEL.read_text()
    return re.findall(r'addTab\([^;]*?tr\("([^"]+)"\)\s*\)', text, re.S)


def documented_labels() -> dict[str, set[str]]:
    """Labels each doc file presents to the reader as a UI control.

    Both markup families mark them up explicitly — MyST as {guilabel}`X`, LaTeX
    as \\ui{X} — which is what makes this checkable at all: an unmarked mention
    in prose is indistinguishable from ordinary English.
    """
    found: dict[str, set[str]] = {}
    for path in DOC_FILES:
        if not path.exists():
            continue
        text = path.read_text()
        labels = set(re.findall(r"\{guilabel\}`([^`]+)`", text))
        labels |= set(re.findall(r"\\ui\{([^}]+)\}", text))
        found[path.name] = labels
    return found


def main() -> int:
    tabs = panel_tabs()
    print(f"Visual Effects tabs in {PANEL.relative_to(ROOT)}: {tabs}\n")
    check(len(tabs) >= 2, "the panel's addTab() calls are still readable here")
    if len(tabs) < 2:
        # Parsing broke; every check below would be vacuously true.
        print("\nThe tab labels could not be extracted — the check cannot run.")
        return 1

    known = set(tabs)
    docs = documented_labels()

    print("No documented Visual Effects tab is missing from the panel:")
    # A doc label counts as a tab reference when it matches a CURRENT tab name
    # or a name this panel is known to have used before. The historical set is
    # what gives the test its teeth: without it, a stale "Occlusion" in the docs
    # reads as some unrelated control and slips through.
    RETIRED = {"Lighting", "Occlusion"}
    for name, labels in docs.items():
        stale = sorted(labels & RETIRED)
        check(not stale,
              f"{name} names no retired tab "
              f"({', '.join(stale)} — the panel calls it "
              f"{', '.join(sorted(known & {'Light', 'SSAO'}))} now)"
              if stale else f"{name} names no retired tab")

    print("Every tab the panel has is documented somewhere:")
    everywhere: set[str] = set()
    for labels in docs.values():
        everywhere |= labels
    for tab in tabs:
        check(tab in everywhere,
              f"the {tab!r} tab is named in the documentation")

    print("\nAll docs/UI label checks passed." if failures == 0
          else f"\n{failures} docs/UI label check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
