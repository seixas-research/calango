# Citing Calango and what it used

{menuselection}`Help --> About Calango` has a **Citations** tab, right
after Third-Party Licenses — the natural next question once you know what
Calango is built on is how to cite the pieces you actually used in a
result.

---

## What is on the tab

One continuously-numbered, Nature-format reference list ("Surname, F. M."
authors, an abbreviated italicized journal, a bold volume, pages or an
article number, the year in parentheses), grouped under four headers:

| Group | Covers |
|---|---|
| **Core** | ASE — the library every generated script builds its structure and runs its calculator through — and Calango itself |
| **Databases** | Whichever of Materials Project, C2DB and PubChem the Database Browser actually queried |
| **Calculators** | The DFT/MD engines the wizards generate scripts for — GPAW, Quantum ESPRESSO, VASP, SIESTA, LAMMPS, MACE, xTB, DFTB+ and Calango's own native DFTB engine (CalangoDftb, sharing DFTB+'s SCC-DFTB method citation) — cite the ones a given result actually ran on, not the whole list |
| **Libraries** | Python packages customary to cite where a venue expects it: spglib, NumPy, phonopy, icet, dftd4 |

Some entries carry a short note under them — "cite together with ASE,
above", "for the default GFN2-xTB method" — where a project's own citation
guidance ties one paper to another or to a specific configuration.

Every reference was checked against the project's own current "how to
cite" guidance (docs page, README, or `CITATION.cff`) rather than copied
from an older paper that may since have been superseded — GPAW, phonopy
and the Materials Project have each changed their recommended citation in
recent years.

---

## The BibTeX viewer

Each reference has a {guilabel}`BibTeX viewer…` button beside it, opening
a small dialog with:

- a read-only, monospaced view of the complete BibTeX entry — correct
  entry type (`@article`, `@inproceedings`, `@software`, …), a sensible
  citation key, and every field the reference above states;
- {guilabel}`Copy`, which copies the entry to the clipboard;
- {guilabel}`Export…`, which saves it as its own `.bib` file, named after
  the citation key.

One dialog class handles every entry — it is parameterized by which
citation opened it, not one dialog per reference.

:::{note}
The list is compiled from what Calango's own integrations actually use,
not every engine the wizards can drive. Dozens more calculators are
reachable through their ASE integration (see
{doc}`/reference/dependencies`) without a canonical citation of their own
appearing here; a run on one of those is cited from that engine's own
documentation.
:::
