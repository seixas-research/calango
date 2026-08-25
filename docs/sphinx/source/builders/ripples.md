# 2D Ripples

{menuselection}`Modules --> 2D Materials --> 2D Ripples…` imposes a
**sinusoidal out-of-plane corrugation** on a monolayer supercell, and
contracts the cell in-plane so the sheet's own length is unchanged.

It is the one entry on that menu that *produces* a structure — the four below
it read one — which is why it sits first, behind a separator.

## Why

A monolayer built by any of the 2D builders comes out perfectly flat, and a
real one is not: suspended graphene and every other 2D crystal carries static
ripples, and their amplitude is what couples to the electronic structure, to
the bending rigidity and to the effective membrane area. Imposing a ripple of
a *known* amplitude and wavelength is how that dependence gets measured — one
structure per amplitude, each run through whatever calculation the answer
needs.

---

## The profile

The displacement is along the out-of-plane normal, sinusoidal in the in-plane
**fractional** coordinates:

$$h(f_1, f_2) = A\,\sin(2\pi n_1 f_1)\,\sin(2\pi n_2 f_2) \qquad \text{(both directions)}$$

$$h(f_1) = A\,\sin(2\pi n_1 f_1) \qquad \text{(one direction only)}$$

:::{important}
**Fractional, not Cartesian — and that is what makes it work on the cells
these materials actually have.** Written as $\sin(2\pi x/L_x)$ the profile is
periodic only when the cell is orthogonal and $L_x$ is exactly the cell
length. Written in fractional coordinates with an **integer** number of
periods it is periodic by construction, for *any* cell shape — which matters
immediately, because graphene, h-BN and the TMDs all have hexagonal cells
whose two in-plane vectors are at 120°. A supercell of graphene's primitive
cell is the ordinary case here, not the awkward one. For an orthogonal cell
the two forms are the same expression.
:::

{guilabel}`Periods per cell` is a whole number and the control has no other
setting: the displacement at fractional 0 and at fractional 1 belong to the
same periodic image, so a fractional period count is a **seam** — a step in
the sheet at the cell boundary, which no amount of relaxation repairs. One
period per cell makes the wavelength the supercell size, which is the usual
way of asking "what does a ripple of *this* wavelength do?".

{guilabel}`Out-of-plane axis` is seeded from the geometry (the axis whose
atoms span far less than the cell) and left editable. A thick slab in a
modest cell and a thin one in a huge cell are not reliably distinguishable
from coordinates, and getting this wrong does not produce a slightly wrong
sheet — it corrugates the structure *sideways*.

---

## The in-plane contraction

Rippling a sheet does not stretch it: the atoms are the same atoms at the
same bond lengths, and the extra path length the ripple takes has to come
from somewhere. It comes from the **footprint**. A sheet whose intrinsic
(arc) length is $L_0$ occupies a projected length $L < L_0$ once it is
corrugated, and leaving the cell at $L_0$ while displacing the atoms would be
applying a tensile strain of exactly the arc-length excess — silently, and
growing as $A^2$.

So each rippled cell vector is contracted to the $L$ that satisfies

$$\int_0^{L}\sqrt{1 + (\mathrm{d}h/\mathrm{d}x)^2}\;\mathrm{d}x = L_0$$

solved **numerically**. No small-amplitude expansion: the leading behaviour
$L_0 - L \approx \pi^2 n^2 A^2 / L$ is a good check on the solver and a bad
substitute for it.

| $A$ over a 25 Å cell | exact $L$ | expansion $L_0 - \pi^2A^2/L_0$ | spurious strain |
|---|---|---|---|
| 0.5 Å | 24.901206 Å | 24.901304 Å | +0.0004 % |
| 1.0 Å | 24.603632 Å | 24.605216 Å | +0.0064 % |
| 2.0 Å | 23.394263 Å | 23.420863 Å | **+0.1137 %** |
| 3.0 Å | 21.300230 Å | 21.446942 Å | **+0.6888 %** |

The last column is tensile strain applied to the sheet *for free* by taking
the shortcut. For a 2D material a tenth of a percent is a real shift in the
band structure, so the shortcut is an anchor for the numerics and not a
replacement for them.

**How it is solved.** The substitution $u = 2\pi n x/L$ turns the integral
into $\tfrac{L}{2\pi}\int_0^{2\pi}\sqrt{1 + k^2\cos^2 u}\,\mathrm{d}u$ with
$k = 2\pi n A/L$ — a smooth, **periodic** integrand, for which the trapezoid
rule converges faster than any power of the step size. (Simpson's rule, which
is better on a generic interval, is strictly worse on this one.) The
contraction itself is bisection: the arc length is monotonically increasing
in $L$, so the bracket cannot fail.

{guilabel}`Contract the cell to preserve arc length` is **on by default**.
Off leaves the flat footprint, which is a different physical system —
rippled *and* strained — and occasionally the one wanted; it is never the one
you get by accident with the box ticked.

:::{note}
**What is preserved exactly, and what is not.** The arc length is preserved
exactly *along the two cell vectors*. Along a general in-plane direction
($\mathbf{a}_1 + \mathbf{a}_2$, say) it is preserved only to the extent the
two per-axis contractions imply, because a per-axis scheme has two numbers to
place and a surface has a continuum of directions.

Measured on a 6×6 graphene supercell (120° cell) at $A = 0.6$ Å, one period:
the mean C–C bond goes from 1.420282 Å flat to **1.423687 Å (+0.24 %)** with
the contraction, against **1.435091 Å (+1.04 %)** without it — a factor of
4.3 less strain. The remainder is the finite-bond one: arc length is a
statement about a *curve*, and a 1.42 Å chord across a 14.8 Å ripple does not
quite follow one. Relaxing that away means relaxing the membrane, which is a
calculation and not a builder — run the result through Geometry Optimization
with the cell released in-plane.
:::

Only the direction(s) the profile varies along are contracted: nothing is
stored along an axis the sheet is straight in. The **lattice angle survives
exactly** — a contraction is a uniaxial compression of the footprint, not a
shear — and the vacuum axis is untouched, because the vacuum is not part of
the membrane.

---

## The amplitude series

{guilabel}`Amplitude series` generates a whole ramp instead of a single
structure: $N$ frames with the amplitude linear from {guilabel}`A minimum` to
{guilabel}`A maximum`, opened as one scrubbable trajectory.

This is the point of the module, really — a single rippled sheet answers
nothing; $E(A)$ does. Save it (File → Save Trajectory As…) and load it into
an Orchestration **Structure Container** node to fan a calculation out over
the series, exactly as {menuselection}`Simulation --> Random Noise Setup`'s
ensemble is consumed ({doc}`/simulations/orchestration`).

Every frame is a **full build** at its own amplitude, contraction included —
not one build interpolated, which would get the contraction (quadratic in
$A$) right only at its two ends. Each frame carries its own amplitude as the
per-atom scalar field `ripple_amplitude`: it is the one channel a structure
has that survives an extended-XYZ round trip, so the amplitude a frame was
built at is still readable after the trajectory has been saved, reloaded and
fanned out.

A frame whose amplitude the cell cannot hold is **dropped**, with a count and
a reason — not silently replaced by a flat sheet sitting in the middle of a
scan claiming to be an amplitude it is not.

---

## What it refuses

- **A structure with no unit cell.** The profile is periodic in fractional
  coordinates, and there are none.
- **An out-of-plane axis it could not determine** — you pick one rather than
  the module guessing.
- **An amplitude the cell cannot contract around.** A sinusoid of amplitude
  $A$ with $n$ periods travels $4nA$ vertically whatever its footprint, so no
  cell shorter than that can hold it. Refused, not clamped: a clamped cell
  would silently stretch the sheet, which is precisely the error the
  contraction exists to prevent. The wizard says so *before* Generate is
  pressed — the live read-out under the profile controls shows the
  contraction in Å and in percent, from the same solver the build uses.

---

## What was verified

In `ripples_2d`, against closed forms and against the defining equations —
never against a previous run of this code. The cell used throughout is a
**hexagonal** graphene supercell, because a Cartesian profile would be
periodic in a cubic box and quietly discontinuous in that one:

- the quadrature returns *exactly* the footprint for a flat sheet, agrees
  with the small-amplitude expansion to the order of the term it drops, and
  scales with the period count as the substitution says it must (three
  periods over 30 Å is exactly three times one period over 10 Å);
- the contraction satisfies **its own defining equation**: the sinusoid over
  the contracted cell is as long as the original flat cell to within
  $10^{-12}$, at every amplitude tested; $A \to 0$ recovers the flat cell
  exactly, not merely closely;
- the transform preserves atom count, species and in-plane **fractional**
  coordinates ($2\times10^{-16}$ worst drift), the displacement matches
  $h(f_1,f_2)$ at every atom's own fractional coordinates to $4\times10^{-16}$
  Å, and the profile at fractional 0 and 1 is identical along both axes —
  no seam;
- the 120° in-plane angle survives, the vacuum axis does not move, and the
  bond-length figures in the note above;
- the series returns the count it was asked for, tagged, strictly increasing,
  with the $A = 0$ frame the flat sheet exactly and a one-frame series taking
  the low endpoint with no division by zero on the ramp.
