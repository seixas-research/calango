# Effective band structure (unfolding)

A defect, dopant or alloy calculation lives in a supercell, and a supercell's
band structure is folded into a Brillouin zone too small to read against the
host material's dispersion. {menuselection}`Electronics --> Effective Bands
(Unfolding)…` maps the supercell's states back onto the **primitive** zone by
the Popescu–Zunger construction (Phys. Rev. B **85**, 085201 (2012)): every
supercell eigenstate gets a spectral weight

$$
P_{Km}(k) = \sum_{\mathbf{g}\,\in\,\mathbf{G}_\text{prim}} |C_{Km}(\mathbf{g})|^2,
$$

and the viewer draws the Gaussian-broadened spectral function

$$
A(k, E) = \sum_m P_{Km}(k)\,\delta(E - E_m)
$$

as an intensity heatmap — host-like bands come out bright, defect-localized
states come out as faint or flat features.

---

## Two cells, one matrix

The wizard's first stage links the two geometries. The supercell is the
active document; the **primitive reference is another open tab**, chosen from
a combo — open the pristine host cell first ({menuselection}`Build --> From
Database…` or a saved file), or the wizard refuses to open and says so.

The mapping matrix $M$ (supercell $= M \cdot$ primitive) is deduced
automatically as $M = S \cdot P^{-1}$ rounded to integers, with the largest
deviation from an integer reported as the **residual**. Untick
{guilabel}`Deduce M from the two cells` to type the nine entries by hand —
useful when the primitive cell is expressed in a different but equivalent
setting.

| Control | Default | Meaning |
|---|---|---|
| {guilabel}`Tolerance` | 0.02 | how far $M$ may sit from integers before the cells are called incommensurate |
| {guilabel}`Force commensurability by rescaling the unit cell` | off | rebuild the primitive as $P = M^{-1} S$ |
| {guilabel}`Deduce M from the two cells` | on | $M = S \cdot P^{-1}$, rounded |

:::{admonition} A relaxed supercell needs the loose tolerance — or the rebuild
:class: note

A pristine-cell tolerance (~10⁻³) rejects exactly the case unfolding is for.
Relax a 2×2×2 supercell with a defect and the lattice constant moves: a 1 %
change puts $M$'s residual near 0.02 even though the unfolding is perfectly
well posed. The default tolerance accepts it; an unrelated primitive cell
still misses by 0.1 or more, so the check keeps catching the mistake it
exists for.

Forcing commensurability is **not a fudge** for a relaxed supercell: the
supercell *is* $M \times$ something — just not $M \times$ the pristine host
any more — and $P = M^{-1}S$ recovers the host cell it actually relaxed
into. The wizard reports the strain of that rebuild (worst relative change
in vector length); a few per cent is the relaxation, much more means the
wrong primitive cell was chosen. Worked example — Au₇Pt: a 2×2×2 Au
supercell with one Pt, relaxed by +0.755 % — $M = 2I$ deduced correctly,
residual 0.015, and after forcing, the residual falls to $5\times10^{-17}$.
:::

The verdict line restates everything: $|\det M|$ (the number of primitive
cells), the atom-count cross-check (a mismatch consistent with a defect is
reported as exactly that), and the rebuild strain when forcing is on.

% TODO screenshot: Effective Bands stage 1 with the cells group, mapping matrix and verdict line
```{figure} /_static/img/elec_unfolding_wizard.png
:alt: The Structure and Geometry Link stage with the deduced mapping matrix and verdict
:width: 92%
:figclass: screenshot

Stage 1 — the supercell/primitive link and the deduced mapping matrix.
```

---

## Spectral-function settings

| Control | Default | Meaning |
|---|---|---|
| {guilabel}`Energy window` | −10 … +10 eV | window of the $A(k,E)$ map |
| {guilabel}`Energy mesh bins` | 400 | resolution of the energy axis |
| {guilabel}`Gaussian broadening σ` | 0.05 eV | broadening per eigenvalue |
| {guilabel}`Weight threshold` | 1e-4 | states below this weight are not drawn |

σ must exceed the eigenvalue spacing or the map degenerates into isolated
dots; too large and distinct branches merge. Unfolding produces a long tail
of ~10⁻⁶ weights that add nothing but cost — the threshold trims it.

The k-path (final stage) is built on the **primitive** lattice — that is the
zone the effective band structure is drawn in — with the embedded
Brillouin-zone editor and ASE's suggested path as the default. The editor is
rebuilt whenever the primitive selection changes.

---

## Backends

Only **GPAW, Quantum ESPRESSO and SIESTA** are allowed, and only GPAW is
complete: the projection needs the plane-wave expansion coefficients of the
supercell eigenstates, which GPAW's PW mode exposes directly (PW is required
regardless of the mode chosen on the calculator stage). The QE and SIESTA
scripts converge the supercell and then stop at a clearly marked `# EDIT ME`
hook with a `NotImplementedError`: those codes only expose wavefunctions
through their own file formats (`.save/wfc*.dat`, `.WFSX`), so the reader is
left to the user.

Unlike {doc}`/electronic/optics` or {doc}`/electronic/gw`, unfolding does
**not** inherit a completed SCF baseline — the generated script converges the
supercell itself, then band-diagonalizes at fixed density on the folded
k-points ($K = M^\top k$, reduced to the first zone).

The script re-checks commensurability with the same tolerance the wizard
accepted — a run the dialog accepted cannot then be rejected by its own
script — and stages the primitive cell as `primitive.extxyz` next to
`run.py`, so the job directory is self-contained and uploads to a cluster
unchanged.

:::{note}
The {guilabel}`Points per segment` value is written into the script but the
current path construction defers to ASE's own sampling
(`bandpath(..., npoints=None)`) — treat the k-path density as ASE-chosen for
now.
:::

---

## What the run checks before it writes anything

The $|\det M|$ primitive wavevectors that fold onto one $\mathbf{K}$ partition
the plane-wave basis, so **their weights sum to exactly 1 for every band** —
pristine cell or defective, metal or insulator. No physics enters that
statement, which is what makes it useful: a wrong unit, a wrong folding offset
or a wrong lattice all break it, and all three otherwise produce a *plausible*
map rather than an error.

The script therefore evaluates the identity at three points along the path
before projecting anything, prints the deviation, and refuses to write
`effective_bands.json` if it exceeds $10^{-3}$. A healthy run reports

```text
CALANGO_INFO partition check ok (max |sum-1| = 8.88e-16)
```

Three points rather than one because paths usually start at $\Gamma$, where the
folding offset vanishes and the check cannot discriminate.

:::{note}
The projection lattice is taken as $M^{-1}\!\cdot\!\mathbf{A}_\text{super}$
rather than read from the primitive file. The two differ whenever the supercell
was relaxed — the commensurability warning says by how much — and the residual
is harmless for the band *path* but fatal for the projection, whose acceptance
test asks whether a coordinate is an integer. That error grows with $|G|$, so a
few-per-mille cell mismatch discards the high-$G$ half of every state.
:::

:::{warning}
For a **spin-polarized** run both channels are projected into the same column.
$A(k, E)$ is a sum over states and the heatmap has no spin axis, so this is the
right object to plot — but a defect level present in one channel only (an NV
centre's, for instance) is then indistinguishable from one present in both. The
run says so on stdout.
:::

---

## The viewer

`effective_bands.json` stores **per-state weights, not a precomputed
image** — which is what lets the viewer re-derive the map when you change σ,
the intensity threshold or the Fermi shift, all without re-running the
calculation.

% TODO screenshot: Effective band structure heatmap with colormap and threshold controls
```{figure} /_static/img/elec_unfolding_viewer.png
:alt: The unfolded spectral function A(k,E) as a heatmap with adjustable sigma and threshold
:width: 92%
:figclass: screenshot

$A(k, E)$ over the primitive-zone path — bright host bands, faint
defect-derived features.
```

- {guilabel}`Colormap` — Viridis (default), Plasma, Coolwarm, Inferno,
  Cividis.
- {guilabel}`Threshold` (default 0.02) — intensity below this fraction of the
  maximum is not drawn; unfolding always produces a low-weight haze, and
  raising the threshold *rescales* what remains instead of just clipping it.
- {guilabel}`σ` — re-applied to the stored weights on the fly.
- {guilabel}`E − E_F` (on by default) — energies relative to the Fermi level,
  with a dashed reference line.
- {guilabel}`Export Image…` (3× print resolution) and
  {guilabel}`Export Data…` — a long-format CSV, one row per $(k, E)$ cell,
  with the Fermi convention encoded **in the column name**
  (`energy_minus_ef_eV` vs `energy_eV`) rather than in a comment line.

---

## Validated behavior

The unfolding core is pinned by `tests/BandUnfoldingTest.cpp`: matrix
deduction for diagonal and non-diagonal supercells, rejection of
incommensurate and singular cells, the $M^\top$ (not $M$) folding convention
— the classic error, visible only for non-diagonal supercells — the
relaxed-supercell force-commensurate arc (deduces $M = 2I$, reports the 1.5 %
strain, becomes exactly commensurate), and the spectral function's
normalization (each state's integrated contribution equals its weight,
whatever the bin count).
