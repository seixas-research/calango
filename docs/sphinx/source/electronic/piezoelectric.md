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
across each component's ordered strain series (anchored at the $\varepsilon =
0$ reference point, which is now re-converged with symmetry off exactly like
every strained point — see the note below) before differencing — the classic
pitfall of this method, and the reason a raw finite difference of two Berry
phases is not safe to trust.
:::

(piezoelectric-2d)=
## 2D and monolayer structures

A monolayer — periodic in-plane, with a vacuum gap along the third axis —
stresses assumptions a bulk-crystal implementation of this method tends to
bake in, since the vacuum axis is not a real lattice parameter at all. The
wizard detects one automatically (the same geometric read every other 2D-aware
wizard's vacuum-axis combo seeds from: a large fractional-coordinate gap along
one axis — this is what catches a structure built with `pbc=[True, True,
True]` and a vacuum gap, the ordinary ASE slab convention, which carries no
other sign of being 2D) and adjusts the run in three ways:

**Strain components.** {guilabel}`Strain Components` disables the boxes that
would strain the vacuum axis — for the common case of vacuum along $c$, that
is $\varepsilon_{zz}$, $\varepsilon_{yz}$ and $\varepsilon_{xz}$, leaving
$\varepsilon_{xx}$, $\varepsilon_{yy}$ and $\varepsilon_{xy}$ as the physically
meaningful set. Leaving every box unchecked defaults to exactly that in-plane
set (not all six) once a 2D structure is detected. This is enforced in the
generated script too, not only the UI: an explicit selection that still names
a vacuum-touching component is filtered out there as well, and a selection
left with nothing in-plane refuses outright rather than running a zero-point
stencil. A generated-script assertion also confirms, at run time, that no
atom actually moved along the vacuum axis under an in-plane strain — the
structural guarantee the restriction above is supposed to provide, checked
rather than merely trusted.

**Polarization.** The out-of-plane Cartesian row of the tensor (the
component along the vacuum axis) is left `NaN`, never fit: the periodic Berry
phase along a vacuum direction integrates almost entirely empty space and is
not a well-defined polarization for a slab, unlike the in-plane rows a dense
in-plane k-mesh makes physical. For 2H-MoS$_2$'s point group ($D_{3h}$/$-6m2$)
this costs nothing the symmetrization step would not have zeroed anyway — the
two rows this leaves ($e_{x,xx}$ and $e_{y,yy}$, related by $e_{11} =
-e_{12}$) are the whole of what the tensor reports for a hexagonal monolayer.
The generated script also best-effort warns (not refuses) if the baseline's
own k-mesh does not sample the vacuum axis with exactly one point, and if the
detected vacuum gap looks under 6 Å — both diagnostics, printed as
`CALANGO_WARN` lines in the job log, not run-blocking.

**Units.** $e_{ij}$ is ordinarily C/m$^2$ — the polarization derivative
divided by the full 3D cell volume, vacuum included. For a slab that makes
the reported number **vacuum-dependent**: pad the cell with more empty space
and $e_{ij}$ shrinks, even though nothing physical changed. A 2D structure
therefore also gets a **2D (sheet) coefficient in C/m**, obtained by
multiplying the ordinary C/m$^2$ value back by the vacuum axis's own cell
length — since volume = area × vacuum length for the orthogonal-vacuum-axis
convention every strain restricted to in-plane components preserves, this
recovers $dP/d\varepsilon$ per unit **area**, independent of how much vacuum
was chosen. The results viewer's {guilabel}`Show:` combo grows four more
entries for a 2D run (the same proper/improper × symmetrized/raw choices, in
C/m), and the summary line names the detected vacuum axis.

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

## A failed strain point does not lose the others

A single strain point's SCF failing to converge, or its Berry-phase
evaluation raising, is caught and recorded (with the point's own strain and
error message) rather than crashing the whole run — the loop moves on to the
rest of the stencil, and the job log names exactly which point and why. A
Voigt column left with no surviving point at all is reported as `NaN` in the
tensor and named explicitly in the results viewer, rather than silently
missing.

## Results

The viewer shows the $e_{ij}$ tensor (switchable between proper/improper and
symmetrized/raw, plus the four 2D/C-per-m variants described above when the
structure was detected as 2D), the point group spglib detected, the $d_{ij}$
tensor when a stiffness was supplied, and — per requested component — the
$P(\varepsilon)$ points and fitted line behind that column, so the finite
difference's assumptions are inspectable rather than opaque. The summary line
also names any Voigt component that has no data (every strain point for it
failed) and, for a 2D run, the detected vacuum axis.
