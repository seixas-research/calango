# Adsorbates and adsorption sites

Two tools decorate a surface. {menuselection}`Build --> Add adsorbate` places **one**
atom or molecule on the current geometry — on a detected high-symmetry site or at an
explicit position. {menuselection}`Analysis --> Adsorption & Catalysis` works in bulk:
it lists every detected site, decorates selections of them, and generates coverage
structures. Both open results as **new tabs**, leaving the clean surface untouched —
deliberately, since the clean surface is the reference an adsorption energy is measured
against.

---

## How sites are detected

Both tools share one native C++ site detector. Surface atoms are the undercoordinated
ones (coordination below the structure's maximum, with neighbours counted inside 1.3×
the summed covalent radii); sites are then built from them:

| Site | Geometry |
|---|---|
| top | directly above one surface atom |
| bridge | above the midpoint of two neighbouring surface atoms |
| fcc / hcp | above the centroid of three mutually neighbouring surface atoms — **hcp when an atom of the subsurface layer sits directly under the centroid, fcc when the space beneath is empty** |
| hollow | a threefold site on a structure with no second layer to classify against (a single sheet) |

The essential detail is the normal: **the outward normal of a surface atom is taken as
the direction away from the mean of its neighbours**, not as a hardcoded $+z$. On a
slab's top layer that reproduces $+z$; on a nanoparticle it is the local radial-outward
direction — so sites land on the true outer surface of icosahedra, cuboctahedra and
Wulff shapes, and adsorbates follow curved facets. Same-type sites closer than 0.25 Å
(minimum-image) are merged, and a pair or triple only forms a bridge/hollow when the
members' normals agree — which keeps sites on a single facet and off the *bottom* face
of a thin slab.

Detection is geometry, not chemistry: it works on periodic slabs and finite clusters
alike. A structure with no distinct outer layer simply yields no sites, and the dialogs
stay usable through their Cartesian modes.

---

## Add adsorbate

The dialog detects sites when it opens and offers two tabs.

**{guilabel}`Single Atom`** — an editable element combo, led by the adsorbate elements
that come up constantly (H, O, N, C, S, halogens, alkali metals, late transition metals)
with the full periodic table below a separator.

**{guilabel}`Molecule / Radical`** — an editable combo over ASE's molecule database,
*extended with the open-shell fragments a surface actually binds* (OH, OOH, CH₃, NH₂,
…); anything you type that is not a database entry is parsed as a plain chemical
formula. Two further controls matter:

- {guilabel}`Binding atom` — the atom that faces the surface; it is placed on the site
  axis and the rest of the molecule follows rigidly. **Which atom binds is the chemistry
  of the adsorption**, so it stays editable; the default follows convention (O for OH
  and H₂O, C for CO).
- {guilabel}`Orientation` — three angles, all defaulting to 0°. *Tilt from normal*: 0°
  stands the molecule upright, 90° lays it flat, 180° inverts it — CO adsorbs upright,
  an aromatic ring lies flat, and the binding energy is a function of exactly this.
  *Azimuth about normal*: which way a tilted molecule leans, and the in-plane
  orientation of a flat one. *Roll about own axis*: visible only for adsorbates that are
  not axially symmetric — a methyl group, a ring.

### Placement

Each tab carries the same {guilabel}`Placement` group with two modes:

- {guilabel}`High-symmetry site` — pick a detected site from the combo (each labelled
  with its type and coordinates) and a {guilabel}`Height above site`, default **1.90 Å**
  (0–20), measured from the site to the binding atom along the outward normal. The
  height is a starting guess — the relaxation decides the real bond length; 1.5–2.2 Å is
  the usual range for a chemisorbed atom.
- {guilabel}`Cartesian position` — explicit $x, y, z$, seeded just above the structure's
  top face so the manual mode starts somewhere plausible. In this mode the height is 0
  by construction: the typed position already names the exact spot, and adding an offset
  on top of it would make the number you typed a lie.

A live preview line reports what will be built — adsorbate, anchor position, site type,
and the resulting atom count. The result opens as a tab named like *Pt₃₆ + CO (fcc)*.

% TODO screenshot: Add Adsorbate dialog on the Molecule tab with CO selected, binding atom C, fcc site chosen and the preview line visible
```{figure} /_static/img/builders_adsorption_dialog.png
:alt: The Add Adsorbate dialog showing molecule selection, binding atom, orientation angles and site placement controls
:width: 92%
:figclass: screenshot

Add adsorbate. The site combo lists every detected site with its coordinates; the
preview line states exactly what Finish will build.
```

---

## Adsorption & Catalysis

{menuselection}`Analysis --> Adsorption & Catalysis` opens a two-tab window over the
same detector, for site statistics and batch decoration.

### Site identification and geometry generation

The first tab lists every detected site in a table, with a {guilabel}`Show sites` filter
(All / top / bridge / fcc / hcp / hollow) and a summary count. Select any number of
rows, choose an adsorbate — a pre-populated combo (OH, O, H, CO, CHO, H₂O, N₂, O₂, NH₃,
CH₄) that also accepts any `ase.build.molecule` name or chemical formula — set the
{guilabel}`Height` (default **2.0 Å**, 0.5–10, anchor atom above the surface layer), and
press {guilabel}`Place on Selected Sites`.

The {guilabel}`Output` choice decides the shape of the result:
{guilabel}`Individual workspace tabs`, one geometry per tab — best for comparing site preferences side by side
— or {guilabel}`Single trajectory tab`, all geometries as frames of one document,
convenient for batch export and for scrubbing through a site series.

### Coverage

The second tab builds partial-monolayer structures on one site family:

- {guilabel}`Site family` — top, fcc, hcp, bridge or hollow.
- {guilabel}`Coverage` — a spin box and slider in lockstep, **0–1 ML in steps of 0.01,
  default 0.25 ML**. Coverage is defined against the number of sites in the chosen
  family: 0.25 ML on fcc sites occupies a quarter of them.
- {guilabel}`Minimum separation` — default 3.0 Å, centre-to-centre between occupied
  sites; 0 disables the constraint.

{guilabel}`Generate Coverage` picks the occupied subset and opens the decorated
structure. **If the requested coverage cannot be reached at the requested separation,
the dialog says what it actually built** — "Placed 3 of 6 adsorbates … the generated
structure is 0.12 ML" — because a paper quoting the requested coverage over the achieved
one would be wrong.

:::{note}
Site occupation within the coverage tool is geometric selection, not thermodynamics.
Real adlayers order through adsorbate–adsorbate interactions — c(2×2) phases, islanding,
coverage-dependent binding — which only relaxation and statistics on top of these
starting structures can reveal. Generate the geometry here, then compare total energies
per coverage with your calculator (see {doc}`/simulations/jobs`).
:::

:::{important}
Detected sites are ideal lattice positions on the *unrelaxed* surface. On a
reconstructed or defective surface, detect sites *after* relaxing the clean slab — the
detector reads the geometry it is given, and a rumpled top layer shifts bridge and
hollow positions by real fractions of an ångström.
:::

% TODO screenshot: Adsorption & Catalysis coverage tab, fcc family at 0.25 ML on a 4x4 slab with the minimum-separation control visible
```{figure} /_static/img/builders_adsorption_coverage.png
:alt: The Coverage tab with site family, coverage slider at 0.25 ML and minimum separation controls
:width: 92%
:figclass: screenshot

The coverage generator. When the separation constraint caps the coverage below the
request, the achieved monolayer fraction is reported.
```
