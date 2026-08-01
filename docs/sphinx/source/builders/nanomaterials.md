# Nanomaterials and nanoparticles

Three builders generate low-dimensional structures from scratch: the Nanomaterial
Builder for carbon and TMD sheets, ribbons and tubes; the Nanoparticle & Cluster Builder
for metallic clusters; and the Graphene Oxide builder for functionalized sheets. The
first two run through ASE; graphene oxide is **native C++** and works without a Python
environment.

---

## Nanomaterial builder

{menuselection}`Build --> Nanomaterials` generates four families of low-dimensional
structures. Pick the type at the top; the form below changes to match.

| Type | Parameters and defaults |
|---|---|
| Graphene sheet | lattice constant $a$ = 2.46 Å, repeats $n_x \times n_y$ = 4 × 4, vacuum 10 Å |
| Graphene nanoribbon | width and length 6 unit cells each, {guilabel}`Edge type` {guilabel}`Zigzag` or {guilabel}`Armchair`, {guilabel}`Hydrogen-terminate edges` on by default |
| Carbon nanotube | chiral indices $(n, m)$ = (6, 6), length 4 unit cells, C–C bond 1.42 Å, vacuum 8 Å — $(0,0)$ is rejected |
| TMD monolayer (MX₂) | editable formula combo (`MoS2`, `MoSe2`, `WS2`, `WSe2`, `MoTe2`, `NbSe2`, or anything you type), {guilabel}`Phase` `2H` or `1T`, lattice constant 3.16 Å, X–X thickness 3.19 Å, repeats 4 × 4, vacuum |

The edge type of a nanoribbon is not cosmetic: zigzag ribbons carry edge states at the
Fermi level while armchair ribbons open a width-dependent gap. Hydrogen termination
saturates the edge dangling bonds; turn it off only when the bare edge is the object of
study.

---

## Nanoparticle & cluster builder

{menuselection}`Build --> Nanoparticle Builder` builds finite clusters in a two-stage
dialog: pick a method, then its parameters. Both methods share an element picker (full
periodic table) and a {guilabel}`Lattice constant` field whose special value *default*
uses ASE's tabulated value for the element.

### Wulff construction

The equilibrium shape of a crystal minimises total surface energy at fixed volume; the
Wulff construction produces it from the relative surface energies of the exposed facets.

- **Facet table** — one row per facet: $h$, $k$, $l$ and $\gamma$ (relative surface
  energy). Defaults: $(111) = 1.00$, $(100) = 1.10$, $(110) = 1.20$, edited with
  {guilabel}`Add Facet` / {guilabel}`Remove Selected`.
- {guilabel}`Crystal structure` — `fcc`, `bcc` or `sc`.
- {guilabel}`Target size (atoms)` — default 200 (10–100 000). The construction lands on
  the nearest achievable closed shape.
- {guilabel}`Size rounding` — `closest`, `above` or `below`.

**Only the ratios of the $\gamma$ values matter** — scaling every row by the same factor
changes nothing. Lowering $\gamma(111)$ relative to $\gamma(100)$ grows the (111) facets
and drives an fcc metal from a cube toward a truncated octahedron.

### Symmetric crystal clusters

The second method builds closed-shell magic clusters and carved crystallites:

| Shape | Size control |
|---|---|
| Icosahedron, dodecahedron, cuboctahedron, octahedron | {guilabel}`Shells / layers`, default 3 (1–60) |
| Decahedron | $p / q / r$ indices, defaults 3 / 3 / 0 |
| Spherical cluster | {guilabel}`Crystal structure` `fcc`, `bcc` or `hcp`; {guilabel}`Radius` default 10 Å (2–100) — every atom within the radius is carved from the bulk lattice |

The spherical carve is the tool when you want a specific diameter; the Wulff
construction is the tool when you want the thermodynamic shape. The icosahedron and
decahedron are *non-crystalline* motifs — fivefold-twinned particles that beat single
crystals at small sizes — which no Wulff construction over a single lattice can produce.

Both methods centre the cluster in a vacuum box and clear periodicity.

:::{note}
These are ideal geometric shapes at bulk bond lengths. Real nanoparticles relax —
surface bonds contract, vertices round off — so relax the cluster before quoting a
cohesive energy or comparing isomers a few meV apart.
:::

% TODO screenshot: Nanoparticle builder stage 2 in Wulff mode, facet table with three rows and a ~200-atom truncated octahedron result
```{figure} /_static/img/builders_nanomaterials_wulff.png
:alt: Wulff construction parameters with the facet energy table and the resulting truncated-octahedral gold nanoparticle
:width: 92%
:figclass: screenshot

A Wulff gold nanoparticle. Only the γ ratios reach the shape; the target atom count is
met by the nearest closed shell.
```

---

## Graphene oxide

{menuselection}`Modules --> 2D Materials --> Graphene Oxide` builds a graphene sheet
decorated with oxygen-bearing functional groups at target coverages, in a two-stage
wizard. It runs natively in C++.

Graphene oxide has no single structure — it is a non-stoichiometric, disordered
material, and the accepted picture (Lerf–Klinowski) is a basal plane carrying epoxides
and hydroxyls, with carboxyls and carbonyls at edges and defects. What the builder
honestly produces is a **representative random sample from that family at a requested
composition**, not "the" structure; it reports exactly what it placed and the seed
reproduces it.

### Stage 1 — base lattice and supercell

- {guilabel}`Lattice` — *Primitive* (2-atom rhombohedral cell, $a = b = 2.46$ Å at 60°)
  or *Conventional rectangular* (4-atom orthogonal cell, 2.46 × 4.26 Å, the default —
  orthogonal axes make later supercells and interfaces easier to reason about).
- {guilabel}`Supercell (nx · ny)` — each 1–40, default 4 × 4. A summary reports the
  carbon count.

The sheet is built in the $xy$ plane with a fixed **20 Å vacuum** along $z$ — the groups
stand roughly 1.5 Å off the plane, and a thinner cell would have them interacting with
their own image.

### Stage 2 — functionalization and coverages

Four groups, each with a checkbox and a coverage spin (default 10 %, step 2.5 %):

| Group | Consumes | Notes |
|---|---|---|
| Epoxide (−O−) | **two** carbons | bridges a C–C bond; both carbons rehybridize to sp³ |
| Hydroxyl (−OH) | one carbon | above or below the plane |
| Carboxyl (−COOH) | one carbon | brings a carbon of its own; an *edge* group in reality — placing it on a periodic basal plane is a stated modeling compromise |
| Carbonyl (=O) | one carbon | likewise an edge group in practice |

**Coverage is the fraction of basal carbons each group consumes, not the fraction of
groups** — an epoxide costs two carbons, so the values are additive: 10 % epoxide plus
10 % hydroxyl functionalizes 20 % of the sheet. The default selection is epoxide +
hydroxyl, the basal-plane pair of the Lerf–Klinowski picture.

{guilabel}`Decorate both faces` (on by default) alternates groups between the two sides;
restricting to one face puts an artificial dipole across the sheet. {guilabel}`Seed`
(default 0) makes the decoration reproducible — record it with any result.

### Placement rules and shortfalls

Groups are placed in the order epoxide → carboxyl → carbonyl → hydroxyl, on shuffled
sites; epoxides go first because they need a bonded *pair* of free carbons, the most
constrained requirement. The one hard constraint is chemical: **a carbon can host at
most one group** — a second would make it pentavalent, which is not improbable but
impossible.

When the lattice runs out of free carbons the shortfall is reported after building
("epoxide: placed 14 of 20 …"), never silently absorbed. Above roughly 60 % total
coverage the epoxides begin to struggle to find bonded free pairs, and the summary warns
about it in advance. The report also includes the resulting C:O ratio, the standard
experimental composition measure.

% TODO screenshot: Graphene oxide wizard stage 2 with epoxide and hydroxyl checked at 10% each and the coverage summary visible
```{figure} /_static/img/builders_nanomaterials_go.png
:alt: Functionalization stage of the Graphene Oxide builder with per-group coverage spins and the additive-coverage summary
:width: 92%
:figclass: screenshot

Stage 2 of the Graphene Oxide builder. The summary counts groups, warns near saturation,
and the seed reproduces the exact decoration.
```
