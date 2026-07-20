#include "core/BrillouinZone.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace calango::core {

namespace {

struct Plane {
    Vec3 g;   ///< generating reciprocal lattice vector (outward normal)
    double d; ///< |g|²/2 — the bisector plane offset
};

} // namespace

BrillouinZoneData computeBrillouinZone(const UnitCell& cell)
{
    if (!cell.isDefined())
        throw std::invalid_argument("Brillouin zone requires a non-degenerate unit cell");

    const auto& a = cell.vectors();
    const double volume = a[0].dot(a[1].cross(a[2]));
    const double twoPi = 2.0 * M_PI;

    BrillouinZoneData bz;
    bz.reciprocal = {a[1].cross(a[2]) * (twoPi / volume),
                     a[2].cross(a[0]) * (twoPi / volume),
                     a[0].cross(a[1]) * (twoPi / volume)};

    // Bisector planes of reciprocal lattice points in the ±2 shells.
    std::vector<Plane> planes;
    for (int n1 = -2; n1 <= 2; ++n1) {
        for (int n2 = -2; n2 <= 2; ++n2) {
            for (int n3 = -2; n3 <= 2; ++n3) {
                if (n1 == 0 && n2 == 0 && n3 == 0)
                    continue;
                const Vec3 g = bz.reciprocal[0] * n1 + bz.reciprocal[1] * n2
                    + bz.reciprocal[2] * n3;
                planes.push_back({g, 0.5 * g.dot(g)});
            }
        }
    }

    double scale = 0.0;
    for (const auto& b : bz.reciprocal)
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

} // namespace calango::core
