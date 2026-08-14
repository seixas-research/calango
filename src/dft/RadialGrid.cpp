#include "dft/RadialGrid.hpp"
#include "dft/Constants.hpp"

#include <algorithm>
#include <cmath>

namespace calango::dft {

RadialGrid::RadialGrid(std::size_t points, double outerRadiusBohr,
                       double innerScaleBohr)
{
    // Simpson's rule needs an ODD number of points (an even number of
    // intervals). Rounding up rather than refusing: the caller asked for a
    // resolution, not for a specific array length, and silently integrating
    // with a trapezoid fallback would cost an order of accuracy nobody sees.
    if (points < 5)
        points = 5;
    if (points % 2 == 0)
        ++points;
    if (!(outerRadiusBohr > 0.0) || !(innerScaleBohr > 0.0))
        return;

    const double n = static_cast<double>(points - 1);
    const double b = std::log(1.0 + outerRadiusBohr / innerScaleBohr) / n;
    innerScale_ = innerScaleBohr;
    step_ = b;

    r_.resize(points);
    drdi_.resize(points);
    for (std::size_t i = 0; i < points; ++i) {
        const double x = b * static_cast<double>(i);
        // expm1, not exp(x) - 1: at i = 0 the two differ by everything. The
        // first point must be EXACTLY zero, because that is where the nuclear
        // cusp is and a mesh starting at 1e-12 instead of 0 puts a 1/r
        // singularity on a grid point.
        r_[i] = innerScaleBohr * std::expm1(x);
        drdi_[i] = innerScaleBohr * b * std::exp(x);
    }

    // Composite Simpson in `i`, times the Jacobian. The h of the i-mesh is 1
    // by construction, which is the whole reason for the transformation.
    weights_.assign(points, 0.0);
    for (std::size_t i = 0; i < points; ++i) {
        const double simpson = (i == 0 || i + 1 == points) ? 1.0
            : (i % 2 == 1)                                 ? 4.0
                                                           : 2.0;
        weights_[i] = simpson * drdi_[i] / 3.0;
    }
}

double RadialGrid::integrate(const std::vector<double>& values) const
{
    if (values.size() != r_.size())
        return 0.0;
    // Compensated summation: an all-electron radial integral spans values from
    // the core (10^6) to the tail (10^-15), and a plain running sum loses the
    // tail entirely once the core has been added.
    double sum = 0.0;
    double compensation = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const double term = weights_[i] * values[i] - compensation;
        const double next = sum + term;
        compensation = (next - sum) - term;
        sum = next;
    }
    return sum;
}

double RadialGrid::integrateSpherical(const std::vector<double>& values) const
{
    if (values.size() != r_.size())
        return 0.0;
    std::vector<double> scaled(values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
        scaled[i] = values[i] * r_[i] * r_[i];
    return integrate(scaled);
}

double RadialGrid::radiusAt(double index) const
{
    if (r_.empty())
        return 0.0;
    return innerScale_ * std::expm1(step_ * index);
}

double RadialGrid::jacobianAt(double index) const
{
    if (r_.empty())
        return 0.0;
    return innerScale_ * step_ * std::exp(step_ * index);
}

double RadialGrid::interpolateIndex(const std::vector<double>& values,
                                    double index) const
{
    const std::size_t n = r_.size();
    if (values.size() != n || n < 4)
        return 0.0;
    if (index <= 0.0)
        return values.front();
    if (index >= static_cast<double>(n - 1))
        return values.back();

    // Cubic through four consecutive points, interpolating in the INDEX where
    // the mesh is uniform. In `i` the Lagrange denominators are the integers
    // −6, 2, −2, 6, so the weights are written out rather than looped: this
    // runs inside the innermost ODE step of every eigenvalue search.
    auto base = static_cast<std::ptrdiff_t>(std::floor(index)) - 1;
    if (base < 0)
        base = 0;
    if (base > static_cast<std::ptrdiff_t>(n) - 4)
        base = static_cast<std::ptrdiff_t>(n) - 4;
    const double t = index - static_cast<double>(base);
    const double w0 = -(t - 1.0) * (t - 2.0) * (t - 3.0) / 6.0;
    const double w1 = t * (t - 2.0) * (t - 3.0) / 2.0;
    const double w2 = -t * (t - 1.0) * (t - 3.0) / 2.0;
    const double w3 = t * (t - 1.0) * (t - 2.0) / 6.0;
    const auto i0 = static_cast<std::size_t>(base);
    return w0 * values[i0] + w1 * values[i0 + 1] + w2 * values[i0 + 2]
        + w3 * values[i0 + 3];
}

double RadialGrid::interpolate(const std::vector<double>& values,
                               double radius) const
{
    if (values.size() != r_.size() || r_.size() < 4)
        return 0.0;
    if (radius <= 0.0)
        return values.front();
    if (radius >= r_.back())
        return 0.0; // outside the mesh a confined function is exactly zero

    // Invert r(i) = a(e^{bi} − 1) analytically rather than searching for the
    // bracketing point. Two reasons, and the second is the one that shows up
    // in a profile: the mesh is known in closed form so the index cannot
    // disagree with the mesh it came from, and this runs tens of millions of
    // times per self-consistency cycle — once per atom per image per grid
    // point — where a binary search is the difference between seconds and
    // minutes.
    //
    // Interpolating in the INDEX, not in r, is also what keeps it accurate:
    // the mesh is uniform in i, so a cubic there is well conditioned
    // everywhere, while in r the same cubic would be fitted across points a
    // thousandfold apart in spacing.
    const double index = std::log1p(radius / innerScale_) / step_;
    return interpolateIndex(values, index);
}

std::vector<double> RadialGrid::cumulative(
    const std::vector<double>& values) const
{
    std::vector<double> integral(r_.size(), 0.0);
    if (values.size() != r_.size() || r_.size() < 3)
        return integral;

    // Cumulative Simpson, NOT a partial sum of the composite weights.
    //
    // That distinction is the whole content of this function. The composite
    // rule's (1, 4, 2, 4, …, 4, 1)/3 pattern is only a valid quadrature for
    // the WHOLE interval: stopping halfway leaves the running total holding a
    // 4 or a 2 where an endpoint 1 belongs, which is an O(h) error at every
    // point rather than the O(h⁴) the rule is chosen for. Summing the weights
    // as they come looks obviously right and is wrong by about a percent —
    // small enough to pass a casual eyeball and large enough to ruin an
    // electrostatic potential.
    //
    // So: Simpson across each PAIR of intervals for the even points, and the
    // 3-point Newton-Cotes formula (5, 8, −1)/12 for the odd ones in between,
    // which is the standard construction and is exact for cubics like the rest.
    //
    // `f` is already the integrand in `i`: the physical values times dr/di.
    std::vector<double> f(r_.size());
    for (std::size_t i = 0; i < r_.size(); ++i)
        f[i] = values[i] * drdi_[i];

    for (std::size_t i = 0; i + 2 < f.size(); i += 2) {
        integral[i + 1] =
            integral[i] + (5.0 * f[i] + 8.0 * f[i + 1] - f[i + 2]) / 12.0;
        integral[i + 2] =
            integral[i] + (f[i] + 4.0 * f[i + 1] + f[i + 2]) / 3.0;
    }
    // An even point count leaves the last point unreached by the pair loop.
    // The grid constructor makes the count odd, so this is belt and braces
    // for a grid built some other way.
    if (f.size() % 2 == 0 && f.size() >= 3) {
        const std::size_t last = f.size() - 1;
        integral[last] = integral[last - 1]
            + (5.0 * f[last] + 8.0 * f[last - 1] - f[last - 2]) / 12.0;
    }
    return integral;
}

std::vector<double> RadialGrid::hartreePotential(
    const std::vector<double>& density) const
{
    std::vector<double> potential(r_.size(), 0.0);
    if (density.size() != r_.size() || r_.size() < 3)
        return potential;

    //   inner(r) = ∫₀^r ρ r'² dr'   (enclosed charge / 4π)
    //   outer(r) = ∫_r^∞ ρ r'  dr'  = total − ∫₀^r ρ r' dr'
    std::vector<double> innerIntegrand(r_.size());
    std::vector<double> outerIntegrand(r_.size());
    for (std::size_t i = 0; i < r_.size(); ++i) {
        innerIntegrand[i] = density[i] * r_[i] * r_[i];
        outerIntegrand[i] = density[i] * r_[i];
    }
    const std::vector<double> inner = cumulative(innerIntegrand);
    const std::vector<double> outerBelow = cumulative(outerIntegrand);
    const double outerTotal = outerBelow.back();
    std::vector<double> outer(r_.size(), 0.0);
    for (std::size_t i = 0; i < r_.size(); ++i)
        outer[i] = outerTotal - outerBelow[i];

    for (std::size_t i = 0; i < r_.size(); ++i) {
        // At r = 0 the first term is 0/0 in the limit — the enclosed charge
        // vanishes as r³ while the divisor vanishes as r — so it is dropped
        // and the potential there is the outer term alone, which is its
        // correct finite value.
        const double enclosed = r_[i] > 0.0 ? inner[i] / r_[i] : 0.0;
        potential[i] = kFourPi * (enclosed + outer[i]);
    }
    return potential;
}

} // namespace calango::dft
