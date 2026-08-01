# Symmetry and magnetic symmetry

Two dialogs classify the structure's symmetry. The first asks the
crystallographic question — which space group, and what do its
representations say a Raman or IR spectrum should show. The second asks the
magnetic one — which of the 1651 magnetic space groups the coordinates
*together with the magnetic moments* realize. Both need `spglib` and a
periodic structure.

The {guilabel}`Structure` dock keeps an always-on readout of the detected
space group with its own tolerance control; the dialogs below are the full
analyses built on the same spglib detection.

---

## Symmetry, Raman & IR activity

{menuselection}`Analysis --> Symmetry, Raman && IR Activity…` opens one
dialog with one {guilabel}`Tolerance` control (spglib's `symprec`, default
**0.001 Å**) and a {guilabel}`Detect` button. Everything in the dialog is
computed from that single detection — the identity block, the Wyckoff
table, the character table, and the mode activity — so no two tabs can ever
describe different groups.

The identity block reports the space group (symbol and number), the point
group in **both crystallographic conventions** — the Hermann–Mauguin symbol
spglib reports plus its Schönflies counterpart, e.g. *3m* (C₃ᵥ) — the
crystal system, the Hall number, and the count of inequivalent sites.

:::{tip}
A relaxed cell that reads *P1* for an obviously symmetric crystal is
numerical noise, not broken symmetry — raise the tolerance to 0.01 Å or
more and detect again.
:::

**Equivalent positions** lists every atom with fractional coordinates, its
Wyckoff letter, and its equivalence class, so you can see which atoms the
group relates.

**Character table** shows the character table of the detected point group —
rows are irreducible representations with Mulliken symbols, columns are
conjugacy classes, identity first. **The table is generated numerically
from the group's own operations via the class-sum algebra, not looked up**,
so it follows the tolerance-dependent detection above; paired
complex-conjugate irreps are shown as their physically real 2D sum, the
spectroscopic convention.

**Raman & IR activity** is the Γ-point factor-group (nuclear-site)
analysis: which optical phonons are Raman-active, IR-active, or silent, by
symmetry alone. The mechanical representation

$$
\chi(R) \;=\; (\pm 1 + 2\cos\theta)\, N_\text{unmoved}(R)
$$

is reduced into irreps, the three acoustic translations are subtracted, and
activity follows from the vector representation (IR) and the symmetric
polarizability representation (Raman). The tab reports the space group,
point group, atoms in the primitive cell, the 3N mode count, and a table of
irreps with degeneracy, optical-mode count, and activity — counted in irrep
*copies*, because a triply degenerate T₂g appearing twice is six modes and
two spectral lines.

:::{tip}
For silicon in the diamond structure the result is the textbook
$\Gamma = T_{2g} + T_{1u}$: one triply degenerate Raman-active optical
mode, plus the acoustic branch.
:::

:::{note}
Mulliken subscripts are assigned from Cartesian axis geometry. In
low-symmetry orthorhombic groups the subscript convention can come out
permuted relative to a particular textbook's axis choice — the activities
and degeneracies are unaffected. This tab predicts *which* modes are
active; computing the actual spectra is
{menuselection}`Electronics --> Raman and IR Spectroscopy…`
({doc}`/electronic/raman_ir`).
:::

% TODO screenshot: the Symmetry dialog on diamond-Si, Raman & IR activity tab showing T2g Raman / T1u acoustic
```{figure} /_static/img/analysis_symmetry.png
:alt: Symmetry dialog with space group readout and the Raman/IR activity table for silicon
:width: 92%
:figclass: screenshot

Diamond silicon: space group $Fd\bar{3}m$, point group $m\bar{3}m$ (O$_h$),
and the factor-group table reducing Γ to T₂g + T₁u.
```

---

## Magnetic space group

{menuselection}`Analysis --> Magnetic Space Group…` determines which of the
**1651 magnetic space groups** (MSGs) the structure realizes, from its
coordinates *together with* its magnetic moments.

An ordinary space group asks only where the atoms are. Once atoms carry
moments, an operation that maps a moment onto its own reverse is no longer
a symmetry — unless it is combined with *time reversal* $T$. Writing the
full group as $\mathcal{M} = \mathcal{G} + \mathcal{A}$, with $\mathcal{G}$
the unitary operations and $\mathcal{A}$ the antiunitary ones, the
Belov–Neronova–Smirnova (BNS) classification has four types:

| Type | Structure of $\mathcal{A}$ | Count | Meaning |
|---|---|---|---|
| I | empty | 230 | every symmetry unitary; time reversal broken outright |
| II | $T\mathcal{G}$ ("grey") | 230 | $T$ alone is a symmetry — only possible when every moment vanishes: the non-magnetic case |
| III | $T g_0 \mathcal{G}$, $g_0$ not a pure translation | 674 | half the parent group survives only with $T$; magnetic cell = crystallographic cell |
| IV | $g_0$ *is* a pure translation | 517 | an **anti-translation**; the magnetic cell is a supercell — the classic two-sublattice antiferromagnet |

The classification follows Watanabe, Po and Vishwanath, *Sci. Adv.* **4**,
eaat8685 (2018).

### Supplying the moments

{guilabel}`Magnetic moments from` offers {guilabel}`Automatic (computed,
else initial)`, {guilabel}`Computed moments (magmoms)`, and
{guilabel}`Initial moments (initial_magmoms)`. The two stored sources
answer different questions: the seed says what was *asked* for, the
converged result says what the calculation *found* — and an ordering that
collapsed during the SCF is exactly the case where they disagree.

Whichever is loaded, **the $m_x, m_y, m_z$ cells of the moment table stay
editable**. Flip a sublattice, cant a moment out of the axis, or type in an
ordering the file never carried, then press {guilabel}`Determine` — asking
"what would the group be if this sublattice flipped?" is the normal way to
use this module. A collinear input (all moments along $z$) is analysed as
collinear; any transverse component switches to the non-collinear
treatment, in which moments are axial vectors that rotate with the
operations.

### Tolerances

Two separate tolerances, side by side: the **positional** tolerance is
spglib's `symprec` (default 10⁻⁴ Å), and the **moment** tolerance is in
$\mu_B$ (default 0.001 $\mu_B$). They are separate because a moment
converged to 1.98 $\mu_B$ against its neighbour's −2.02 $\mu_B$ is one
antiferromagnet, not two inequivalent sublattices — and only a tolerance in
$\mu_B$ can say so. Raise it to absorb SCF noise; lower it to resolve a
genuine ferrimagnetic inequivalence.

### Reading the result

- {guilabel}`BNS number` — the label $S.L$, e.g. `221.97`: $S$ is one of the
  230 ordinary space groups, $L$ distinguishes its magnetic descendants.
  This is the identifier the tables are keyed by.
- {guilabel}`Type` — I–IV with a one-line statement of what it means, and,
  for type IV, the anti-translation vector itself in fractional
  coordinates.
- {guilabel}`OG number` — the Opechowski–Guccione label, the other
  convention in the literature, listed so either can be cross-referenced.
  The status line adds the UNI number (of 1651) and the Litvin number.
- {guilabel}`Parent space group` — the $S$ of the BNS label: the space
  group of the *unitary* subgroup.
- {guilabel}`Crystallographic space group` — what the same structure would
  be with the moments **ignored**, shown side by side with the parent. The
  comparison is the physics: magnetic order can only lower the symmetry,
  and the difference between these two is exactly what the magnetism broke.
  The dialog also counts it — *n* of the operations survive only in
  combination with time reversal.
- {guilabel}`Magnetic ordering` — non-magnetic, ferromagnetic,
  ferrimagnetic, or antiferromagnetic, read off $|\sum_i \mathbf{m}_i|$
  against $\sum_i |\mathbf{m}_i|$, both reported.
- {guilabel}`Magnetic class` — per atom in the moment table: the
  equivalence class *under the magnetic group*. Two atoms of the same
  element in different classes are the two sublattices of an
  antiferromagnet.

The {guilabel}`Symmetry operations` tab lists every element of the group as
a rotation matrix and translation; those marked $1'$ are the antiunitary
ones — the spatial operation alone is not a symmetry, only its combination
with time reversal is.

:::{tip}
The same cell, three answers. Take Fe at $(0,0,0)$ and
$(\tfrac12,\tfrac12,\tfrac12)$ in a cube — geometrically bcc, $Im\bar{3}m$
(No. 229). Zero moments give the grey type II group. Parallel moments give
type I, still with parent 229. Antiparallel moments give **BNS `221.97`,
type IV**: the body-centring translation now maps each moment onto its
reverse, so it survives only with $T$, and the unitary group drops to the
simple-cubic $Pm\bar{3}m$ (No. 221) while the crystallographic group stays
229. This is Calango's `magnetic_space_group` integration test.
:::

:::{warning}
Needs `spglib` with its magnetic tables and a periodic structure. A
structure carrying no moments at all is answered as a grey type II group —
correctly — and the dialog says so rather than leaving the reading
unexplained.
:::
