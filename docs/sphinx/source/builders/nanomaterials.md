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

The graphene oxide functionality is a family of four modules, each doing one job and
consuming the last one's output rather than one combined flow:

- **Graphene Oxide Builder** generates a Graphene Oxide Build.
- **GO-MDMC** takes a Graphene Oxide Build in and produces a trajectory.
- **GO Functional Group Analysis** and **GO Pair Correlation** each take either a
  Graphene Oxide Build or a GO-MDMC trajectory — whichever is open — and are read-only:
  neither one changes the structure it is given.

**Graphene Oxide Builder** generates a decorated structure. **GO-MDMC** anneals *where*
an existing decoration's groups sit, at fixed composition — several independent runs
(different temperature, seed, cycle count) from the *same* build, each producing its own
trajectory without disturbing the build itself. **GO Functional Group Analysis** and
**GO Pair Correlation** are read-only analyses of a build or a trajectory's classification
and geometry; see their own sections below. All four are grouped under
{menuselection}`Modules --> Graphene Oxide`.

One classification implementation serves every module:
`core::GrapheneOxideBuilder::findFunctionalGroups()` /
`functionalGroupLabels()`, the same bonding-based classifier the builder itself uses to
decide where a group may go. GO-MDMC, the analysis modules, and the viewport's
functional-group Cast all read the *same* answer — nothing re-derives its own notion of
"which carbon is which".

### The Graphene Oxide Build contract

Every module below reads a structure through this contract rather than its own bespoke
state. `core::GrapheneOxideBuilder::build()` writes four per-atom scalar fields onto the
structure it returns — the same `Structure::setScalarField()` mechanism already used for
things like grain IDs, index-aligned with the atoms and round-tripping through `.calproj`
and extxyz with no serializer changes required:

| Field | Meaning |
|---|---|
| `edge` | 1 on a framework carbon classified as an edge carbon, 0 elsewhere (pre-existing). |
| `go_group` | The functional-group kind (epoxide/hydroxyl/carboxyl/carbonyl, encoded the same way `functionalGroupLabels()` returns it) for every atom of a placed group, its host carbon(s) included; −1 for pristine carbon, terminating hydrogens and anything else. |
| `go_group_id` | A non-negative integer unique per placed group *instance*, shared by every atom of that instance's cluster — "every atom of group #7" is a filter over this field. −1 wherever `go_group` is −1. |
| `go_pair_id` | Shared by the two hydroxyl instances placed together under {guilabel}`Hydroxyls antiposition` — the antiposition pairing registry. −1 everywhere else. |

A structure that predates this contract, or graphene oxide imported from anywhere else,
has none of these fields. `core::GrapheneOxideBuilder::hasClassification()` is the
pre-flight check every consuming module runs; when it fails, `classifyFromBonding()` is
the *one* fallback — it recomputes all four fields from bonding alone
(`findFunctionalGroups()`, plus a geometric re-derivation of antiposition pairs: two
hydroxyl clusters whose host carbons are bonded and whose oxygens sit on opposite faces).
Both a pre-split project and a foreign import go through this exact same code, so "what
counts as a group" never has two definitions.

### Graphene Oxide Builder — generation

{menuselection}`Modules --> Graphene Oxide --> Graphene Oxide Builder…` builds a
functionalized carbon substrate — an infinite periodic sheet or a finite flake — in a
two-stage wizard. It runs natively in C++, and it is generation only: nothing here
launches a calculation. The refinement that used to be an optional third stage of this
dialog is now the separate GO-MDMC module below.

Graphene oxide has no single structure — it is a non-stoichiometric, disordered
material, and the accepted picture (Lerf–Klinowski) is a basal plane carrying epoxides
and hydroxyls, with carboxyls and carbonyls at edges and defects.
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

Four groups, in two families that are kept strictly apart:

| Group | Region | Consumes | Delivers | Notes |
|---|---|---|---|---|
| Epoxide (−O−) | basal | **two** carbons | 1 O | bridges a C–C bond; both carbons rehybridize to sp³ |
| Hydroxyl (−OH, sp³) | basal | one carbon | 1 O | above or below the plane, C–O 1.48 Å |
| Carboxyl (−COOH) | edge | one carbon | **2 O** | brings a carbon of its own; aryl C–C 1.48 Å |
| Carbonyl (=O) | edge | one carbon | 1 O | quinone-like, collinear with the C–H it replaces |

:::{note}
**Edge chemistry is carboxyl and carbonyl only.** A phenolic edge −OH used to be offered
as a third edge group and has been removed: the two that remain are what oxidative
exfoliation produces at a rim in quantity, and a third edge group with its own weight
made the edge composition under-determined without adding chemistry anyone was asking
the generator for.

This is a statement about what is *built*. A structure loaded from a file may well carry
phenolic hydroxyls, and the group finder still finds them — reported as *hydroxyl*, which
is what they are; the host carbon's coordination and the `edge` field say where it sits.
:::

On a periodic sheet the edge controls are hidden and the panel says why — earlier
versions placed carboxyls and carbonyls on the basal plane as a stated modeling
compromise, and no longer do.

#### Composition — the O/C and H/O sliders

{guilabel}`Set by` defaults to **Target O/C ratio**, and the composition is set with
sliders rather than typed numbers:

{guilabel}`Oxidation (O/C)` — oxygen per carbon in the finished structure: every oxygen
over every carbon, **including the ones carboxyls bring with them**. That is what XPS
reports, and unlike C/O it is linear in oxygen content and has a meaningful zero. The
slider runs **0.0 to 0.5**:

- **0.0** — pristine graphene, no oxygen at all.
- **0.5** — the stoichiometric ceiling, C₂O. Every oxygen needs two carbons to sit on
  (an epoxide bridges a C–C bond; a hydroxyl rehybridizes one carbon while the sheet
  still has to hold together), so no carbon framework holds more. Letting the slider
  past it would be offering compositions that do not exist.
- Heavily oxidized graphene oxide reaches 0.4–0.5, typical material 0.1–0.25, reduced
  graphene oxide below 0.1. The readout shows the equivalent C/O beside it.

{guilabel}`Basal chemistry (H/O)` — hydrogen per oxygen on the basal plane, which is the
same thing as the hydroxyl share of the basal groups: an epoxide brings one oxygen and no
hydrogen, a hydroxyl one of each. Because both deliver exactly one oxygen, weighting by
group and weighting by oxygen are identical, so the label is exact rather than
approximate:

- **0.0** — only epoxides.
- **1.0** — only hydroxyls.
- **2/3 (default)** — epoxide : hydroxyl = 1:2, the Lerf–Klinowski picture's usual case,
  somewhat more hydroxyl than epoxide.

{guilabel}`Hydroxyls antiposition` (off by default) forces every hydroxyl onto a
*neighbouring* pair of basal carbons — the same bonded pair an epoxide may bridge —
rather than each hydroxyl sitting on its own, independently chosen carbon. One −OH stands
above the plane and the other below it: a trans-diol, not two unrelated single sites. Each
placement therefore delivers two hydroxyls at once, so a requested count can be rounded up
by at most one to keep every hydroxyl paired. The pair's own two faces are always split —
this ignores {guilabel}`Decorate both faces` for its own hydroxyls, since opposite faces
are the whole point; that setting still governs epoxides and any hydroxyl placed while
this is off.

#### Edge oxidation

A separate box, shown for a nanoflake only, with two sliders — because they answer two
different questions, and folding them into one would make "more edge oxidation" silently
also mean "more carboxyl", which is not a relationship the chemistry has.

{guilabel}`Oxygen at the edges` — the share of the oxygen budget delivered at the rim
rather than on the basal plane: the edge oxidation **density**. 0.0 puts every oxygen on
the basal plane and leaves the rim fully hydrogen-terminated; 1.0 oxidizes only the rim.
Both endpoints are categorical and are honoured even if that means missing the target;
anything between is a soft split where whichever region is furthest behind gets the next
group, and one carries on alone once the other runs out of sites. A large flake has far
more basal than edge carbons, so a small edge share is the ordinary case.

{guilabel}`Edge chemistry (COOH/O)` — what that oxygen becomes: the carboxyl share of the
edge groups. 0.0 gives quinone-like carbonyls only; 1.0 gives carboxyls only. Because a
carboxyl delivers **two** oxygens and a carbonyl one, a share stated in oxygen is
converted to a propensity per group — $f$ oxygens from carboxyls means $f/2$ carboxyl
groups against $1-f$ carbonyls.

The placement loop is iterative rather than a closed-form count precisely because a
carboxyl moves *both* sides of the ratio, and it stops at the **closest reachable**
composition rather than the first one past the target — with a carboxyl worth two
oxygens, placing blindly until the ratio drops below the target can overshoot by more
than stopping short would have missed.

#### Explicit coverages

The alternative {guilabel}`Set by` mode replaces the sliders with one spin per group.
Each is the fraction of the carbons *in that group's region* which the group consumes,
not the fraction of groups. An epoxide costs two carbons and the others one, so the
values are additive within a region: 10 % epoxide plus 10 % hydroxyl functionalizes 20 %
of the basal plane, while 50 % carboxyl carboxylates half the rim.

{guilabel}`Decorate both faces` (on by default) alternates *basal* groups between the two
sides; restricting to one face puts an artificial dipole across the sheet. Edge groups
lie in the plane and are unaffected. {guilabel}`Seed` (default 0) makes the decoration
reproducible — record it with any result.

### Placement rules and shortfalls

Under explicit coverages, groups are placed in the order epoxide → carboxyl → carbonyl →
hydroxyl on shuffled sites; epoxides go first because they need a bonded
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
:alt: Functionalization stage of the Graphene Oxide builder with the O/C and H/O sliders and the edge oxidation box
:width: 92%
:figclass: screenshot

Stage 2 of the Graphene Oxide builder. Total oxidation is an O/C slider capped at the
C₂O limit, basal chemistry an H/O slider running from all-epoxide to all-hydroxyl, and
edge oxidation has its own density and carboxyl:carbonyl controls. The seed reproduces
the exact decoration.
```

## GO-MDMC — hybrid MD/MC refinement

Stage 2 above places groups at random, subject to the chemical constraints listed there;
it says nothing about whether that particular *arrangement* is favorable. **GO-MDMC**
({menuselection}`Modules --> Graphene Oxide --> GO-MDMC…`) is **MDMC** ("Molecular
Dynamics / Monte Carlo"), a hybrid annealing loop that relocates functional groups —
never adds, removes, or changes which kinds are present — to sample lower-energy
arrangements.

### Selecting an input build

GO-MDMC takes a Graphene Oxide Build as **input**, not a structure it produces itself.
Opening it shows every open document whose structure satisfies
`GrapheneOxideBuilder::hasClassification()` — falling back to `classifyFromBonding()`
for a build made before this module existed, or for graphene oxide imported from
elsewhere, when at least one functional group is findable on it — and lets you pick
which one to refine. A document with neither a persisted classification nor any
findable functional group (a structure that plainly isn't graphene oxide) is refused,
with a message pointing at the builder.

The selected build is then **copied** into a fresh document before the wizard opens; the
run stages *that* document's structure as `structure.extxyz` (the same mechanism every
job in the application uses), so the source build is never read from again once the run
starts, let alone written to. This is what makes several independent runs from the same
build safe: pick the same build again with a different temperature, seed or cycle count,
and each run gets its own copy and its own trajectory. Whether feeding a build to
GO-MDMC through the Orchestration node canvas — several runs fanned out from one source
node — is worth wiring as a first-class node pair is tracked in `FUTURE.md` rather than
half-built here; the underlying fan-out/copy-based-staging pattern the canvas already
uses for every other node would support it directly.

One cycle: pick a random existing group, release its host carbon(s) back to
the free-site pool, draw a new site **of the same kind**, rebuild the group
there, run a short MD burst under the chosen calculator, then accept or
reject the move by the Metropolis criterion at the configured temperature.
Because a move only ever relocates a group to another site of its own kind,
the sheet's total inventory — how many epoxides, hydroxyls, carboxyls,
carbonyls — is an invariant of the whole run; only *where* they sit changes.
The MD burst is the **only** relaxation mechanism in the run: there is
deliberately no geometry optimizer anywhere in the protocol.

### Initial equilibration

The builder places every group analytically on a *flat* sheet — each host
carbon is still planar sp² where the chemistry wants it pyramidal — so an
as-built structure carries forces of ~10 eV/Å on its carbons and tens of eV
of strain in total. Released in a single short burst, that is a thermal shock
of thousands of kelvin for a few femtoseconds, and whichever group the builder
placed closest to a neighbour comes apart; the first real MACE-MP run of this
module died exactly so, before its first cycle.

The run therefore opens with an **equilibration** stage of its own
({guilabel}`Equilibration steps`, 400 by default, under
{guilabel}`Equilibration friction`, 0.1 fs⁻¹ — five times the per-cycle
coupling, because this stage has the strain to drain *as* it is released).
The chemistry is checked every 10 steps. A group that still opens is
**relocated** to a freshly drawn free site of its own kind — the sampler's
own move, inventory preserved — and the dynamics resumes from the last intact
state; each relocation is reported in the log and counted in
`mdmc_summary.json` (`equilibration_relocations`). The run refuses only when
relocating stops helping (a temperature that breaks the chemistry at any
site) or when the carbon framework itself comes apart. Zero steps skips the
stage and starts the walk from the as-built geometry's own energy.

### Proposals, clearance and reversion

Every proposal passes a **steric clearance** before any energy is evaluated:
heavy atoms of different groups no closer than 2.0 Å, nothing closer than
1.6 Å to a hydrogen (a hydrogen bond still passes). This is deliberately
wider than the builder's own 1.55 Å placement rule — the builder defines the
motif space of a composition and rightly admits an epoxide with a same-face
hydroxyl on an adjacent carbon (1.89 Å oxygen to oxygen on the flat sheet, a
real Lerf–Klinowski motif); the clearance prices a *burst*, and under
MACE-MP-0 (either size) the dynamics does not relax that contact apart, it
opens the epoxide, so proposing it only buys a full burst's worth of
rejection. The two numbers are plain constants at the top of the generated
script's clearance section, to be edited if your calculator keeps the motif.
A refused proposal is counted as `rejected_clash`, separately from moves the
burst actually broke (`rejected_topology`) and from Metropolis rejections.

Whether a group's chemistry survived a burst is judged at **1.3 ×** the
covalent radii, not the 1.2 × that recognizes the groups on the cold input.
The two are different questions: measured on an intact sheet under
MACE-MP-0, the longest intact O–H at 300 K came within 0.5 % of the 1.2 ×
cutoff, and at 400 K several intact hydroxyls crossed it transiently while
only a real proton transfer crossed 1.3 ×. A bond that has really gone is at
2–2.5 Å within femtoseconds and clears either number; the looser one only
stops "broken" from meaning "warm".

A rejected move is reverted *exactly*: positions, momenta, the group
inventory, the site pools — and, under NPT, the **cell**. The barostat is
the per-axis Berendsen variant masked to the in-plane vectors (the mask is
derived from the sheet's own normal, whatever the input file says about
periodicity along the vacuum), so the vacuum gap and every out-of-plane bond
are never scaled with the lattice; and its temperature coupling follows the
friction knob rather than ASE's 500 fs production default, which would leave
a burst of tens of femtoseconds effectively unthermostatted.

```{admonition} What the Metropolis test compares
:class: note

The energies compared are the instantaneous potential energies at the end of
two thermal bursts, not minimized energies, so each carries the thermal
fluctuation of the whole sheet (of order *N·k*{sub}`B`*T*/2, a few eV for a
100-atom sheet at 300 K). This is inherent to the MD-as-relaxation protocol —
it is what makes the run an annealing walk rather than a basin-hopping
search — and it is why a low acceptance ratio at 300 K is normal rather than
a sign something is wrong.

The other half of the story is the burst length. A freshly placed group
carries placement strain, and a burst too short to dissipate it hands
Metropolis a trial energy biased upward by that amount, so nothing is
accepted however good the new site is. The burst's length is measured, not
guessed: at 300 K with a
0.5 fs step, ΔE is still +1.8 eV after 10–20 steps, +0.4 eV at 40, inside
the ±0.4 eV thermal noise around 80–120, and *negative* (−0.8 to −1.8 eV) by
200 — only then does the burst carry the sheet's own slow relaxation past the
placement strain, so the energy keeps dropping and Metropolis keeps
accepting. The default nevertheless stays in the protocol's own regime —
many cheap cycles with a short burst each (20 steps); the figures above are
what to expect when lengthening it. The equilibration stage is logged
(energy and temperature every 100 steps) and drives the progress bar; the
equilibrated structure is then streamed once, as the run's first frame.
`metrics.json`
records the ΔE each Metropolis test actually saw as `trial_delta`, one value
per judged cycle: if it sits eV above zero cycle after cycle, the burst is
too short for the move to be judged fairly — lengthen it before touching the
temperature.
```

```{admonition} Colours on a thermal frame
:class: note

{guilabel}`Redefine Cast on every accepted move` recolours each frame from
its own bonding. An MDMC frame is a thermal snapshot, and the application's
cold 1.15 × bond tolerance reads a hot, intact bond as broken — on one 335 K
frame two of six epoxides showed as carbonyls. Those frames are therefore
classified at the same 1.3 × tolerance the run judges its own chemistry by
(`GrapheneOxideBuilder::kThermalBondTolerance`); the analysis modules and the
builder keep the cold default for built or relaxed geometries.
```

If the input build carries antiposition pairs (its `go_pair_id` field, read
straight off the build rather than re-typed as a checkbox here), each
bonded, opposite-face hydroxyl pair is one of these "groups" in its own
right: MDMC recovers every such pair from the input's own geometry once, at
the start, and from then on moves, checks and reports it as a single
compound unit — drawing a new *bonded pair* of free carbons (the same pool
an epoxide draws from) and rebuilding both −OH groups with a fresh,
independent opposite-face split. A swap can therefore never separate the two
halves of a pair onto unrelated carbons; a hydroxyl with no eligible partner
(possible when a requested count was odd) moves on its own, exactly as it
would with an unpaired build.

MDMC is where a **calculator** is chosen, and `SimulationWizardBase`'s
engine picker, environment resolution and script review are reused rather
than duplicated. Settings include the annealing temperature, the number of
MC cycles, the MD burst length between moves, the equilibration stage above,
and (under **Output**) how much of the run to stream to the viewport live.
The cost estimate on the page counts the equilibration steps too.

### Live partial results

#### Two viewport tabs, and only two

Starting a run opens exactly **two** viewport tabs, each following one of the two
structure files the script appends to as it goes:

{guilabel}`GO-MDMC / All Structures`
: every geometry the walk visits — the equilibrated starting point, each MD burst,
  and each trial configuration, accepted or rejected — in the order they happened.

{guilabel}`GO-MDMC / Accepted`
: the accepted configurations alone, each carrying its acceptance ordinal. This is
  the ensemble; the tab is kept even when a run accepts nothing, because "nothing was
  accepted" is a result and not a missing one.

Both follow their file on the same `metrics.json` polling tick the plots use, so
frames appear as they are written. Neither is seeded with the input geometry as frame
0 — the input carries no evaluated energy, and scrubbing back onto it would show a
frame unlike every other one.

The run's working copy of the input build exists internally (it is what gets staged as
`structure.extxyz`) but **does not open a tab of its own**, and neither does the
generic streamed-trajectory tab other frame-producing runs get: *All Structures*
already carries the same geometries, with the per-atom functional-group columns the
stdout frame stream cannot express. A tab you had open before starting the run is
untouched.

#### Plots and counters

While a run is in progress, the **Results** dock's {guilabel}`Energy` tab plots total
energy against cycle exactly like any other monitored job, on the same polling channel
(`metrics.json`, written by `_calango_metric()`) — reopening the panel reconnects to a
run already under way the same way it does for any other job type.

One more tab is GO-MDMC specific:

{guilabel}`Acceptance` plots the **windowed** (last 50 judged moves) Metropolis
acceptance rate, broken out **by move type** — each functional-group swap kind, plus
antiposition pair moves counted separately from ordinary hydroxyl moves — because a
per-move-class rate is the actual diagnostic for whether a run is mixing: a kind stuck
near 0% means that move is never finding room, which a single overall acceptance number
hides. Only kinds actually attempted get a line, so a build with no carboxyls does not
clutter the legend with a permanent flat zero.

(go-mdmc-summary)=

The run's **counters** live in a window of their own rather than in a Results tab.
{guilabel}`MDMC Summary` shows the five whole-run quantities — cycles done out of
total, MD steps, accepted count, acceptance percentage and elapsed time — above the
same acceptance analysis as a table instead of a trend: one row per move kind (again,
only kinds attempted), each showing attempts, accepted count and the **cumulative**
acceptance ratio for the whole run so far. The plot favors the windowed rate because it
shows drift; the table favors the cumulative rate because it is the number that actually
settles.

It opens two ways:

* **automatically when a run finishes**, and only when it finishes *successfully* — a
  failed or aborted run is already reported by the Processes panel (a red row whose
  tooltip carries the reason) and its counters stopped meaning anything when it died;
* **on double-clicking the GO-MDMC row in the Processes panel**, during the run or long
  afterwards. During a run the numbers update underneath the open window; for a finished
  one they are read back from that run's `proc_<id>/metrics.json`. (Double-clicking any
  other kind of process still loads that run's result, which is what double-click has
  always done.)

The window is modeless and single, so double-clicking again raises the one that is
already open, and it is bound to one run — polling another process leaves it alone.

Everything here refreshes from the very same `metrics.json` the Energy tab reads, so a
completed run's acceptance analysis stays inspectable afterward exactly like the energy
trace does, and {guilabel}`Export Data…` / {guilabel}`Export CSV…` write it out the same
way every other export button does.

#### Cast follows the chemistry, frame by frame

**Redefine Cast on every accepted move** (Output page, on by default)
closes a gap a fixed, frame-0 Cast would otherwise leave open: since MDMC's
whole point is relocating groups, a Cast computed once at the start goes
stale the moment the first move is accepted — the carbon that WAS
"epoxide" may now be bare, and a bare carbon elsewhere may have just become
one.

With the option on, every frame that arrives in either live tab is
reclassified from its own bonding —
using the *same* connectivity-based classifier
(`core::GrapheneOxideBuilder::functionalGroupLabels()`) the builder itself
uses to decide where a group can go, not a second, re-derived notion of
"which carbon is which" — and the result becomes that frame's own
{ref}`per-frame Cast override <per-frame-cast>`. Scrubbing or
playing back the resulting trajectory recolors the affected carbons live:
epoxide-amber, hydroxyl-blue, carboxyl-magenta, carbonyl-teal, bare carbon
in cast 0 — a **fixed** key across the whole run, so a color always means
the same chemistry in every frame, even though *which atoms* wear it
changes. Turning the option off leaves the older, flat behavior: one Cast,
computed from whichever frame happened to be current when it was last set.

### From chemistry to conductivity

The functional-group Cast above answers "where is the oxygen?" — the
question one step further on is whether the *unoxidized* carbon still forms
an unbroken conducting sheet. {doc}`Analysis --> Benzene-Ring / sp2
Percolation Analysis… </analysis/percolation>` answers exactly that: it
finds every six-membered ring, classifies each intact/disrupted from this
same `functionalGroupLabels()` call, and reports whether the intact rings
still percolate the periodic cell. Run against a whole MDMC trajectory, its
intact-ring-fraction and largest-domain plots are the structural side of the
oxidation-vs-conductivity trade-off the run traces out.

## GO Functional Group Analysis — census and geometry

{menuselection}`Modules --> Graphene Oxide --> GO Functional Group Analysis…`
is a read-only analysis of a Graphene Oxide Build or a GO-MDMC trajectory:
which groups are present and how much they distort the sheet around them.
Current structure, or every frame of a loaded trajectory — the same scope
radios {doc}`Analysis --> Benzene-Ring / sp2 Percolation Analysis…
</analysis/percolation>` uses.

Classification is, once again, `core::GrapheneOxideBuilder::
findFunctionalGroups()` — the one implementation every GO module in this
family reads from bonding, never a second one. Opening the dialog on a
structure with no persisted classification (a project saved before it
existed, or graphene oxide imported from anywhere else) works exactly the
same way: nothing here needs the persisted fields at all, since the
geometric measurements below have to read the frame's actual atoms
regardless.

**Census** — a table of instances and *surface carbons* per group (2 for an
epoxide, 1 for the others), each group's surface concentration (instances
over framework carbons), the pristine sp2 carbon's own share, how many basal
(epoxide/hydroxyl) oxygens sit above vs. below the mean sheet plane, and how
many antiposition pairs are present.

**Geometry** — four distributions, each a histogram with mean and σ marked
on it, resolved by environment:

- **C-C bond length**, pristine (both endpoints unfunctionalized) vs.
  functionalized-adjacent (either endpoint hosts a group) — the sp3
  rehybridization signal.
- **C-C-C angle**, resolved by whether the CENTER carbon is functionalized —
  the sheet distortion right at a functionalized site vs. everywhere else.
- **C-O-C angle**, epoxide only — the strained three-membered ring angle.
- **C-O-H angle**, hydroxyl and carboxyl — both carry an explicit hydrogen on
  their oxygen. Carbonyl (=O, no hydrogen at all) is reported as
  **skipped**, by name, rather than silently missing from this distribution.

For a trajectory, each distribution also gets an evolution plot: the MEAN of
that environment's samples plotted against frame, so an MDMC run's effect on
the local geometry (not just which groups are present) is visible over the
course of the annealing. A frame with zero samples of an environment is a
gap in that line, never a plotted zero.

**Highlight** recolors the current structure by the chosen group kind (or
"Pristine framework", or "All group kinds" — the same fixed key
{ref}`per-frame-cast` uses for GO-MDMC) via the Cast machinery, always the
current structure even when the scope above is the whole trajectory — the
same convention RingPercolationDialog's own "Apply Coloring" follows.
Results table, distributions and evolution plots all export: **Export
CSV…** writes the full census and per-frame distribution means; **Export
Plots…** saves whichever geometry tab is currently showing as a PNG.

## GO Pair Correlation — is the decoration ordered or clustered?

{menuselection}`Modules --> Graphene Oxide --> GO Pair Correlation…` asks a
different question from the census above: not *how much* of each group is
present, but *where* — do epoxides sit next to other epoxides, or does the
decoration avoid putting two of the same kind near each other? This is the
Warren-Cowley short-range-order question, borrowed directly from alloy
theory.

### The mapping

Every framework carbon has a "species": its functionalization state —
pristine, epoxide-C, hydroxyl-C, carboxyl-C or carbonyl-C — standing in for
the chemical element a real alloy's Warren-Cowley parameter would use. Every
site is still physically carbon; only the LABEL changes. This module reuses
`core::computeWarrenCowley()` — the {doc}`Warren-Cowley Analysis
</analysis/order>` module's own, already-tested SRO math — completely
unchanged: its only new work is building a carbon-only structure with an
internal, never-displayed fake atomic number standing in for each
functionalization state (the same "sublattice only, spectators dropped"
trick the SQS generator already uses to feed a non-trivial structure subset
through the identical function), so the shell math, the periodic-image
handling and the α formula are the alloy module's code, not a second
implementation of it.

$$\alpha_{ij}(n) = 1 - \frac{P_{j|i}(n)}{c_j}$$

for coordination shell $n$, the probability $P_{j|i}(n)$ that a neighbor of
an $i$-type site in that shell is of type $j$, and $c_j$ the overall
concentration of $j$. Reading $\alpha_{ij}$ for two DIFFERENT species $i
\neq j$ (the matrix's off-diagonal, and the number the shell heatmap and the
evolution plot both show): $\alpha < 0$ is attraction/ordering between the
two groups (unlike neighbors preferred), $\alpha > 0$ is
clustering/repulsion-of-unlike (like neighbors preferred), $\alpha = 0$ is
the random-decoration expectation. (The diagonal, $\alpha_{ii}$, carries the
opposite sign convention by construction — a formula property of the
multicomponent parameter, not a second rule to memorize.)

### Shells

{guilabel}`Shells` sets how many coordination shells to compute (default 3).
The radius cutoffs are not a hardcoded lattice-sum formula — they are
discovered empirically from a real pristine sheet's own bonding
(`core::honeycombShellCutoffs()`), the same "measure it, do not assume it"
discipline the shell-enumeration test applies: shell 1 has 3 neighbors
(nearest C-C bond), shell 2 has 6 (next-nearest, same sublattice), shell 3
has 3 (opposite sublattice, twice the bond length) — verified against a
built sheet's actual coordination, not memorized.

### Reading the results

The **matrix** view shows one shell's full $\alpha_{ij}$ table, one row per
central species and one column per neighbor species, shaded blue for
$\alpha < 0$ and red for $\alpha > 0$ (white at zero) — pick the shell from
the combo above it. Each cell also carries a **counting-statistics error
bar**: $\sigma_p = \sqrt{p(1-p)/N}$ on the underlying neighbor-pair
probability, propagated to $\sigma_\alpha = \sigma_p / c_j$ — the honest
uncertainty a single structure's finite neighbor count carries, shown
alongside the number rather than left implicit.

For a trajectory, the **evolution** plot tracks one chosen $\alpha_{ij}$ at
shell 1 against frame — the scientific question an MDMC run's Monte Carlo
sampling actually answers: is it driving the decoration toward ordering or
toward clustering? The statistics line above the plot reports the
trajectory-averaged value with an autocorrelation-corrected standard error
(`core::analyseSeries()`, the same convention
{doc}`Thermodynamic Integration </simulations/thermodynamic_integration>`
already established for MD/MC series, since naive $\sigma/\sqrt{N}$ across
correlated frames under-reports the error) alongside the plain
block-averaged figure as an independent cross-check.

**Export CSV…** writes every shell's full matrix, every frame, with its
counting error; **Export Plot…** saves the evolution plot.
