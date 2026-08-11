#include "dft/IntegrationGrid.hpp"

#include <algorithm>
#include <cmath>

namespace calango::dft {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFourPi = 12.566370614359172;

/// How much further than the nearest centre a centre can be and still affect
/// the partition at a point. Becke's smoothed step is within 10^-12 of 1 once
/// the two distances differ by more than about half the separation, so this is
/// a generous bound rather than a tuned one; the volume test in `dft_grids`
/// is what would catch it being too small.
constexpr double kPartitionMarginBohr = 10.0;

double distance(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    const double dx = a[0] - b[0];
    const double dy = a[1] - b[1];
    const double dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// Add every distinct sign-and-permutation image of (a, b, c) with the given
/// weight. The Lebedev rules are built entirely from such orbits of the
/// octahedral group, which is why their tables are four numbers rather than
/// several hundred coordinates.
void addOctahedralOrbit(std::vector<AngularPoint>& points, double a, double b,
                        double c, double weight)
{
    std::vector<std::array<double, 3>> seen;
    const double values[3] = {a, b, c};
    const int order[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2},
                             {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (const auto& permutation : order) {
        for (int signs = 0; signs < 8; ++signs) {
            std::array<double, 3> p{
                values[permutation[0]] * ((signs & 1) ? -1.0 : 1.0),
                values[permutation[1]] * ((signs & 2) ? -1.0 : 1.0),
                values[permutation[2]] * ((signs & 4) ? -1.0 : 1.0)};
            const bool duplicate =
                std::any_of(seen.begin(), seen.end(),
                            [&p](const std::array<double, 3>& q) {
                                return std::abs(p[0] - q[0]) < 1.0e-12
                                    && std::abs(p[1] - q[1]) < 1.0e-12
                                    && std::abs(p[2] - q[2]) < 1.0e-12;
                            });
            if (duplicate)
                continue;
            seen.push_back(p);
            points.push_back({p[0], p[1], p[2], weight});
        }
    }
}

/// Gauss-Legendre nodes and weights on [−1, 1] by Newton iteration on P_n.
/// Generated rather than tabulated: the recurrence and its derivative are
/// three lines, and a table would cap the reachable accuracy at whatever was
/// typed in.
void gaussLegendre(int n, std::vector<double>& nodes,
                   std::vector<double>& weights)
{
    nodes.assign(static_cast<std::size_t>(n), 0.0);
    weights.assign(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        // Chebyshev start, within O(1/n²) of the Legendre root, so Newton
        // converges in three or four iterations.
        double x = std::cos(kPi * (i + 0.75) / (n + 0.5));
        double derivative = 0.0;
        for (int iteration = 0; iteration < 100; ++iteration) {
            double p0 = 1.0;
            double p1 = 0.0;
            for (int k = 0; k < n; ++k) {
                const double p2 = p1;
                p1 = p0;
                p0 = ((2.0 * k + 1.0) * x * p1 - k * p2) / (k + 1.0);
            }
            derivative = n * (x * p0 - p1) / (x * x - 1.0);
            const double delta = p0 / derivative;
            x -= delta;
            if (std::abs(delta) <= 1.0e-15)
                break;
        }
        nodes[static_cast<std::size_t>(i)] = x;
        weights[static_cast<std::size_t>(i)] =
            2.0 / ((1.0 - x * x) * derivative * derivative);
    }
}

/// Becke's smoothing step, iterated three times.
///
/// p(x) = (3x − x³)/2 maps [−1,1] onto itself with zero derivative at both
/// ends. Composing it three times makes the transition between two atoms'
/// regions sharp enough to keep each integrand smooth on its own centre,
/// while every derivative stays continuous — which is what a quadrature needs
/// and what a hard Voronoi boundary does not provide.
inline double beckeStep(double mu)
{
    double f = mu;
    for (int i = 0; i < 3; ++i)
        f = 1.5 * f - 0.5 * f * f * f;
    return 0.5 * (1.0 - f);
}

} // namespace

std::vector<AngularPoint> AngularGrid::lebedev(int points)
{
    std::vector<AngularPoint> grid;
    switch (points) {
    case 6: // degree 3
        addOctahedralOrbit(grid, 1.0, 0.0, 0.0, 1.0 / 6.0);
        break;
    case 14: // degree 5
        addOctahedralOrbit(grid, 1.0, 0.0, 0.0, 1.0 / 15.0);
        addOctahedralOrbit(grid, 1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0),
                           1.0 / std::sqrt(3.0), 3.0 / 40.0);
        break;
    case 26: // degree 7
        addOctahedralOrbit(grid, 1.0, 0.0, 0.0, 1.0 / 21.0);
        addOctahedralOrbit(grid, 1.0 / std::sqrt(2.0), 1.0 / std::sqrt(2.0),
                           0.0, 4.0 / 105.0);
        addOctahedralOrbit(grid, 1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0),
                           1.0 / std::sqrt(3.0), 27.0 / 840.0);
        break;
    case 50: { // degree 11
        addOctahedralOrbit(grid, 1.0, 0.0, 0.0, 4.0 / 315.0);
        addOctahedralOrbit(grid, 1.0 / std::sqrt(2.0), 1.0 / std::sqrt(2.0),
                           0.0, 64.0 / 2835.0);
        addOctahedralOrbit(grid, 1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0),
                           1.0 / std::sqrt(3.0), 27.0 / 1280.0);
        // The (1, 1, 3)/√11 orbit. The ORDER of the components is the whole
        // content of this line: put the 3 in the wrong slot and the rule is
        // still 50 points with weights summing to one, still integrates
        // constants exactly, and is wrong from l = 4 upwards — which shows up
        // as a total energy off in the third digit and nowhere else. It is
        // pinned by the exactness test in `dft_grids`, not by this comment.
        const double s = std::sqrt(11.0);
        addOctahedralOrbit(grid, 1.0 / s, 1.0 / s, 3.0 / s,
                           14641.0 / 725760.0);
        break;
    }
    default:
        break;
    }
    if (static_cast<int>(grid.size()) != points)
        return {};
    return grid;
}

std::vector<AngularPoint> AngularGrid::product(int thetaPoints, int phiPoints)
{
    std::vector<AngularPoint> grid;
    if (thetaPoints < 1 || phiPoints < 1)
        return grid;
    std::vector<double> nodes;
    std::vector<double> weights;
    gaussLegendre(thetaPoints, nodes, weights);
    grid.reserve(static_cast<std::size_t>(thetaPoints) * phiPoints);
    for (int i = 0; i < thetaPoints; ++i) {
        const double cosTheta = nodes[static_cast<std::size_t>(i)];
        const double sinTheta =
            std::sqrt(std::max(0.0, 1.0 - cosTheta * cosTheta));
        for (int j = 0; j < phiPoints; ++j) {
            const double phi = 2.0 * kPi * j / phiPoints;
            // Weights normalised to sum to 1: the Legendre weight over 2 for
            // the cos θ half, and 1/phiPoints for the uniform φ half.
            grid.push_back({sinTheta * std::cos(phi), sinTheta * std::sin(phi),
                            cosTheta,
                            weights[static_cast<std::size_t>(i)] * 0.5
                                / phiPoints});
        }
    }
    return grid;
}

std::vector<AngularPoint> AngularGrid::forDegree(int degree)
{
    if (degree <= 3)
        return lebedev(6);
    if (degree <= 5)
        return lebedev(14);
    if (degree <= 7)
        return lebedev(26);
    if (degree <= 11)
        return lebedev(50);
    // Above the tabulated orders: Gauss-Legendre needs ⌈(d+1)/2⌉ nodes in
    // cos θ, and the uniform φ rule needs more than d nodes to be exact for
    // e^{i·d·φ}.
    const int theta = (degree + 2) / 2;
    const int phi = degree + 1;
    return product(theta, phi);
}

void AngularGrid::realSphericalHarmonics(double x, double y, double z,
                                         int lMax, std::vector<double>& values)
{
    if (lMax < 0)
        lMax = 0;
    values.assign(harmonicCount(lMax), 0.0);
    const double norm = std::sqrt(x * x + y * y + z * z);
    const double ct = norm > 0.0 ? z / norm : 1.0;
    const double rho = std::sqrt(std::max(0.0, 1.0 - ct * ct));
    // cos(mφ) and sin(mφ) by the Chebyshev recursion on the in-plane unit
    // vector, so no atan2 and no trigonometric call per m.
    const double planar = std::sqrt(x * x + y * y);
    const double cosPhi = planar > 1.0e-14 ? x / planar : 1.0;
    const double sinPhi = planar > 1.0e-14 ? y / planar : 0.0;

    std::vector<double> cosM(static_cast<std::size_t>(lMax) + 1, 0.0);
    std::vector<double> sinM(static_cast<std::size_t>(lMax) + 1, 0.0);
    cosM[0] = 1.0;
    sinM[0] = 0.0;
    for (int m = 1; m <= lMax; ++m) {
        cosM[static_cast<std::size_t>(m)] =
            cosM[static_cast<std::size_t>(m - 1)] * cosPhi
            - sinM[static_cast<std::size_t>(m - 1)] * sinPhi;
        sinM[static_cast<std::size_t>(m)] =
            sinM[static_cast<std::size_t>(m - 1)] * cosPhi
            + cosM[static_cast<std::size_t>(m - 1)] * sinPhi;
    }

    // Associated Legendre by the standard two-term recursions, WITHOUT the
    // Condon-Shortley phase — consistently omitted, which only matters if a
    // sign convention has to match another program's.
    for (int m = 0; m <= lMax; ++m) {
        double pmm = 1.0;
        for (int k = 1; k <= m; ++k)
            pmm *= (2.0 * k - 1.0) * rho;
        double previous = 0.0;
        double current = pmm;
        for (int l = m; l <= lMax; ++l) {
            if (l > m) {
                const double next =
                    (ct * (2.0 * l - 1.0) * current - (l + m - 1.0) * previous)
                    / (l - m);
                previous = current;
                current = next;
            }
            // N = sqrt((2l+1)/4π · (l−m)!/(l+m)!); the factorial ratio is
            // built by a product rather than by two factorials, which
            // overflow at l ≈ 85 and lose digits long before that.
            double ratio = 1.0;
            for (int k = l - m + 1; k <= l + m; ++k)
                ratio /= static_cast<double>(k);
            const double n =
                std::sqrt((2.0 * l + 1.0) / kFourPi * ratio);
            const auto base = static_cast<std::size_t>(l * l + l);
            if (m == 0) {
                values[base] = n * current;
            } else {
                const double scaled = std::sqrt(2.0) * n * current;
                values[base + static_cast<std::size_t>(m)] =
                    scaled * cosM[static_cast<std::size_t>(m)];
                values[base - static_cast<std::size_t>(m)] =
                    scaled * sinM[static_cast<std::size_t>(m)];
            }
        }
    }
}

void AngularGrid::realSolidHarmonics(double x, double y, double z, int lMax,
                                     std::vector<double>& values,
                                     std::vector<double>& gradients)
{
    if (lMax < 0)
        lMax = 0;
    const std::size_t count = harmonicCount(lMax);
    values.assign(count, 0.0);
    gradients.assign(count * 3, 0.0);

    // S_lm = r^l Y_lm, evaluated through the spherical harmonics already
    // available. Y_lm normalises internally, so this is exactly the
    // homogeneous polynomial.
    const auto solid = [lMax, count](double a, double b, double c,
                                     std::vector<double>& out) {
        realSphericalHarmonics(a, b, c, lMax, out);
        const double r = std::sqrt(a * a + b * b + c * c);
        for (int l = 1; l <= lMax; ++l) {
            const double scale = std::pow(r, static_cast<double>(l));
            const auto begin = static_cast<std::size_t>(l * l);
            const auto end = static_cast<std::size_t>((l + 1) * (l + 1));
            for (std::size_t i = begin; i < end && i < count; ++i)
                out[i] *= scale;
        }
    };

    solid(x, y, z, values);
    const double radius = std::sqrt(x * x + y * y + z * z);
    const double step = 1.0e-4 * std::max(radius, 1.0e-8);
    std::vector<double> plus;
    std::vector<double> minus;
    for (int axis = 0; axis < 3; ++axis) {
        double p[3] = {x, y, z};
        double m[3] = {x, y, z};
        p[axis] += step;
        m[axis] -= step;
        solid(p[0], p[1], p[2], plus);
        solid(m[0], m[1], m[2], minus);
        for (std::size_t i = 0; i < count; ++i)
            gradients[i * 3 + static_cast<std::size_t>(axis)] =
                (plus[i] - minus[i]) / (2.0 * step);
    }
}

double IntegrationGrid::braggRadiusBohr(int atomicNumber)
{
    // Bragg-Slater empirical radii in angstrom, H through Kr. They enter only
    // as the size bias of the partition and as the radial scale, so a missing
    // entry degrades accuracy rather than correctness.
    static const double kRadii[] = {
        0.35, 0.35, 1.45, 1.05, 0.85, 0.70, 0.65, 0.60, 0.50, 0.45, 1.80,
        1.50, 1.25, 1.10, 1.00, 1.00, 1.00, 1.00, 2.20, 1.80, 1.60, 1.40,
        1.35, 1.40, 1.40, 1.40, 1.35, 1.35, 1.35, 1.35, 1.30, 1.25, 1.15,
        1.15, 1.15, 1.10};
    constexpr double kBohrPerAngstrom = 1.8897261254578281;
    const int count = static_cast<int>(sizeof(kRadii) / sizeof(kRadii[0]));
    const double angstrom =
        (atomicNumber >= 1 && atomicNumber <= count)
        ? kRadii[atomicNumber - 1]
        : 1.0;
    return angstrom * kBohrPerAngstrom;
}

Outcome IntegrationGrid::build(const std::vector<Atom>& atoms,
                               const std::vector<std::array<double, 3>>& lattice,
                               int radialShells, int angularDegree,
                               double outerRadiusBohr)
{
    points_.clear();
    shellRadii_.clear();
    shellWeights_.clear();
    directions_.clear();
    atomCount_ = atoms.size();
    if (atoms.empty())
        return Outcome::invalid("integration grid: no atoms");
    if (radialShells < 8)
        return Outcome::invalid("integration grid: too few radial shells");
    if (!(outerRadiusBohr > 0.0))
        return Outcome::invalid("integration grid: outer radius must be > 0");
    if (!lattice.empty() && lattice.size() != 3)
        return Outcome::invalid(
            "integration grid: a periodic cell needs exactly three vectors");

    directions_ = AngularGrid::forDegree(angularDegree);
    if (directions_.empty())
        return Outcome::invalid("integration grid: no angular rule available");

    // --- Radial shells ---------------------------------------------------
    // Gauss-Chebyshev of the second kind under Becke's r = R(1+x)/(1−x) map.
    // The map sends a finite interval to the half line, so a fixed number of
    // nodes covers the cusp and the tail at once; the Chebyshev weights have
    // a closed form, so there is no root finding.
    //
    // The same shell radii are used for every atom. That costs a little
    // accuracy against per-element radial scaling, and buys the property that
    // makes the electrostatics tractable: every atom's grid is the SAME
    // radial mesh times the SAME directions, so a quantity can be moved
    // between the point list and the (shell, direction) decomposition the
    // radial Poisson solver needs without interpolation.
    const double scale = 0.5 * outerRadiusBohr / 2.0;
    for (int i = 1; i <= radialShells; ++i) {
        const double theta = kPi * i / (radialShells + 1.0);
        const double x = std::cos(theta);
        const double r = scale * (1.0 + x) / (1.0 - x);
        if (r > outerRadiusBohr || !(r > 0.0))
            continue;
        // ∫f r² dr = Σ [π/(n+1) sin θ] · f(r) r² · dr/dx, dr/dx = 2R/(1−x)².
        const double drdx = 2.0 * scale / ((1.0 - x) * (1.0 - x));
        const double weight =
            kPi / (radialShells + 1.0) * std::sin(theta) * r * r * drdx;
        shellRadii_.push_back(r);
        shellWeights_.push_back(weight);
    }
    if (shellRadii_.empty())
        return Outcome::invalid(
            "integration grid: the radial map produced no shells inside the "
            "outer radius");
    // Ascending, so a shell index is a radius ordering — the Poisson solver
    // integrates outward and inward over them.
    std::vector<std::size_t> order(shellRadii_.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(),
              [this](std::size_t a, std::size_t b) {
                  return shellRadii_[a] < shellRadii_[b];
              });
    {
        std::vector<double> radii(order.size());
        std::vector<double> weights(order.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            radii[i] = shellRadii_[order[i]];
            weights[i] = shellWeights_[order[i]];
        }
        shellRadii_.swap(radii);
        shellWeights_.swap(weights);
    }

    // --- Partition centres, including periodic images ---------------------
    struct Centre {
        std::array<double, 3> position{{0.0, 0.0, 0.0}};
        double radius = 0.0;
        std::size_t atom = 0;
        bool home = false; ///< in the reference cell
    };
    std::vector<Centre> centres;
    const double reach = outerRadiusBohr + 12.0;
    std::array<int, 3> repeats{{0, 0, 0}};
    if (!lattice.empty()) {
        for (int axis = 0; axis < 3; ++axis) {
            const auto& v = lattice[static_cast<std::size_t>(axis)];
            const double length =
                std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            repeats[static_cast<std::size_t>(axis)] =
                length > 1.0e-8
                ? static_cast<int>(std::ceil(reach / length))
                : 0;
        }
    }
    for (int i = -repeats[0]; i <= repeats[0]; ++i)
        for (int j = -repeats[1]; j <= repeats[1]; ++j)
            for (int k = -repeats[2]; k <= repeats[2]; ++k) {
                std::array<double, 3> shift{{0.0, 0.0, 0.0}};
                if (!lattice.empty()) {
                    for (int c = 0; c < 3; ++c)
                        shift[static_cast<std::size_t>(c)] =
                            i * lattice[0][static_cast<std::size_t>(c)]
                            + j * lattice[1][static_cast<std::size_t>(c)]
                            + k * lattice[2][static_cast<std::size_t>(c)];
                }
                for (std::size_t a = 0; a < atoms.size(); ++a) {
                    Centre centre;
                    for (int c = 0; c < 3; ++c)
                        centre.position[static_cast<std::size_t>(c)] =
                            atoms[a].position[static_cast<std::size_t>(c)]
                            + shift[static_cast<std::size_t>(c)];
                    centre.radius = braggRadiusBohr(atoms[a].atomicNumber);
                    centre.atom = a;
                    centre.home = (i == 0 && j == 0 && k == 0);
                    centres.push_back(centre);
                }
            }

    // --- Points, with the Becke partition weight --------------------------
    points_.resize(atoms.size() * shellRadii_.size() * directions_.size());
    std::vector<double> distances(centres.size(), 0.0);
    std::vector<std::size_t> active;
    active.reserve(centres.size());
    for (std::size_t a = 0; a < atoms.size(); ++a) {
        // Which centre IS this atom in the reference cell.
        std::size_t self = 0;
        for (std::size_t c = 0; c < centres.size(); ++c)
            if (centres[c].home && centres[c].atom == a)
                self = c;

        for (std::size_t s = 0; s < shellRadii_.size(); ++s) {
            for (std::size_t d = 0; d < directions_.size(); ++d) {
                const AngularPoint& direction = directions_[d];
                GridPoint point;
                point.atom = a;
                point.shell = s;
                point.direction = d;
                for (int c = 0; c < 3; ++c) {
                    const double unit = c == 0 ? direction.x
                        : c == 1              ? direction.y
                                              : direction.z;
                    point.position[static_cast<std::size_t>(c)] =
                        atoms[a].position[static_cast<std::size_t>(c)]
                        + shellRadii_[s] * unit;
                }

                // Becke cell functions. Everything is a product of smoothed
                // steps in the elliptical coordinate between pairs of
                // centres, so a point that is unambiguously inside one atom
                // gets weight 1 there and 0 everywhere else, and the sum is
                // one BY CONSTRUCTION rather than by normalisation luck.
                //
                // Screened first. Written as the literal double product over
                // every centre it is O(N²) per grid point, and with a periodic
                // image list that is hundreds of centres — ten billion step
                // functions for one silicon cell. A centre further from the
                // point than the nearest one plus a margin has s(μ) = 1
                // against everything that matters and cell function zero, so
                // it changes neither the numerator nor the denominator.
                double nearest = 1.0e300;
                for (std::size_t c = 0; c < centres.size(); ++c) {
                    distances[c] = distance(point.position, centres[c].position);
                    nearest = std::min(nearest, distances[c]);
                }
                const double screen = nearest + kPartitionMarginBohr;
                active.clear();
                for (std::size_t c = 0; c < centres.size(); ++c)
                    if (distances[c] <= screen)
                        active.push_back(c);

                double total = 0.0;
                double selfProduct = 0.0;
                for (const std::size_t ci : active) {
                    double product = 1.0;
                    for (const std::size_t cj : active) {
                        if (cj == ci)
                            continue;
                        const double separation = distance(centres[ci].position,
                                                           centres[cj].position);
                        if (separation < 1.0e-8)
                            continue;
                        double mu = (distances[ci] - distances[cj]) / separation;
                        // Size adjustment: the boundary between a big atom
                        // and a small one belongs nearer the small one. Becke's
                        // a is clamped at ±0.5, beyond which the transformed
                        // coordinate stops being monotone in mu and the
                        // partition would fold over itself.
                        const double chi =
                            centres[ci].radius / centres[cj].radius;
                        const double u = (chi - 1.0) / (chi + 1.0);
                        double shift = u / (u * u - 1.0);
                        shift = std::clamp(shift, -0.5, 0.5);
                        mu += shift * (1.0 - mu * mu);
                        mu = std::clamp(mu, -1.0, 1.0);
                        product *= beckeStep(mu);
                        if (product < 1.0e-300)
                            break;
                    }
                    total += product;
                    if (ci == self)
                        selfProduct = product;
                }
                const double partition =
                    total > 0.0 ? selfProduct / total : 0.0;
                point.weight = partition * shellWeights_[s]
                    * direction.weight * kFourPi;
                points_[index(a, s, d)] = point;
            }
        }
    }
    return Outcome::success();
}

double IntegrationGrid::integrate(const std::vector<double>& values) const
{
    if (values.size() != points_.size())
        return 0.0;
    double sum = 0.0;
    double compensation = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const double term = points_[i].weight * values[i] - compensation;
        const double next = sum + term;
        compensation = (next - sum) - term;
        sum = next;
    }
    return sum;
}

} // namespace calango::dft
