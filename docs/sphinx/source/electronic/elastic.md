# Elastic properties

{menuselection}`Electronics --> Elastic Properties…` computes the elastic
stiffness tensor

$$
C_{ij} \qquad (i, j = 1..6 \text{ Voigt}),
$$

by the finite-strain method: apply a small homogeneous strain to a relaxed
reference cell and read off either the **stress** or the **energy** at each
strained point, then fit. It reuses exactly the strain-generation core
({doc}`/electronic/piezoelectric` differentiates the Berry-phase
**polarization** over the same Calango-precomputed deformation gradients) —
here the same strained structures feed a stress or energy readout instead,
so the strain stencil, the 2D vacuum-axis handling, and the point-group
symmetrization step are shared code, not a parallel reimplementation.

Unlike Piezoelectric Tensor, a ground-state baseline is **optional**: the
Berry phase needs GPAW specifically, but stress and energy are available
from any ASE calculator (EMT, MACE, VASP, Quantum ESPRESSO, …). Pick
"(none)" in the baseline combo to strain the current structure with the
ordinary Calculator Settings page instead.

---

## Stress-strain vs. energy-strain

**Stress-strain** (primary) reads $\sigma_i = C_{ij}\,\varepsilon_j$ directly
off the stress tensor at each strained point: a single-component strain
sweep already fills an entire matrix **column** at once (stress is already a
6-vector), and one strain magnitude ($\pm\delta$, the central difference)
suffices.

**Energy-strain** (fallback, for a calculator with no `get_stress()`) fits
the diagonal term from the energy curvature,

$$
C_{jj} = \frac{1}{V_0}\frac{d^2E}{d\varepsilon_j^2},
$$

a quadratic fit needing at least 3 points. The **off-diagonal** $C_{jk}$
($j \neq k$) additionally needs a combined two-component strain — the
standard "independent/combined strain" technique (Ravindran et al., *J.
Appl. Phys.* **84**, 4891 (1998); Fast et al., *Phys. Rev. B* **51**, 17431
(1995)):

$$
C_{jk} = \frac{1}{V_0}\frac{1}{4\delta^2}\Big[
    E(+\delta_j,+\delta_k) - E(+\delta_j,-\delta_k)
    - E(-\delta_j,+\delta_k) + E(-\delta_j,-\delta_k)\Big],
$$

4 extra evaluations per off-diagonal pair on top of the diagonal stencil —
for the full 6-component bulk case that is 15 pairs × 4 = 60 extra points,
shown in the wizard's cost estimate before launching.

{guilabel}`Method` offers **Auto** (probe the reference point's
`get_stress()`; fall back to energy-strain for the **whole** run if it
raises, rather than mixing methods across components — MIXING methods per
component would not be physically consistent), or either method explicitly.

## Clamped-ion vs. relaxed-ion

Identical terminology to the Piezoelectric Tensor wizard. **Clamped-ion**
(the default) leaves the internal coordinates at their strain-scaled
positions and re-converges only the electronic state. **Relaxed-ion**
additionally minimizes the internal coordinates at each fixed, strained
cell — the physical, mechanical-equilibrium response, at the cost of one
geometry optimization per strain point.

(elastic-2d)=
## 2D and monolayer structures

A monolayer is detected the same way as in the Piezoelectric Tensor wizard
(the geometric vacuum-gap read that catches a structure built with
`pbc=[True, True, True]` and a vacuum gap, the ordinary ASE slab
convention) and adjusts the run in three ways:

**Strain components.** Only the in-plane Voigt set is generated (e.g. xx,
yy, xy for vacuum along c) — the cell size along the vacuum direction is
**never** changed. Ionic relaxation, when enabled, is a different story:
atoms are free to move **along** the vacuum normal (thickness change,
buckling) — a monolayer is not necessarily mirror-symmetric — only the
**cell** stays fixed.

**Tensor entries.** Both the ROW (stress component, e.g. $\sigma_{zz}$) and
the COLUMN (strain component) touching the vacuum axis are left `NaN`: a
slab's out-of-plane stress is exactly as ill-defined as the piezoelectric
module's out-of-plane polarization, for the same reason (the periodic cell
integrates however much vacuum was chosen). The masking happens **after**
point-group symmetrization, not before — the same fix the piezoelectric
module needed after a real run showed `0.0 * nan == nan` poisoning every
row once a rotation multiplied a prematurely-masked entry by an
exact-zero coefficient (see `CLAUDE.md`'s verification-habits note); this
module was written with that lesson already applied.

**Units.** $C_{ij}$ is ordinarily GPa — divided by the full 3D cell volume,
vacuum included, so padding the cell with more empty space would shrink the
reported value even though nothing physical changed. A 2D structure
therefore also reports the in-plane block in **N/m** (area-normalized):

$$
C_{ij}^{\text{2D}} = C_{ij}^{\text{3D}} \times L_{\text{vacuum}},
$$

which — because $V = A \times L_{\text{vacuum}}$ for the orthogonal-vacuum
convention every in-plane-only strain preserves — recovers $d^2E/d\varepsilon^2$
per unit **area**, independent of how much vacuum was chosen (verified
directly: re-running the graphene demonstration below at two different
vacuum heights, 16 Å and 30 Å, reproduces the same $C_{11}^{\text{2D}}$ to
within numerical noise).

**Derived 2D quantities**, from the in-plane block $C_{11}, C_{22}, C_{12},
C_{66}$:

$$
E_x = \frac{C_{11}C_{22} - C_{12}^2}{C_{22}}, \quad
E_y = \frac{C_{11}C_{22} - C_{12}^2}{C_{11}}, \quad
\nu_{xy} = \frac{C_{12}}{C_{22}}, \quad
\nu_{yx} = \frac{C_{12}}{C_{11}},
$$

satisfying the reciprocity $E_x \nu_{yx} = E_y \nu_{xy}$ exactly; for an
isotropic sheet ($C_{11} = C_{22}$, e.g. any hexagonal monolayer) these
collapse to the textbook single $E_x = E_y$ and $\nu_{xy} = \nu_{yx} =
C_{12}/C_{11}$. The **2D layer modulus** (resistance to isotropic in-plane
compression) is $(C_{11}+C_{22}+2C_{12})/4$.

**2D Born stability**: $C_{11}C_{22} - C_{12}^2 > 0$ and $C_{66} > 0$ — the
2D restriction of the general eigenvalue criterion to the in-plane Voigt
block, since a monolayer's strain never touches anything outside it.

---

## Born stability criteria

Two independent checks, both shown:

- **General** (universal, Born & Huang): every eigenvalue of $C$ positive.
  Always computed.
- **Crystal-class closed form**, from Mouhat & Coudert, "Necessary and
  sufficient elastic stability conditions in various crystal systems",
  *Phys. Rev. B* **90**, 224104 (2014) — covering cubic, hexagonal,
  tetragonal, trigonal and orthorhombic. A structure whose point group
  falls outside these five Laue classes (monoclinic, triclinic, or spglib
  unavailable) falls back to the general criterion only.

| Class | Criteria |
|---|---|
| Cubic | $C_{11}-C_{12}>0$; $C_{11}+2C_{12}>0$; $C_{44}>0$ |
| Hexagonal | $C_{11}-C_{12}>0$; $2C_{13}^2 < C_{33}(C_{11}+C_{12})$; $C_{44}>0$ |
| Tetragonal | as hexagonal, plus $C_{66}>0$ |
| Trigonal | $C_{11}-C_{12}>0$; $C_{44}>0$; $2C_{13}^2 < C_{33}(C_{11}+C_{12})$; $2C_{14}^2 < C_{44}(C_{11}-C_{12})$ |
| Orthorhombic | $C_{11}>0$; $C_{11}C_{22}>C_{12}^2$; $C_{11}C_{22}C_{33}+2C_{12}C_{13}C_{23}-C_{11}C_{23}^2-C_{22}C_{13}^2-C_{33}C_{12}^2>0$; $C_{44},C_{55},C_{66}>0$ |

Unlike the piezoelectric tensor, $C_{ij}$ is **never** forced to zero by
inversion symmetry — every rank-4 tensor is inversion-invariant — so there
is no centrosymmetric refusal here.

## Moduli

Voigt (uniform-strain, upper bound), Reuss (uniform-stress, lower bound, via
the compliance $S = C^{-1}$) and Hill (arithmetic mean — the standard
practical isotropic estimate) bulk and shear moduli, Hill, *Proc. Phys. Soc.
A* **65**, 349 (1952); the isotropic Young's modulus and Poisson's ratio are
derived from the Hill averages. Reuss/Hill are left unavailable (rather than
silently `0`) when the tensor does not invert cleanly.

## Results

The viewer shows the 6×6 $C_{ij}$ tensor (raw or symmetrized, GPa), both
Born stability criteria with pass/fail per inequality, the moduli (or, for a
2D result, the layer modulus / in-plane Young's / Poisson's ratio and 2D
Born stability), and — per requested component — the stress-or-energy vs.
strain points behind that column, so the linearity (or, for energy-strain,
the curvature) the fit assumes is inspectable rather than opaque.

---

## Verification

### 3D: copper (EMT), cross-validated between both methods

An EMT (Effective Medium Theory, ships with ASE) run on bulk FCC copper,
all six Voigt components, clamped-ion:

| Method | $C_{11}$ | $C_{12}$ | $C_{44}$ |
|---|---|---|---|
| Stress-strain ($\delta=0.5\%$) | 158.32 GPa | 105.34 GPa | 81.67 GPa |
| Energy-strain ($\delta=1.0\%$) | 158.33 GPa | 108.09 GPa | 80.46 GPa |

The two methods — genuinely independent numerics, a different strain
magnitude, and for the off-diagonal term a completely different stencil
(the combined two-component energy curvature vs. the direct stress
derivative) — agree to 0.005 GPa on $C_{11}$ and 1-3% on $C_{12}$/$C_{44}$,
exactly the level of agreement expected between two different
finite-difference schemes at different step sizes. Cubic symmetry
($C_{11}=C_{22}=C_{33}$, $C_{12}=C_{13}=C_{23}$, $C_{44}=C_{55}=C_{66}$)
emerges from the fit to within numerical noise (~1e-12 GPa) even before
spglib symmetrization runs. Both runs pass the general eigenvalue criterion
and every cubic closed-form criterion (point group `m-3m` detected); Voigt
bulk modulus 123.0 GPa, Hill Poisson ratio 0.31-0.32 — the textbook
experimental values for Cu are $C_{11}=168.4$, $C_{12}=121.4$,
$C_{44}=75.4$ GPa and $B \approx 137$-140 GPa (Simmons & Wang, *Single
Crystal Elastic Constants and Calculated Aggregate Properties*) — EMT is a
simple pair-and-density potential fit primarily to cohesive
energy/lattice constant/bulk modulus, not shear-related constants
specifically, so a 6-15% deviation from experiment while landing on the
correct symmetry, stability, and bulk-modulus order of magnitude is the
expected, reasonable outcome for this cheap engine, not a red flag for the
module's own correctness.

### 2D: graphene (MACE-MP-0 foundation potential) — honest caveat

A monolayer graphene run (MACE-MP-0, medium, stress-strain, $\delta=0.5\%$,
clamped-ion, 30 Å vacuum) gives $C_{11}^{\text{2D}} \approx 19.6$ N/m,
$C_{22}^{\text{2D}} \approx 19.6$ N/m (isotropic to <0.1%, as a hexagonal
sheet must be), $\nu_{xy} = \nu_{yx} \approx 0.337$, layer modulus
$\approx 13.1$ N/m, and correctly reports 2D-Born-**stable**. Re-running at
16 Å vacuum instead of 30 Å reproduces $C_{11}^{\text{2D}}$ to within 5%
(18.6 vs. 19.6 N/m), confirming the vacuum-independence the N/m conversion
is supposed to guarantee, and a hand-written script bypassing Calango
entirely (straining graphene and reading `atoms.get_stress()` directly with
the same MACE calculator) reproduced $C_{11}$ to 6 significant figures —
the module's numerics are exactly what they should be.

The **magnitude**, however, is roughly **15-20× below** graphene's
well-established literature values: Young's modulus $\approx 340$ N/m and
Poisson's ratio $\approx 0.16$-$0.19$ (e.g. Kudin, Scuseria & Yakobson,
*Phys. Rev. B* **64**, 235406 (2001); Lee et al., *Science* **321**, 385
(2008), the direct experimental measurement) — the values this doc's task
description asked to be verified rather than trusted from memory. This is
not a Calango defect: it is a **documented limitation of the unfine-tuned
MACE-MP-0 foundation potential specifically for 2D/van der Waals systems** —
independently confirmed here by also trying the MACE-OMat-0 checkpoint
(22.8 N/m, same order of magnitude, same large gap from literature) and
corroborated by the wider MLIP literature: "the original MACE-MP-0b3
underestimates elastic constants and bulk modulus" with "large errors
(20-80%) for most properties, likely due to the low emphasis on stress
during training," and specifically "for 2D materials like graphene, the
foundation model conspicuously lacks accurate equilibrium structures"
(fine-tuning studies on universal MLIPs, arXiv:2506.21935, arXiv:2506.07401).
**Trust the pipeline's symmetry, stability verdict and vacuum-independence
here; do not trust the absolute magnitude from an unfine-tuned foundation
potential on a 2D system** — a DFT baseline (GPAW) or a graphene-fine-tuned
MLIP would be needed for a publication-grade number.
