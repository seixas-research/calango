# From databases

{menuselection}`Build --> From Database` opens the Database Browser, the structure
source most sessions start from. It has three tabs — {guilabel}`Bulk`,
{guilabel}`Materials Project` and {guilabel}`PubChem` — ordered so that the one needing
no network and no API key comes first. Every fetched or built structure opens as a **new
workspace tab**; nothing in the browser modifies an existing document.

All three tabs run through the embedded Python bridge, so a working ASE environment is a
prerequisite (see {doc}`/python_environment`).

% TODO screenshot: Database Browser on the Bulk tab, prototype mode, with the live 3D preview showing a silicon diamond cell
```{figure} /_static/img/builders_database_bulk.png
:alt: The Bulk tab of the Database Browser with formula, prototype and lattice-parameter fields above a live 3D preview
:width: 92%
:figclass: screenshot

The Bulk tab. The preview rebuilds live as the formula and lattice parameters change;
{kbd}`Enter` in the formula field builds immediately.
```

---

## Bulk crystals

The {guilabel}`Bulk` tab builds periodic crystals two ways, chosen with the
{guilabel}`Build from` combo.

### Prototype mode (`ase.build.bulk`)

Type a formula — `Si`, `Au`, `NaCl`, `GaAs` — or pick elements from the
{guilabel}`Periodic Table…` button, choose a crystal structure, and press
{guilabel}`Build Crystal`. Twelve prototypes are offered, ordered most-used first:

| Prototype | Notes |
|---|---|
| `fcc`, `bcc`, `sc` | cubic elements |
| `hcp` | takes $c/a$ (ideal $\sqrt{8/3} \approx 1.6330$ preselected when checked) |
| `diamond` | the default structure, with $a = 5.43$ Å prefilled for Si |
| `rocksalt`, `zincblende`, `cesiumchloride`, `fluorite` | binary cubic compounds |
| `wurtzite` | binary hexagonal; takes $c/a$ |
| `bct` | body-centred tetragonal; takes $c$ or $c/a$ |
| `orthorhombic` | the only prototype with an independent $b$ — the {guilabel}`Set b` row is enabled (and pre-checked) exactly for it |

Parameters that ASE treats as optional stay optional here: leaving
{guilabel}`Lattice constant a` at 0 displays *ASE default* and uses the tabulated experimental value for
the element. {guilabel}`Set c/a ratio` and {guilabel}`Set c directly` are mutually
exclusive, as they are in ASE, and the two {guilabel}`Cell shape` checkboxes
({guilabel}`Conventional cubic cell`, {guilabel}`Orthorhombic cell`) exclude each other
likewise. Rows that a prototype cannot use are disabled rather than silently ignored.

**Typing a single element symbol auto-fills the form from ASE's reference states** —
structure, lattice constant and $c/a$ where applicable — so `Ti` lands on `hcp` with the
experimental cell before you touch anything. Compound formulas get no auto-fill; there
is no single ground-state entry to read.

:::{note}
ASE's `bulk()` vocabulary also names `st`, `mcl` and `rhombohedral`, but those are not
actually constructible through `bulk()` (verified against ASE 3.29), so Calango does not
offer them. The space-group mode below covers those lattices instead.
:::

### Space group + Wyckoff basis

The second mode drives `ase.spacegroup.crystal`: a space-group number (1–230, default
225 = Fm-3m), cell lengths and angles (defaults 5.64 Å and 90° — rocksalt NaCl, matching
the default site table), an optional {guilabel}`Reduce to the primitive cell` checkbox,
and a table of symmetry-inequivalent Wyckoff sites — element, fractional $u\,v\,w$,
occupancy. The space group generates all equivalent positions; you enter only the
representatives. Error messages come straight from ASE ("spacegroup … requires …"),
which are specific enough to act on.

---

## Materials Project

The {guilabel}`Materials Project` tab fetches structures from the online Materials
Project database.

- {guilabel}`API key` — a masked field, saved as you type. It is pre-filled from the
  stored setting or, failing that, from `MP_API_KEY` in the environment.
- {guilabel}`.env file` — shows the environment file the key can be imported from, with
  {guilabel}`Browse…` to point at a directory containing a `.env` and {guilabel}`Reload`
  to re-import `MP_API_KEY` from it.
- {guilabel}`Fetch a single entry by ID` — type an ID such as `mp-149` (silicon) and
  press {kbd}`Enter` or {guilabel}`Fetch Structure`. The entry opens as a new tab named
  after the ID.

### Search

Below the direct fetch sits a multi-material search with three modes:

| Mode | Query | Returns |
|---|---|---|
| Chemical system (exact) | `Li-Fe-O` or `Li Fe O` | phases made of *only* these elements |
| Contains elements | `Li Fe O` | phases including these elements, plus any others |
| Formula | `LiFePO4` | a specific stoichiometry |

Results are sorted by energy above hull, so a capped search (default `max 100`, up to
1000) returns the most stable phases. The table shows formula (bold when the phase is on
the convex hull), space group, band gap, $E$ above hull, site count and MP-ID; every
column sorts, and a filter box narrows the list live.

Select rows and either {guilabel}`Open Selected in Separate Workspace Tabs` — each tab
labelled like *mp-149 Si*, ID plus formula — or
{guilabel}`Group Selected into Single Trajectory File`, which combines two or more entries into one multi-frame document in
the table's current sort order, useful for scrubbing through a composition series. A
failed ID never aborts the rest of the batch; failures are listed at the end.

:::{warning}
Both fetch and search need a network connection and a personal API key from
<https://materialsproject.org/api>. Grouped frames usually have differing atom counts
(different phases) — save such a document as extended XYZ, not `.traj`, to keep every
frame.
:::

---

## PubChem

The {guilabel}`PubChem` tab retrieves 3D molecular conformers from the online PubChem
database through `ase.data.pubchem.pubchem_atoms_search`. **No API key is required** —
only a network connection.

Search by one of three fields:

| Field | Example |
|---|---|
| Name | `benzene` |
| SMILES | `c1ccccc1` |
| CID | `241` |

{guilabel}`Fetch 3D Conformer` (or {kbd}`Enter` in the query field) downloads the
molecule and opens it as a new tab named after the query. A query with no match, an
ambiguous name, or a network failure produces an error in place — nothing partial is
opened.

:::{note}
PubChem serves the database's stored 3D conformer — a reasonable gas-phase geometry, not
one optimised with your calculator. Relax it before quoting geometric or energetic
properties.
:::

---

## Sample files in the repository

The Calango source tree ships an `examples/` directory of ready-made input files
covering the common demonstration cases. They are ordinary structure files — open them
with {menuselection}`File --> Open --> Structure` ({kbd}`Ctrl+O`); see {doc}`/data_io`
for the format list.

| File | Content |
|---|---|
| `Si_diamond.vasp`, `diamond.vasp` | silicon and carbon in the diamond structure |
| `Au.cif`, `Au_cubic.cif` | gold, primitive and conventional cells |
| `graphene_unitcell.vasp`, `graphene_supercell.vasp` | graphene, 1×1 and supercell |
| `MoS2.cif`, `mos2_2h_bulk.vasp`, `mos2_1h_monolayer.vasp` | MoS₂ bulk and monolayer |
| `1Tprime-MoS2.vasp`, `2H-NbS2.vasp`, `3R-NbS2.vasp` | TMD polytypes |
| `benzene.xyz`, `naphthalene.xyz`, `coronene.xyz` | aromatic molecules |
| `water.xyz`, `acetic_acid.xyz` | small molecules |
| `10FC.cif` | a larger crystallographic test case |

% TODO screenshot: Materials Project tab after a Li-Fe-O search, results table sorted by E above hull with two rows selected
```{figure} /_static/img/builders_database_mp.png
:alt: Materials Project search results with formula, space group, band gap and energy-above-hull columns
:width: 92%
:figclass: screenshot

A chemical-system search. On-hull phases are bold; selected rows can open as separate
tabs or as one trajectory.
```
