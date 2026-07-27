#include "gui/OverlayModel.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace calango::gui {

namespace {

using core::Vec3;

constexpr double kPi = std::numbers::pi;

/// Tessellation of the lattice-plane quad. 48x48 is fine enough that a
/// field-sliced plane reads as a smooth colour map rather than as facets, and
/// coarse enough to rebuild interactively while a slider is dragged.
constexpr int kPlaneDivisions = 48;

void appendVertex(std::vector<float>& out, const Vec3& p, const QColor& c)
{
    out.push_back(static_cast<float>(p.x));
    out.push_back(static_cast<float>(p.y));
    out.push_back(static_cast<float>(p.z));
    out.push_back(static_cast<float>(c.redF()));
    out.push_back(static_cast<float>(c.greenF()));
    out.push_back(static_cast<float>(c.blueF()));
}

void appendTri(std::vector<float>& out, const Vec3& a, const Vec3& b,
               const Vec3& c, const QColor& ca, const QColor& cb,
               const QColor& cc)
{
    appendVertex(out, a, ca);
    appendVertex(out, b, cb);
    appendVertex(out, c, cc);
}

QColor lerpColor(const QColor& a, const QColor& b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

/// Per-cell colour of a parametric surface cell (i, j) of an (ni x nj) grid.
QColor texel(const Overlay& p, int i, int j, int ni, int nj)
{
    switch (p.texture) {
    case Overlay::TextureStyle::Checkerboard: {
        const int su = std::max(1, ni / 8), sv = std::max(1, nj / 8);
        return ((i / su + j / sv) % 2 == 0) ? p.color : p.color2;
    }
    case Overlay::TextureStyle::Gradient:
        return lerpColor(p.color, p.color2,
                         static_cast<double>(i) / std::max(1, ni - 1));
    default: // Solid / Glassy / Wireframe all use the base colour
        return p.color;
    }
}

Vec3 rotateEuler(const Vec3& p, const Vec3& degrees)
{
    const double rx = degrees.x * kPi / 180.0;
    const double ry = degrees.y * kPi / 180.0;
    const double rz = degrees.z * kPi / 180.0;
    // Rz . Ry . Rx
    Vec3 v = p;
    v = {v.x, v.y * std::cos(rx) - v.z * std::sin(rx),
         v.y * std::sin(rx) + v.z * std::cos(rx)};
    v = {v.x * std::cos(ry) + v.z * std::sin(ry), v.y,
         -v.x * std::sin(ry) + v.z * std::cos(ry)};
    v = {v.x * std::cos(rz) - v.y * std::sin(rz),
         v.x * std::sin(rz) + v.y * std::cos(rz), v.z};
    return v;
}

/// Any unit vector perpendicular to `axis`, chosen so the cross product is
/// never near-degenerate.
Vec3 perpendicular(const Vec3& axis)
{
    const Vec3 helper = std::abs(axis.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    return helper.cross(axis).normalized();
}

void genSphere(const Overlay& p, std::vector<float>& out)
{
    const bool ellipsoid = p.kind == Overlay::Kind::Ellipsoid;
    const double rx = p.size.x;
    const double ry = ellipsoid ? p.size.y : p.size.x;
    const double rz = ellipsoid ? p.size.z : p.size.x;
    const bool corrugated = p.finish == Overlay::SurfaceFinish::Corrugated;
    const int rings = std::clamp(p.resolution, 8, 128);
    const int slices = 2 * rings;
    const auto pt = [&](int i, int j) {
        const double v = kPi * i / rings;
        const double u = 2.0 * kPi * j / slices;
        const double disp =
            corrugated ? 1.0 + 0.12 * std::sin(6 * u) * std::sin(6 * v) : 1.0;
        return p.center
            + Vec3{rx * std::sin(v) * std::cos(u) * disp,
                   ry * std::sin(v) * std::sin(u) * disp,
                   rz * std::cos(v) * disp};
    };
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < slices; ++j) {
            const QColor c = texel(p, i, j, rings, slices);
            appendTri(out, pt(i, j), pt(i + 1, j), pt(i + 1, j + 1), c, c, c);
            appendTri(out, pt(i, j), pt(i + 1, j + 1), pt(i, j + 1), c, c, c);
        }
    }
}

void genBox(const Overlay& p, std::vector<float>& out)
{
    const Vec3 h = p.size * 0.5;
    const auto corner = [&](int sx, int sy, int sz) {
        return p.center
            + rotateEuler({sx * h.x, sy * h.y, sz * h.z}, p.rotationDeg);
    };
    const int idx[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1},
                           {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    const Vec3 verts[8] = {corner(-1, -1, -1), corner(1, -1, -1),
                           corner(-1, 1, -1),  corner(1, 1, -1),
                           corner(-1, -1, 1),  corner(1, -1, 1),
                           corner(-1, 1, 1),   corner(1, 1, 1)};
    for (int f = 0; f < 6; ++f) {
        const QColor c = texel(p, f, 0, 6, 1);
        appendTri(out, verts[idx[f][0]], verts[idx[f][1]], verts[idx[f][2]],
                  c, c, c);
        appendTri(out, verts[idx[f][0]], verts[idx[f][2]], verts[idx[f][3]],
                  c, c, c);
    }
}

void genTubeCone(const Overlay& p, std::vector<float>& out, bool cone)
{
    const Vec3 axisVec = p.endPoint - p.center;
    const double len = axisVec.norm();
    if (len < 1e-6)
        return; // zero-length tube: nothing to draw, and the basis degenerates
    const Vec3 axis = axisVec / len;
    const Vec3 uAxis = perpendicular(axis);
    const Vec3 vAxis = axis.cross(uAxis).normalized();
    const bool corrugated = p.finish == Overlay::SurfaceFinish::Corrugated;
    const int slices = std::clamp(p.resolution, 6, 128);
    const auto ring = [&](int j, double frac) {
        const double th = 2.0 * kPi * j / slices;
        const double rr = (cone ? p.radius * (1.0 - frac) : p.radius)
            * (corrugated ? 1.0 + 0.10 * std::sin(8 * th) : 1.0);
        return p.center + axis * (len * frac)
            + (uAxis * std::cos(th) + vAxis * std::sin(th)) * rr;
    };
    for (int j = 0; j < slices; ++j) {
        const QColor c = texel(p, j, 0, slices, 1);
        const Vec3 b0 = ring(j, 0.0), b1 = ring(j + 1, 0.0);
        const Vec3 t0 = ring(j, 1.0), t1 = ring(j + 1, 1.0);
        appendTri(out, b0, b1, t1, c, c, c);
        if (cone)
            appendTri(out, b0, b1, p.center + axis * len, c, c, c); // apex
        else
            appendTri(out, b0, t1, t0, c, c, c);
        appendTri(out, p.center, b1, b0, c, c, c); // base cap
    }
}

void genDisk(const Overlay& p, std::vector<float>& out)
{
    const Vec3 n =
        p.normal.norm() > 1e-9 ? p.normal.normalized() : Vec3{0, 0, 1};
    const Vec3 uAxis = perpendicular(n);
    const Vec3 vAxis = n.cross(uAxis).normalized();
    const int slices = std::clamp(p.resolution, 6, 256);
    const auto rim = [&](int j) {
        const double th = 2.0 * kPi * j / slices;
        return p.center
            + (uAxis * std::cos(th) + vAxis * std::sin(th)) * p.radius;
    };
    for (int j = 0; j < slices; ++j) {
        const QColor c = texel(p, j, 0, slices, 1);
        appendTri(out, p.center, rim(j), rim(j + 1), c, c, c);
    }
}

void genPlane(const Overlay& p, std::vector<float>& out)
{
    const Vec3 n =
        p.normal.norm() > 1e-9 ? p.normal.normalized() : Vec3{0, 0, 1};
    const Vec3 uAxis = perpendicular(n);
    const Vec3 vAxis = n.cross(uAxis).normalized();
    const bool corrugated = p.finish == Overlay::SurfaceFinish::Corrugated;
    const int n_ = std::clamp(p.resolution, 2, 128);
    const double R = p.radius;
    const auto pt = [&](int i, int j) {
        const double s = (static_cast<double>(i) / n_ * 2.0 - 1.0) * R;
        const double t = (static_cast<double>(j) / n_ * 2.0 - 1.0) * R;
        const double d =
            corrugated ? 0.15 * R * std::sin(4 * s) * std::sin(4 * t) : 0.0;
        return p.center + uAxis * s + vAxis * t + n * d;
    };
    for (int i = 0; i < n_; ++i) {
        for (int j = 0; j < n_; ++j) {
            const QColor c = texel(p, i, j, n_, n_);
            appendTri(out, pt(i, j), pt(i + 1, j), pt(i + 1, j + 1), c, c, c);
            appendTri(out, pt(i, j), pt(i + 1, j + 1), pt(i, j + 1), c, c, c);
        }
    }
}

/// The (hkl) plane: a tessellated quad centred on the cell, optionally
/// colour-mapped from a volumetric field. Carried over from the retired
/// Lattice Plane dialog.
void genLatticePlane(const Overlay& p, const core::Structure* structure,
                     std::vector<float>& faces, std::vector<float>& edges)
{
    if (!structure || !structure->cell().isDefined())
        return; // no lattice, so (hkl) means nothing

    const auto& v = structure->cell().vectors();
    const Vec3 a = v[0], b = v[1], c = v[2];

    // Plane normal = h.(b x c) + k.(c x a) + l.(a x b), the reciprocal vectors.
    Vec3 normal = a.cross(b) * static_cast<double>(p.miller[2])
        + b.cross(c) * static_cast<double>(p.miller[0])
        + c.cross(a) * static_cast<double>(p.miller[1]);
    if (normal.norm() < 1e-9)
        normal = c; // degenerate (0 0 0): fall back to the c-axis normal
    normal = normal.normalized();

    const Vec3 uAxis = perpendicular(normal);
    const Vec3 vAxis = normal.cross(uAxis).normalized();
    const Vec3 planeOrigin = (a + b + c) * 0.5 + normal * p.offset;
    const double R = p.extent;

    // Field slice: Cartesian -> field-box fractional by Cramer's rule.
    const bool useField = p.sliceField && p.field && !p.field->empty();
    double lo = 0.0, hi = 1.0, invDet = 0.0;
    Vec3 fBC, fCA, fAB;
    if (useField) {
        lo = p.field->minValue();
        hi = p.field->maxValue();
        if (hi <= lo)
            hi = lo + 1.0;
        fBC = p.field->spanB.cross(p.field->spanC);
        fCA = p.field->spanC.cross(p.field->spanA);
        fAB = p.field->spanA.cross(p.field->spanB);
        const double det = p.field->spanA.dot(fBC);
        invDet = std::abs(det) > 1e-12 ? 1.0 / det : 0.0;
    }

    const auto colorAt = [&](const Vec3& point) -> QColor {
        if (!useField || invDet == 0.0)
            return p.color;
        const Vec3 dp = point - p.field->origin;
        const double uf = dp.dot(fBC) * invDet;
        const double vf = dp.dot(fCA) * invDet;
        const double wf = dp.dot(fAB) * invDet;
        const double value = p.field->samplePeriodic(
            uf * p.field->nx, vf * p.field->ny, wf * p.field->nz);
        return render::ColorMap::sample(
            p.gradient, static_cast<float>((value - lo) / (hi - lo)));
    };

    const auto point = [&](int i, int j) {
        const double s =
            (static_cast<double>(i) / kPlaneDivisions * 2.0 - 1.0) * R;
        const double t =
            (static_cast<double>(j) / kPlaneDivisions * 2.0 - 1.0) * R;
        return planeOrigin + uAxis * s + vAxis * t;
    };

    for (int i = 0; i < kPlaneDivisions; ++i) {
        for (int j = 0; j < kPlaneDivisions; ++j) {
            const Vec3 p00 = point(i, j), p10 = point(i + 1, j),
                       p11 = point(i + 1, j + 1), p01 = point(i, j + 1);
            const QColor c00 = colorAt(p00), c10 = colorAt(p10),
                         c11 = colorAt(p11), c01 = colorAt(p01);
            appendVertex(faces, p00, c00);
            appendVertex(faces, p10, c10);
            appendVertex(faces, p11, c11);
            appendVertex(faces, p00, c00);
            appendVertex(faces, p11, c11);
            appendVertex(faces, p01, c01);
        }
    }

    if (p.showEdges) {
        const std::array<Vec3, 4> corners = {
            point(0, 0), point(kPlaneDivisions, 0),
            point(kPlaneDivisions, kPlaneDivisions), point(0, kPlaneDivisions)};
        for (int e = 0; e < 4; ++e) {
            appendVertex(edges, corners[e], p.color);
            appendVertex(edges, corners[(e + 1) % 4], p.color);
        }
    }
}

} // namespace

QString Overlay::kindName(Kind kind)
{
    switch (kind) {
    case Kind::LatticePlane:
        return QCoreApplication::translate("Overlay", "Lattice plane");
    case Kind::Text:
        return QCoreApplication::translate("Overlay", "Text");
    case Kind::Box:
        return QCoreApplication::translate("Overlay", "Box");
    case Kind::Sphere:
        return QCoreApplication::translate("Overlay", "Sphere");
    case Kind::Ellipsoid:
        return QCoreApplication::translate("Overlay", "Ellipsoid");
    case Kind::Tube:
        return QCoreApplication::translate("Overlay", "Tube");
    case Kind::Cone:
        return QCoreApplication::translate("Overlay", "Cone");
    case Kind::Plane:
        return QCoreApplication::translate("Overlay", "Plane");
    case Kind::Disk:
        return QCoreApplication::translate("Overlay", "Disk");
    }
    return {};
}

QString Overlay::displayName() const
{
    // The type always leads, so the list reads as a typed inventory even when
    // every entry has been left at its default name.
    const QString type = kindName(kind);
    if (!name.isEmpty())
        return QStringLiteral("%1 — %2").arg(type, name);
    if (kind == Kind::Text && !text.isEmpty())
        return QStringLiteral("%1 — %2").arg(type, text);
    if (kind == Kind::LatticePlane) {
        return QStringLiteral("%1 (%2 %3 %4)")
            .arg(type)
            .arg(miller[0])
            .arg(miller[1])
            .arg(miller[2]);
    }
    return type;
}

void appendOverlayGeometry(
    const Overlay& overlay, const core::Structure* structure,
    std::vector<float>& faces, std::vector<float>& edges,
    std::vector<render::StructureRenderer::OverlayRange>& ranges)
{
    if (!overlay.visible || !overlay.isGeometry())
        return;

    if (overlay.kind == Overlay::Kind::LatticePlane) {
        const int first = static_cast<int>(faces.size() / 6);
        genLatticePlane(overlay, structure, faces, edges);
        const int count = static_cast<int>(faces.size() / 6) - first;
        if (count > 0)
            ranges.push_back({first, count, static_cast<float>(overlay.opacity)});
        return;
    }

    std::vector<float> tri;
    switch (overlay.kind) {
    case Overlay::Kind::Sphere:
    case Overlay::Kind::Ellipsoid:
        genSphere(overlay, tri);
        break;
    case Overlay::Kind::Box:
        genBox(overlay, tri);
        break;
    case Overlay::Kind::Tube:
        genTubeCone(overlay, tri, /*cone=*/false);
        break;
    case Overlay::Kind::Cone:
        genTubeCone(overlay, tri, /*cone=*/true);
        break;
    case Overlay::Kind::Disk:
        genDisk(overlay, tri);
        break;
    case Overlay::Kind::Plane:
        genPlane(overlay, tri);
        break;
    case Overlay::Kind::LatticePlane:
    case Overlay::Kind::Text:
        return; // handled above / painted, not tessellated
    }
    if (tri.empty())
        return;

    if (overlay.texture == Overlay::TextureStyle::Wireframe) {
        // Wireframe is not a fill style — it is the absence of one. Each
        // triangle (3 verts x 6 floats) becomes its three edges, drawn opaque
        // through the edge stream.
        for (std::size_t v = 0; v + 18 <= tri.size(); v += 18) {
            const auto edge = [&](std::size_t i0, std::size_t i1) {
                for (int k = 0; k < 6; ++k)
                    edges.push_back(tri[i0 + k]);
                for (int k = 0; k < 6; ++k)
                    edges.push_back(tri[i1 + k]);
            };
            edge(v, v + 6);
            edge(v + 6, v + 12);
            edge(v + 12, v);
        }
        return;
    }

    const int first = static_cast<int>(faces.size() / 6);
    faces.insert(faces.end(), tri.begin(), tri.end());
    ranges.push_back({first, static_cast<int>(tri.size() / 6),
                      static_cast<float>(overlay.opacity)});
}

} // namespace calango::gui
