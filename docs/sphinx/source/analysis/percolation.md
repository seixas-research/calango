# Benzene-ring formation and sp2 percolation

{menuselection}`Analysis --> Benzene-Ring / sp2 Percolation Analysis…` turns a
graphene oxide structure's chemistry into a graph-theoretic question:
**does an unbroken sp2 pathway still cross the sheet?** Conductivity in
graphene oxide rides on the sp2 network, not the sp3 one, so the fraction of
intact benzene rings and whether they still connect edge-to-edge is the
structural side of the oxidation-vs-conductivity story an MDMC run traces
out.

The tool works on any structure — it is not gated behind having used the
{doc}`Graphene Oxide builder </builders/nanomaterials>` — but reports zero
rings on anything without a carbon framework, which is the honest answer
rather than a refusal to open.

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
frame is loaded — an MDMC run, most often) repeats the analysis per frame and
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
accepted move** in the {doc}`MDMC refinement </builders/nanomaterials>`
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
