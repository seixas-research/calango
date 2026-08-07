# Nanomaterials and nanoparticles

Three builders generate low-dimensional structures from scratch: the Nanomaterial
Builder for carbon and TMD sheets, ribbons and tubes; the Nanoparticle & Cluster Builder
for metallic clusters; and the Graphene Oxide builder for functionalized sheets and
finite graphene nanoflakes. The first two run through ASE; graphene oxide is **native
C++** and works without a Python environment.

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

{menuselection}`Modules --> 2D Materials --> Graphene Oxide` builds a functionalized
carbon substrate — an infinite periodic sheet or a finite flake — in a two-stage wizard.
It runs natively in C++.

Graphene oxide has no single structure — it is a non-stoichiometric, disordered
material, and the accepted picture (Lerf–Klinowski) is a basal plane carrying epoxides
and hydroxyls, with carboxyls, carbonyls and phenolic hydroxyls at edges and defects.
What the builder honestly produces is a **representative random sample from that family
at a requested composition**, not "the" structure; it reports exactly what it placed and
the seed reproduces it.

### Stage 1 — base structure

{guilabel}`Base` chooses the carbon skeleton, and it is the choice that decides whether
there is any edge chemistry to do at all.

**Periodic sheet** — infinite, and therefore *edgeless*. Only basal-plane chemistry
applies.

- {guilabel}`Lattice` — *Primitive* (2-atom rhombohedral cell, $a = b = 2.46$ Å at 60°)
  or *Conventional rectangular* (4-atom orthogonal cell, 2.46 × 4.26 Å, the default —
  orthogonal axes make later supercells and interfaces easier to reason about).
- {guilabel}`Supercell (nx · ny)` — each 1–40, default 4 × 4.

The sheet is built in the $xy$ plane with a fixed **20 Å vacuum** along $z$ — the groups
stand roughly 1.5 Å off the plane, and a thinner cell would have them interacting with
their own image.

**Nanoflake** — a finite, hexagonal, all-armchair $D_{6h}$ graphene molecule with both
regions: a basal interior and a hydrogen-terminated rim.

- {guilabel}`Index m` — the size knob, giving $\mathrm{C}_{6m^2}\mathrm{H}_{6m}$, built
  from $3m(m-1)+1$ fused rings, $m = 1 \ldots 12$:

  | $m$ | Name | Formula | Basal C | Edge C |
  |---|---|---|---|---|
  | 1 | benzene | C₆H₆ | 0 | 6 |
  | 2 | coronene | C₂₄H₁₂ | 12 | 12 |
  | 3 | circumcoronene *(default)* | C₅₄H₁₈ | 36 | 18 |
  | 4 | dicircumcoronene | C₉₆H₂₄ | 72 | 24 |
  | 5 | tricircumcoronene | C₁₅₀H₃₀ | 120 | 30 |

  In general $6m(m-1)$ basal and $6m$ edge carbons. The names are labels on a series,
  not the thing being built: the generator takes $m$ and nothing else, and the names run
  out long before $m$ does. The flake is built from *rings* rather than carved from a
  sheet by a distance cutoff, which is what makes the formula exact — a carve gives
  ragged edges whose atom count depends on where the cut lands.
- {guilabel}`Hydrogen-terminate the unreacted edge` (on by default) caps every edge
  carbon that did not react, as in the parent hydrocarbon. Off leaves radical dangling
  bonds — deliberate for edge-state studies, a mistake otherwise.

A flake is **not periodic**. It is fitted with a box carrying 10 Å of vacuum on every
side, because plane-wave codes demand a cell; what it must not have is a neighbour.

### Basal carbons and edge carbons

Every framework carbon is classified from its own coordination, and the classification
is what the chemistry below is keyed to:

| | Coordination | Chemistry |
|---|---|---|
| **Basal** | three carbon neighbours | sp² → sp³; the group stands *off* the plane |
| **Edge** | fewer than three | stays sp²; the group replaces the carbon's hydrogen, *in* the plane |

The result carries a per-atom scalar field named `edge` (1 on edge carbons, 0 elsewhere),
so the split can be inspected in the viewport rather than taken on faith — colour by it
from the Representation panel.

### Stage 2 — functionalization and oxidation level

Five groups in two boxes, each with a checkbox and a spin:

| Group | Region | Consumes | Delivers | Notes |
|---|---|---|---|---|
| Epoxide (−O−) | basal | **two** carbons | 1 O | bridges a C–C bond; both carbons rehybridize to sp³ |
| Hydroxyl (−OH, sp³) | basal | one carbon | 1 O | above or below the plane, C–O 1.48 Å |
| Carboxyl (−COOH) | edge | one carbon | **2 O** | brings a carbon of its own; aryl C–C 1.48 Å |
| Carbonyl (=O) | edge | one carbon | 1 O | quinone-like, collinear with the C–H it replaces |
| Edge hydroxyl (−OH, sp²) | edge | one carbon | 1 O | phenolic: C–O 1.36 Å, in the plane |

The two families are kept strictly apart. On a periodic sheet the edge box is greyed out
and says why — earlier versions placed carboxyls and carbonyls on the basal plane as a
stated modeling compromise, and no longer do.

{guilabel}`Set by` chooses how much oxygen goes on:

**Explicit coverages per group** — each spin is the fraction of the carbons *in that
group's region* which the group consumes, not the fraction of groups. An epoxide costs
two carbons and the others one, so the values are additive within a region: 10 % epoxide
plus 10 % hydroxyl functionalizes 20 % of the basal plane, while 50 % carboxyl
carboxylates half the rim. The default selection is epoxide + hydroxyl, the basal-plane
pair of the Lerf–Klinowski picture.

**Target C/O ratio** — the spins become *relative propensities* (only their ratios
matter) and the builder places groups until the finished structure reaches
{guilabel}`Target C/O`: every carbon, **including the ones carboxyls bring with them**,
over every oxygen. That is what XPS reports. Heavily oxidized graphene oxide sits near 2,
mildly oxidized near 4–10, reduced graphene oxide at 10 and beyond.

The loop is iterative rather than a closed-form count precisely because a carboxyl moves
*both* sides of the ratio, and it stops at the **closest reachable** composition rather
than the first one past the target — with a carboxyl worth two oxygens, placing blindly
until the ratio drops below the target can overshoot by more than stopping short would
have missed.

{guilabel}`Basal : edge split` sets the fraction of the oxygen budget delivered by basal
groups (default 70 %); whichever region is furthest behind its share gets the next group.
The endpoints are categorical: 100 % places no edge chemistry at all and 0 % no basal
chemistry, even if that means missing the target. Anything in between is a soft split,
and one region carries on alone once the other runs out of sites.

{guilabel}`Decorate both faces` (on by default) alternates *basal* groups between the two
sides; restricting to one face puts an artificial dipole across the sheet. Edge groups
lie in the plane and are unaffected. {guilabel}`Seed` (default 0) makes the decoration
reproducible — record it with any result.

### Placement rules and shortfalls

Under explicit coverages, groups are placed in the order epoxide → carboxyl → carbonyl →
edge hydroxyl → hydroxyl on shuffled sites; epoxides go first because they need a bonded
*pair* of free basal carbons, the most constrained requirement. Two hard constraints hold
in every mode, and both are chemical rather than statistical:

1. **A carbon hosts at most one group.** It has one out-of-plane valence once it
   rehybridizes to sp³, and an edge carbon one substitutable hydrogen; a second group
   would make it pentavalent, which is not improbable but impossible. A reserved carbon
   leaves the pool permanently — and a functionalized edge carbon has had its hydrogen
   *substituted*, not added to, so it is not also capped.
2. **Nothing is placed on top of anything else.** Groups on neighbouring sites can point
   their substituents at each other; the builder tries each site in several orientations
   (which face, which way the hydroxyl hydrogen swings) and refuses any placement that
   would bring atoms of two different groups within 1.55 Å. Strained contacts are
   expected in an unrelaxed structure and relax out; atoms closer than a covalent bond
   are fused, and no optimizer recovers from that.

When sites run out the shortfall is reported after building ("epoxide: placed 14 of
20 …"), never silently absorbed; an unreachable C/O target is reported the same way,
along with the ratio actually achieved. Above roughly 60 % total basal coverage the
epoxides begin to struggle to find bonded free pairs, and the summary warns in advance.

% TODO screenshot: Graphene oxide wizard stage 2 on a nanoflake, target C/O mode, with the basal and edge boxes both populated
```{figure} /_static/img/builders_nanomaterials_go.png
:alt: Functionalization stage of the Graphene Oxide builder with separate basal and edge group boxes and the target C/O controls
:width: 92%
:figclass: screenshot

Stage 2 of the Graphene Oxide builder. Basal and edge chemistry are separate boxes, the
oxidation level is set either per group or by target C/O, and the seed reproduces the
exact decoration.
```
