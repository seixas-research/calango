#include "core/Ripples.hpp"

#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace calango::core {

namespace {

/// Quadrature points over one period. The integrand is smooth and periodic,
/// so the trapezoid rule's error falls faster than any power of 1/N here
/// (see rippleArcLength's own comment); 512 is far past double precision for
/// every amplitude this module admits, and the whole integral is 512 square
/// roots — a cost that does not need managing.
constexpr int kQuadraturePoints = 512;

/// Bisection steps for the contraction solve. Each halves the bracket, and
/// the bracket starts at the flat length, so 200 steps is exact to the last
/// representable double many times over. It is bounded rather than
/// tolerance-driven so a pathological input cannot spin.
constexpr int kBisectionSteps = 200;

int clampedPeriods(int periods) { return periods >= 1 ? periods : 1; }

/// The two in-plane axis indices for an out-of-plane axis, in index order.
std::pair<int, int> inPlaneAxes(int normalAxis)
{
    switch (normalAxis) {
    case 0:
        return {1, 2};
    case 1:
        return {0, 2};
    default:
        return {0, 1};
    }
}

double fractionalComponent(const Vec3& fractional, int axis)
{
    return axis == 0 ? fractional.x : (axis == 1 ? fractional.y : fractional.z);
}

/// Does this direction ripple along the given in-plane slot (0 = first,
/// 1 = second)?
bool ripplesAlong(RippleOptions::Direction direction, int slot)
{
    switch (direction) {
    case RippleOptions::Direction::XY:
        return true;
    case RippleOptions::Direction::X:
        return slot == 0;
    case RippleOptions::Direction::Y:
        return slot == 1;
    }
    return false;
}

} // namespace

double rippleArcLength(double amplitude, double length, int periods)
{
    const int n = clampedPeriods(periods);
    if (length <= 0.0)
        return 0.0;
    // h(x) = A sin(2π n x / L)  ⇒  dh/dx = k cos(u), with k = 2π n A / L and
    // u = 2π n x / L. The substitution turns the integral into
    //
    //     S = (L / 2π) ∫₀^{2π} √(1 + k² cos² u) du
    //
    // — the n periods collapse into the prefactor, which is why the answer
    // scales the way it does and why one period's worth of quadrature is
    // enough however many periods are asked for.
    const double k = 2.0 * kPi * static_cast<double>(n) * amplitude / length;
    if (k == 0.0)
        return length; // a flat sheet's arc length is exactly its footprint
    const double k2 = k * k;
    // Trapezoid over a full period. The endpoints coincide, so the usual
    // half-weights cancel into a plain average of N samples on a uniform
    // grid — the periodic case where trapezoid is spectrally accurate.
    double sum = 0.0;
    for (int i = 0; i < kQuadraturePoints; ++i) {
        const double u = 2.0 * kPi * static_cast<double>(i)
            / static_cast<double>(kQuadraturePoints);
        const double c = std::cos(u);
        sum += std::sqrt(1.0 + k2 * c * c);
    }
    const double mean = sum / static_cast<double>(kQuadraturePoints);
    return length * mean;
}

double rippleContractedLength(double amplitude, double flatLength, int periods)
{
    const int n = clampedPeriods(periods);
    const double a = std::abs(amplitude);
    if (flatLength <= 0.0)
        return 0.0;
    if (a == 0.0)
        return flatLength; // nothing to store, nothing to contract
    // The hard floor. Whatever its footprint, a sinusoid of amplitude A with
    // n periods climbs and descends 4nA in total, so its arc length can
    // never be less than that: an amplitude asking for more than the sheet
    // HAS is not a tighter cell, it is a structure that does not exist.
    // Refused rather than clamped — a clamp would return a cell whose
    // rippling silently stretches the sheet, which is precisely the error
    // this whole contraction exists to prevent.
    if (flatLength <= 4.0 * static_cast<double>(n) * a)
        return 0.0;

    // rippleArcLength is increasing in L, so [lo, hi] brackets the root by
    // construction: at L = flatLength the arc length exceeds flatLength (the
    // sheet is longer than its footprint), and as L falls the arc length
    // falls toward 4nA, which is below flatLength by the check above.
    double lo = 0.0;
    double hi = flatLength;
    for (int step = 0; step < kBisectionSteps; ++step) {
        const double mid = 0.5 * (lo + hi);
        if (rippleArcLength(a, mid, n) < flatLength)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

Structure applyRipples(const Structure& sheet, const RippleOptions& options,
                       std::string* error)
{
    const auto refuse = [&](const char* why) {
        if (error)
            *error = why;
        return sheet;
    };
    if (error)
        error->clear();
    if (!sheet.cell().isDefined())
        return refuse("a rippled sheet needs a unit cell: the profile is "
                      "periodic in the cell's own fractional coordinates, "
                      "and a structure without one has none");
    if (options.normalAxis < 0 || options.normalAxis > 2)
        return refuse("no out-of-plane axis was determined for this "
                      "structure; pick one on the wizard rather than "
                      "letting the module guess");

    const auto [firstAxis, secondAxis] = inPlaneAxes(options.normalAxis);
    const std::array<Vec3, 3> vectors = sheet.cell().vectors();
    const double amplitude = options.amplitude;
    const int periods[2] = {clampedPeriods(options.periodsFirst),
                            clampedPeriods(options.periodsSecond)};
    const int axes[2] = {firstAxis, secondAxis};

    // Fractional coordinates FIRST, in the original cell. Everything below
    // is stated in them: they are what the contraction preserves and what
    // the profile is evaluated on.
    std::vector<Vec3> fractional;
    fractional.reserve(sheet.size());
    for (const Atom& atom : sheet.atoms())
        fractional.push_back(sheet.cell().cartesianToFractional(atom.position));

    // -- The contraction ---------------------------------------------------
    std::array<Vec3, 3> rippled = vectors;
    if (options.contractInPlane && amplitude != 0.0) {
        for (int slot = 0; slot < 2; ++slot) {
            if (!ripplesAlong(options.direction, slot))
                continue;
            const int axis = axes[slot];
            const double flat = vectors[static_cast<std::size_t>(axis)].norm();
            const double contracted =
                rippleContractedLength(amplitude, flat, periods[slot]);
            if (contracted <= 0.0)
                return refuse(
                    "the amplitude is too large for this cell to contract "
                    "around: a sinusoid of amplitude A with n periods is at "
                    "least 4nA long whatever its footprint, and that already "
                    "exceeds the cell vector it would have to fit into. Use "
                    "a smaller amplitude, fewer periods, or a larger "
                    "supercell");
            // Direction preserved, length changed: contracting a cell is a
            // uniaxial compression of the footprint, not a re-orientation
            // of the lattice.
            rippled[static_cast<std::size_t>(axis)] =
                vectors[static_cast<std::size_t>(axis)] * (contracted / flat);
        }
    }

    Structure result = sheet;
    UnitCell cell = sheet.cell();
    cell.setVectors(rippled);
    result.setCell(cell);

    // The out-of-plane direction: the GEOMETRIC normal of the (possibly
    // contracted) in-plane pair, signed to point the same way as the
    // out-of-plane cell vector. Contraction only rescales an in-plane
    // vector, so this is the same direction before and after — computed
    // after it anyway, because "perpendicular to the plane the atoms are
    // actually in" is the definition, not a value carried over.
    const Vec3 normal = [&] {
        const Vec3 n = rippled[static_cast<std::size_t>(firstAxis)]
                           .cross(rippled[static_cast<std::size_t>(secondAxis)])
                           .normalized();
        const Vec3& outOfPlane = rippled[static_cast<std::size_t>(
            options.normalAxis)];
        return n.dot(outOfPlane) >= 0.0 ? n : n * -1.0;
    }();

    for (std::size_t i = 0; i < result.size(); ++i) {
        // Rebuild the position from the PRESERVED fractional coordinates in
        // the contracted cell, then displace. In that order: the profile is
        // a function of the fractional coordinate, which the contraction
        // does not change, so a displacement applied first would be a
        // displacement of the wrong sheet.
        const Vec3& frac = fractional[i];
        result.atoms()[i].position = cell.fractionalToCartesian(frac);

        double h = amplitude;
        for (int slot = 0; slot < 2; ++slot) {
            if (!ripplesAlong(options.direction, slot))
                continue;
            const double f = fractionalComponent(frac, axes[slot]);
            h *= std::sin(2.0 * kPi * static_cast<double>(periods[slot]) * f);
        }
        result.atoms()[i].position += normal * h;
    }
    return result;
}

std::vector<std::shared_ptr<Structure>> buildRippleSeries(
    const Structure& sheet, const RippleOptions& options, double minAmplitude,
    double maxAmplitude, int count, std::string* error)
{
    std::vector<std::shared_ptr<Structure>> frames;
    if (error)
        error->clear();
    const int members = std::max(count, 1);
    frames.reserve(static_cast<std::size_t>(members));
    for (int i = 0; i < members; ++i) {
        // Linear ramp, with the LAST frame exactly at maxAmplitude and the
        // first exactly at minAmplitude — a single-frame series is the
        // minimum, and gets minAmplitude.
        const double t = members > 1
            ? static_cast<double>(i) / static_cast<double>(members - 1)
            : 0.0;
        RippleOptions member = options;
        member.amplitude = minAmplitude + t * (maxAmplitude - minAmplitude);
        std::string memberError;
        Structure built = applyRipples(sheet, member, &memberError);
        if (!memberError.empty()) {
            // Dropped, and said so. A frame silently replaced by the flat
            // input would sit in the middle of an amplitude scan claiming
            // to be an amplitude it is not.
            if (error && error->empty())
                *error = memberError;
            continue;
        }
        // The amplitude, carried on the frame itself. A per-atom scalar
        // field is the one channel that survives an extxyz round trip, so
        // the tag is still there after the trajectory has been saved and
        // fanned out through a Structure Container.
        built.setScalarField("ripple_amplitude",
                             std::vector<double>(built.size(),
                                                 member.amplitude));
        frames.push_back(std::make_shared<Structure>(std::move(built)));
    }
    return frames;
}

} // namespace calango::core
