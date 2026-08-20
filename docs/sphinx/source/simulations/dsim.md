# DSIM (Dilute Solution Interpolation)

{menuselection}`Modules --> Alloys --> DSIM (Dilute Solution Interpolation)…`
computes the mixing enthalpy $\Delta H_\text{mix}(x)$ of a binary A-B
substitutional solid solution, interpolated between its two dilute-solution
limits, from exactly four *ab initio*/MLIP calculations — no calculation at
any intermediate composition.

Working theory: L. Seixas, R. M. Tromer, R. O. Figueiredo, J. M. Almeida,
*"Enthalpies of mixing from dilute solutions to high-entropy alloys"*
(2026). Equation numbers below are that paper's own; `src/core/Dsim.hpp`/
`.cpp` and `src/core/DsimScriptGenerator.cpp` cite them directly so the
implementation can be audited against the paper equation by equation, the
same convention as {doc}`/simulations/egqca`.

---

## The model

For an $N$-component alloy the mixing enthalpy is a sum over pairwise
contributions,

$$
\Delta H_\text{mix}(\mathbf{X}) = \sum_{i<j}^{N} \Omega_{ij}\, X_i X_j,
$$

the regular-solution model when $\Omega_{ij}$ is a constant. DSIM instead
lets the interaction parameter depend on composition — the **subregular**
solution model:

$$
\Omega_{ij}^{\sigma} = \frac{M_{j[i]}\,X_i + M_{i[j]}\,X_j}{X_i + X_j + \varepsilon},
$$

which for a binary $A_{1-x}B_x$ reduces to

$$
\Delta H_\text{mix}^{\sigma}(x) = M_{2[1]}\,x(1-x)^2 + M_{1[2]}\,x^2(1-x),
$$

where $M_{i[j]}$ ("the differential mixing enthalpy of solute $i$ diluted in
host $j$") is the *slope* of $\Delta H_\text{mix}$ at the dilute limit where
$i$ is the minority species:

$$
M_{2[1]} = \left.\frac{d\Delta H_\text{mix}}{dx}\right|_{x=0}, \qquad
M_{1[2]} = -\left.\frac{d\Delta H_\text{mix}}{dx}\right|_{x=1}.
$$

Each slope is measured from **one** *ab initio* calculation — a single
substitutional impurity of $i$ in a supercell of pure $j$ — against the
mixing-enthalpy definition

$$
\Delta h_\text{mix}(x) = E(A_{1-x}B_x) - \big[(1-x)E(A) + x E(B)\big],
$$

evaluated at the one composition a single substitution in an $N_\text{atoms}$
supercell actually realizes:

$$
M_{2[1]} = \Delta h_\text{mix}(x = 1/N_\text{atoms}),
$$

and symmetrically $M_{1[2]} = \Delta h_\text{mix}(x = 1 - 1/N_\text{atoms})$
for a single $A$ impurity diluted in a supercell of pure $B$. So the whole
binary curve comes from exactly **four** numbers: the two pristine-supercell
energies (the $x=0$/$x=1$ reference points) and the two single-impurity
supercell energies (the two dilute-limit slopes) — nothing at any
intermediate composition. For $N$ components this needs $N(N-1)$
dilute-impurity calculations plus $N$ pristine references — $N^2$ total,
the paper's own headline scaling result against the exponential
$\mathcal{O}(d^{N-1})$ of the Special Quasirandom Structure (SQS) method.

**Unit convention.** $E(Z)$ in Eq. 9 is exactly what its caption says: the
**total** energy of an $N_\text{atoms}$ supercell — no per-atom division
anywhere. An earlier version of this module divided every energy by the
supercell's atom count before building $M_{i[j]}$, reasoning (wrongly) that
an intensive-looking $\Delta H_\text{mix}(x)$ required an intensive input;
that shipped, was caught by a real MACE Au-Pt run coming out with a peak
enthalpy ~27$\times$ below Fig. 2a of the paper, and was fixed. The
$x_0$-weighted subtraction inside $M_{i[j]}$ is *itself* what makes it an
(effectively) intensive, supercell-size-converged quantity: expand
Eq. 9-10 with $E(Z)$ as raw totals and $M_{i[j]}$ reduces exactly to the
standard single-substitutional-impurity formation energy,
$E_\text{imp} - (N_\text{atoms}-1)e_j - e_i$ for solute $i$ in host $j$
($e_i$/$e_j$ the pure elements' per-atom energies) — an ordinary,
$N$-converged quantity in its own right, typically 0.05-1 eV, with no
further division belonging anywhere in the pipeline. Confirmed against the
paper's own worked example (`oncapintada`'s
`examples/testing_subregular_model.ipynb`): its `energy_matrix` there is
populated with raw `atoms.get_potential_energy()` supercell totals (e.g.
$-85.98\,\text{eV}$ for a 27-atom Au cell) with no division by atom count
anywhere in the notebook, and the resulting `enthalpy_of_mixing(x)` lands
directly on the same few-kJ/mol scale as the literature Au-Pt data plotted
alongside it. `BaseAlloy._convert_energy`'s "energies are computed in
eV/atom" docstring describes the *output* (kJ per mole of alloy atoms, the
ordinary metallurgical sense of the paper's own kJ/mol axis — which this
per-defect $M$ already delivers once $x_0 = 1/N_\text{atoms}$ is folded in),
not a per-atom requirement on the *input* energy matrix — see
`core::computeMMatrix()`'s doc comment for the full derivation.

Validity, per the paper: single-phase substitutional solid solutions —
demonstrated on six fcc noble-metal binaries (Au-Pt, Au-Pd, Ag-Pd, Au-Ag,
Cu-Pd, Cu-Pt) plus the covalently-bonded Si-Ge system. The model assumes
the **same crystal structure** for both end members (it interpolates a
mixing enthalpy between two references of one lattice, not a structural
transformation), and needs a supercell large enough that one impurity does
not interact with its own periodic images — the paper uses a 3×3×3 fcc
primitive-cell supercell (27 atoms, $x_0 = 1/27$), Calango's own default.

---

## Workflow

Unlike every existing multi-calculation alloy module (EGQCA, CVM, and the
Orchestration canvas's SqsGenerator/ClusterExpansionFit/CvmEntropy/KkrCpa/
TdbGenerator — all of which run **in-process** against a batch a *prior* job
already computed), DSIM needs $N + N(N-1)$ **new** calculations of its own
($N \geq 2$ components). It therefore follows the Elastic Properties /
Piezoelectric Tensor template: one wizard, one generated script that loops
over every calculation class internally, one result file — no incoming-link
fan-out, on the canvas or off it (see "Orchestration" below for why that is
true there too).

1. **Settings (Stage 1)**: a list of $N \geq 2$ pristine reference
   structures, added from an open document ("Add from Open Document…") or
   imported from a file ("Import from File…", any format
   {doc}`/simulations/scripts` already reads) — each its own element's own
   native geometry (not a shared template relabeled), refused if it is not
   single-species or duplicates an already-added element — plus the
   supercell repeat count (default 3×3×3, the paper's own choice). A
   "Multi-phase alloy" checkbox appears here too, enabled only for $N=2$ —
   see "Multi-phase alloys" below.
2. Stage 2 is the shared Calculator Settings, exactly as in every other
   `SimulationWizardBase` wizard — any ASE calculator works (EMT, MACE,
   GPAW, VASP, …), not restricted to one engine, the same stance as
   `ElasticConfig`.
3. **Geometry Optimization Settings (Stage 3)**: DSIM's own second settings
   page — optimizer, force convergence (`ForceConvergenceControl`, default
   0.02 eV/Å, the paper's own criterion, not the shared 0.05 default every
   other wizard using that control starts from), max steps per supercell,
   and cell-relaxation mode (`CellRelaxationControls`, the same widget
   Geometry Optimization and Cluster Expansion's batch relax use — but
   defaulting to **on** here, unlike those: the paper's protocol relaxes
   the cell unconditionally, so there is no fixed-cell mode to opt out
   into). Broken out as its own stage rather than folded into Stage 1's
   settings so these choices get the same visible, configurable standing
   every other relaxation-capable wizard gives them, instead of being
   silently hardcoded to the paper's own defaults with nothing on screen
   saying so.
4. Stage 4 is the shared ASE Script Review.
5. On leaving Stage 1, every pristine supercell is built once, synchronously,
   in C++ — `pybridge::AseBridge::makeSupercell()` on each of the $N$ input
   structures independently (each keeps its own native lattice; the model
   still assumes one crystal STRUCTURE type across all $N$, per the "Unit
   convention" note's validity claims, but not one shared starting lattice
   CONSTANT) — and every impurity supercell is a single-atom relabel of the
   relevant host's pristine supercell. They are baked into the generated
   script as literal `ase.Atoms(...)` geometry — the same "precompute in
   C++, bake into the script" idiom `ElasticScriptGenerator` uses for its
   strain matrices — so DSIM needs no staged trajectory file.
6. The generated script relaxes every supercell (**ions and cell** — the
   paper's protocol: force criterion < 0.02 eV/Å including unit-cell
   relaxation, never single-point), computes the full $N \times N$
   differential-mixing-enthalpy matrix and — depending on $N$ — the
   interpolated binary $\Delta H_\text{mix}(x)$ curve ($N=2$), the ternary
   composition-triangle grid ($N=3$), and always the $N(N-1)/2$ pairwise
   binary sub-curves, with the *same* formulas as `core::Dsim`
   (reimplemented, per the project's "generated scripts never import
   Calango" convention), and writes `dsim.json` (schema `calango.dsim/2`).

---

## Outputs

The DSIM Mixing Enthalpy viewer opens automatically when a run finishes (or
on demand from the Processes panel), showing:

- **For $N=2$**: $\Delta H_\text{mix}(x)$ vs. composition, native
  `EgqcaPlotWidget` rendering (see {doc}`/reference/dependencies`), with the
  two dilute-limit tangent lines drawn alongside the curve (toggleable —
  see below) — the same "black dashed line" convention as the paper's
  Fig. 1(e)/2.
- **For $N=3$**: $\Delta H_\text{mix}$ over the composition triangle, native
  `DsimTernaryPlotWidget` rendering — filled, continuous-looking cells
  (mpltern-style) rasterized directly from the closed-form grid (no
  interpolation-from-scattered-points needed, since the grid already IS
  regular), reusing `TernaryClusterHullWidget`'s projection/gridline/
  colourbar chrome. The colourbar's tick labels render in `PlotPalette`'s
  ordinary text colour (not a separate "tick text" token no other DSIM
  plot has), its $\Delta H_\text{mix}$ label renders the real Δ glyph (a
  literal "DH_mix" string, not the intended one, was the bug), it sits a
  fixed 14px off the triangle's own right edge rather than a gap computed
  from the plot bounds, and it can be hidden entirely — a
  `showColorbar` toggle on `DsimPlotStyle`, next to the tangent-lines one.
- **For $N \geq 4$**: the $N(N-1)/2$ pairwise binary sub-curves together on
  one `EgqcaPlotWidget` (species $i$ vs. $j$, every other species held at
  0) — no direct $N$-dimensional visualization exists, so this is the
  fallback, also available as a cross-section for any $N$.
- **A table** of every pristine + impurity supercell's energy (formula, atom
  count, total and per-atom energy, relaxation convergence).
- **Customize Appearance…** (`DsimPlotStyleDialog`, the same live-update
  convention as `EnergyDiagramStyleDialog`/`OpticsPlotStyleDialog`): curve
  and tangent-line colours, a toggle for the tangent lines themselves (also
  directly on the viewer, one click, not just in this dialog — disabled for
  $N \neq 2$, where there is no single dilute-limit-pair concept), and a
  kJ/mol ↔ eV/atom unit switch for the plotted curve — a direct way to
  cross-check the plotted scale against a hand calculation without leaving
  the viewer.
- **Export Image…** / **Export Data…** (CSV: the table followed by the full
  composition grid, always in kJ/mol regardless of the viewer's current
  unit toggle), the established pattern.

## Cross-validation

`tests/DsimTest.cpp` checks `core::Dsim`'s formulas against closed-form
algebraic identities (the $x=0$/$x=1$ endpoints and the two tangent-line
slopes are exact by construction of Eq. 7) and against a fixture taken
directly from the paper's own worked Au-Pt example (`oncapintada`'s
`examples/testing_subregular_model.ipynb`, real GPAW total supercell
energies): the two implementations agree to **max deviation < 7×10⁻¹⁸
eV/atom**, full IEEE-754 double precision, and the peak $\Delta H_\text{mix}$
(≈5.1 kJ/mol near $x=0.7$) lands where Fig. 2a of the paper puts it —
a fixed regression anchor against ever reintroducing the per-atom-division
bug above (which would put this peak ~27$\times$ too low).
`tests/dsim_oncapintada_test.py` re-runs an independent, live version of the
same comparison (EMT-relaxed Cu-Pd) against the paper's own reference
implementation when the `onca` conda environment (private, not a Calango
dependency) is present, and self-skips otherwise.

A real end-to-end run — the actual generated script, not just the analysis
formulas — was also exercised against **MACE** for Au-Pt (the paper's own
system, `MACE-matpes-pbe-omat-ft`, the same foundation model the paper's
authors use in the same notebook): all four supercells relaxed and
converged (19-23 steps each), the cell volume dropped by 14% between the
Au- and Pt-pristine supercells despite starting from the same geometry —
direct evidence the unit-cell relaxation is real, not just the ions — and
the resulting curve peaked at 6.9 kJ/mol near $x=0.56$, the same
few-kJ/mol scale as the paper (MACE's own peak position and magnitude
differ somewhat from GPAW-PBE's, an expected consequence of using a
different potential, not a remaining bug).

## What else from `oncapintada` was ported, and what was not

`oncapintada`'s own DSIM implementation (`subregular_model.py`) is the one
piece of that package this module is a native C++ port of. The rest of the
package was inventoried and, for the most part, deliberately **not**
ported:

- **`phase_diagram.py`** (spinodal/binodal/critical-point extraction from a
  $G(x,T)$ grid) — the strongest remaining candidate, genuinely useful
  alongside DSIM's own $\Delta H_\text{mix}(x)$, but out of scope for this
  module's first version; a future addition, not implemented here.
- **`qca.py`** (Guggenheim's quasichemical approximation) — **not ported,
  because Calango already has it**: `core::ClusterVariation`'s
  `CvmApproximation::Pair` ("Bethe-Peierls-Guggenheim… exact on a Bethe
  lattice/Cayley tree") is the same theory under its other name, already
  exposed via {doc}`/simulations/cluster_expansion`'s CVM window.
- **`bonds_counter.py`'s Warren-Cowley SRO formula** — **not ported, same
  reason**: `core::computeWarrenCowley()` (Modules → Alloys → Warren-Cowley
  Analysis…) already implements $\alpha_{ij} = 1 - p_{ij}/c_j$, and more
  generally than the reference (shell-resolved, not a single global count).
- **`vibrational.py`**'s quasi-harmonic phonon free-energy integrals — not
  ported; Calango's own `core::PhononThermodynamics` already covers the
  same physics from a native phonon run.
- **`constants.py`** — not ported as a module; its eV↔kJ/mol conversion
  factor is `core::kEvToKjPerMol` (`core/Dsim.hpp`), using the more precise
  CODATA Faraday-constant value than the reference's own rounded one (a
  harmless discrepancy, noted for the record).
- **`disordered_alloy.py`** (largest-remainder composition rounding for a
  multi-atom target concentration) — not needed: DSIM's workflow only ever
  builds a **single** substitutional impurity per supercell, which the
  existing single-index relabel in `DsimWizard` covers directly.
- **`bonds_model.py`, `monte_carlo.py`, `generator.py`, `polymorphism.py`,
  `pair_distribution_function.py`, `view.py`, `yaml_parser.py`** — skipped:
  a synthetic-lattice toy Hamiltonian, empty stub classes, an unimplemented
  (and, on the package's own supported Python versions, unimportable) SQS
  generator, and several genuinely empty files. Nothing to port.

## Multicomponent (N ≥ 2) support

Every layer is N-component general, not a binary special case with N
bolted on:

- **`core::Dsim`**: `computeMMatrix()`/`enthalpyOfMixing()` implement
  Eq. 4+6 for an arbitrary $N \times N$ energy matrix; `solveDsimBinary()`
  is a convenience wrapper around `solveDsimMulticomponent()`, not a
  separate implementation. `simplexGrid()` mirrors oncapintada's own
  `MultiComponentAlloy.simplex_grid()` construction for the $N=3$ ternary
  grid. `tests/DsimTest.cpp` checks all of this against a live run of
  `oncapintada.MultiComponentAlloy` on its own $N=3$ test fixture (a
  28-point composition-simplex grid), agreeing to full floating-point
  precision — a real cross-validation, not just internal self-consistency.
- **`DsimScriptGenerator`/`DsimConfig`** take `std::vector<Structure>
  pristine`/`std::vector<std::vector<Structure>> impurity`, building
  $N + N(N-1)$ literal `ase.Atoms(...)` structures and looping over all of
  them — verified end to end for $N=3$ (Ag-Au-Cu, EMT: 9 supercells
  relaxed and converged, 231-point ternary grid, 3 pairwise curves) as
  well as $N=2$.
- **`DsimWizard`**'s Stage 1 is a structure LIST (add from an open
  document or import a file), not a single active structure plus a typed
  element symbol — see "Workflow" above.
- **`DsimResultsWindow`** switches between the binary curve ($N=2$), the
  ternary composition-triangle map ($N=3$, `DsimTernaryPlotWidget` — see
  "Outputs" above), and the pairwise-curve fallback ($N \geq 4$)
  automatically, from which of `dsim.json`'s `analysis.binary`/
  `analysis.ternary` keys is non-null.

$N \geq 4$ has no *direct* full-dimensional visualization (the paper's own
answer for its quinary AgAuCuPdPt case is 1D composition-line slices
through the higher-dimensional simplex, e.g. Fig. 6c-e) — the pairwise
fallback is a deliberate, simpler substitute, not an attempt at one.

## Multi-phase alloys

The model above (and its "Validity" note) assumes both end members share
**one** crystal structure — right for Au-Pt, wrong for a pair like Fe-Co,
where the two elements are stable in *different* structures (bcc and hcp
respectively). Checking "Multi-phase alloy" in Stage 1 (enabled only for
$N=2$) switches to a different pipeline for exactly that case: two
independent DSIM binary branches, one per element's own crystal structure,
solved separately and then shifted onto **one common energy reference** so
they are directly comparable — the lower curve at each composition is the
stable phase, the same question a CALPHAD lattice-stability diagram
answers.

**The physics.** Each branch is an *ordinary* Eq. 7 binary solve (nothing
new there) on its own crystal-structure template: the bcc branch's four
supercells are bcc Fe (the real, stable element — Stage 1's own input
structure), "Co" built by **relabeling** Fe's bcc template to Co and
relaxing it there, Co diluted in bcc Fe, and Fe diluted in (Co-on-bcc); the
hcp branch is the symmetric four built from Co's own hcp template. Eq. 7
makes every such raw branch curve exactly zero at **both** its own
endpoints by construction — right for an ordinary single-lattice alloy, but
wrong here: the bcc branch's $x=1$ endpoint is "Co forced onto bcc", not
real (hcp) Co, so it is not the reference the *other* branch (or the real
world) uses for pure Co. `core::applyLatticeStabilityShift` corrects this:
a constant, per-atom, **linear-in-$x$** offset that moves the bcc branch's
$x=1$ value onto hcp Co's own (relaxed, stable) energy instead of bcc Co's,
and symmetrically moves the hcp branch's $x=0$ value onto bcc Fe's. Each
shift is exactly the *lattice stability* of that element in the other
structure — $E(\text{element, wrong structure}) - E(\text{element, its own
stable structure})$, eV/atom — computed directly from the two branches' own
pristine energies, no extra calculation needed:

$$
\Delta H_\text{mix}^{\text{bcc, corrected}}(x) =
\Delta H_\text{mix}^{\text{bcc, raw}}(x) + x \cdot \big[e_\text{Co}^\text{bcc} - e_\text{Co}^\text{hcp}\big],
\qquad
\Delta H_\text{mix}^{\text{hcp, corrected}}(x) =
\Delta H_\text{mix}^{\text{hcp, raw}}(x) + (1-x) \cdot \big[e_\text{Fe}^\text{hcp} - e_\text{Fe}^\text{bcc}\big].
$$

So the bcc branch's corrected curve is zero at Fe ($x=0$, its own native
element) but a **finite** value at Co ($x=1$) equal to Co's own bcc-hcp
energy difference, per atom — and the hcp branch is the mirror image, zero
at Co and finite at Fe. `core::solveDsimMultiPhase()` is the entry point
(`DsimPhaseBranchEnergies` in, `DsimMultiPhaseResult` — both branches, raw
*and* corrected — out); `tests/DsimTest.cpp` checks this against a
hand-derivable, round-number Fe-Co-shaped fixture (10-atom supercells) to
full closed-form precision, including that a linear shift moves a curve's
endpoint *values* but never its dilute-limit tangent *slopes*.

**Both branches need the same atom count.** The shift above divides an
energy *difference between the two branches* by one shared
`supercellAtomCount` — only a valid per-atom quantity when both branches'
pristine supercells really hold the same number of atoms. `DsimWizard`
enforces this: if the two input structures' own unit cells have different
atom counts (e.g. bcc's 1-atom primitive rhombohedral cell against hcp's
2-atom conventional cell), it refuses to build the multi-phase supercells
and says so, rather than silently computing a shift across mismatched $N$
— pick input cells with matching atoms-per-cell (both primitive, or both
conventional) to avoid this.

**Generation and output.** `core::generateDsimMultiPhaseScript()` relaxes
all eight supercells (the same `relax()` routine and Stage 3 settings as
the ordinary path), computes each branch's own $2\times 2$ energy/M
matrix and raw curve, applies the shift above, and writes `dsim.json` with
schema `calango.dsim/3` — a `multi_phase.phase_a`/`phase_b` pair, each
carrying its own label, `raw`, and `corrected` curve (the same
`x_grid`/`enthalpy_eV_per_atom`/`enthalpy_kJ_per_mol`/`dHdx_at_0_eV`/
`dHdx_at_1_eV` shape the ordinary binary schema's `analysis.binary` block
uses, reused rather than reinvented). `DsimResultsWindow` recognizes the
schema and plots both branches' **corrected** curves together on one
`EgqcaPlotWidget` — the same "several series, one plot" shape the $N \geq
4$ pairwise view already uses, labelled by phase rather than species pair
— with the tangent-lines toggle disabled (two branches, four slopes; the
feature actually being compared is which curve sits lower at each $x$, not
either one's own dilute-limit slope) and its own 8-supercell table
(`pristine_A_phase`/`pristine_B_phase`/`B_in_A_phase`/`A_in_B_phase`, one
group per branch).

**Validated end to end for Fe(bcc)-Co(hcp)**, MACE (`MACE-matpes-pbe-omat-ft`,
the same foundation model the Au-Pt cross-validation above uses — plain EMT
has no Fe/Co parameters): all eight 54-atom supercells (bcc's and hcp's own
2-atom conventional cells, 3×3×3, matching atom counts as the "same atom
count" requirement above needs) relaxed and converged (16-36 steps each).
MACE puts bcc Fe 0.171 eV/atom below hcp Fe and hcp Co 0.075 eV/atom below
bcc Co — the CORRECT stable structure for each element, not just a
plausible-looking number — which are exactly the two lattice-stability
shifts applied: the bcc branch's corrected curve is 0 at Fe ($x=0$) and
**0.075 eV/atom (7.25 kJ/mol) at Co** ($x=1$), the hcp branch's is
**0.171 eV/atom (16.5 kJ/mol) at Fe** ($x=0$) and 0 at Co ($x=1$) — exactly
the endpoint behaviour this feature was built for. The two curves cross
near $x \approx 0.685$ (68.5% Co): MACE predicts the bcc-based solid
solution is the lower-energy (stable) phase for Co content below that, hcp
above it — qualitatively the shape of the real Fe-Co phase diagram, where
the bcc α-phase extends across most of the Fe-rich-to-mid range and the hcp
$\varepsilon$-phase dominates near pure Co.

## Orchestration

`OrchestrationTask::Dsim` is a `Simulation`-family node (it launches a
real job — $N + N(N-1)$ relaxations in one generated script — through the
ordinary `JobRunner` machinery), configured by double-clicking it to open
the *same* `DsimWizard` the menu path uses. This is structurally simpler
than it might look, for one reason: every other multi-structure shape on
this canvas (Container's batch list, `ChargedDefects`' two named baseline
slots, …) exists to solve "how does a structure/result reach this node
from an upstream parent", and DSIM's don't need solving — a DSIM node
takes **no incoming link at all**. Its $N$ structures are configured
directly on the node's own wizard (Stage 1 builds the same open-document/
file-import list it does from the menu), the same "source of its own
structures" shape `Container` has. So adding it needed no new port/link-
arity mechanism (`orchestrationInputSlots()` returns `{}` for it, like
every ordinary single-structure Simulation task) and no special-casing in
`openNodeWizard()` the way `MaceTrainer` needs (its wizard *is* an
ordinary `SimulationWizardBase`) — it is registered exactly like
`GeometryOptimization` or `Phonon`, just with an empty input-slot list.
