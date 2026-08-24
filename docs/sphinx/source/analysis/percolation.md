# Aromatic Percolation Analysis

{menuselection}`Modules --> Graphene Oxide --> Aromatic Percolation Analysis…`
turns a graphene oxide structure's chemistry into a graph-theoretic question:
**does an unbroken sp2 pathway still cross the sheet?** Conductivity in
graphene oxide rides on the sp2 network, not the sp3 one, so the fraction of
intact benzene rings and whether they still connect edge-to-edge is the
structural side of the oxidation-vs-conductivity story an MCMD run traces
out.

The tool works on any structure — it is not gated behind having used the
{doc}`Graphene Oxide builder </builders/nanomaterials>` — but reports zero
rings on anything without a carbon framework, which is the honest answer
rather than a refusal to open.

---

## Two criteria, and when they disagree

Calango offers **two** percolation analyses over the same carbon framework,
side by side under {menuselection}`Modules --> Graphene Oxide`. They ask the
same physical question — *does an unbroken pathway still cross the sheet?* —
under different definitions of "pathway", and the difference is the reason both
exist.

| | {guilabel}`Aromatic Percolation Analysis` | {guilabel}`π Percolation Analysis` |
|---|---|---|
| A site is | an **intact benzene ring** — all six carbons free of oxygen | a **carbon carrying a p orbital** — unoxidized and three-coordinate |
| Sites connect when | two rings share a C–C edge | two such carbons are bonded |
| Requires a ring | yes | **no** |
| Reports | intact-ring fraction, sp² domains | π-carbon fraction, conjugated domains |

**One epoxide breaks three hexagons at once.** A carbon belongs to three rings,
so oxidizing it disqualifies all three — which makes the aromatic criterion
strict, and correctly so for a question about aromaticity. For a question about
*conduction* it is stricter than the physics: a polyene chain of sp² carbons
threading between oxidized islands carries a π system and contains no intact
hexagon at all. The aromatic analysis scores that as nothing; the π analysis
sees it.

The π network always **contains** the intact-ring network — every carbon of an
intact ring is a π carbon — so it can only ever be the more connected of the
two, and any axis the rings percolate is one the π network percolates. Measured
on a 6 × 6 sheet at 16 % epoxide coverage: intact rings 0.46, π carbons 0.83,
and the rings percolate **one** in-plane axis where the π network percolates
**both**.

### The π criterion, exactly

A carbon carries a π orbital when **both** hold:

* it carries no oxygen functional group — the same
  `GrapheneOxideBuilder::functionalGroupLabels()` classification the aromatic
  analysis reads, so neither re-derives sp² vs sp³ by a second method;
* it has **at most three σ neighbours**, counting bonded atoms of any element.
  Three is sp² (the honeycomb interior, and an edge carbon with a terminating
  hydrogen); four is sp³ and has no p orbital left, oxygen or no oxygen.

The second half is what separates this from the sp²-carbon fraction the
aromatic analysis already reports, which asks only about the oxygen: an
unoxidized but four-coordinate carbon — a CH₂ in a hydrogenated defect — is
counted as sp² there and correctly excluded here.

The rule is deliberately **conservative**. It makes no claim about bond
alternation, planarity or aromaticity, only about which carbons still have an
orbital to conjugate with — which is what a percolation question needs and all
a bonding graph can honestly support.

Everything else is shared: the same periodicity-aware winding-number test for
whether a domain wraps the cell, the same current-structure/whole-trajectory
scope, the same {guilabel}`Apply Coloring` (one cast per conjugated domain,
sp³ and non-carbon in the default cast), and the same CSV and plot exports.

---

## Ring detection

Every six-membered carbon ring is found directly from bond topology: a
**chordless** (induced) six-cycle in the carbon-carbon bond graph, including
cycles that close through a periodic bond. A ring only closes when the six
bond vectors sum to exactly zero lattice translations — the requirement that
makes it a real hexagonal face rather than a walk that wound around the
periodic cell — so the same routine finds every hexagon in a bulk sheet, a
one-direction-periodic ribbon, and a finite flake without being told which
kind of structure it is looking at, and correctly counts even graphene's own
two-atom primitive cell (where an "A-B" ring edge is a different physical
bond every time it is used, not the same one three times over).

Each ring is then classified against the **same** per-carbon
functional-group labelling the {doc}`Graphene Oxide builder
</builders/nanomaterials>` itself computes
(`core::GrapheneOxideBuilder::functionalGroupLabels()`) — a second, re-derived
notion of "which carbon is sp2" is exactly the kind of drift this reuse
avoids:

- **Intact** — all six carbons carry no epoxide, hydroxyl, carbonyl or
  carboxyl group.
- **Disrupted** — at least one does.

## sp2 domains and the percolation criterion

Intact rings sharing a C–C bond are edges in a graph whose nodes are the
rings themselves. Its connected components are the **sp2 domains** — the
contiguous conducting patches a disrupted ring or a stray epoxide can break
apart.

A domain **percolates** an axis when it reaches all the way around the
periodic cell along it: walking ring-to-ring within the domain, some pair of
paths to the same ring differ by a non-zero lattice translation along that
axis — the standard winding-number test for a periodic-boundary-spanning
cluster. An axis the structure is not periodic along can never percolate,
and a finite flake never percolates at all.

The report gives, per structure:

| Quantity | Meaning |
|---|---|
| Intact-ring fraction | intact rings / all rings found |
| sp2 carbon fraction | unfunctionalized carbons / all carbons — independent of ring membership, so a dangling edge carbon with no group still counts |
| Domains | connected components of intact rings |
| Largest domain | the domain with the most rings, and its own per-axis percolation |
| Percolates a / b / c | true if *any* domain percolates that axis |

:::{note}
sp2 carbon fraction and intact-ring fraction usually move together but are
not the same number: a lone unfunctionalized carbon at a disrupted junction
counts toward the first without ever completing a ring for the second.
:::

## Scope: one structure, or a whole trajectory

{guilabel}`Current structure only` analyzes what is on screen.
{guilabel}`Every frame of the loaded trajectory` (enabled once more than one
frame is loaded — an MCMD run, most often) repeats the analysis per frame and
fills in:

- a results table, one row per frame;
- **intact-ring fraction vs. frame** and **largest domain / total rings vs.
  frame** — the two plots that make the oxidation-vs-conductivity trade-off
  visible: watch the first fall as the dose rises, and the second collapse
  once no single domain reaches across the sheet any more.

{guilabel}`Export CSV…` writes the results table; {guilabel}`Export Plots…`
saves both charts stacked into one image.

## Coloring the viewport

{guilabel}`Apply Coloring` always colors the **current** structure (not every
frame of a trajectory scope) by Cast: one color per sp2 domain — the same
golden-angle hue rotation the polycrystal grain viewer uses, so two
same-numbered domains in different structures are not implied to be
"the same" domain by a shared color — with disrupted rings and anything that
is not carbon left in the default cast.

:::{note}
This repaints the *current* frame only. Unlike **Redefine Cast on every
accepted move** in the {doc}`MCMD refinement </builders/nanomaterials>`
stage, scrubbing a trajectory after {guilabel}`Apply Coloring` does not
recolor each frame by its own domains — re-run {guilabel}`Apply Coloring`
on whichever frame you want colored.
:::

% TODO screenshot: the dialog's results table and plots beside a viewport colored by sp2 domain
```{figure} /_static/img/analysis_percolation.png
:alt: Ring/percolation dialog with the results table, the two time-evolution plots, and a viewport colored by sp2 domain
:width: 92%
:figclass: screenshot

A partially oxidized sheet: the largest domain (warm hue) still spans the
cell in one direction; smaller domains (cooler hues) are cut off by
disrupted rings between them.
```
