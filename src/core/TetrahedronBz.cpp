#include "core/TetrahedronBz.hpp"

#include <algorithm>
#include <cmath>

namespace calango::core {

namespace {

using Vec3 = std::array<double, 3>;

Vec3 operator-(const Vec3& a, const Vec3& b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Vec3 operator+(const Vec3& a, const Vec3& b)
{
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
Vec3 operator*(const Vec3& a, double s)
{
    return {a[0] * s, a[1] * s, a[2] * s};
}
double dot(const Vec3& a, const Vec3& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}
double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }

/// Solve M g = r for g, with M given as three ROW vectors. Returns false when
/// the rows are degenerate (a flat or collapsed tetrahedron).
bool solve3(const Vec3& m0, const Vec3& m1, const Vec3& m2, const Vec3& r,
            Vec3& g)
{
    const double det = dot(m0, cross(m1, m2));
    if (std::abs(det) < 1e-30)
        return false;
    // Cramer's rule. Three 3x3 determinants is cheaper and better conditioned
    // here than a general solver, and the matrix is fixed-size.
    const Vec3 c12 = cross(m1, m2);
    const Vec3 c20 = cross(m2, m0);
    const Vec3 c01 = cross(m0, m1);
    g = {(r[0] * c12[0] + r[1] * c20[0] + r[2] * c01[0]) / det,
         (r[0] * c12[1] + r[1] * c20[1] + r[2] * c01[1]) / det,
         (r[0] * c12[2] + r[1] * c20[2] + r[2] * c01[2]) / det};
    return true;
}

/// The six pairs of corner indices that form a tetrahedron's edges.
constexpr int kEdges[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};

/// The polygon eps(k) = level cuts out of a tetrahedron: up to four vertices,
/// each with the barycentric coordinates that interpolate any linear quantity
/// onto it, plus the (constant) gradient.
///
/// Shared by both deltas. The single delta measures this polygon's area; the
/// double delta walks its edges looking for where the SECOND band crosses the
/// level. Extracting it is what keeps the two routines from drifting apart in
/// their idea of where the surface is.
struct SurfacePolygon {
    Vec3 vertex[4];
    std::array<double, 4> bary[4];
    int count = 0;
    Vec3 gradient{};
    double slope = 0.0;
};

bool buildSurfacePolygon(const std::array<Vec3, 4>& positions,
                         const std::array<double, 4>& energies, double level,
                         SurfacePolygon& polygon)
{
    // Strict sign change only. A corner sitting exactly ON the level is a
    // measure-zero event in the energy sweep; admitting it would create
    // duplicate polygon vertices and a degenerate area for no physical gain.
    polygon.count = 0;
    for (const auto& edge : kEdges) {
        const int a = edge[0];
        const int b = edge[1];
        const double da = energies[a] - level;
        const double db = energies[b] - level;
        if (da * db >= 0.0)
            continue;
        if (polygon.count >= 4)
            break; // a plane cuts a tetrahedron in at most 4 points
        const double t = da / (da - db); // in (0, 1)
        const int i = polygon.count;
        polygon.vertex[i] = positions[a] + (positions[b] - positions[a]) * t;
        polygon.bary[i] = {0.0, 0.0, 0.0, 0.0};
        polygon.bary[i][a] = 1.0 - t;
        polygon.bary[i][b] = t;
        ++polygon.count;
    }
    if (polygon.count < 3)
        return false;

    if (!solve3(positions[1] - positions[0], positions[2] - positions[0],
                positions[3] - positions[0],
                {energies[1] - energies[0], energies[2] - energies[0],
                 energies[3] - energies[0]},
                polygon.gradient))
        return false;
    polygon.slope = norm(polygon.gradient);
    // A band flat across the tetrahedron puts a finite amount of state at a
    // single energy: the delta is not representable and 1/|grad| diverges.
    // Dropped rather than clamped — clamping would invent weight.
    if (polygon.slope < 1e-12)
        return false;

    // Four points can come out of the edge loop in an order that traces a
    // bow-tie, which gives a wrong (smaller) area and a wrong edge walk, so
    // they are sorted by angle about the centroid in the plane of the
    // surface. The plane normal is grad eps, by construction.
    if (polygon.count == 4) {
        Vec3 centre{0.0, 0.0, 0.0};
        for (int i = 0; i < 4; ++i)
            centre = centre + polygon.vertex[i];
        centre = centre * 0.25;
        const Vec3 normal = polygon.gradient * (1.0 / polygon.slope);
        Vec3 axis1 = polygon.vertex[0] - centre;
        const double axis1Length = norm(axis1);
        if (axis1Length < 1e-18)
            return false;
        axis1 = axis1 * (1.0 / axis1Length);
        const Vec3 axis2 = cross(normal, axis1);
        int order[4] = {0, 1, 2, 3};
        double angle[4];
        for (int i = 0; i < 4; ++i) {
            const Vec3 d = polygon.vertex[i] - centre;
            angle[i] = std::atan2(dot(d, axis2), dot(d, axis1));
        }
        std::sort(order, order + 4,
                  [&angle](int a, int b) { return angle[a] < angle[b]; });
        Vec3 sortedVertex[4];
        std::array<double, 4> sortedBary[4];
        for (int i = 0; i < 4; ++i) {
            sortedVertex[i] = polygon.vertex[order[i]];
            sortedBary[i] = polygon.bary[order[i]];
        }
        for (int i = 0; i < 4; ++i) {
            polygon.vertex[i] = sortedVertex[i];
            polygon.bary[i] = sortedBary[i];
        }
    }
    return true;
}

} // namespace

bool TetrahedronBz::tetrahedronDeltaWeights(
    const std::array<Vec3, 4>& positions, const std::array<double, 4>& energies,
    double level, std::array<double, 4>& cornerWeights)
{
    SurfacePolygon polygon;
    if (!buildSurfacePolygon(positions, energies, level, polygon))
        return false;

    // Area, and the barycentric average over it. Fanned into triangles from
    // the first vertex; each contributes its area and the mean of its three
    // corners' barycentric coordinates, and the polygon average is the
    // area-weighted mean of those — exact, because those coordinates are
    // linear over the surface.
    double area = 0.0;
    std::array<double, 4> weighted{0.0, 0.0, 0.0, 0.0};
    for (int i = 1; i + 1 < polygon.count; ++i) {
        const double triangle = 0.5
            * norm(cross(polygon.vertex[i] - polygon.vertex[0],
                         polygon.vertex[i + 1] - polygon.vertex[0]));
        if (triangle <= 0.0)
            continue;
        area += triangle;
        for (int corner = 0; corner < 4; ++corner)
            weighted[corner] += triangle
                * (polygon.bary[0][corner] + polygon.bary[i][corner]
                   + polygon.bary[i + 1][corner])
                / 3.0;
    }
    if (area <= 0.0)
        return false;

    const double scale = 1.0 / polygon.slope;
    for (int corner = 0; corner < 4; ++corner)
        cornerWeights[corner] = weighted[corner] * scale;
    return true;
}

bool TetrahedronBz::tetrahedronDoubleDeltaWeights(
    const std::array<Vec3, 4>& positions,
    const std::array<double, 4>& energies1,
    const std::array<double, 4>& energies2, double level,
    std::array<double, 4>& cornerWeights)
{
    // The first band's surface, as a polygon.
    SurfacePolygon polygon;
    if (!buildSurfacePolygon(positions, energies1, level, polygon))
        return false;

    // The second band's gradient, for the measure below.
    Vec3 gradient2{};
    if (!solve3(positions[1] - positions[0], positions[2] - positions[0],
                positions[3] - positions[0],
                {energies2[1] - energies2[0], energies2[2] - energies2[0],
                 energies2[3] - energies2[0]},
                gradient2))
        return false;
    const Vec3 normalCross = cross(polygon.gradient, gradient2);
    const double crossNorm = norm(normalCross);
    // Parallel gradients mean the two constant-energy surfaces are parallel:
    // they either never meet or coincide, and in both cases there is no
    // one-dimensional intersection for this measure to describe. Nesting
    // between two bands that disperse identically is a genuine singularity,
    // not something to approximate with a huge number.
    if (crossNorm < 1e-20)
        return false;

    // Coincident surfaces first. When the two bands are identical over this
    // tetrahedron, eps2 - level equals eps1 - level, which is zero on the
    // whole polygon: the "intersection" is a 2D surface, not a line, and the
    // measure below does not describe it. Left to the edge walk it is worse
    // than useless — the interpolated values are zero to within rounding, so
    // their SIGNS are noise and the walk manufactures a segment out of it.
    // q = 0 with the same band is exactly this case.
    {
        double spread = 0.0;
        for (int c = 0; c < 4; ++c)
            spread = std::max(spread, std::abs(energies2[c] - energies1[c]));
        double scale = 0.0;
        for (int c = 0; c < 4; ++c)
            scale = std::max(scale, std::abs(energies1[c] - level));
        if (spread <= 1e-12 * std::max(scale, 1.0))
            return false;
    }

    // Where the SECOND band crosses the level along the first surface's
    // boundary. eps2 is linear, so on a convex polygon it changes sign on
    // exactly two edges — those two points are the ends of the segment.
    Vec3 ends[2];
    std::array<double, 4> endBary[2];

    const auto walk = [&](double level2) {
        int found = 0;
        for (int i = 0; i < polygon.count && found < 2; ++i) {
            const int j = (i + 1) % polygon.count;
            // eps2 interpolated onto each polygon vertex by its barycentric
            // coordinates — the same interpolation the corner weights use, so
            // the two cannot disagree about where the surface is.
            double vi = 0.0;
            double vj = 0.0;
            for (int c = 0; c < 4; ++c) {
                vi += polygon.bary[i][c] * energies2[c];
                vj += polygon.bary[j][c] * energies2[c];
            }
            const double di = vi - level2;
            const double dj = vj - level2;
            if (di * dj >= 0.0)
                continue;
            const double t = di / (di - dj);
            ends[found] = polygon.vertex[i]
                + (polygon.vertex[j] - polygon.vertex[i]) * t;
            for (int c = 0; c < 4; ++c)
                endBary[found][c] = polygon.bary[i][c]
                    + (polygon.bary[j][c] - polygon.bary[i][c]) * t;
            ++found;
        }
        return found == 2;
    };

    if (!walk(level)) {
        // THE DEGENERATE CASE, and it is the common one rather than an edge
        // case. On the surface eps1 = level the second band satisfies
        // eps2 - level = eps2 - eps1, whose zero set is a plane. When q is a
        // grid vector — which it always is, since k+q has to be a sampled
        // state — that plane can fall exactly ON a grid plane, i.e. on
        // tetrahedron FACES. The intersection line then never enters any
        // tetrahedron's interior and a strict sign test finds nothing at all.
        //
        // Measured on free electrons at 32^3: a q of an EVEN number of grid
        // steps put the plane at k_x = -q/2 exactly on a lattice plane, and
        // of 3400 tetrahedra bracketing the level in both bands, 2 produced
        // a segment. The nesting function came out ~0 instead of its
        // closed-form value.
        //
        // Resolved by nudging the second level by a hair. The nesting
        // function is smooth in the level, so the answer changes by O(eps);
        // geometrically the plane moves off the face into exactly one of the
        // two tetrahedra sharing it, so the segment is counted once rather
        // than zero times or twice. Applied ONLY on failure, so a
        // non-degenerate tetrahedron is untouched, and scaled to this
        // tetrahedron's own energy span so it is dimensionless in effect.
        const auto [lo, hi] = std::minmax_element(energies2.begin(),
                                                  energies2.end());
        const double span = *hi - *lo;
        const double nudge = 1e-6 * (span > 0.0 ? span : 1.0);
        if (!walk(level + nudge))
            return false;
    }

    const double length = norm(ends[1] - ends[0]);
    if (length <= 0.0)
        return false;

    // d3k = du dv dw / |grad1 x grad2|, so the segment's length divided by
    // that cross product IS the integral. The integrand is linear along the
    // segment, so its average is the mean of the two endpoints.
    const double scale = length / crossNorm;
    for (int corner = 0; corner < 4; ++corner)
        cornerWeights[corner] =
            scale * 0.5 * (endBary[0][corner] + endBary[1][corner]);
    return true;
}

TetrahedronBz::TetrahedronBz(
    std::array<int, 3> grid,
    const std::array<std::array<double, 3>, 3>& reciprocal)
    : grid_(grid)
{
    for (int axis = 0; axis < 3; ++axis)
        grid_[axis] = std::max(1, grid_[axis]);
    pointCount_ = static_cast<std::size_t>(grid_[0])
        * static_cast<std::size_t>(grid_[1]) * static_cast<std::size_t>(grid_[2]);

    const Vec3 b0{reciprocal[0][0], reciprocal[0][1], reciprocal[0][2]};
    const Vec3 b1{reciprocal[1][0], reciprocal[1][1], reciprocal[1][2]};
    const Vec3 b2{reciprocal[2][0], reciprocal[2][1], reciprocal[2][2]};
    bzVolume_ = std::abs(dot(b0, cross(b1, b2)));

    // One microcell's edge vectors.
    const Vec3 e0 = b0 * (1.0 / grid_[0]);
    const Vec3 e1 = b1 * (1.0 / grid_[1]);
    const Vec3 e2 = b2 * (1.0 / grid_[2]);

    // The eight corners of a microcell, indexed by the bits of (a, b, c).
    Vec3 corner[8];
    for (int c = 0; c < 8; ++c)
        corner[c] = e0 * static_cast<double>(c & 1)
            + e1 * static_cast<double>((c >> 1) & 1)
            + e2 * static_cast<double>((c >> 2) & 1);

    // Six tetrahedra sharing the 0-7 body diagonal. This is the standard
    // decomposition; every microcell uses the same one, so the choice of
    // diagonal is consistent across the grid and the tetrahedra tile the zone
    // without gaps or overlaps.
    static constexpr int kSplit[6][4] = {{0, 1, 3, 7}, {0, 1, 5, 7},
                                         {0, 2, 3, 7}, {0, 2, 6, 7},
                                         {0, 4, 5, 7}, {0, 4, 6, 7}};
    for (int t = 0; t < 6; ++t)
        for (int v = 0; v < 4; ++v)
            shapes_[t][v] = corner[kSplit[t][v]];

    // Every microcell, each cut the same way. The corner INDICES wrap around
    // the zone; the corner POSITIONS above do not, which is what keeps the
    // geometry of a boundary microcell correct.
    tetrahedra_.reserve(pointCount_ * 6);
    for (int i0 = 0; i0 < grid_[0]; ++i0)
        for (int i1 = 0; i1 < grid_[1]; ++i1)
            for (int i2 = 0; i2 < grid_[2]; ++i2)
                for (int t = 0; t < 6; ++t) {
                    std::array<std::size_t, 4> quad{};
                    for (int v = 0; v < 4; ++v) {
                        const int c = kSplit[t][v];
                        quad[v] = index(i0 + (c & 1), i1 + ((c >> 1) & 1),
                                        i2 + ((c >> 2) & 1));
                    }
                    tetrahedra_.push_back(quad);
                }
}

std::size_t TetrahedronBz::index(int i1, int i2, int i3) const
{
    const auto wrap = [](int i, int n) {
        const int m = i % n;
        return m < 0 ? m + n : m;
    };
    const int a = wrap(i1, grid_[0]);
    const int b = wrap(i2, grid_[1]);
    const int c = wrap(i3, grid_[2]);
    return (static_cast<std::size_t>(a) * grid_[1] + b) * grid_[2] + c;
}

void TetrahedronBz::accumulateDeltaWeights(const std::vector<double>& energies,
                                           double level,
                                           std::vector<double>& weights) const
{
    if (energies.size() < pointCount_ || weights.size() < pointCount_)
        return;
    const double inverseVolume = 1.0 / bzVolume_;
    std::array<double, 4> corners{};
    for (std::size_t t = 0; t < tetrahedra_.size(); ++t) {
        const auto& quad = tetrahedra_[t];
        const std::array<double, 4> e{energies[quad[0]], energies[quad[1]],
                                      energies[quad[2]], energies[quad[3]]};
        // Cheap rejection before any geometry: most tetrahedra do not touch
        // the level at all, and on a fine grid this is the difference between
        // a usable routine and an unusable one.
        const auto [lo, hi] = std::minmax_element(e.begin(), e.end());
        if (level < *lo || level > *hi)
            continue;
        if (!tetrahedronDeltaWeights(shapes_[t % 6], e, level, corners))
            continue;
        for (int v = 0; v < 4; ++v)
            weights[quad[v]] += corners[v] * inverseVolume;
    }
}

void TetrahedronBz::accumulateDoubleDeltaWeights(
    const std::vector<double>& energies1, const std::vector<double>& energies2,
    double level, std::vector<double>& weights) const
{
    if (energies1.size() < pointCount_ || energies2.size() < pointCount_
        || weights.size() < pointCount_)
        return;
    const double inverseVolume = 1.0 / bzVolume_;
    std::array<double, 4> corners{};
    for (std::size_t t = 0; t < tetrahedra_.size(); ++t) {
        const auto& quad = tetrahedra_[t];
        const std::array<double, 4> e1{energies1[quad[0]], energies1[quad[1]],
                                       energies1[quad[2]], energies1[quad[3]]};
        // BOTH bands must bracket the level, and rejecting on that first is
        // what keeps this affordable: on a real mesh the overwhelming
        // majority of tetrahedra fail one or the other.
        const auto [lo1, hi1] = std::minmax_element(e1.begin(), e1.end());
        if (level < *lo1 || level > *hi1)
            continue;
        const std::array<double, 4> e2{energies2[quad[0]], energies2[quad[1]],
                                       energies2[quad[2]], energies2[quad[3]]};
        const auto [lo2, hi2] = std::minmax_element(e2.begin(), e2.end());
        if (level < *lo2 || level > *hi2)
            continue;
        if (!tetrahedronDoubleDeltaWeights(shapes_[t % 6], e1, e2, level,
                                           corners))
            continue;
        for (int v = 0; v < 4; ++v)
            weights[quad[v]] += corners[v] * inverseVolume;
    }
}

double TetrahedronBz::nesting(const std::vector<double>& energies1,
                              const std::vector<double>& energies2,
                              double level) const
{
    std::vector<double> weights(pointCount_, 0.0);
    accumulateDoubleDeltaWeights(energies1, energies2, level, weights);
    double total = 0.0;
    for (const double w : weights)
        total += w;
    return total;
}

double TetrahedronBz::dos(const std::vector<double>& energies,
                          double level) const
{
    std::vector<double> weights(pointCount_, 0.0);
    accumulateDeltaWeights(energies, level, weights);
    double total = 0.0;
    for (const double w : weights)
        total += w;
    return total;
}

} // namespace calango::core
