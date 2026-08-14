// Isosurface mesh integrity.
//
// THE BUG THIS EXISTS FOR. extractIsosurface() emitted complementary
// marching-tetrahedra cases with the SAME vertex order. Complements share the
// surface geometry but have opposite orientation, so roughly half the mesh was
// wound backwards. Nothing looked wrong until a renderer trusted the winding:
// isosurface.frag flips its normal on gl_FrontFacing, which turned those
// (correct, gradient-derived) normals inward, zeroed the diffuse term and the
// specular gated on it, and dropped the facets to the ambient floor — a dense
// speckle of dark pits that read as holes under the glossy material.
//
// The check below is the one that catches it: every triangle's GEOMETRIC
// normal (from the winding) must agree in sign with its VERTEX normals (from
// the field gradient). Those are two independent sources of the same
// direction, so agreement is a real constraint and not a tautology.

// The second half of this file covers PERIODIC CONTINUATION. A Wannier
// function's centre lands wherever the wannierization put it, so its lobe
// routinely straddles a cell face; extracting over the home cell alone cuts it
// flat there and drops the remainder on the opposite side of the box. Those
// tests build a Gaussian centred exactly ON a corner — split eight ways by the
// naive extraction — and assert the continued surface against the closed form
// of a Gaussian's level set, which is a sphere of known radius.

#include "core/IsosurfaceContinuation.hpp"
#include "core/MarchingCubes.hpp"
#include "core/VolumetricData.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using calango::core::Vec3;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

void checkClose(double got, double want, double tol, const std::string& what)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  %-4s %s (got %.6g, want %.6g, tol %g)\n", ok ? "ok" : "FAIL",
                what.c_str(), got, want, tol);
    if (!ok)
        ++failures;
}

/// A sphere of radius r centred in a cubic box, sampled as f = r - |x - c|.
/// Positive inside, so the isosurface at 0 is the sphere and the OUTWARD
/// direction is that of decreasing f — the same convention the extractor's
/// gradient normal uses.
calango::core::VolumetricData sphereField(int n, double boxLength, double radius)
{
    calango::core::VolumetricData field;
    field.nx = field.ny = field.nz = n;
    field.origin = {0.0, 0.0, 0.0};
    field.spanA = Vec3{boxLength, 0.0, 0.0};
    field.spanB = Vec3{0.0, boxLength, 0.0};
    field.spanC = Vec3{0.0, 0.0, boxLength};
    field.values.resize(static_cast<std::size_t>(n) * n * n);
    const double centre = 0.5 * boxLength;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            for (int k = 0; k < n; ++k) {
                const double x = boxLength * i / n - centre;
                const double y = boxLength * j / n - centre;
                const double z = boxLength * k / n - centre;
                const double r = std::sqrt(x * x + y * y + z * z);
                field.values[static_cast<std::size_t>((i * n + j) * n + k)] =
                    radius - r;
            }
    return field;
}

struct WindingReport {
    int triangles = 0;
    int agreeing = 0;
    int disagreeing = 0;
    int degenerate = 0;
};

WindingReport inspect(const calango::core::IsoMesh& mesh)
{
    WindingReport report;
    for (std::size_t t = 0; t + 2 < mesh.positions.size(); t += 3) {
        const Vec3& a = mesh.positions[t];
        const Vec3& b = mesh.positions[t + 1];
        const Vec3& c = mesh.positions[t + 2];
        const Vec3 geometric = (b - a).cross(c - a);
        const double area = geometric.norm();
        if (area < 1e-12) {
            ++report.degenerate;
            continue;
        }
        // Average of the three gradient normals: the direction the surface
        // faces, computed with no reference to the winding at all.
        Vec3 vertex = mesh.normals[t] + mesh.normals[t + 1] + mesh.normals[t + 2];
        const double vn = vertex.norm();
        if (vn < 1e-12) {
            ++report.degenerate;
            continue;
        }
        ++report.triangles;
        if (geometric.dot(vertex) > 0.0)
            ++report.agreeing;
        else
            ++report.disagreeing;
    }
    return report;
}

void testWindingIsConsistent()
{
    std::printf("Triangle winding agrees with the gradient normals:\n");
    const auto field = sphereField(48, 10.0, 3.0);
    const auto mesh = calango::core::extractIsosurface(field, 0.0);

    check(!mesh.positions.empty(), "the sphere produced a mesh");
    check(mesh.positions.size() == mesh.normals.size(),
          "every vertex carries a normal");
    check(mesh.positions.size() % 3 == 0, "and the mesh is whole triangles");

    const WindingReport report = inspect(mesh);
    std::printf("       %d triangles: %d agree, %d disagree, %d degenerate\n",
                report.triangles, report.agreeing, report.disagreeing,
                report.degenerate);
    // ALL of them, not most: a single reversed triangle is a visible pit under
    // the glossy material, and before the fix roughly half the mesh reversed.
    check(report.disagreeing == 0,
          "no triangle is wound against its own normal");
    check(report.triangles > 500, "on a mesh large enough for that to mean "
                                  "something");
}

void testNormalsAreUnitLength()
{
    std::printf("Normals are usable as-is:\n");
    const auto field = sphereField(40, 10.0, 3.0);
    const auto mesh = calango::core::extractIsosurface(field, 0.0);
    double worst = 0.0;
    for (const Vec3& n : mesh.normals)
        worst = std::max(worst, std::abs(n.norm() - 1.0));
    checkClose(worst, 0.0, 1e-9,
               "every normal is unit length, so the shader need not renormalise");
}

void testNormalsPointOutward()
{
    std::printf("Normals point away from the enclosed region:\n");
    // For f = r - |x - c| the interior is positive, so the outward direction
    // is that of DECREASING f. A sphere makes that checkable against geometry:
    // the outward normal is the radial direction.
    const double box = 10.0;
    const double radius = 3.0;
    const auto field = sphereField(48, box, radius);
    const auto mesh = calango::core::extractIsosurface(field, 0.0);
    const Vec3 centre{0.5 * box, 0.5 * box, 0.5 * box};

    int outward = 0;
    int inward = 0;
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec3 radial = mesh.positions[i] - centre;
        if (radial.norm() < 1e-9)
            continue;
        if (mesh.normals[i].dot(radial) > 0.0)
            ++outward;
        else
            ++inward;
    }
    std::printf("       %d outward, %d inward\n", outward, inward);
    check(inward == 0, "every normal points away from the sphere centre");
}

void testGeometryIsTheSphere()
{
    std::printf("The extracted surface is where it should be:\n");
    const double box = 10.0;
    const double radius = 3.0;
    const auto field = sphereField(64, box, radius);
    const auto mesh = calango::core::extractIsosurface(field, 0.0);
    const Vec3 centre{0.5 * box, 0.5 * box, 0.5 * box};

    double worst = 0.0;
    for (const Vec3& p : mesh.positions)
        worst = std::max(worst, std::abs((p - centre).norm() - radius));
    // One grid spacing is 10/64 = 0.156 A; linear interpolation along the
    // edges puts every vertex well inside that.
    checkClose(worst, 0.0, 0.05,
               "every vertex lies on the sphere to within the grid spacing");
}

void testDegenerateTrianglesAreRare()
{
    std::printf("Degenerate triangles:\n");
    const auto field = sphereField(48, 10.0, 3.0);
    const auto mesh = calango::core::extractIsosurface(field, 0.0);
    const WindingReport report = inspect(mesh);
    // Marching tetrahedra produces slivers where the surface clips a corner;
    // they are harmless (zero area contributes no pixels) but a large fraction
    // would mean the interpolation is collapsing edges.
    const double fraction = static_cast<double>(report.degenerate)
        / std::max(1, report.triangles + report.degenerate);
    check(fraction < 0.05,
          "fewer than 5% of triangles are degenerate slivers");
    std::printf("       %.2f%% degenerate\n", 100.0 * fraction);
}

// ---------------------------------------------------------------------------
// Periodic continuation
// ---------------------------------------------------------------------------

constexpr double kBox = 10.0;   ///< cubic cell edge, A
constexpr int kGrid = 64;       ///< nodes per axis
constexpr double kSigma = 0.8;  ///< Gaussian width, A
constexpr double kIso = 0.2;    ///< isovalue, as a fraction of the peak (1.0)

/// Radius of the level set f = kIso of a unit-amplitude Gaussian. Closed form:
/// exp(-r^2 / 2 sigma^2) = v  =>  r = sigma * sqrt(2 ln(1/v)).
double analyticRadius()
{
    return kSigma * std::sqrt(2.0 * std::log(1.0 / kIso));
}

/// A unit-amplitude Gaussian centred at the fractional position `frac`,
/// sampled on the PERIODIC grid — every image contributes, exactly as a real
/// Wannier cube's does.
calango::core::VolumetricData periodicGaussian(const Vec3& frac)
{
    calango::core::VolumetricData field;
    field.nx = field.ny = field.nz = kGrid;
    field.origin = {0.0, 0.0, 0.0};
    field.spanA = Vec3{kBox, 0.0, 0.0};
    field.spanB = Vec3{0.0, kBox, 0.0};
    field.spanC = Vec3{0.0, 0.0, kBox};
    field.values.resize(static_cast<std::size_t>(kGrid) * kGrid * kGrid);
    const double centre[3] = {frac.x * kBox, frac.y * kBox, frac.z * kBox};
    // Minimum image along each axis: what makes the sampled function genuinely
    // periodic rather than a Gaussian with a discontinuity at the far face.
    const auto delta = [](double x, double c) {
        double d = x - c;
        d -= kBox * std::round(d / kBox);
        return d;
    };
    for (int i = 0; i < kGrid; ++i)
        for (int j = 0; j < kGrid; ++j)
            for (int k = 0; k < kGrid; ++k) {
                const double dx = delta(kBox * i / kGrid, centre[0]);
                const double dy = delta(kBox * j / kGrid, centre[1]);
                const double dz = delta(kBox * k / kGrid, centre[2]);
                const double r2 = dx * dx + dy * dy + dz * dz;
                field.values[static_cast<std::size_t>((i * kGrid + j) * kGrid
                                                      + k)] =
                    std::exp(-r2 / (2.0 * kSigma * kSigma));
            }
    return field;
}

/// Number of connected components of a triangle soup, welding coincident
/// vertices first (the extractor emits no index buffer).
int componentCount(const calango::core::IsoMesh& mesh)
{
    const std::size_t triangles = mesh.positions.size() / 3;
    if (triangles == 0)
        return 0;
    const auto welded = calango::core::weldVertices(mesh);
    std::vector<int> parent(triangles);
    for (std::size_t i = 0; i < triangles; ++i)
        parent[i] = static_cast<int>(i);
    const std::function<int(int)> find = [&](int a) {
        while (parent[static_cast<std::size_t>(a)] != a) {
            parent[static_cast<std::size_t>(a)] =
                parent[static_cast<std::size_t>(
                    parent[static_cast<std::size_t>(a)])];
            a = parent[static_cast<std::size_t>(a)];
        }
        return a;
    };
    std::vector<int> owner(welded.points.size(), -1);
    for (std::size_t t = 0; t < triangles; ++t)
        for (std::size_t k = 0; k < 3; ++k) {
            const std::size_t p =
                static_cast<std::size_t>(welded.index[3 * t + k]);
            if (owner[p] < 0)
                owner[p] = static_cast<int>(t);
            else {
                const int a = find(static_cast<int>(t)), b = find(owner[p]);
                if (a != b)
                    parent[static_cast<std::size_t>(a)] = b;
            }
        }
    std::unordered_set<int> roots;
    for (std::size_t t = 0; t < triangles; ++t)
        roots.insert(find(static_cast<int>(t)));
    return static_cast<int>(roots.size());
}

void testPeriodicCentroidFindsASplitFunction()
{
    std::printf("The periodic centroid finds a function split by the "
                "boundary:\n");
    // Exactly on the corner: the ordinary centre of mass of this data is the
    // middle of the box, which is where the function is NOT.
    const auto field = periodicGaussian({0.0, 0.0, 0.0});
    const Vec3 c = calango::core::periodicCentroid(field);
    // Fold into [-L/2, L/2) before measuring: the corner is the same point as
    // (L, L, L), and either answer is right.
    const auto fold = [](double v) { return v - kBox * std::round(v / kBox); };
    const double d = Vec3{fold(c.x), fold(c.y), fold(c.z)}.norm();
    checkClose(d, 0.0, kBox / kGrid,
               "a Gaussian on the corner is located to within one grid step");

    const auto off = periodicGaussian({0.7, 0.2, 0.95});
    const Vec3 c2 = calango::core::periodicCentroid(off);
    const double d2 = Vec3{fold(c2.x - 7.0), fold(c2.y - 2.0), fold(c2.z - 9.5)}
                          .norm();
    checkClose(d2, 0.0, kBox / kGrid,
               "and so is one at an arbitrary fractional position");
}

void testWindowIsAnExactPeriodicContinuation()
{
    std::printf("The extended field is the periodic continuation, exactly:\n");
    const auto field = periodicGaussian({0.0, 0.0, 0.0});
    const Vec3 centre = calango::core::periodicCentroid(field);
    const auto window = calango::core::periodicWindow(field, centre, 0.5);

    check(window.nx == 2 * kGrid && window.ny == 2 * kGrid
              && window.nz == 2 * kGrid,
          "margin 0.5 makes the window two cells across");

    // Every window node must equal SOME node of the original, and the
    // displacement between them must be a lattice translation. Checking the
    // value and the position together is what rules out an off-by-one shift:
    // a wrong offset still copies real data, just from the wrong place.
    double worstValue = 0.0;
    double worstPosition = 0.0;
    for (int i = 0; i < window.nx; i += 7)
        for (int j = 0; j < window.ny; j += 7)
            for (int k = 0; k < window.nz; k += 7) {
                const Vec3 p = window.position(i, j, k);
                // Fractional position in the ORIGINAL cell, wrapped.
                const double fx = p.x / kBox, fy = p.y / kBox, fz = p.z / kBox;
                const auto node = [](double f) {
                    const double g = f * kGrid;
                    int n = static_cast<int>(std::llround(g)) % kGrid;
                    return n < 0 ? n + kGrid : n;
                };
                worstValue = std::max(
                    worstValue,
                    std::abs(window.at(i, j, k)
                             - field.at(node(fx), node(fy), node(fz))));
                // The residual after removing whole lattice vectors must be a
                // grid node, i.e. an integer multiple of the step.
                const double step = kBox / kGrid;
                const auto residual = [step](double f) {
                    const double g = f * kBox / step;
                    return std::abs(g - std::round(g)) * step;
                };
                worstPosition =
                    std::max({worstPosition, residual(fx), residual(fy),
                              residual(fz)});
            }
    checkClose(worstValue, 0.0, 0.0,
               "every window value is a copied node value, bit for bit");
    checkClose(worstPosition, 0.0, 1e-9,
               "and every window node sits exactly on a periodic image node");
}

void testNaiveExtractionSplitsTheFunction()
{
    std::printf("Without continuation the surface really is cut (the bug):\n");
    const auto field = periodicGaussian({0.0, 0.0, 0.0});
    const auto mesh = calango::core::extractIsosurface(field, kIso);
    check(!mesh.positions.empty(), "the home cell yields a surface");

    // Eight corner fragments, not one lobe. This is the "before" the rest of
    // the file is the "after" of; without it the continued-surface checks
    // could pass on data that never needed continuing.
    const int pieces = componentCount(mesh);
    std::printf("       %d connected component(s) over the home cell\n", pieces);
    check(pieces == 8, "the lobe comes out as the eight corner pieces it was "
                       "cut into");

    const Vec3 corner{0.0, 0.0, 0.0};
    double worst = 0.0;
    for (const Vec3& p : mesh.positions)
        worst = std::max(worst, std::abs((p - corner).norm() - analyticRadius()));
    check(worst > 1.0, "and its vertices are nowhere near one sphere about the "
                       "centre");
}

void testContinuedSurfaceIsTheAnalyticSphere()
{
    std::printf("With continuation the same data is one whole sphere:\n");
    const auto field = periodicGaussian({0.0, 0.0, 0.0});
    const Vec3 centre = calango::core::periodicCentroid(field);
    const auto mesh =
        calango::core::extractContinuedIsosurface(field, kIso, centre, 0.5);

    check(!mesh.positions.empty(), "the continued extraction yields a surface");
    check(componentCount(mesh) == 1,
          "as a single connected component — the periodic copies the window "
          "also covers are dropped");

    // The closed form. Every vertex of a Gaussian's level set lies on a sphere
    // of radius sigma*sqrt(2 ln(1/v)); a continuation that mis-shifted the
    // data by even one cell would put a whole lobe 10 A off.
    const double radius = analyticRadius();
    double worst = 0.0;
    for (const Vec3& p : mesh.positions)
        worst = std::max(worst, std::abs((p - centre).norm() - radius));
    std::printf("       analytic radius %.4f A, worst deviation %.4f A\n",
                radius, worst);
    // One grid step is 0.156 A; the tetrahedral decomposition's long diagonals
    // carry the interpolation error, which is quadratic in the edge length.
    checkClose(worst, 0.0, 0.04,
               "every vertex lies on the analytic level set");

    // And it is a FULL sphere, not the fraction the home cell held: the solid
    // angle is covered in every octant.
    int octants[8] = {0};
    for (const Vec3& p : mesh.positions) {
        const Vec3 d = p - centre;
        octants[(d.x > 0 ? 1 : 0) | (d.y > 0 ? 2 : 0) | (d.z > 0 ? 4 : 0)]++;
    }
    bool allPresent = true;
    for (const int n : octants)
        allPresent = allPresent && n > 100;
    check(allPresent, "with vertices in all eight octants around the centre");
}

void testContinuedSurfaceIsClosedAndManifold()
{
    std::printf("The continued mesh is closed and manifold — no seam:\n");
    const auto field = periodicGaussian({0.0, 0.0, 0.0});
    const Vec3 centre = calango::core::periodicCentroid(field);
    const auto mesh =
        calango::core::extractContinuedIsosurface(field, kIso, centre, 0.5);

    // THE check for "no seam". Every edge of a closed manifold surface is
    // shared by exactly two triangles: a hole leaves edges with one, and a
    // duplicated/stitched boundary leaves them with three or four. Welding
    // first is what makes this a statement about the surface rather than about
    // the extractor's vertex duplication.
    const auto welded = calango::core::weldVertices(mesh);
    std::map<std::pair<int, int>, int> edges;
    const std::size_t triangles = mesh.positions.size() / 3;
    for (std::size_t t = 0; t < triangles; ++t) {
        const int v[3] = {welded.index[3 * t], welded.index[3 * t + 1],
                          welded.index[3 * t + 2]};
        for (int k = 0; k < 3; ++k) {
            int a = v[k], b = v[(k + 1) % 3];
            if (a == b)
                continue; // degenerate sliver, contributes no edge
            if (a > b)
                std::swap(a, b);
            ++edges[{a, b}];
        }
    }
    int boundary = 0, nonManifold = 0;
    for (const auto& [edge, count] : edges) {
        if (count == 1)
            ++boundary;
        else if (count > 2)
            ++nonManifold;
    }
    std::printf("       %zu unique edges: %d boundary, %d non-manifold\n",
                edges.size(), boundary, nonManifold);
    check(boundary == 0, "no edge is left open (the surface has no hole where "
                         "the cell face used to be)");
    check(nonManifold == 0, "and none is shared by more than two triangles");

    // Euler characteristic of a sphere. V - E + F = 2 is a global statement
    // that no local edge count can fake.
    const long long V = static_cast<long long>(welded.points.size());
    const long long E = static_cast<long long>(edges.size());
    const long long F = static_cast<long long>(triangles);
    std::printf("       V %lld - E %lld + F %lld = %lld\n", V, E, F, V - E + F);
    check(V - E + F == 2, "and the mesh is topologically a sphere");
}

void testContinuationKeepsTheWindingConsistent()
{
    std::printf("Continuation does not reintroduce the glossy pits:\n");
    // The regression this whole file exists for, re-run on the continued path:
    // a reversed triangle across the stitching region would read as a dark pit
    // under the lit isosurface shader exactly as before.
    const auto field = periodicGaussian({0.0, 0.0, 0.0});
    const Vec3 centre = calango::core::periodicCentroid(field);
    const auto mesh =
        calango::core::extractContinuedIsosurface(field, kIso, centre, 0.5);

    const WindingReport report = inspect(mesh);
    std::printf("       %d triangles: %d agree, %d disagree, %d degenerate\n",
                report.triangles, report.agreeing, report.disagreeing,
                report.degenerate);
    check(report.disagreeing == 0,
          "no triangle is wound against its own normal");
    check(report.triangles > 500, "on a mesh large enough for that to mean "
                                  "something");

    double worstNormal = 0.0;
    for (const Vec3& n : mesh.normals)
        worstNormal = std::max(worstNormal, std::abs(n.norm() - 1.0));
    checkClose(worstNormal, 0.0, 1e-9, "and every normal is still unit length");

    // Outward everywhere, including the half that came from the neighbouring
    // image — a sign flip there is what a naive stitch would produce.
    int inward = 0;
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec3 radial = mesh.positions[i] - centre;
        if (radial.norm() > 1e-9 && mesh.normals[i].dot(radial) <= 0.0)
            ++inward;
    }
    check(inward == 0, "with every normal pointing away from the centre");
}

void testMarginControlsHowMuchIsShown()
{
    std::printf("The margin is a real control, and copies stay dropped:\n");
    const auto field = periodicGaussian({0.0, 0.0, 0.0});
    const Vec3 centre = calango::core::periodicCentroid(field);

    // Even at margin 0 the function is whole: recentring alone unwraps it.
    const auto tight =
        calango::core::extractContinuedIsosurface(field, kIso, centre, 0.0);
    check(componentCount(tight) == 1,
          "margin 0 already shows one lobe (recentring alone unwraps it)");

    // A wide window covers several neighbouring images. The unfiltered
    // extraction sees them; the filtered one must not.
    const auto window = calango::core::periodicWindow(field, centre, 1.0);
    const auto unfiltered = calango::core::extractIsosurface(
        window, kIso, nullptr, calango::core::FieldWrap::Clamped);
    const int before = componentCount(unfiltered);
    const auto filtered =
        calango::core::extractContinuedIsosurface(field, kIso, centre, 1.0);
    const int after = componentCount(filtered);
    std::printf("       margin 1.0: %d component(s) extracted, %d kept\n",
                before, after);
    check(before > 1, "a three-cell window really does contain periodic copies");
    check(after == 1, "and only the function being displayed survives");

    // Same surface either way — the margin decides how much context is
    // extracted, not what the lobe looks like.
    const double radius = analyticRadius();
    double worst = 0.0;
    for (const Vec3& p : filtered.positions)
        worst = std::max(worst, std::abs((p - centre).norm() - radius));
    checkClose(worst, 0.0, 0.04,
               "and the kept lobe is still the analytic level set");
}

void testContinuationHandlesATriclinicCell()
{
    std::printf("Continuation is not limited to orthogonal cells:\n");
    // A sheared cell exercises the reciprocal-vector fractional transform in
    // both the window construction and the component filter; getting either
    // wrong (rows vs columns) is invisible for a cubic box.
    calango::core::VolumetricData field;
    field.nx = field.ny = field.nz = 48;
    field.origin = {0.0, 0.0, 0.0};
    field.spanA = Vec3{9.0, 0.0, 0.0};
    field.spanB = Vec3{3.5, 8.5, 0.0};
    field.spanC = Vec3{1.5, 2.0, 9.5};
    field.values.resize(48u * 48 * 48);
    // Centre it on a corner again, so it wraps in all three directions.
    for (int i = 0; i < 48; ++i)
        for (int j = 0; j < 48; ++j)
            for (int k = 0; k < 48; ++k) {
                double best = 0.0;
                // Nearest image over the 27 surrounding cells.
                for (int a = -1; a <= 1; ++a)
                    for (int b = -1; b <= 1; ++b)
                        for (int c = -1; c <= 1; ++c) {
                            const double fa = double(i) / 48 + a;
                            const double fb = double(j) / 48 + b;
                            const double fc = double(k) / 48 + c;
                            const Vec3 p = field.spanA * fa + field.spanB * fb
                                + field.spanC * fc;
                            best = std::max(
                                best, std::exp(-p.dot(p)
                                               / (2.0 * kSigma * kSigma)));
                        }
                field.values[static_cast<std::size_t>((i * 48 + j) * 48 + k)] =
                    best;
            }

    const Vec3 centre = calango::core::periodicCentroid(field);
    const auto mesh =
        calango::core::extractContinuedIsosurface(field, kIso, centre, 0.5);
    check(!mesh.positions.empty(), "a sheared cell yields a surface");
    check(componentCount(mesh) == 1, "as one component");
    double worst = 0.0;
    for (const Vec3& p : mesh.positions)
        worst = std::max(worst, std::abs((p - centre).norm() - analyticRadius()));
    std::printf("       worst deviation %.4f A on the triclinic grid\n", worst);
    // Coarser grid (48 vs 64) over a larger step, so a looser bound than the
    // cubic case — still two orders below the cell size a shear error costs.
    checkClose(worst, 0.0, 0.08,
               "and the lobe is the analytic sphere, so the fractional "
               "transform is right in both places that use it");
}

/// Every edge shared by exactly two triangles, reported as (boundary,
/// non-manifold). A closed surface gives (0, 0); a hole gives boundary edges.
std::pair<int, int> edgeDefects(const calango::core::IsoMesh& mesh)
{
    const auto welded = calango::core::weldVertices(mesh);
    std::map<std::pair<int, int>, int> edges;
    const std::size_t triangles = mesh.positions.size() / 3;
    for (std::size_t t = 0; t < triangles; ++t)
        for (int k = 0; k < 3; ++k) {
            int a = welded.index[3 * t + static_cast<std::size_t>(k)];
            int b = welded.index[3 * t + static_cast<std::size_t>((k + 1) % 3)];
            if (a == b)
                continue;
            if (a > b)
                std::swap(a, b);
            ++edges[{a, b}];
        }
    int boundary = 0, nonManifold = 0;
    for (const auto& [edge, count] : edges) {
        (void)edge;
        if (count == 1)
            ++boundary;
        else if (count > 2)
            ++nonManifold;
    }
    return {boundary, nonManifold};
}

void testEmptySpaceSkippingMissesNothing()
{
    std::printf("Empty-space skipping never drops a crossing:\n");
    // The extractor skips a block of cells whose NODES all sit on one side of
    // the isovalue. The way to get that wrong is the block's far face: its last
    // cell reaches one node PAST its own extent, so a scan that stops at the
    // block's own width declares a block empty while the surface crosses its
    // boundary — and the mesh comes out with holes exactly on the block grid.
    //
    // A hole is what this catches. A missed block leaves open edges, and the
    // count is a hard 0 rather than a fraction: one dropped cell is one
    // visible puncture.
    //
    // Both grid parities are covered: 64 is a whole number of 8-cell blocks,
    // 61 leaves a ragged last block in every direction, and the ragged one is
    // where an end-clamp is easiest to get wrong.
    for (const int n : {64, 61}) {
        const auto field = sphereField(n, 10.0, 3.0);
        const auto mesh = calango::core::extractIsosurface(field, 0.0);
        const auto [boundary, nonManifold] = edgeDefects(mesh);
        std::printf("       n=%d: %zu triangles, %d boundary edge(s), %d "
                    "non-manifold\n",
                    n, mesh.positions.size() / 3, boundary, nonManifold);
        check(boundary == 0,
              std::string("n=") + std::to_string(n)
                  + ": no block boundary punched a hole");
        check(nonManifold == 0,
              std::string("n=") + std::to_string(n) + ": and none doubled up");
    }

    // Sweep the isovalue across the field's range. A skip predicate that
    // disagrees with the mask test at the ENDS — where whole blocks sit exactly
    // at the level — shows up here and nowhere else.
    const auto field = sphereField(48, 10.0, 3.0);
    int holes = 0;
    for (int step = -20; step <= 20; ++step) {
        const double iso = 0.1 * step;
        const auto mesh = calango::core::extractIsosurface(field, iso);
        if (mesh.positions.empty())
            continue;
        if (edgeDefects(mesh).first != 0)
            ++holes;
    }
    check(holes == 0,
          "and no isovalue across the field's range opens one either");

    // The skip must also be exact on a field that is CONSTANT over large
    // regions, where min == max == isovalue and the comparison is on the
    // boundary of its own predicate.
    calango::core::VolumetricData flat;
    flat.nx = flat.ny = flat.nz = 32;
    flat.origin = {0.0, 0.0, 0.0};
    flat.spanA = Vec3{8.0, 0.0, 0.0};
    flat.spanB = Vec3{0.0, 8.0, 0.0};
    flat.spanC = Vec3{0.0, 0.0, 8.0};
    flat.values.assign(32u * 32 * 32, 1.0);
    // One cube of a different value in the middle: the only crossing there is.
    for (int i = 12; i < 20; ++i)
        for (int j = 12; j < 20; ++j)
            for (int k = 12; k < 20; ++k)
                flat.values[static_cast<std::size_t>((i * 32 + j) * 32 + k)] = 5.0;
    const auto step = calango::core::extractIsosurface(flat, 3.0);
    check(!step.positions.empty(),
          "a lone feature in a constant field is still found");
    check(edgeDefects(step).first == 0,
          "and it comes out closed, with the constant bulk skipped");
    // The bulk's own value is the boundary of the skip predicate: blocks there
    // have min == max == isovalue. Those must be skipped (no corner is ABOVE
    // the level, so no cell straddles), while the inclusion's interface — which
    // shares that same minimum — must not be.
    const auto atBulk = calango::core::extractIsosurface(flat, 1.0);
    check(atBulk.positions.size() == step.positions.size(),
          "at the bulk's own value the interface is still found, whole");
    check(edgeDefects(atBulk).first == 0, "and still closed");

    // A genuinely uniform field has no crossing anywhere, at any level.
    calango::core::VolumetricData uniform = flat;
    uniform.values.assign(uniform.values.size(), 1.0);
    check(calango::core::extractIsosurface(uniform, 1.0).positions.empty()
              && calango::core::extractIsosurface(uniform, 0.5).positions.empty()
              && calango::core::extractIsosurface(uniform, 2.0).positions.empty(),
          "and a uniform field yields nothing at, below or above its value");
}

void testWeldVerticesContract()
{
    std::printf("The shared vertex weld does what both its callers assume:\n");
    // TWO callers depend on this now — the connected-component filter behind
    // the periodic continuation, and VolumetricPanel's Laplacian smoothing,
    // which used to carry its own copy of it. The smoother has no test of its
    // own and reaches this only through a Qt widget, so the contract it relies
    // on is pinned here instead.
    calango::core::IsoMesh mesh;
    const Vec3 a{1.0, 2.0, 3.0};
    const Vec3 b{4.0, 5.0, 6.0};
    const Vec3 c{7.0, 8.0, 9.0};
    // Two triangles sharing the edge (a, b) — the case the whole weld exists
    // for: marching cubes emits those endpoints twice, once per triangle.
    mesh.positions = {a, b, c, b, a, Vec3{1.0, 0.0, 0.0}};
    mesh.normals.assign(mesh.positions.size(), Vec3{0.0, 0.0, 1.0});

    const auto welded = calango::core::weldVertices(mesh);
    check(welded.index.size() == mesh.positions.size(),
          "every vertex gets an index");
    check(welded.points.size() == 4,
          "and the six vertices collapse onto the four distinct points");
    check(welded.index[0] == welded.index[4],
          "the shared endpoint a is one point");
    check(welded.index[1] == welded.index[3],
          "and so is the shared endpoint b");
    check(welded.index[2] != welded.index[0]
              && welded.index[2] != welded.index[1],
          "while a distinct vertex keeps a distinct index");
    // First-seen ordering: the smoother copies welded.points and writes the
    // smoothed positions back through the index, so a permutation here would
    // scramble the surface rather than smooth it.
    check(welded.points[0].x == a.x && welded.points[0].y == a.y
              && welded.points[0].z == a.z,
          "points are in first-seen order");
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec3& p = welded.points[static_cast<std::size_t>(welded.index[i])];
        const Vec3 d = p - mesh.positions[i];
        if (d.norm() > 1e-12) {
            check(false, "every index resolves to its own position");
            return;
        }
    }
    check(true, "every index resolves to its own position");

    // On a real surface the weld has to actually close it: a closed manifold
    // mesh welds to a graph in which every point carries at least three
    // triangles. If it did not, the smoother's neighbour lists would be full of
    // isolated vertices and it would silently do nothing to them.
    const auto field = sphereField(32, 10.0, 3.0);
    const auto sphere = calango::core::extractIsosurface(field, 0.0);
    const auto sphereWeld = calango::core::weldVertices(sphere);
    std::vector<int> uses(sphereWeld.points.size(), 0);
    for (const int index : sphereWeld.index)
        ++uses[static_cast<std::size_t>(index)];
    int isolated = 0, thin = 0;
    for (const int n : uses) {
        if (n == 0)
            ++isolated;
        else if (n < 3)
            ++thin;
    }
    std::printf("       %zu vertices welded to %zu points; %d unused, %d used "
                "by fewer than three triangles\n",
                sphere.positions.size(), sphereWeld.points.size(), isolated,
                thin);
    check(sphereWeld.points.size() < sphere.positions.size() / 2,
          "a real surface welds to well under half its raw vertex count");
    check(isolated == 0, "no welded point is left unreferenced");
    check(thin == 0, "and every one is shared by at least three triangles");
}

} // namespace

int main()
{
    std::printf("Isosurface mesh integrity\n\n");
    testWindingIsConsistent();
    testNormalsAreUnitLength();
    testNormalsPointOutward();
    testGeometryIsTheSphere();
    testDegenerateTrianglesAreRare();

    std::printf("\nPeriodic continuation\n\n");
    testPeriodicCentroidFindsASplitFunction();
    testWindowIsAnExactPeriodicContinuation();
    testNaiveExtractionSplitsTheFunction();
    testContinuedSurfaceIsTheAnalyticSphere();
    testContinuedSurfaceIsClosedAndManifold();
    testContinuationKeepsTheWindingConsistent();
    testMarginControlsHowMuchIsShown();
    testContinuationHandlesATriclinicCell();
    testEmptySpaceSkippingMissesNothing();
    testWeldVerticesContract();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
