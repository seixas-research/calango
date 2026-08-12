#include "core/PhaseDiagram.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>

namespace calango::core {

namespace {

struct SampledPoint {
    double x = 0.0;
    double g = 0.0;
    int phase = -1;
};

/// Cross product of (o->a) and (o->b) in the (x, G) plane.
double cross2(const SampledPoint& o, const SampledPoint& a,
              const SampledPoint& b)
{
    return (a.x - o.x) * (b.g - o.g) - (a.g - o.g) * (b.x - o.x);
}

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec3 operator-(const Vec3& a, const Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 crossProduct(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

double dotProduct(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

struct HullFace {
    int a = 0;
    int b = 0;
    int c = 0;
    Vec3 normal;
    double offset = 0.0; ///< dot(normal, P[a])
    bool dead = false;
};

/// 2D convex hull (monotone chain, full loop) of the projected points, used
/// for the coplanar fallback.
std::vector<int> convexHull2d(const std::vector<Vec3>& points)
{
    std::vector<int> order(points.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&points](int a, int b) {
        if (points[static_cast<std::size_t>(a)].x
            != points[static_cast<std::size_t>(b)].x)
            return points[static_cast<std::size_t>(a)].x
                < points[static_cast<std::size_t>(b)].x;
        return points[static_cast<std::size_t>(a)].y
            < points[static_cast<std::size_t>(b)].y;
    });
    const auto turn = [&points](int o, int a, int b) {
        const Vec3& po = points[static_cast<std::size_t>(o)];
        const Vec3& pa = points[static_cast<std::size_t>(a)];
        const Vec3& pb = points[static_cast<std::size_t>(b)];
        return (pa.x - po.x) * (pb.y - po.y) - (pa.y - po.y) * (pb.x - po.x);
    };
    std::vector<int> hull;
    for (int pass = 0; pass < 2; ++pass) {
        // The lower chain needs two points before it can test a turn; the
        // upper chain must not eat into the lower one, so its floor is one
        // above whatever the lower chain left. Writing this as `hull.size()+1`
        // for BOTH passes reads plausibly and indexes hull[-1] on the second
        // point of the first pass — which is silent memory corruption, not a
        // crash, and it only shows up on the degenerate inputs that reach the
        // coplanar path.
        const std::size_t base = pass == 0 ? 2 : hull.size() + 1;
        for (int index : order) {
            while (hull.size() >= base
                   && turn(hull[hull.size() - 2], hull.back(), index) <= 0.0)
                hull.pop_back();
            hull.push_back(index);
        }
        hull.pop_back();
        std::reverse(order.begin(), order.end());
    }
    return hull;
}

} // namespace

std::vector<std::array<int, 3>>
lowerConvexHull3d(const std::vector<double>& x, const std::vector<double>& y,
                  const std::vector<double>& z)
{
    const std::size_t n = std::min({x.size(), y.size(), z.size()});
    std::vector<Vec3> points;
    points.reserve(n);
    std::vector<int> original;
    original.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]) || !std::isfinite(z[i]))
            continue;
        points.push_back({x[i], y[i], z[i]});
        original.push_back(static_cast<int>(i));
    }
    if (points.size() < 3)
        return {};

    // Scale of the point cloud, for a relative epsilon. An absolute one would
    // be meaningless: the caller has already normalized z, but x and y are
    // whatever coordinates it chose.
    double extent = 0.0;
    for (const Vec3& p : points) {
        extent = std::max({extent, std::fabs(p.x), std::fabs(p.y),
                           std::fabs(p.z)});
    }
    if (extent <= 0.0)
        extent = 1.0;
    const double eps = 1e-9 * extent;

    // --- The initial tetrahedron ------------------------------------------
    // Four points that are as non-degenerate as this cloud allows: the two
    // most distant, the point furthest from their line, the point furthest
    // from their plane. Picking the first four in order instead is the classic
    // way to get a hull that is silently wrong on a regular grid, where the
    // first four points are always collinear or coplanar.
    int i0 = 0;
    int i1 = -1;
    double best = eps;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Vec3 d = points[i] - points[0];
        const double distance = std::sqrt(dotProduct(d, d));
        if (distance > best) {
            best = distance;
            i1 = static_cast<int>(i);
        }
    }
    if (i1 < 0)
        return {}; // every point coincides

    int i2 = -1;
    best = eps;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vec3 area = crossProduct(
            points[static_cast<std::size_t>(i1)] - points[static_cast<std::size_t>(i0)],
            points[i] - points[static_cast<std::size_t>(i0)]);
        const double magnitude = std::sqrt(dotProduct(area, area));
        if (magnitude > best) {
            best = magnitude;
            i2 = static_cast<int>(i);
        }
    }
    if (i2 < 0)
        return {}; // all collinear: no surface at all

    const Vec3 baseNormal = crossProduct(
        points[static_cast<std::size_t>(i1)] - points[static_cast<std::size_t>(i0)],
        points[static_cast<std::size_t>(i2)] - points[static_cast<std::size_t>(i0)]);
    const double baseOffset =
        dotProduct(baseNormal, points[static_cast<std::size_t>(i0)]);
    int i3 = -1;
    best = eps * std::sqrt(dotProduct(baseNormal, baseNormal));
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double distance =
            std::fabs(dotProduct(baseNormal, points[i]) - baseOffset);
        if (distance > best) {
            best = distance;
            i3 = static_cast<int>(i);
        }
    }

    std::vector<std::array<int, 3>> result;
    if (i3 < 0) {
        // COPLANAR. Not a failure: three pure components with no solution
        // phase put every point on one plane, and the answer — a single
        // three-phase triangle spanning the diagram — is the physically
        // correct one. Fan-triangulate the 2D hull of the projection.
        const std::vector<int> loop = convexHull2d(points);
        if (loop.size() < 3)
            return {};
        for (std::size_t k = 1; k + 1 < loop.size(); ++k) {
            result.push_back({original[static_cast<std::size_t>(loop[0])],
                              original[static_cast<std::size_t>(loop[k])],
                              original[static_cast<std::size_t>(loop[k + 1])]});
        }
        return result;
    }

    // --- Incremental hull --------------------------------------------------
    std::vector<HullFace> faces;
    const Vec3 interior{
        (points[static_cast<std::size_t>(i0)].x + points[static_cast<std::size_t>(i1)].x
         + points[static_cast<std::size_t>(i2)].x + points[static_cast<std::size_t>(i3)].x) / 4.0,
        (points[static_cast<std::size_t>(i0)].y + points[static_cast<std::size_t>(i1)].y
         + points[static_cast<std::size_t>(i2)].y + points[static_cast<std::size_t>(i3)].y) / 4.0,
        (points[static_cast<std::size_t>(i0)].z + points[static_cast<std::size_t>(i1)].z
         + points[static_cast<std::size_t>(i2)].z + points[static_cast<std::size_t>(i3)].z) / 4.0};

    const auto makeFace = [&points](int a, int b, int c) {
        HullFace face;
        face.a = a;
        face.b = b;
        face.c = c;
        face.normal = crossProduct(
            points[static_cast<std::size_t>(b)] - points[static_cast<std::size_t>(a)],
            points[static_cast<std::size_t>(c)] - points[static_cast<std::size_t>(a)]);
        face.offset = dotProduct(face.normal, points[static_cast<std::size_t>(a)]);
        return face;
    };
    const auto pushOriented = [&](int a, int b, int c) {
        HullFace face = makeFace(a, b, c);
        // Orient outward: the interior point must be strictly behind.
        if (dotProduct(face.normal, interior) - face.offset > 0.0)
            face = makeFace(a, c, b);
        faces.push_back(face);
    };
    pushOriented(i0, i1, i2);
    pushOriented(i0, i1, i3);
    pushOriented(i0, i2, i3);
    pushOriented(i1, i2, i3);

    for (std::size_t p = 0; p < points.size(); ++p) {
        const int index = static_cast<int>(p);
        if (index == i0 || index == i1 || index == i2 || index == i3)
            continue;
        // Visible faces. The tolerance is relative to the face's own normal
        // magnitude, so a long thin triangle is not judged by the same
        // absolute threshold as a large one.
        bool any = false;
        for (HullFace& face : faces) {
            if (face.dead)
                continue;
            const double scale = std::sqrt(dotProduct(face.normal, face.normal));
            if (dotProduct(face.normal, points[p]) - face.offset
                > eps * (scale > 0.0 ? scale : 1.0)) {
                face.dead = true;
                any = true;
            }
        }
        if (!any)
            continue; // inside the hull so far

        // Horizon: a directed edge of a dead face whose reverse is not also on
        // a dead face. Directed-edge bookkeeping is what keeps the new faces
        // consistently oriented without re-deriving "outward" from a point
        // that may by now be on the surface.
        std::set<std::pair<int, int>> deadEdges;
        for (const HullFace& face : faces) {
            if (!face.dead)
                continue;
            deadEdges.insert({face.a, face.b});
            deadEdges.insert({face.b, face.c});
            deadEdges.insert({face.c, face.a});
        }
        std::vector<std::pair<int, int>> horizon;
        for (const auto& edge : deadEdges)
            if (deadEdges.find({edge.second, edge.first}) == deadEdges.end())
                horizon.push_back(edge);

        faces.erase(std::remove_if(faces.begin(), faces.end(),
                                   [](const HullFace& f) { return f.dead; }),
                    faces.end());
        for (const auto& edge : horizon)
            faces.push_back(makeFace(edge.first, edge.second, index));
    }

    // --- Keep the lower half ----------------------------------------------
    // A face belongs to the lower hull when its OUTWARD normal points down.
    // Vertical faces (n_z == 0) belong to neither half and are dropped: they
    // are the side walls of the hull, not part of the Gibbs surface.
    for (const HullFace& face : faces) {
        const double scale = std::sqrt(dotProduct(face.normal, face.normal));
        if (scale <= 0.0)
            continue;
        if (face.normal.z / scale >= -1e-9)
            continue;
        result.push_back({original[static_cast<std::size_t>(face.a)],
                          original[static_cast<std::size_t>(face.b)],
                          original[static_cast<std::size_t>(face.c)]});
    }
    return result;
}

BinarySection computeBinarySection(const std::vector<GibbsPhase>& phases,
                                   double temperatureK, int compositionSteps)
{
    BinarySection section;
    section.temperatureK = temperatureK;
    const int steps = std::max(2, compositionSteps);
    const double stride = 1.0 / static_cast<double>(steps - 1);

    // Every phase is sampled on the SAME global composition grid, restricted
    // to its own range, plus its exact end points. A common grid is what makes
    // "a gap wider than one grid step" a usable test for a two-phase field
    // further down: with per-phase grids the spacing would differ and the test
    // would have to be per-pair.
    std::vector<SampledPoint> points;
    for (std::size_t p = 0; p < phases.size(); ++p) {
        const GibbsPhase& phase = phases[p];
        if (!phase.gibbs)
            continue;
        const double lo = std::clamp(std::min(phase.minMoleFraction,
                                              phase.maxMoleFraction), 0.0, 1.0);
        const double hi = std::clamp(std::max(phase.minMoleFraction,
                                              phase.maxMoleFraction), 0.0, 1.0);
        std::vector<double> compositions;
        if (hi - lo < stride * 0.5) {
            compositions.push_back(0.5 * (lo + hi)); // stoichiometric
        } else {
            compositions.push_back(lo);
            for (int i = 0; i < steps; ++i) {
                const double x = static_cast<double>(i) * stride;
                if (x > lo && x < hi)
                    compositions.push_back(x);
            }
            compositions.push_back(hi);
        }
        for (const double x : compositions) {
            const double g = phase.gibbs(x, temperatureK);
            if (!std::isfinite(g))
                continue;
            points.push_back({x, g, static_cast<int>(p)});
        }
    }
    if (points.empty())
        return section;

    std::sort(points.begin(), points.end(),
              [](const SampledPoint& a, const SampledPoint& b) {
                  if (a.x != b.x)
                      return a.x < b.x;
                  return a.g < b.g;
              });

    // Andrew's monotone chain, lower half, popping collinear points — see the
    // header for why this differs from core::computeConvexHull.
    std::vector<SampledPoint> chain;
    for (const SampledPoint& point : points) {
        // At one composition only the lowest-energy phase can be a vertex, and
        // the sort has already put it first.
        if (!chain.empty() && chain.back().x == point.x)
            continue;
        while (chain.size() >= 2
               && cross2(chain[chain.size() - 2], chain.back(), point) <= 0.0)
            chain.pop_back();
        chain.push_back(point);
    }

    section.vertexX.reserve(chain.size());
    section.vertexPhase.reserve(chain.size());
    for (const SampledPoint& vertex : chain) {
        section.vertexX.push_back(vertex.x);
        section.vertexPhase.push_back(vertex.phase);
    }

    // A two-phase field is EITHER a wide gap in the hull OR a change of phase
    // between adjacent vertices, and both tests are needed:
    //
    //  - Width alone misses a lens narrower than the composition grid. Those
    //    are not exotic: in the published Nb-Re assessment the bcc/liquid lens
    //    is nearly degenerate — at 2750 K the liquid is more stable than bcc
    //    at BOTH pure ends and the two phases' excess terms cancel — so the
    //    whole Nb-rich side appeared to melt congruently at a single
    //    temperature, which is not what the database says.
    //  - A phase change alone misses every miscibility gap, where one phase
    //    sits at both ends of the tie-line.
    //
    // Two DIFFERENT phases at adjacent hull vertices are always in
    // equilibrium, however close together they are; only a congruent point is
    // an exception, and that is a single point, not an interval.
    const double gapThreshold = stride * 1.5;
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        const bool wide = chain[i + 1].x - chain[i].x > gapThreshold;
        const bool changesPhase = chain[i].phase != chain[i + 1].phase;
        if (!wide && !changesPhase)
            continue;
        BinaryTieLine tie;
        tie.xLeft = chain[i].x;
        tie.xRight = chain[i + 1].x;
        tie.leftPhase = chain[i].phase;
        tie.rightPhase = chain[i + 1].phase;
        section.tieLines.push_back(tie);
    }
    return section;
}

BinaryPhaseDiagram
computeBinaryPhaseDiagram(const std::vector<GibbsPhase>& phases,
                          const BinaryPhaseDiagramOptions& options)
{
    BinaryPhaseDiagram diagram;
    for (const GibbsPhase& phase : phases)
        diagram.phaseNames.push_back(phase.name);

    const int steps = std::max(1, options.temperatureSteps);
    for (int i = 0; i < steps; ++i) {
        const double fraction = steps == 1
            ? 0.0
            : static_cast<double>(i) / static_cast<double>(steps - 1);
        const double temperature = options.minTemperatureK
            + fraction * (options.maxTemperatureK - options.minTemperatureK);
        diagram.sections.push_back(
            computeBinarySection(phases, temperature, options.compositionSteps));
    }
    return diagram;
}

std::vector<int> binaryAssemblageAt(const BinarySection& section, double x)
{
    for (const BinaryTieLine& tie : section.tieLines) {
        if (x > tie.xLeft && x < tie.xRight) {
            if (tie.leftPhase == tie.rightPhase)
                return {tie.leftPhase};
            return {tie.leftPhase, tie.rightPhase};
        }
    }
    // Single-phase: the vertex whose composition is nearest.
    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < section.vertexX.size(); ++i) {
        const double distance = std::fabs(section.vertexX[i] - x);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = section.vertexPhase[i];
        }
    }
    if (best < 0)
        return {};
    return {best};
}

TernaryIsothermalSection
computeTernaryIsothermalSection(const std::vector<TernaryGibbsPhase>& phases,
                                const TernarySectionOptions& options)
{
    TernaryIsothermalSection section;
    section.temperatureK = options.temperatureK;
    for (const TernaryGibbsPhase& phase : phases)
        section.phaseNames.push_back(phase.name);
    if (phases.empty()) {
        section.note = "No phases were supplied.";
        return section;
    }

    const int steps = std::max(2, options.gridSteps);
    for (std::size_t p = 0; p < phases.size(); ++p) {
        if (!phases[p].gibbs)
            continue;
        for (int i = 0; i <= steps; ++i) {
            for (int j = 0; i + j <= steps; ++j) {
                const double xB = static_cast<double>(i) / steps;
                const double xC = static_cast<double>(j) / steps;
                const double g = phases[p].gibbs(xB, xC, options.temperatureK);
                if (!std::isfinite(g))
                    continue;
                section.points.push_back(
                    {xB, xC, g, static_cast<int>(p)});
            }
        }
    }
    if (section.points.size() < 3) {
        section.note = "The phases produced no usable Gibbs energies on the "
                       "composition grid.";
        return section;
    }

    // --- Conditioning: subtract the corner plane, scale to unit range -------
    // Both steps preserve the lower hull exactly (see the header). Without
    // them the hull is built on coordinates spanning [0,1] in two axes and
    // ~10^4 in the third, and the visibility test degenerates into "every face
    // is visible from every point".
    double cornerA = 0.0;
    double cornerB = 0.0;
    double cornerC = 0.0;
    bool haveA = false;
    bool haveB = false;
    bool haveC = false;
    for (const TernaryPoint& point : section.points) {
        if (point.xB == 0.0 && point.xC == 0.0
            && (!haveA || point.gibbsJPerMol < cornerA)) {
            cornerA = point.gibbsJPerMol;
            haveA = true;
        }
        if (point.xB == 1.0 && (!haveB || point.gibbsJPerMol < cornerB)) {
            cornerB = point.gibbsJPerMol;
            haveB = true;
        }
        if (point.xC == 1.0 && (!haveC || point.gibbsJPerMol < cornerC)) {
            cornerC = point.gibbsJPerMol;
            haveC = true;
        }
    }
    std::vector<double> xs;
    std::vector<double> ys;
    std::vector<double> zs;
    xs.reserve(section.points.size());
    ys.reserve(section.points.size());
    zs.reserve(section.points.size());
    double span = 0.0;
    for (const TernaryPoint& point : section.points) {
        const double reference = cornerA + point.xB * (cornerB - cornerA)
            + point.xC * (cornerC - cornerA);
        span = std::max(span, std::fabs(point.gibbsJPerMol - reference));
    }
    if (span <= 0.0)
        span = 1.0;
    for (const TernaryPoint& point : section.points) {
        const double reference = cornerA + point.xB * (cornerB - cornerA)
            + point.xC * (cornerC - cornerA);
        xs.push_back(point.xB);
        ys.push_back(point.xC);
        zs.push_back((point.gibbsJPerMol - reference) / span);
    }

    for (const std::array<int, 3>& triangle : lowerConvexHull3d(xs, ys, zs)) {
        TernaryFacet facet;
        facet.vertex[0] = triangle[0];
        facet.vertex[1] = triangle[1];
        facet.vertex[2] = triangle[2];
        section.facets.push_back(facet);
    }
    if (section.facets.empty()) {
        section.note = "The Gibbs surface produced no lower hull; every "
                       "sampled point is collinear.";
        return section;
    }
    section.ok = true;
    return section;
}

} // namespace calango::core
