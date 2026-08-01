# Charge analysis

Four workflows interrogate the charge and its rearrangements: Born effective
charges, partial-charge partitioning, charged-defect formation energies, and
the charge density difference. Two live in the {guilabel}`Electronics` menu,
two in {guilabel}`Analysis` — they are documented together because they answer
the same family of questions.

---

## Born effective charges

{menuselection}`Electronics --> Born Effective Charges…` computes $Z^*$, the
change in macroscopic polarization per unit atomic displacement — the quantity
behind IR intensities ({doc}`/electronic/raman_ir`) and the LO–TO splitting of
polar phonons.

On VASP and Quantum ESPRESSO the module takes the **DFPT path**:
`LEPSILON = .TRUE.` for VASP, `ph.x` with `epsil = .true.` for QE. That is
**one linear-response run instead of 6N displaced SCFs**, and it is an
*analytic* derivative — no displacement amplitude to trade off against SCF
noise, and no linearity assumption left to check. The macroscopic dielectric
tensor $\varepsilon_\infty$ comes free with the same run. All engines write
the same `born_charges.json`, so the viewer and the phonon LO–TO machinery are
engine-agnostic.

:::{note}
Both routes need an **insulator or semiconductor**. For a metal the
macroscopic polarization is not defined: VASP writes no Born block and `ph.x`
refuses `epsil`. The generated scripts say that rather than failing obscurely.
:::

---

## Partial charges

{menuselection}`Analysis --> Partial Charge Analysis…` partitions a converged
electron density among the atoms. Three schemes are offered — {guilabel}`Bader`
(topological zero-flux basins), {guilabel}`Voronoi` (nearest-atom cells) and
{guilabel}`Hirshfeld` (stockholder weights against a promolecule) — and **all
three are native**: they run on one standardized 3D grid and know nothing
about the code that produced it.

{guilabel}`Charge-density source` lists completed processes whose directories
hold a density; {guilabel}`Density format` is auto-detected, with explicit
entries for a directory holding output from more than one engine:

| Engine | Density read |
|---|---|
| GPAW | `get_all_electron_density()` from the run's `.gpw` |
| VASP | `AECCAR0` + `AECCAR2` when the run wrote them (`LAECHG = .TRUE.`), otherwise `CHGCAR` |
| Quantum ESPRESSO | `pp.x` (`plot_num = 0`) exports a cube first; set `CALANGO_PP_X` if `pp.x` is not on `PATH` |

:::{warning}
**Bader needs the all-electron density.** Partitioned on the pseudo-valence
density alone, its basins follow the wrong topology and the charges come out
systematically small. For VASP that means `AECCAR0` and `AECCAR2`; given only
a `CHGCAR` the run says so in its log rather than returning quiet nonsense.
:::

{guilabel}`Current structure only` partitions the displayed frame;
{guilabel}`All structures in the trajectory` partitions every frame — and it
is not a loop over one density. **A charge is a property of a converged
density**, so each frame needs its own; the run looks for one density file per
frame and reports what it found rather than reusing a single density as though
it described every geometry.

The results table carries a summary line: the net charge, the range, and the
mean per element — the number people actually quote. **Read the net charge
first**: for a neutral cell it must come out near zero, anything larger is
density the integration lost, and the summary flags a residual above
**0.05 e** on its own. {guilabel}`Write into Trajectory` stores the charges on
the structure as `initial_charges` — the array name ASE reads and writes —
through the undo stack; saving as extended XYZ carries them as a file column,
and the same field tints the viewport, so the colours and the file cannot
disagree.

% TODO screenshot: Partial Charge Analysis dialog with per-atom table, summary line and viewport tinted by charge
```{figure} /_static/img/elec_charges.png
:alt: Partial charge analysis with per-atom table and charge-tinted viewport
:width: 92%
:figclass: screenshot

Bader charges with the net-charge check in the summary line and the same
values tinting the structure.
```

---

## Charged defects

{menuselection}`Electronics --> Charged defects…` computes formation energies
of a defect in several charge states, reading total energies and band edges
from `vasprun.xml` or the `pw.x` output — one fixed-geometry SCF per charge
state.

The charge is set through each engine's own mechanism, and the sign
conventions differ in a way worth stating plainly:

- **VASP:** `NELECT` — an *absolute* electron count, so **q = +1 is one
  electron fewer** than neutral. The script computes the count; you never
  write the offset yourself.
- **Quantum ESPRESSO:** `tot_charge`, which follows the physical sign
  directly.

The finite-size (image-charge) correction matters whenever charged supercells
are compared:

:::{warning}
The FNV correction on these engines is **delegated to pymatgen**
(`pymatgen-analysis-defects`), reading `LOCPOT` for the potential alignment.
Where pymatgen is not installed the run reports **uncorrected** energies and
says so loudly. That is deliberate: uncorrected is a legitimate mode — it is
exactly what a supercell-convergence study needs — whereas a hand-rolled
correction that has never been checked against a reference is a plausible
number of the right magnitude and the wrong value, applied silently to every
point of the diagram.
:::

---

## Charge density difference

{menuselection}`Analysis --> Charge Density Difference (CDD)…` computes

$$
\Delta\rho = \rho_{A+B} - \rho_{A} - \rho_{B},
$$

the redistribution of charge caused by putting two fragments together. Each
density on its own is dominated by the atomic cores and shows nothing; **the
difference shows the bond**.

**Step 1 — density source.** Pick a completed single-point run; it supplies
$\rho_{A+B}$ and, more importantly, the calculator both fragments are rebuilt
from — the engine is taken from the run's provenance rather than asked again,
because $\Delta\rho$ only means something if all three densities were computed
identically. Choose {guilabel}`All-electron density` or
{guilabel}`Pseudodensity`; the pseudodensity is usually the clearer field to
read, since it carries no nuclear cusps and the isosurface scale is set by the
bonding features rather than the cores.

| Engine | Guarantee |
|---|---|
| GPAW | restarts from the `.gpw` and reads the exact calculator back out of it — nothing left to re-specify |
| VASP | reads `CHGCAR`, or `AECCAR0` + `AECCAR2` for the all-electron form (needs `LAECHG = .TRUE.` on the parent); fragments re-run with the parent's settings |
| Quantum ESPRESSO | exports each density through `pp.x` (`plot_num = 0`) as a cube; set `CALANGO_PP_X` if needed |

:::{warning}
For VASP and QE the fragment runs **pin the FFT grid explicitly to the
parent's** (`NGXF`/`NGYF`/`NGZF`, `nr1`/`nr2`/`nr3`). Both codes choose that
grid from the cutoff *and* the cell contents, so a fragment with fewer atoms
can land on a different one — and two densities sampled on different grids
cannot be subtracted at all. GPAW needs none of this because the restart
already fixes the grid.
:::

**Step 2 — subsystem partition.** Two columns list the atoms; the split is
exhaustive — every atom belongs to exactly one side — because $\Delta\rho$
only integrates to zero if A and B together are the whole system.

Both fragments are recomputed in the parent's own cell, at the parent's
geometry, with the parent's parameters. **Nothing is relaxed**: $\Delta\rho$
is defined at one geometry, and letting a fragment relax would mix charge
transfer with structural rearrangement into a quantity nobody can read off an
isosurface. Cutting a closed-shell molecule in half leaves open-shell
fragments that often refuse the parent's settings — the run tries the exact
parameters first, then adds smearing, then spin polarization, and logs which
was needed. The reported net charge is the check: A and B together must hold
exactly the electrons A+B does.

The difference is written as `cdd.cube` and loaded automatically into the
{guilabel}`Volumetric Data` dock, where a Coolwarm map is the natural choice —
accumulation and depletion sit on opposite sides of white. The status bar
reports how much charge was redistributed, from `cdd.json`.
