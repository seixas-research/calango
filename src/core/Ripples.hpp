#pragma once

#include "core/Structure.hpp"

#include <memory>
#include <vector>

namespace calango::core {

/// Sinusoidal out-of-plane rippling of a 2D material's supercell.
///
/// WHAT THIS IS FOR. A monolayer built by any of the 2D builders comes out
/// perfectly flat, and a real one is not: suspended graphene and every other
/// 2D crystal carries static ripples, and their amplitude is what couples to
/// the electronic structure, to the bending rigidity and to the effective
/// membrane area. Imposing a ripple of a KNOWN amplitude and wavelength is
/// how that dependence gets measured — one structure per amplitude, each run
/// through whatever calculation the answer needs.
///
/// THE PROFILE. Displacement along the out-of-plane normal, sinusoidal in
/// the in-plane FRACTIONAL coordinates:
///
///     h(f₁, f₂) = A · sin(2π n₁ f₁) · sin(2π n₂ f₂)      ('xy')
///     h(f₁)     = A · sin(2π n₁ f₁)                       ('x only')
///     h(f₂)     = A · sin(2π n₂ f₂)                       ('y only')
///
/// Fractional, not Cartesian, and that is the whole reason there is no seam
/// at the cell boundary. Written as sin(2πx/L) the profile is periodic only
/// when the cell is orthogonal and L is exactly the cell length; written in
/// fractional coordinates with an INTEGER number of periods it is periodic
/// by construction, for any cell shape — which matters immediately, because
/// the 2D materials this is for (graphene, h-BN, the TMDs) all have
/// hexagonal cells whose two in-plane vectors are at 120°. For an orthogonal
/// cell the two forms are the same expression.
///
/// THE CONTRACTION, and why it is not optional physics. Rippling a sheet
/// does not stretch it: the atoms are the same atoms at the same bond
/// lengths, and the extra path length the ripple takes has to come from
/// somewhere. It comes from the FOOTPRINT. A sheet whose intrinsic
/// (arc) length is L₀ occupies a projected length L < L₀ once it is
/// corrugated, and leaving the cell at L₀ while displacing the atoms would
/// be applying a tensile strain of exactly the arc-length excess — silently,
/// and growing as A². So each rippled cell vector is contracted to the L
/// that satisfies
///
///     ∫₀^L √(1 + (dh/dx)²) dx = L₀
///
/// solved numerically (see rippleContractedLength). No small-amplitude
/// expansion: the leading behaviour L₀ − L ≈ π²n²A²/L is a good check on the
/// solver and a bad substitute for it — by A = 2 Å over a 25 Å cell it is
/// out by more than a tenth of a percent of the cell length, which is a
/// tenth of a percent of tensile strain applied to the sheet for free.
///
/// NON-ORTHOGONAL CELLS, which is nearly all of them. Graphene, h-BN and the
/// TMDs all have in-plane vectors at 120°, and everything above is stated so
/// that this needs no special case: the profile is a function of the
/// fractional coordinates, and the arc length along a lattice direction
/// depends only on that vector's LENGTH and on the height profile along it —
/// not on the angle between the two. Each rippled cell vector is contracted
/// along its own direction, so the lattice angle is preserved exactly and
/// the contraction is a uniaxial compression of the footprint rather than a
/// shear.
///
/// What that does and does not guarantee, stated plainly: the arc length is
/// preserved exactly ALONG THE TWO CELL VECTORS. Along a general in-plane
/// direction (a₁ + a₂, say) it is preserved only to the extent the two
/// per-axis contractions imply, because a per-axis scheme has two numbers to
/// place and a surface has a continuum of directions. There is no scheme
/// with two parameters that does better; the alternative is a full membrane
/// relaxation, which is a calculation and not a builder.
struct RippleOptions {
    /// Which in-plane direction(s) the profile varies along.
    enum class Direction {
        XY, ///< the product of both sines — a two-dimensional egg-box ripple
        X,  ///< corrugated along the first in-plane vector only
        Y,  ///< corrugated along the second in-plane vector only
    };

    Direction direction = Direction::XY;
    /// Peak displacement, in Å. This is the amplitude of the profile above,
    /// so the sheet spans 2A between its lowest and highest points.
    double amplitude = 0.5;
    /// Periods per cell, along the first and second in-plane vectors.
    /// INTEGERS, because a non-integer count is a seam: the displacement at
    /// fractional 0 and at fractional 1 are the same atom's, and they must
    /// agree. Values below 1 are clamped to 1.
    int periodsFirst = 1;
    int periodsSecond = 1;
    /// The out-of-plane cell axis (0/1/2). The two others are the in-plane
    /// pair, in index order. -1 asks applyRipples to take the axis its
    /// caller could not determine, and is refused rather than guessed.
    int normalAxis = 2;
    /// Contract the rippled in-plane vector(s) so the arc length is
    /// preserved (see the class comment). ON by default: leaving it off
    /// produces a sheet that is rippled AND stretched, which is a different
    /// physical system and only occasionally the one wanted.
    bool contractInPlane = true;
};

/// Arc length of h(x) = A·sin(2π n x / L) over one cell, x ∈ [0, L].
///
/// Computed by the TRAPEZOID rule, which is not the usual default and is the
/// right one here: after the substitution u = 2πnx/L the integrand
/// √(1 + k² cos² u) is smooth and PERIODIC over [0, 2π], and for a smooth
/// periodic integrand the trapezoid rule converges faster than any power of
/// the step size — Simpson's rule, which is better on a generic interval, is
/// strictly worse on this one. A few hundred points reach double precision.
///
/// Degenerates correctly: A = 0 gives exactly L, and n ≤ 0 is treated as
/// n = 1 the way RippleOptions clamps it.
double rippleArcLength(double amplitude, double length, int periods);

/// The contracted cell length L for which rippleArcLength(A, L, n) equals
/// `flatLength` — i.e. the footprint a sheet of intrinsic length
/// `flatLength` occupies once it is corrugated.
///
/// Returns 0 when the request is impossible. A sinusoid of amplitude A with
/// n periods travels 4nA vertically whatever its footprint, so no cell
/// shorter than that can hold it: `flatLength` ≤ 4nA has no solution, and
/// reporting one would be inventing a structure. Callers refuse the build.
///
/// Bisection, not Newton: rippleArcLength is monotonically increasing in L
/// (from 4nA at L → 0 to L itself as L → ∞), so bisection cannot fail to
/// bracket, and the derivative Newton would need is another quadrature.
double rippleContractedLength(double amplitude, double flatLength,
                              int periods);

/// The rippled structure. Atom count, species and in-plane FRACTIONAL
/// coordinates are all preserved; the cell's in-plane vectors are contracted
/// (unless switched off) and every atom is displaced along the out-of-plane
/// normal by h.
///
/// The normal is the geometric one — normalize(a_i × a_j) of the two
/// in-plane vectors, signed to agree with the out-of-plane cell vector — not
/// the out-of-plane vector itself. For every 2D cell this application builds
/// the two coincide; where they do not, only the geometric normal leaves the
/// in-plane fractional coordinates untouched, which is the invariant the
/// whole transform is stated in terms of.
///
/// `error` is filled and the input returned unchanged when the request
/// cannot be met: no cell, an out-of-plane axis that was never determined,
/// or an amplitude too large for the cell to contract around (see
/// rippleContractedLength).
Structure applyRipples(const Structure& sheet, const RippleOptions& options,
                       std::string* error = nullptr);

/// An amplitude SERIES: `count` structures with the amplitude ramped
/// linearly from `minAmplitude` to `maxAmplitude`, for a scan through the
/// Structure Container / Orchestration fan-out.
///
/// Every frame is a full ripple build at its own amplitude, contraction
/// included — NOT one build interpolated, which would get the contraction
/// (quadratic in A) wrong everywhere except the two endpoints.
///
/// Each frame carries its own amplitude as the per-atom scalar field
/// "ripple_amplitude", constant across the frame: it is the one channel a
/// Structure has that survives an extxyz round trip, so the amplitude a
/// frame was built at is still readable after the trajectory has been saved,
/// reloaded and fanned out. `count` ≤ 1 yields the single `minAmplitude`
/// frame. A frame whose amplitude the cell cannot hold is DROPPED with a
/// note in `error` rather than silently replaced by a flat one.
std::vector<std::shared_ptr<Structure>> buildRippleSeries(
    const Structure& sheet, const RippleOptions& options, double minAmplitude,
    double maxAmplitude, int count, std::string* error = nullptr);

} // namespace calango::core
