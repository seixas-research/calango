#include "core/BrillouinZone.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace calango::core {

namespace {

struct Plane {
    Vec3 g;   ///< generating lattice vector (outward normal)
    double d; ///< |g|²/2 — the bisector plane offset
};

/// Bisector planes of the lattice points spanned by `basis` in the ±2
/// shells — two shells is enough for any Niggli-reasonable cell. Shared by
/// wignerSeitzCell() (which needs the planes to find the cell's vertices)
/// and clipToWignerSeitzCell() (which needs them to clip triangles to it):
/// one function so both agree on exactly which cell they mean.
std::vector<Plane> bisectorPlanes(const std::array<Vec3, 3>& basis)
{
    std::vector<Plane> planes;
    for (int n1 = -2; n1 <= 2; ++n1) {
        for (int n2 = -2; n2 <= 2; ++n2) {
            for (int n3 = -2; n3 <= 2; ++n3) {
                if (n1 == 0 && n2 == 0 && n3 == 0)
                    continue;
                const Vec3 g =
                    basis[0] * n1 + basis[1] * n2 + basis[2] * n3;
                planes.push_back({g, 0.5 * g.dot(g)});
            }
        }
    }
    return planes;
}

/// A polygon vertex plus one auxiliary scalar riding along for the clip —
/// today that's gradientMagnitude, the only per-vertex quantity (besides
/// position, which the clip works in anyway) that has to survive a cut.
struct ClipVertex {
    Vec3 pos;
    double scalar = 0.0;
};

/// Clip a convex polygon against the half-space x·g ≤ d (Sutherland-Hodgman).
/// A cut edge lands on the plane by linear interpolation, which for a facet
/// of an isosurface keeps the new edge exactly on the surface — and, since
/// `scalar` is interpolated by the same parameter `t`, keeps it consistent
/// with the interpolated position rather than picked from whichever original
/// endpoint happened to survive.
std::vector<ClipVertex> clipHalfSpace(const std::vector<ClipVertex>& polygon,
                                      const Plane& plane)
{
    std::vector<ClipVertex> out;
    const auto n = polygon.size();
    const auto side = [&plane](const Vec3& v) { return v.dot(plane.g) - plane.d; };
    for (std::size_t i = 0; i < n; ++i) {
        const ClipVertex& a = polygon[i];
        const ClipVertex& b = polygon[(i + 1) % n];
        const double da = side(a.pos);
        const double db = side(b.pos);
        if (da <= 0.0)
            out.push_back(a);
        if ((da < 0.0 && db > 0.0) || (da > 0.0 && db < 0.0)) {
            const double t = da / (da - db);
            out.push_back({a.pos + (b.pos - a.pos) * t,
                           a.scalar + (b.scalar - a.scalar) * t});
        }
    }
    return out;
}

} // namespace

PolyhedronMesh wignerSeitzCell(const std::array<Vec3, 3>& basis)
{
    const double volume = basis[0].dot(basis[1].cross(basis[2]));
    if (!(std::abs(volume) > 1e-12))
        throw std::invalid_argument(
            "Wigner-Seitz cell requires a non-degenerate lattice basis");

    PolyhedronMesh bz;
    const std::vector<Plane> planes = bisectorPlanes(basis);

    double scale = 0.0;
    for (const auto& b : basis)
        scale = std::max(scale, b.norm());
    const double tol = 1e-6 * scale * scale; // tolerance on x·g - d
    const double dedupeSq = 1e-10 * scale * scale;

    // Vertices: intersections of three planes that satisfy all half-spaces.
    const auto inside = [&](const Vec3& x) {
        return std::all_of(planes.begin(), planes.end(), [&](const Plane& p) {
            return x.dot(p.g) <= p.d + tol;
        });
    };

    for (std::size_t i = 0; i < planes.size(); ++i) {
        for (std::size_t j = i + 1; j < planes.size(); ++j) {
            const Vec3 gjk = planes[i].g.cross(planes[j].g);
            for (std::size_t k = j + 1; k < planes.size(); ++k) {
                const double det = gjk.dot(planes[k].g);
                if (std::abs(det) < 1e-9 * scale * scale * scale)
                    continue;
                // Cramer's rule for [g_i; g_j; g_k] x = [d_i; d_j; d_k].
                const Vec3 x = (planes[j].g.cross(planes[k].g) * planes[i].d
                                + planes[k].g.cross(planes[i].g) * planes[j].d
                                + planes[i].g.cross(planes[j].g) * planes[k].d)
                    / det;
                if (!inside(x))
                    continue;
                const bool duplicate =
                    std::any_of(bz.vertices.begin(), bz.vertices.end(), [&](const Vec3& v) {
                        const Vec3 diff = v - x;
                        return diff.dot(diff) < dedupeSq;
                    });
                if (!duplicate)
                    bz.vertices.push_back(x);
            }
        }
    }

    // Faces: vertices lying on each plane, ordered CCW around the outward
    // normal (the plane's g vector).
    for (const Plane& plane : planes) {
        std::vector<int> onPlane;
        for (std::size_t v = 0; v < bz.vertices.size(); ++v) {
            if (std::abs(bz.vertices[v].dot(plane.g) - plane.d) < tol)
                onPlane.push_back(static_cast<int>(v));
        }
        if (onPlane.size() < 3)
            continue;

        Vec3 centroid;
        for (const int v : onPlane)
            centroid += bz.vertices[static_cast<std::size_t>(v)];
        centroid = centroid / static_cast<double>(onPlane.size());

        const Vec3 normal = plane.g.normalized();
        const Vec3 u =
            (bz.vertices[static_cast<std::size_t>(onPlane.front())] - centroid).normalized();
        const Vec3 w = normal.cross(u); // (u, w, normal) right-handed => CCW angles

        std::sort(onPlane.begin(), onPlane.end(), [&](int lhs, int rhs) {
            const Vec3 pl = bz.vertices[static_cast<std::size_t>(lhs)] - centroid;
            const Vec3 pr = bz.vertices[static_cast<std::size_t>(rhs)] - centroid;
            return std::atan2(pl.dot(w), pl.dot(u)) < std::atan2(pr.dot(w), pr.dot(u));
        });
        bz.faces.push_back(std::move(onPlane));
    }

    return bz;
}

BrillouinZoneData computeBrillouinZone(const UnitCell& cell)
{
    if (!cell.isDefined())
        throw std::invalid_argument(
            "Brillouin zone requires a non-degenerate unit cell");

    const auto& a = cell.vectors();
    const double volume = a[0].dot(a[1].cross(a[2]));
    const double twoPi = 2.0 * M_PI;

    BrillouinZoneData bz;
    bz.reciprocal = {a[1].cross(a[2]) * (twoPi / volume),
                     a[2].cross(a[0]) * (twoPi / volume),
                     a[0].cross(a[1]) * (twoPi / volume)};

    PolyhedronMesh mesh = wignerSeitzCell(bz.reciprocal);
    bz.vertices = std::move(mesh.vertices);
    bz.faces = std::move(mesh.faces);
    return bz;
}

PolyhedronMesh computeWignerSeitzCell(const UnitCell& cell)
{
    // Empty rather than throwing: this one is called from the render path on
    // whatever structure is loaded, including molecules with no cell at all.
    // A caller that has a cell already knows it does.
    if (!cell.isDefined())
        return {};
    return wignerSeitzCell(cell.vectors());
}

IsoMesh clipToWignerSeitzCell(const IsoMesh& mesh, const std::array<Vec3, 3>& basis)
{
    IsoMesh out;
    if (mesh.positions.size() < 3)
        return out;

    // Most restrictive (nearest) plane first: a fragment translated well away
    // from the cell is then rejected on the first or second test instead of
    // somewhere down a ~124-entry list — correctness is unaffected, only how
    // fast a doomed candidate is discarded.
    std::vector<Plane> sortedPlanes = bisectorPlanes(basis);
    std::sort(sortedPlanes.begin(), sortedPlanes.end(),
              [](const Plane& a, const Plane& b) { return a.d < b.d; });

    // basis * f = v, solved by Cramer's rule, to read off which periodic
    // image of the sampled box a Wigner-Seitz vertex belongs to.
    const double det = basis[0].dot(basis[1].cross(basis[2]));
    const auto fracOf = [&](const Vec3& v) {
        return Vec3{basis[1].cross(basis[2]).dot(v) / det,
                    basis[2].cross(basis[0]).dot(v) / det,
                    basis[0].cross(basis[1]).dot(v) / det};
    };

    // wignerSeitzCell() throws for a degenerate basis before `det` (also
    // ~0 there) is ever used by fracOf() below.
    const PolyhedronMesh ws = wignerSeitzCell(basis);

    // Flatten first, before any cutting: a cut fragment has no gradient
    // normals of its own (its new vertices are interpolated along the cut,
    // not sampled from the field), so every fragment of a source triangle
    // just inherits that triangle's one already-flat normal.
    const IsoMesh flat = flattenTriangleNormals(mesh);

    // The integer shifts actually needed to reach every corner of the
    // Wigner-Seitz cell from the box the field was sampled on — read off the
    // cell's own vertices rather than assumed from a fixed neighbour-shell
    // count, so this holds for any basis, not only cubic/BCC/FCC ones.
    // Floor AND ceil per axis (not a single round): several vertices can sit
    // exactly on a box face (fractional coordinate ±1/2 exactly, e.g. for any
    // lattice with a mirror-symmetric primitive cell), where which neighbour
    // "owns" that point is not decidable in floating point — trying both
    // costs one harmless extra, empty-clipped pass rather than a missed
    // corner.
    std::vector<std::array<int, 3>> shifts{{0, 0, 0}};
    const auto addShift = [&shifts](std::array<int, 3> s) {
        if (std::find(shifts.begin(), shifts.end(), s) == shifts.end())
            shifts.push_back(s);
    };
    for (const Vec3& v : ws.vertices) {
        const Vec3 f = fracOf(v);
        const std::array<int, 2> ix{static_cast<int>(std::floor(f.x)),
                                    static_cast<int>(std::ceil(f.x))};
        const std::array<int, 2> iy{static_cast<int>(std::floor(f.y)),
                                    static_cast<int>(std::ceil(f.y))};
        const std::array<int, 2> iz{static_cast<int>(std::floor(f.z)),
                                    static_cast<int>(std::ceil(f.z))};
        for (int i : ix)
            for (int j : iy)
                for (int k : iz)
                    addShift({i, j, k});
    }

    const bool hasGradientMagnitude =
        flat.gradientMagnitude.size() == flat.positions.size();

    out.positions.reserve(flat.positions.size());
    out.normals.reserve(flat.normals.size());
    if (hasGradientMagnitude)
        out.gradientMagnitude.reserve(flat.gradientMagnitude.size());

    for (std::size_t t = 0; t + 2 < flat.positions.size(); t += 3) {
        const Vec3& normal = flat.normals[t]; // same on t, t+1, t+2

        for (const std::array<int, 3>& shift : shifts) {
            const Vec3 offset = basis[0] * shift[0] + basis[1] * shift[1]
                + basis[2] * shift[2];
            std::vector<ClipVertex> polygon{
                {flat.positions[t] + offset,
                 hasGradientMagnitude ? flat.gradientMagnitude[t] : 0.0},
                {flat.positions[t + 1] + offset,
                 hasGradientMagnitude ? flat.gradientMagnitude[t + 1] : 0.0},
                {flat.positions[t + 2] + offset,
                 hasGradientMagnitude ? flat.gradientMagnitude[t + 2] : 0.0},
            };
            for (const Plane& plane : sortedPlanes) {
                polygon = clipHalfSpace(polygon, plane);
                if (polygon.size() < 3)
                    break;
            }
            for (std::size_t k = 1; k + 1 < polygon.size(); ++k) {
                out.positions.push_back(polygon[0].pos);
                out.positions.push_back(polygon[k].pos);
                out.positions.push_back(polygon[k + 1].pos);
                out.normals.push_back(normal);
                out.normals.push_back(normal);
                out.normals.push_back(normal);
                if (hasGradientMagnitude) {
                    out.gradientMagnitude.push_back(polygon[0].scalar);
                    out.gradientMagnitude.push_back(polygon[k].scalar);
                    out.gradientMagnitude.push_back(polygon[k + 1].scalar);
                }
            }
        }
    }
    return out;
}

} // namespace calango::core
