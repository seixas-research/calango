#include "core/IsosurfaceContinuation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <unordered_set>
#include <vector>

namespace calango::core {

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

/// Fractional coordinates of the direction `d` in the box spanned by
/// (a, b, c), via the reciprocal vectors — no matrix inversion needed, and it
/// stays exact for a triclinic cell.
Vec3 fractionalDelta(const VolumetricData& box, const Vec3& d)
{
    const Vec3 bc = box.spanB.cross(box.spanC);
    const Vec3 ca = box.spanC.cross(box.spanA);
    const Vec3 ab = box.spanA.cross(box.spanB);
    const double det = box.spanA.dot(bc);
    if (std::abs(det) < 1e-30)
        return {};
    return {d.dot(bc) / det, d.dot(ca) / det, d.dot(ab) / det};
}

/// Positive remainder: C's % keeps the sign of the dividend, which for a
/// window that starts at a negative index would read off the wrong end.
int wrapIndex(long long i, int n)
{
    const long long m = i % n;
    return static_cast<int>(m < 0 ? m + n : m);
}

/// Circular mean of a non-negative weight distribution over n grid nodes,
/// returned as a fractional coordinate in [0, 1).
double circularMean(const std::vector<double>& weights)
{
    const std::size_t n = weights.size();
    if (n == 0)
        return 0.0;
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double theta = kTwoPi * static_cast<double>(i) / n;
        re += weights[i] * std::cos(theta);
        im += weights[i] * std::sin(theta);
    }
    // A distribution with no preferred direction (uniform, or all zero) leaves
    // the resultant at the origin, where the angle is meaningless. Report the
    // box centre rather than whatever atan2(0, 0) happens to give.
    if (std::hypot(re, im) < 1e-300)
        return 0.5;
    double frac = std::atan2(im, re) / kTwoPi;
    if (frac < 0.0)
        frac += 1.0;
    return frac;
}

/// Union-find over triangle indices.
class DisjointSet {
public:
    explicit DisjointSet(std::size_t n) : parent_(n)
    {
        std::iota(parent_.begin(), parent_.end(), 0);
    }
    int find(int a)
    {
        while (parent_[static_cast<std::size_t>(a)] != a) {
            parent_[static_cast<std::size_t>(a)] =
                parent_[static_cast<std::size_t>(
                    parent_[static_cast<std::size_t>(a)])];
            a = parent_[static_cast<std::size_t>(a)];
        }
        return a;
    }
    void unite(int a, int b)
    {
        a = find(a);
        b = find(b);
        if (a != b)
            parent_[static_cast<std::size_t>(a)] = b;
    }

private:
    std::vector<int> parent_;
};

} // namespace

Vec3 periodicCentroid(const VolumetricData& field)
{
    if (field.empty())
        return {};

    // Accumulate the three marginal distributions in one pass, then take the
    // circular mean of each. Doing the trigonometry on the marginals rather
    // than per voxel turns 6 transcendental calls per voxel into 6 per NODE.
    std::vector<double> wa(static_cast<std::size_t>(field.nx), 0.0);
    std::vector<double> wb(static_cast<std::size_t>(field.ny), 0.0);
    std::vector<double> wc(static_cast<std::size_t>(field.nz), 0.0);
    for (int ix = 0; ix < field.nx; ++ix)
        for (int iy = 0; iy < field.ny; ++iy)
            for (int iz = 0; iz < field.nz; ++iz) {
                // |psi|^2: a Wannier function is signed, and weighting by the
                // signed amplitude would let the lobes cancel.
                const double v = field.at(ix, iy, iz);
                const double w = v * v;
                wa[static_cast<std::size_t>(ix)] += w;
                wb[static_cast<std::size_t>(iy)] += w;
                wc[static_cast<std::size_t>(iz)] += w;
            }

    const double fa = circularMean(wa);
    const double fb = circularMean(wb);
    const double fc = circularMean(wc);
    return field.origin + field.spanA * fa + field.spanB * fb
        + field.spanC * fc;
}

VolumetricData periodicWindow(const VolumetricData& field, const Vec3& centre,
                              double margin)
{
    if (field.empty())
        return {};
    margin = std::clamp(margin, 0.0, kMaxContinuationMargin);

    const Vec3 frac = fractionalDelta(field, centre - field.origin);
    const double f[3] = {frac.x, frac.y, frac.z};
    const int in[3] = {field.nx, field.ny, field.nz};

    int dims[3];
    long long start[3];
    for (int a = 0; a < 3; ++a) {
        const int halo = static_cast<int>(std::llround(margin * in[a]));
        dims[a] = in[a] + 2 * halo;
        // The centre's own node, backed off by half the window. Rounding to a
        // node is what keeps this a pure index shift: every window node then
        // coincides with a node of some periodic image, so the values below
        // are copied rather than interpolated.
        const long long node = std::llround(f[a] * in[a]);
        start[a] = node - dims[a] / 2;
    }

    VolumetricData out;
    out.nx = dims[0];
    out.ny = dims[1];
    out.nz = dims[2];
    out.label = field.label;
    // position() is affine in the indices, so it is exact for the negative
    // ones a window reaching into the previous image carries.
    out.origin = field.position(static_cast<double>(start[0]),
                                static_cast<double>(start[1]),
                                static_cast<double>(start[2]));
    out.spanA = field.spanA * (static_cast<double>(dims[0]) / in[0]);
    out.spanB = field.spanB * (static_cast<double>(dims[1]) / in[1]);
    out.spanC = field.spanC * (static_cast<double>(dims[2]) / in[2]);
    out.values.resize(static_cast<std::size_t>(dims[0]) * dims[1] * dims[2]);

    // Wrap each axis once into a lookup rather than per voxel: the inner loop
    // is then three array reads and a copy.
    std::vector<int> mapA(static_cast<std::size_t>(dims[0]));
    std::vector<int> mapB(static_cast<std::size_t>(dims[1]));
    std::vector<int> mapC(static_cast<std::size_t>(dims[2]));
    for (int i = 0; i < dims[0]; ++i)
        mapA[static_cast<std::size_t>(i)] = wrapIndex(start[0] + i, in[0]);
    for (int i = 0; i < dims[1]; ++i)
        mapB[static_cast<std::size_t>(i)] = wrapIndex(start[1] + i, in[1]);
    for (int i = 0; i < dims[2]; ++i)
        mapC[static_cast<std::size_t>(i)] = wrapIndex(start[2] + i, in[2]);

    for (int i = 0; i < dims[0]; ++i)
        for (int j = 0; j < dims[1]; ++j) {
            const std::size_t row =
                (static_cast<std::size_t>(i) * dims[1] + j) * dims[2];
            for (int k = 0; k < dims[2]; ++k)
                out.values[row + k] =
                    field.at(mapA[static_cast<std::size_t>(i)],
                             mapB[static_cast<std::size_t>(j)],
                             mapC[static_cast<std::size_t>(k)]);
        }
    return out;
}

IsoMesh keepComponentsAroundCentre(const IsoMesh& mesh,
                                   const VolumetricData& cell,
                                   const Vec3& centre)
{
    const std::size_t vertices = mesh.positions.size();
    const std::size_t triangles = vertices / 3;
    if (triangles == 0)
        return {};

    const WeldedMesh welded = weldVertices(mesh);
    DisjointSet components(triangles);

    // Two triangles sharing any welded vertex are in the same component. The
    // first triangle to claim a point owns it; every later claimant merges
    // into it, which links the whole surface in one pass.
    std::vector<int> owner(welded.points.size(), -1);
    for (std::size_t t = 0; t < triangles; ++t)
        for (std::size_t k = 0; k < 3; ++k) {
            const std::size_t p =
                static_cast<std::size_t>(welded.index[3 * t + k]);
            if (owner[p] < 0)
                owner[p] = static_cast<int>(t);
            else
                components.unite(static_cast<int>(t), owner[p]);
        }

    // A component belongs to the function being displayed if it reaches into
    // the one-cell box around its centre. A neighbouring image's lobe is a
    // whole lattice vector away and cannot.
    std::unordered_set<int> keep;
    for (std::size_t t = 0; t < triangles; ++t) {
        for (std::size_t k = 0; k < 3; ++k) {
            const Vec3 g =
                fractionalDelta(cell, mesh.positions[3 * t + k] - centre);
            if (std::abs(g.x) <= 0.5 && std::abs(g.y) <= 0.5
                && std::abs(g.z) <= 0.5) {
                keep.insert(components.find(static_cast<int>(t)));
                break;
            }
        }
    }
    if (keep.empty())
        return {};

    IsoMesh out;
    const bool colored = mesh.colorValues.size() == vertices;
    out.positions.reserve(vertices);
    out.normals.reserve(vertices);
    if (colored)
        out.colorValues.reserve(vertices);
    for (std::size_t t = 0; t < triangles; ++t) {
        if (keep.find(components.find(static_cast<int>(t))) == keep.end())
            continue;
        for (std::size_t k = 0; k < 3; ++k) {
            const std::size_t v = 3 * t + k;
            out.positions.push_back(mesh.positions[v]);
            if (v < mesh.normals.size())
                out.normals.push_back(mesh.normals[v]);
            if (colored)
                out.colorValues.push_back(mesh.colorValues[v]);
        }
    }
    return out;
}

IsoMesh extractContinuedIsosurface(const VolumetricData& field, double isovalue,
                                   const Vec3& centre, double margin)
{
    if (field.empty())
        return {};
    const VolumetricData window = periodicWindow(field, centre, margin);
    // Clamped, not Periodic: the window already spans more than one period, so
    // wrapping it would join its two outer faces — which hold DIFFERENT images
    // — and hang a spurious sheet across the far side of the view.
    const IsoMesh mesh =
        extractIsosurface(window, isovalue, nullptr, FieldWrap::Clamped);
    return keepComponentsAroundCentre(mesh, field, centre);
}

} // namespace calango::core
