# Piezoelectric tensor

{menuselection}`Electronics --> Piezoelectric Tensor…` computes

$$
e_{i\alpha} = \frac{\partial P_i}{\partial \varepsilon_\alpha}
\qquad (i = 1..3,\ \alpha = 1..6 \text{ Voigt}),
$$

the piezoelectric tensor, by the finite-difference strain-polarization
method: apply a small homogeneous strain to a relaxed reference cell,
evaluate the total polarization $P$ by the modern (Berry-phase) theory at
each strained point, and differentiate. It reuses exactly the GPAW
Berry-phase evaluation {doc}`Born Effective Charges </electronic/charges>`
differentiates over **atomic displacements** — here the same evaluation is
differentiated over **cell strain** instead, and only GPAW is offered for the
same reason.

Like Born Effective Charges, the run starts from a completed
{menuselection}`Simulation --> Single-point Calculation…` `.gpw`, which
supplies both the reference geometry and the calculator every strained run is
rebuilt from — there is no Calculator Settings stage of its own.

---

## The strain stencil

{guilabel}`Strain Components` picks which of the six Voigt components to
differentiate (all six by default). {guilabel}`Strain magnitude δ` is the
smallest sample point (0.5% by default) — squeezed between the same two
errors as the Born Charges displacement: large enough that the polarization
change clears SCF/Berry-phase noise, small enough that
$F = \mathbb{1} + \varepsilon$ stays in the linear-response regime the method
assumes. {guilabel}`Points per component` offers the exact central difference
(2, at $\pm\delta$) or a least-squares fit over four points
($\pm\delta, \pm2\delta$) for a better-conditioned slope — the results viewer
plots the points behind every column, so the linearity the method assumes can
be checked directly rather than trusted.

:::{note}
**The polarization quantum.** $P$ is defined only modulo $eR/\Omega$, and the
Berry phase computed at two different strain points routinely lands on
different branches. The generated script resolves this with `np.unwrap`
across each component's ordered strain series (anchored at the reused
$\varepsilon = 0$ baseline point) before differencing — the classic pitfall
of this method, and the reason a raw finite difference of two Berry phases is
not safe to trust.
:::

## Clamped-ion vs. relaxed-ion

**Clamped-ion** (the default) leaves the internal coordinates at their
strain-scaled positions (`scale_atoms=True`) and re-converges only the SCF.
**Relaxed-ion** additionally minimizes the internal coordinates at each fixed,
strained cell before reading off the polarization — the physical, zero-stress
response, at the cost of one geometry optimization per strain point.

## Proper vs. improper, and symmetry

The raw finite difference is the **improper** (clamped-cell-shape) tensor: it
carries a pure volume-definition artefact the physical **proper** response
does not have. The generated script applies the correction from Vanderbilt,
*Berry-phase theory of proper piezoelectric response*, J. Phys. Chem. Solids
**61**, 147 (2000) (arXiv:cond-mat/9903137, Eq. 15), symmetrized in the
strain pair:

$$
e^{\text{proper}}_{i,(jk)} = e^{\text{improper}}_{i,(jk)}
    + \delta_{jk} P^0_i - \tfrac{1}{2}\left(\delta_{ij} P^0_k + \delta_{ik} P^0_j\right).
$$

The correction vanishes exactly when the polarization index matches the
strained axis (e.g. $e_{zz,z}$) and is otherwise nonzero for both normal
*and* shear columns — it is not, as the formula's un-symmetrized form might
suggest, a normal-strain-only effect.

{guilabel}`Use point-group symmetry` runs spglib on the reference structure
before any strained calculation. If the point group contains the inversion,
**the run refuses outright**: piezoelectricity is forbidden by symmetry for
every centrosymmetric point group, exactly rather than approximately, so no
strained calculation would change the answer. Otherwise the assembled tensor
is symmetrized by averaging the general rank-3 Cartesian tensor transform
$e'_{ijk} = R_{ii'}R_{jj'}R_{kk'}e_{i'j'k'}$ over every point-group
operation — this both zeroes whatever component symmetry forbids and cleans
up the numerical noise in the components it allows.

:::{note}
Symmetry here reduces *which answers are trusted* (the centrosymmetric
refusal, the forbidden-component cleanup); it does not yet skip computing
individual strain directions that symmetry makes redundant with another one
already run. That remains a documented follow-up.
:::

## Converting to d_ij

Supplying an elastic stiffness tensor $C$ (Voigt, GPa) in the wizard turns on
the piezoelectric **strain** tensor $d_{i\alpha} = \sum_\beta e_{i\beta}
S_{\beta\alpha}$, with $S = C^{-1}$ — reported in the conventional pm/V
alongside the stress tensor $e_{ij}$ the wizard always computes. Nothing here
computes $C$ from first principles; it is the user's to supply, e.g. from a
separate elastic-constants calculation.

## Results

The viewer shows the $e_{ij}$ tensor (switchable between proper/improper and
symmetrized/raw), the point group spglib detected, the $d_{ij}$ tensor when a
stiffness was supplied, and — per requested component — the $P(\varepsilon)$
points and fitted line behind that column, so the finite difference's
assumptions are inspectable rather than opaque.
