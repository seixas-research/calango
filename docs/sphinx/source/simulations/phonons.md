# Phonons and normal modes

{menuselection}`Simulation --> Phonon` sets up a finite-displacement vibrational calculation. Calango detects whether the current structure is periodic and adapts: a **crystal** gets supercell finite-displacement phonons (dispersion, DOS, mode eigenvectors), an **isolated molecule** gets Γ-point normal modes via `ase.vibrations` with per-mode animation trajectories. The wizard is a four-stage flow — Calculator Settings → Phonon Settings → q-Path Definition → ASE Script Review — because a q-path only makes sense once the engine is chosen (see {doc}`/simulations/wizards`). For a molecule, stages 2–3 collapse to the handful of controls that still apply.

---

## Finite displacements

Stage 2 decides how the force constants are sampled:

| Setting | Default | Notes |
|---|---|---|
| {guilabel}`Displacement δ` | 0.01 Å | ± per atom, per Cartesian direction |
| {guilabel}`Supercell (nx·ny·nz)` | 2×2×2 | periodic structures only |
| {guilabel}`Symmetry-reduced displacements` | off | spglib via phonopy, see below |
| {guilabel}`Remove residual forces` | off | one extra force evaluation |
| {guilabel}`Enforce acoustic sum rule` | on | the 3 acoustic branches vanish at Γ |
| {guilabel}`q-mesh (DOS)` | 30×30×30 | one spin box per axis, up to 200 |
| {guilabel}`Gaussian smearing σ` | 2.0 cm⁻¹ | PhDOS broadening |

The naive scheme displaces every atom of the supercell by ±δ along x, y and z — **6N force evaluations** for N supercell atoms, which is the dominant cost. Two independent refinements:

- **Symmetry-reduced displacements** route the run through phonopy, which asks spglib for the space group and the site-symmetry irreps, generates only the symmetry-irreducible displacement set, and rebuilds the full force-constant matrix by symmetry — for a high-symmetry cell often an order of magnitude fewer force evaluations. The script decides availability *at run time*: if phonopy is not importable in the job environment it prints a warning and **falls back to the full 6N ASE set**, so the generated script always runs. The log reports the detected space group and the irreducible-vs-naive displacement counts.
- **Residual-force removal** computes the forces on the un-displaced reference geometry and subtracts them from every displaced evaluation. A relaxation stops at a finite `fmax`, so the reference still carries small forces; left in, they add a spurious linear term to the force constants that surfaces as non-zero "acoustic" frequencies at Γ. Costs one extra force evaluation.

:::{note}
Finite-displacement phonons move atoms off their symmetric sites by construction, and GPAW validates every set of positions against the symmetry it detected from the undisplaced geometry — so the generated script always forces GPAW's k-point symmetry reduction off. This is not a choice: leaving it on kills the run on the first displacement with *Broken symmetry!*.
:::

The DOS mesh is **per axis** deliberately: anisotropic cells want anisotropic meshes — a 2D material samples $q_z$ once, not 30 times. Interpolating force constants onto a mesh is cheap next to the force evaluations already done; raise the mesh before lowering σ, because a sharp σ on a coarse mesh produces spikes, not resolution.

---

## LO-TO splitting

In a polar crystal the long-wavelength LO mode is stiffened by the macroscopic electric field its own displacement pattern creates. A finite-displacement supercell is charge-neutral and cannot host that field, so LO and TO come out degenerate at Γ — for MgO that is a ~300 cm⁻¹ error, not a subtlety. The {guilabel}`LO-TO Splitting` group adds the correction analytically from two ingredients:

- {guilabel}`Born charges` — a completed **Born Effective Charges** run on *this* structure (from {menuselection}`Electronics --> Born Effective Charges`), or a `born_charges.json` adopted via {guilabel}`Load…` from another session or machine. The file is validated on load, the atom count is shown in the label, and the script aborts if the tensors do not cover every atom. The Z* tensors are re-symmetrized to satisfy the acoustic sum rule before use.
- {guilabel}`ε∞` — the *electronic* (clamped-ion, high-frequency) dielectric constant, per diagonal axis: taken from a completed Optics run (the ω → 0 limit of ε₁) or typed — a literature value is perfectly legitimate and is what most published dispersions use. **ε∞ = 1 is vacuum** and the dialog says so: nothing would screen the field and the splitting would come out far too large.

The correction is a limit, not a value: at exactly q = 0 the LO frequency depends on the direction of approach, so the script corrects each Γ point on the path using the direction the path arrives from, and reports the TO/LO pairs and the splitting itself. **LO-TO requires the phonopy driver** — `ase.phonons` has no hook for it — so requesting the correction selects that path regardless of the symmetry-reduction checkbox, and a missing phonopy is a hard stop rather than a silent fallback that would look exactly like a non-polar result.

---

## The q-path and outputs

Stage 3 is the same interactive Brillouin-zone path builder the Electronic Structure wizard uses — a phonon dispersion ω(q) is read along exactly the same high-symmetry lines as an electronic band structure. The sampling is the builder's points-per-segment times the segment count (default 100 points overall when no path is set).

A finished run produces, in the job directory:

- **Γ frequencies** in the log, one `CALANGO_RESULT` line per mode in cm⁻¹ and meV, each labeled with its **irreducible representation** (Mulliken symbol) where the symmetry analysis succeeds — degenerate clusters are projected onto the irreps of the point group, and an accidental degeneracy shows as a joined label (`A1g+Eg`) rather than a wrong single one. The labels are best-effort by design: any failure leaves modes unlabeled, never fails the run.
- `phonon_band.json` — the dispersion in cm⁻¹ along the path, with the linear x-axis and special-point labels matching every other Calango band plot (imaginary modes are negative).
- `phonon_dos.json` — the PhDOS on the requested mesh.
- `phonon_modes.json` — complex eigenvectors at the path's named high-symmetry points, for the Vibrational Mode Analysis animation (irrep labels at Γ only — away from the zone center the factor group is not the little group of q). The file declares its `eigenvector_convention`: `mass-weighted` for the phonopy driver (whose eigenvectors are those of the dynamical matrix, $w = \sqrt{M}\,u$) and `displacement` for the ASE driver, which divides the mass weighting out before returning. Reading one as the other moves heavy atoms by $\sqrt{M_\text{heavy}/M_\text{light}}$ too much, which looks entirely plausible on screen.

The Phonon Viewer opens automatically when the job finishes — see {doc}`/simulations/jobs` for the general completion behavior.

**Vibrational Mode Analysis** is a separate module (**Analysis → Vibrational Mode Analysis…**), not a panel of the viewer: it picks its own completed phonon run from a combo (or **Browse…** for a job from an earlier session), reads that run's own `structure.extxyz` for the geometry the eigenvectors are indexed by, and animates the selected branch on the main 3D viewport. Acoustic branches are labeled from the acoustic sum rule ($\sum_i M_i u_i = 0$) rather than from their position in the list, and **Create Mode Trajectory Tab** opens one full period as a scrubbable trajectory whose frames carry the harmonic restoring forces $F = -M\omega^2 u$ and the exact atomic velocities. The Phonon Viewer's "Vibrational Analysis…" button opens that same module on the run it is showing, so the viewer no longer has to stay open.

% TODO screenshot: Phonon Settings stage showing displacement, supercell, symmetry-reduction toggles and the LO-TO splitting group
```{figure} /_static/img/sim_phonons_settings.png
:alt: The phonon displacement settings and LO-TO splitting controls
:width: 92%
:figclass: screenshot

Stage 2 of the Phonon wizard: how the force constants are sampled, and the LO-TO correction assembled from earlier Born-charge and Optics runs.
```

---

## Molecules: Γ-point normal modes

For a non-periodic structure the wizard generates an `ase.vibrations.Vibrations` run instead: same δ, 6N displacements, optional residual-force removal (which keeps the six zero modes near zero instead of drifting imaginary). Outputs are `vibrations.txt`, per-mode `CALANGO_RESULT` frequencies (imaginary modes flagged), and **mode animation trajectories** `vib.<n>.traj` that Calango opens directly — near-zero (translation/rotation) and imaginary modes are skipped, since their animation amplitude diverges as $1/\omega$.

---

## Phonon thermodynamics

From the Phonon Viewer, {guilabel}`Phonon Thermodynamics…` evaluates the harmonic vibrational thermodynamics as integrals over the PhDOS $g(\omega)$:

$$
U(T) = \int \hbar\omega \left[\tfrac{1}{2} + \frac{1}{e^{\hbar\omega/k_BT} - 1}\right] g(\omega)\,d\omega,
\qquad
F(T) = k_BT \int \ln\!\left(2\sinh\frac{\hbar\omega}{2k_BT}\right) g(\omega)\,d\omega,
$$

with $S = (U - F)/T$ and the heat capacity $C_v$ from the standard Einstein-factor integrand. The ½ term is the zero-point energy: $U(0) = F(0) = E_\mathrm{ZPE}$ and $S(0) = 0$ — the Third Law, and a built-in check. At high temperature $C_v$ approaches the classical Dulong–Petit limit $3Nk_B$. The temperature grid defaults to 0–1000 K in 101 points; T = 0 is handled analytically.

The dialog shows two linked plots — energies (U, F in eV) and entropy/heat capacity (meV/K) — on separate panels because they differ by about four orders of magnitude; a hover drives the crosshair on both, and each column exports its own CSV (the entropy CSV carries `S_vib_eV_per_K`, `Cv_eV_per_K` and the zero-point energy) and image.

:::{warning}
Imaginary modes (stored as negative frequencies) are **excluded** from the integrals rather than silently contributing — the harmonic expressions are undefined for them, and a structure with imaginary modes is not at a minimum, so its harmonic thermodynamics are not meaningful anyway. The dialog reports how much DOS weight was discarded.
:::
