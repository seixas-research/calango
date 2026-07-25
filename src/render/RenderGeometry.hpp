#pragma once

#include "core/Vec3.hpp"

#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <vector>

namespace calango::render {

/// Geometry helpers shared by every backend that draws a structure: the
/// OpenGL viewport (StructureRenderer), the offline ray-tracer scene
/// exporters (RayTraceExporter) and the Brillouin-zone view.
///
/// These lived as private copies in each of those files. The multi-bond
/// layout in particular carried a "mirrors StructureRenderer's layout"
/// comment in the exporter — a coupling the compiler could not enforce, so
/// a tweak to the on-screen bond spacing would silently stop matching the
/// ray-traced image. One definition makes that impossible.

/// core::Vec3 (double) -> QVector3D (float), the boundary between the
/// Qt-free model layer and the rendering layers.
inline QVector3D toQt(const core::Vec3& v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y),
            static_cast<float>(v.z)};
}

/// Any unit vector perpendicular to `axis` — the offset direction for the
/// parallel cylinders of double/triple bonds. Camera-independent, so the
/// instance buffer stays static while orbiting.
inline QVector3D perpendicularTo(const QVector3D& axis)
{
    // Cross with whichever cardinal axis is furthest from `axis`, so the
    // product is never near-degenerate.
    const QVector3D reference = std::abs(axis.z()) < 0.9f
        ? QVector3D(0.0f, 0.0f, 1.0f)
        : QVector3D(1.0f, 0.0f, 0.0f);
    return QVector3D::crossProduct(axis, reference).normalized();
}

/// Lateral center offsets (in units of the single-bond radius) and the
/// per-cylinder radius shrink for a bond of the given order. Orders outside
/// 1..3 are clamped — bond orders are user-assigned and never perceived.
inline void multiBondLayout(int order, std::vector<float>& offsets,
                            float& radiusScale)
{
    switch (std::clamp(order, 1, 4)) {
    case 2:
        offsets = {-0.8f, 0.8f};
        radiusScale = 0.55f;
        break;
    case 3:
        offsets = {-1.5f, 0.0f, 1.5f};
        radiusScale = 0.45f;
        break;
    case 4:
        // Aromatic (order 1.5): drawn as a double bond. Two cylinders is the
        // conventional depiction of a delocalized ring bond, and the ring
        // context is what tells it apart from a true double bond.
        offsets = {-0.8f, 0.8f};
        radiusScale = 0.55f;
        break;
    default:
        offsets = {0.0f};
        radiusScale = 1.0f;
        break;
    }
}

} // namespace calango::render
