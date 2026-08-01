# Coordination and chemical order

Three tools that classify *sites* rather than distances: per-atom
coordination and generalized coordination numbers, the Warren–Cowley
short-range-order parameters of an alloy, and the Piaggi–Parrinello
local-entropy fingerprint that separates crystal-like from liquid-like
environments.

---

## Coordination numbers (CN / GCN)

{menuselection}`Analysis --> Coordination Numbers (CN / GCN)…` computes each
atom's coordination number and its generalized coordination number.

| Control | Meaning | Default |
|---|---|---|
| {guilabel}`Neighbor cutoff` | {guilabel}`Covalent radii × tolerance` or {guilabel}`Fixed cutoff radius` | tolerance 1.15 / fixed 3 Å |
| {guilabel}`Bulk CN reference` | The $\mathrm{cn_{max}}$ normalizing the GCN — 12 (fcc/hcp), 8 (bcc), 4 (diamond), or {guilabel}`Auto (max CN found)` | 12 |

The generalized coordination number of Calle-Vallejo and co-workers weights
each neighbour by *its own* coordination:

$$
\mathrm{GCN}_i \;=\; \frac{1}{\mathrm{cn_{max}}} \sum_{j \in \mathcal{N}(i)} \mathrm{cn}_j .
$$

**This is what distinguishes sites that plain CN cannot**: a terrace atom, a
step-edge atom, and a vertex atom on a nanoparticle can share the same CN
yet have clearly different GCN — which is precisely why GCN is such a good
descriptor for catalytic activity.

Results appear as a per-atom table (index, element, CN, GCN) with a summary
line giving ranges and means. Two buttons push the result onto the
structure: {guilabel}`Color Viewport by CN` and
{guilabel}`Color Viewport by GCN` store the values as per-atom fields and
tint the 3D viewport through the custom-property colour mode
({doc}`/representation`).

:::{note}
Periodic images are enumerated explicitly, so a primitive fcc cell
correctly reports CN = 12 even though all twelve neighbours are images of
the single atom in the cell.
:::

% TODO screenshot: the CN/GCN dialog beside the viewport colored by GCN on a nanoparticle
```{figure} /_static/img/analysis_gcn.png
:alt: Coordination dialog with the per-atom table and a nanoparticle viewport colored by GCN
:width: 92%
:figclass: screenshot

GCN separates vertex, edge, and facet sites of a nanoparticle that plain CN
lumps together; {guilabel}`Color Viewport by GCN` makes the map visible.
```

---

## Warren–Cowley chemical order

{menuselection}`Modules --> Alloys --> Warren-Cowley Analysis…` quantifies
chemical short-range order in a multicomponent alloy through

$$
\alpha_{ij} \;=\; 1 - \frac{p_{ij}}{c_j},
$$

where $p_{ij}$ is the probability that a neighbour of an $i$-type atom is of
type $j$, and $c_j$ is the overall concentration of $j$. The sign carries
the physics:

| Value | Meaning |
|---|---|
| $\alpha_{ij} = 0$ | Ideal random alloy |
| $\alpha_{ij} < 0$ | Ordering — unlike pairs preferred |
| $\alpha_{ij} > 0$ | Clustering / segregation of like species |

Set {guilabel}`First shell cutoff` (default 3.2 Å) and, optionally,
{guilabel}`Second shell cutoff` (default 4.8 Å). The dialog reports the
concentrations, the mean coordination of each shell, and the full
$\alpha_{ij}$ matrix for every ordered species pair, exportable as CSV.

:::{tip}
A quick sanity check: a perfectly ordered B2 (CsCl-type) binary gives
$\alpha_{AB} = -1$ in the first shell and $+1$ in the second, while a random
decoration of the same lattice gives values near zero. Run it on an SQS you
just generated ({menuselection}`Modules --> Alloys --> Special Quasirandom
Structure (SQS)…`) to verify the SQS actually achieved near-zero
correlations.
:::

This tool sits under {guilabel}`Modules --> Alloys` beside the SQS generator
and the cluster-expansion pair, because the three form one workflow: build a
disordered structure, quantify its order, expand its energetics.

---

## Local entropy

{menuselection}`Analysis --> Local Entropy Analysis…` computes the per-atom
pair-entropy fingerprint of Piaggi and Parrinello, in units of $k_B$:

$$
s_S^i \;=\; -2\pi\rho \int_0^{r_c}
  \left[\, g_i(r)\ln g_i(r) - g_i(r) + 1 \,\right] r^2 \, \mathrm{d}r ,
$$

with $g_i(r)$ the Gaussian-smoothed radial distribution around atom $i$.

| Control | Meaning | Default |
|---|---|---|
| {guilabel}`Cutoff` | Integration cutoff $r_c$ — three or four coordination shells works well | 5 Å |
| {guilabel}`Broadening σ` | Gaussian width of the smoothed $g_i(r)$ | 0.15 Å |
| {guilabel}`Radial grid points` | Sampling of the integrand | 100 |
| {guilabel}`Average over neighbors` | Smooth $s_i$ over each atom's neighbourhood | off |

The result is stored as the per-atom field `local_entropy` and mapped onto
the atoms immediately, with a distribution histogram in the dialog.
**Lower (more negative) values mark ordered, crystal-like environments;
higher values mark disordered, liquid-like ones** — which makes $s_S^i$ an
effective order parameter for melting, solid–liquid interfaces, and grain
boundaries.

Neighbour averaging sharpens the contrast between ordered and disordered
*regions*: an interface atom inherits some of both phases' signal, and
averaging pushes it toward whichever side it actually sits on. Turn it on
when you want to segment phases, off when you want the raw per-site
fingerprint.

:::{note}
Because `local_entropy` is an ordinary per-atom scalar field, it survives a
save to extended XYZ as a column, reappears on reload, and can drive the
viewport colouring like any imported field ({doc}`/data_io`). A histogram
with two separated peaks is the signature of coexisting phases; track the
peak weights across a trajectory to watch a melt front move.
:::
