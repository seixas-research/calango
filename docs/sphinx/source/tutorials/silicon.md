# Silicon from Start to Finish

This tutorial walks the whole arc of a materials study on one small, well-understood system: crystalline silicon. We build the crystal, relax it, converge a ground state, compute the phonon dispersion, and finish with the electronic band structure and projected density of states.

Silicon is the right first system precisely because the answers are known. **Every stage below ends with a number we can check**, so when something goes wrong we find out at that stage instead of three stages later.

:::{warning}
GPAW is used throughout. Install it into a Conda environment and point Calango at it once, in {menuselection}`Edit --> Preferences…`; every wizard then binds to that environment automatically ({doc}`/python_environment`). Without GPAW we can still follow the tutorial with the {guilabel}`EMT` calculator — the workflow is identical — except for the absence of the electronic-structure stages.
:::

---

## Step 1 — Build the Crystal

We open {menuselection}`Build --> From Database…` and stay on the {guilabel}`Bulk` tab. It is already set up for this system:

| Field | Value |
|---|---|
| {guilabel}`Build from` | Prototype (`ase.build.bulk`) |
| {guilabel}`Formula` | Si |
| {guilabel}`Crystal structure` | diamond |
| {guilabel}`Lattice constant a` | leave at 0 to accept ASE's tabulated value, 5.43 Å |
| {guilabel}`Cell shape` | leave unchecked for the two-atom primitive cell |

Click {guilabel}`Build Crystal` and the crystal opens in a new workspace tab. The **Structure** dock on the left should now read Si2, 2 atoms, with $a = b = c = 3.84$ Å and $\alpha = \beta = \gamma = 60°$ — that is the rhombohedral primitive cell of the diamond lattice, not an error. The conventional cubic cell with its 8 atoms and 90° angles is what ticking {guilabel}`Conventional cubic cell` produces.

% TODO screenshot: the two-atom Si primitive cell in the viewport with the Structure dock showing Fd-3m (227) and the 3.84 Å / 60° cell
```{figure} /_static/img/tutorial_silicon_build.png
:alt: The silicon primitive cell in the viewport, with detected symmetry Fd-3m (227) in the Structure dock
:width: 92%
:figclass: screenshot

The primitive diamond cell. With spglib available, the Structure dock confirms space group Fd-3m (227).
```

:::{tip}
We work in the primitive cell. Every stage after this one scales with the number of atoms — the phonon step worst of all, since it builds a supercell of whatever we hand it. Eight atoms instead of two would make that step roughly an order of magnitude slower for identical physics.
:::

---

## Step 2 — Relax the Geometry

Silicon's tabulated lattice constant is not PBE's lattice constant, so the first job is to let the calculator find its own minimum.

We open {menuselection}`Simulation --> Geometry Optimization…`. The wizard runs in four stages; {guilabel}`Next` carries us through them.

1. **Calculator & Execution Environment.** Choose {guilabel}`GPAW`. The Conda environment is filled in from Preferences.
2. **Calculator Settings.** For this system: {guilabel}`Mode` PW, {guilabel}`Plane-wave cutoff` 400 eV, {guilabel}`XC functional` PBE, {guilabel}`k-points` 8 × 8 × 8.
3. **Optimization Settings.** {guilabel}`Force convergence (fmax)` 0.01 eV/Å. Silicon's primitive cell has no free internal coordinates by symmetry, so this converges almost immediately.
4. **ASE Script Review.** We read the script — it is the whole job, and it is editable. Press {guilabel}`Run`.

The **Processes** dock tracks the run and the **Results** dock plots energy against step as it goes. When it finishes, the relaxed structure loads back into the tab, and double-clicking the completed process (or its {guilabel}`Open Viewer` button) shows the energy and force convergence history.

:::{note}
Relaxing *positions* will not change silicon's lattice constant — BFGS moves atoms, not the cell. Getting PBE's lattice constant (about 5.47 Å, some 0.7 % above experiment, which is PBE behaving normally) means an energy–volume scan: build several cells with the Structure panel's supercell tool or the structure editor, run a single point on each, and fit the minimum. This tutorial keeps the tabulated constant so the later stages stay comparable to published numbers.
:::

---

## Step 3 — Converge the Ground State

{menuselection}`Simulation --> Single-point Calculation…` ({kbd}`Ctrl+R`) now does double duty. It reports the total energy, and — because the calculator is GPAW — it writes `single_point.gpw`, the converged density and wavefunctions.

We use the same calculator settings as Step 2, but raise the k-grid to **12 × 12 × 12**. Run it.

:::{important}
This file is the *baseline* that the later analysis workflows inherit. Electronic Structure, Optics, Wannier Functions, GW and the charge density difference all load a completed ground state and evaluate at fixed density rather than re-converging their own. That is deliberate: a spectrum computed from a silently different SCF solution than the one we inspected is not attributable to anything. It also means those wizards refuse to open until this step exists — if one tells you it needs a baseline, this is the step you skipped.
:::

The single-point viewer — opened from the completed process in the **Processes** dock — shows the total energy, the Fermi level and the maximum residual force.

---

## Step 4 — Phonon Dispersion

We open {menuselection}`Simulation --> Phonon…`. This wizard has four stages.

1. **Calculator & Execution Environment.** GPAW again.
2. **Calculator Settings.** As before.
3. **Phonon Settings.**
   - {guilabel}`Displacement δ` **0.01 Å** — small enough to stay harmonic, large enough to lift the forces clear of numerical noise.
   - {guilabel}`Supercell` **3 × 3 × 3**. The supercell sets how far force constants are sampled, and therefore how much of the dispersion is real rather than interpolated. 2 × 2 × 2 runs faster and already gives the right shape.
   - {guilabel}`Symmetry-reduced displacements` **on**. The naive scheme costs $6N$ force evaluations; silicon's space group relates almost all of them, and spglib (through phonopy) reduces the set by roughly an order of magnitude for identical force constants.
   - {guilabel}`Remove residual forces` **on**. A relaxation stops at a finite fmax, so the reference geometry still carries small forces; left in, they add a spurious linear term that shows up as non-zero acoustic frequencies at $\Gamma$.
   - {guilabel}`Enforce acoustic sum rule` **on**.
4. **q-Path Definition.** The embedded Brillouin-zone builder suggests silicon's conventional path, $\Gamma \to X \to W \to K \to \Gamma \to L$. We accept it.

This is the longest step of the tutorial — it is many force evaluations on a supercell. When it completes, the **Phonon Viewer** opens on its own.

### Checking the Answer

Two atoms in the primitive cell give $3 \times 2 = 6$ branches: three acoustic and three optical. We check these before reading anything else into the plot:

- **The acoustic branches vanish at $\Gamma$.** All three should start at zero. Residual frequencies of a few cm⁻¹ are normal; tens of cm⁻¹ mean the acoustic sum rule or the residual-force subtraction is not doing its job, or the relaxation was too loose.
- **The optical mode at $\Gamma$ sits near 15.5 THz (520 cm⁻¹).** This is silicon's Raman line, one of the best-measured numbers in solid state physics. Within a few percent means the calculation is sound.
- **No imaginary frequencies.** Plotted as negative, they would mean the structure is not at a minimum — for silicon that indicates a problem with the setup, not a real instability.

The {guilabel}`Phonon Thermodynamics…` button in the Phonon Viewer integrates the density of states into internal energy, free energy, entropy and heat capacity over 0–1000 K. Two checks come free: $C_V$ must approach the **Dulong–Petit limit** $3Nk_B$ at high temperature, and the entropy must go to zero at 0 K.

---

## Step 5 — Electronic Structure

We open {menuselection}`Electronics --> Electronic Structure…`. Stage 1 asks for a {guilabel}`Baseline SCF density` — the `single_point.gpw` from Step 3. Select it.

The run is non-self-consistent by construction: it loads that density and evaluates the bands along the k-path at fixed density. We set the k-path the same way as for the phonons ($\Gamma \to X \to W \to K \to \Gamma \to L$) and enable {guilabel}`PDOS` to get the element- and orbital-projected density of states beside the bands.

When it finishes, the band/PDOS viewer opens: bands as $E - E_F$ against k-distance on the left, the PDOS sharing the energy axis on the right.

% TODO screenshot: the band/PDOS viewer for silicon, indirect gap visible with the CBM along Γ→X, PDOS panel on the right
```{figure} /_static/img/tutorial_silicon_bands.png
:alt: Silicon band structure with projected density of states, showing the indirect gap
:width: 92%
:figclass: screenshot

Silicon's bands and PDOS from the inherited baseline. The conduction-band minimum sits along $\Gamma \to X$, not at $\Gamma$.
```

### Checking the Answer

- **The gap is indirect.** The valence-band maximum sits at $\Gamma$; the conduction-band minimum does not — it lies along $\Gamma \to X$, about 85 % of the way to $X$. A band structure showing silicon's minimum at $\Gamma$ is wrong.
- **The gap is too small, and that is expected.** PBE gives roughly **0.6 eV against an experimental 1.17 eV**. This is the standard band-gap underestimation of semilocal DFT, not a convergence failure. Raising the cutoff or the k-grid will not fix it, because it is not an error in the calculation.
- **The PDOS is $sp^3$.** The valence band is a mixture of Si $s$ and $p$ character, as tetrahedral bonding requires.

:::{tip}
To correct the gap rather than accept it, we run {menuselection}`Electronics --> GW Calculations…` on the same baseline. One-shot $G_0W_0$ replaces the DFT exchange–correlation potential with the self-energy and opens semiconductor gaps by typically 0.5–2 eV. The GW viewer reports the DFT gap, the quasiparticle gap and the renormalization between them.
:::

---

## Where to Go Next

The same baseline supports the rest of the analysis suite without recomputing the ground state:

- {menuselection}`Electronics --> Optics…` — the dielectric function $\varepsilon(\omega)$ and the absorption, reflectivity, refractive-index and loss spectra derived from it.
- The {guilabel}`ELF` checkbox under {guilabel}`Density Exports` in the single-point setup — the covalent bond charge between neighbours, written as `elf.cube` by the same SCF and rendered as an isosurface in the **Volumetric Data** dock. There is no separate ELF module: the field comes out of the ground state we already have.
- {menuselection}`Analysis --> Charge Density Difference (CDD)…` — $\Delta\rho = \rho_{A+B} - \rho_A - \rho_B$ between two fragments of the same run, showing where the charge went when they were put together.
- {menuselection}`Wannier Functions --> Wannierization…` — the four $sp^3$ bond orbitals of the diamond lattice.
- {menuselection}`Analysis --> Symmetry, Raman & IR Activity…` — the factor-group analysis that labels silicon's $\Gamma$ optical mode $T_{2g}$ and Raman-active.

We save the workspace with {menuselection}`File --> Project Workspace --> Save Project` ({kbd}`Ctrl+S`) before stopping. Job directories of a saved project are kept alongside the `.calproj` ({doc}`/workspace`), so every result above stays linked in the **Processes** dock and can be reopened without recomputation.
