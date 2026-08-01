# Dislocations

{menuselection}`Build --> Dislocation` inserts a Volterra line defect into the structure
in the current tab by displacing its atoms with a closed-form elastic field. The result
opens as a new tab; the parent is left alone. The builder is **native C++** — no Python
environment involved.

A dislocation is one operation: cut the crystal along a half-plane bounded by a line,
slide the two faces past each other by a lattice vector $\mathbf{b}$, and weld them back
together. Everything this module does is that operation, evaluated analytically at every
atom.

---

## Stage 1 — type and geometry

{guilabel}`Line direction` names the Cartesian axis the dislocation line runs along
(default *z*); the other two axes follow in cyclic order as
$(\mathbf{e}_1, \mathbf{e}_2)$. For a line along $z$ those are $(x, y)$, and an edge dislocation then
has $\mathbf{b}$ along $x$ with its glide-plane normal along $y$ — **naming the line
names everything**.

{guilabel}`Burgers vector |b|` is the magnitude in Å, default 2.5 Å, with a
{guilabel}`Sign` combo for $\pm\mathbf{b}$. Use a lattice repeat of the host: a perfect
dislocation's Burgers vector *is* a lattice vector, and any other length leaves a
stacking fault trailing behind the line that this module does not model.
{guilabel}`Line position (e₁, e₂)` places the line in the plane normal to it, as fractions of the
structure's extent — the default (0.5083, 0.5083) is deliberately a little off centre,
because linear elasticity is singular *on* the line and a line placed exactly on a
lattice site evaluates that atom's displacement at infinity.

| Type | What it builds |
|---|---|
| Edge | Single edge dislocation: $\mathbf{b} \perp$ line, an extra half-plane ends on it, and the atom count is unchanged — this is the elastic field, not a plane of atoms inserted by hand |
| Screw | Single screw dislocation: $\mathbf{b} \parallel$ line; the lattice planes form one continuous helical ramp, and only the component along the line moves |
| Glide | Two opposite edge dislocations in the *same* glide plane — what a dislocation leaves behind after gliding a distance $d$. Between the cores the crystal above the plane has slipped by exactly one $\mathbf{b}$. Conservative: no atom created or destroyed |
| Climb | Two opposite edge dislocations stacked normal to the glide plane, with a platelet of *missing* material between them — a collapsed vacancy disc one $\mathbf{b}$ thick. **Climb is mass transport, so this is the only construction that changes the atom count** — and that change is how you tell it happened |
| Anisotropic | A single dislocation solved from the full elastic tensor via Stroh's sextic formalism — the only option that can describe a *mixed* dislocation, and the only one correct for a strongly anisotropic crystal |

The page requires a periodic cell and a non-empty structure, and says so when either is
missing.

---

## Stage 2 — elasticity

The isotropic types read a single {guilabel}`Poisson ratio` ν, default 0.33 (0–0.499); a
screw dislocation does not depend on it at all. The two dipoles read a
{guilabel}`Core separation` — the distance the dislocation travelled for glide, the height of the
vacancy platelet for climb — whose default 0 means *auto (⅓ of the cell)*.

The anisotropic type reads an elastic tensor instead, built from the constants of a
chosen {guilabel}`Elastic symmetry`:

| Symmetry | Constants |
|---|---|
| Cubic | $C_{11}$, $C_{12}$, $C_{44}$ |
| Hexagonal | $C_{11}$, $C_{12}$, $C_{13}$, $C_{33}$, $C_{44}$ |
| Isotropic | $C_{11}$, $C_{12}$ |

Room-temperature single-crystal presets fill the cubic constants (in GPa):

| Preset | $C_{11}$ | $C_{12}$ | $C_{44}$ |
|---|---|---|---|
| Cu | 168.4 | 121.4 | 75.4 |
| Al | 106.8 | 60.4 | 28.3 |
| α-Fe | 231.4 | 134.7 | 116.4 |
| Ni | 246.5 | 147.3 | 124.7 |
| W | 522.4 | 204.4 | 160.8 |
| Si | 165.6 | 63.9 | 79.5 |

**Only the ratios of the constants reach the displacement field** — the absolute scale
cancels — but keeping physical units means numbers can be read straight off a table. The
{guilabel}`Burgers direction` gives $\mathbf{b}$'s components in the dislocation frame:
$(1,0,0)$ (the default) is a pure edge, $(0,0,1)$ a pure screw, anything between a mixed
dislocation.

The page runs the real builder on every change and reports what it produced — atom
count, largest displacement, closest resulting pair, and any warnings. **It is not a
model of what {guilabel}`Finish` will do; it is what {guilabel}`Finish` does.**

% TODO screenshot: Dislocation wizard stage 2 in anisotropic mode with the Cu preset loaded and the live report showing atom count and max displacement
```{figure} /_static/img/builders_dislocations_wizard.png
:alt: Elasticity stage with the cubic-preset combo, elastic constants in GPa, Burgers direction spins and the live builder report
:width: 92%
:figclass: screenshot

Stage 2. The report at the bottom comes from running the actual builder on the current
settings, not from an estimate.
```

---

## Periodicity

A single dislocation has a net Burgers vector, so its displacement field is multivalued:
it cannot be made periodic in the two directions normal to the line, whatever the cell.
**Edge, Screw and Anisotropic therefore leave the cell periodic only along the line**
and say so — which is the correct setup for a cylinder with free lateral surfaces, the
usual way core structures are computed.

A dipole has zero net Burgers vector, so Glide and Climb stay periodic in all three
directions. Reaching that takes one extra step: superposing two opposite fields cancels
the net Burgers vector but leaves the average *plastic distortion* the pair carries, so
a **compensating uniform distortion — a simple shear for glide, a uniaxial contraction
for climb — is applied to the atoms and to the cell vectors**. Without it the
construction looks perfectly normal in a viewer and carries a $|\mathbf{b}|$-sized
discontinuity at the periodic boundary.

$$
u_z^{\text{screw}}(x, y) = \frac{b}{2\pi} \arctan\!\frac{y - y_0}{x - x_0}
$$

The screw field above makes the multivaluedness visible: going once around the line
advances $u_z$ by exactly $b$ — there is no way to close that seam periodically with a
single line in the cell.

:::{warning}
**Nothing here is relaxed, and linear elasticity is singular at the line.** The handful
of atoms nearest the core arrive at positions that are qualitatively right and
numerically meaningless. Relax the core before quoting anything about it — a core
energy, a Peierls barrier, a dissociation width.
:::

:::{note}
Only the vacancy sense of climb is built (material removed). The interstitial sense
would mean inserting a partial plane of atoms, which needs the periodicity of the host
lattice and not merely its Burgers vector — it is refused rather than approximated. The
module also reports when the requested $|\mathbf{b}|$ is smaller than the plane spacing
it was meant to remove, in which case the result is an elastic dipole with no mass
transport at all.
:::

---

## Practical recipe

For an fcc metal, a sensible first cell is a conventional-cell supercell elongated in
the two directions normal to the intended line (see {doc}`/builders/slabs` for the
supercell matrix tool), with $|\mathbf{b}| = a/\sqrt{2}$ — the
$\tfrac{a}{2}\langle 110\rangle$ perfect Burgers vector. Build a Glide dipole if you need full periodicity
for a plane-wave code; build a single Edge or Screw and treat the cell as a cluster if
you are studying the core with a local potential. Either way, run a relaxation before
any number leaves the viewport.

The stage-2 report's *closest resulting pair* is the number to watch before
{guilabel}`Finish`: a pair distance far below the host's bond length means the line — or
one core of a dipole — landed on top of an atom, and nudging the line position by a few
hundredths of a cell fraction recomputes the report immediately. For a climb dipole,
also compare the atom count against the parent tab: the difference *is* the vacancy
platelet, and a difference of zero means the requested $|\mathbf{b}|$ removed no plane
at all.
