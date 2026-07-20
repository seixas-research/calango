#include "render/RayTraceExporter.hpp"

#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace calango::render {

namespace {

struct SceneSphere {
    QVector3D center;
    float radius;
    QColor color;
};

struct SceneCylinder {
    QVector3D from;
    QVector3D to;
    float radius;
    QColor color;
};

QVector3D toQt(const calango::core::Vec3& v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

QVector3D perpendicularTo(const QVector3D& axis)
{
    const QVector3D reference = std::abs(axis.z()) < 0.9f
        ? QVector3D(0.0f, 0.0f, 1.0f)
        : QVector3D(1.0f, 0.0f, 0.0f);
    return QVector3D::crossProduct(axis, reference).normalized();
}

/// Mirrors StructureRenderer's multi-bond layout so ray-traced images
/// match the viewport (offsets in units of the single-bond radius).
void multiBondLayout(int order, std::vector<float>& offsets, float& radiusScale)
{
    switch (std::clamp(order, 1, 3)) {
    case 2: offsets = {-0.8f, 0.8f}; radiusScale = 0.55f; break;
    case 3: offsets = {-1.5f, 0.0f, 1.5f}; radiusScale = 0.45f; break;
    default: offsets = {0.0f}; radiusScale = 1.0f; break;
    }
}

/// Scene geometry shared by both backends, derived exactly like the
/// viewport's instance buffers.
void collectGeometry(const RayTraceExporter::SceneInputs& in,
                     std::vector<SceneSphere>& spheres,
                     std::vector<SceneCylinder>& cylinders)
{
    const auto& atoms = in.structure->atoms();
    const bool wantBonds = in.style.mode != RepresentationMode::SpaceFilling;

    for (const auto& atom : atoms) {
        spheres.push_back({toQt(atom.position),
                           StructureRenderer::displayRadius(atom.atomicNumber, in.style),
                           StructureRenderer::atomColor(atom.atomicNumber, in.style)});
    }

    if (wantBonds) {
        const float baseRadius = in.style.bondRadius * in.style.bondWidthFactor;
        for (const auto& bond : in.structure->detectBonds(in.style.bondTolerance)) {
            const auto& a = atoms[static_cast<std::size_t>(bond.i)];
            const auto& b = atoms[static_cast<std::size_t>(bond.j)];
            const QVector3D pa = toQt(a.position);
            const QVector3D pbReal = toQt(b.position);
            const QVector3D pbImage = toQt(b.position + bond.imageOffset);
            const QVector3D dir = (pbImage - pa).normalized();
            const float half = pa.distanceToPoint(pbImage) * 0.5f;
            const QColor colorA = StructureRenderer::atomColor(a.atomicNumber, in.style);
            const QColor colorB = StructureRenderer::atomColor(b.atomicNumber, in.style);

            std::vector<float> lateral;
            float radiusScale = 1.0f;
            multiBondLayout(bond.order, lateral, radiusScale);
            const float radius = baseRadius * radiusScale;
            const QVector3D perp = perpendicularTo(dir);
            for (const float offsetUnits : lateral) {
                const QVector3D shift = perp * (offsetUnits * baseRadius);
                cylinders.push_back(
                    {pa + shift, pa + shift + dir * half, radius, colorA});
                if (!bond.crossesBoundary())
                    cylinders.push_back({pa + shift + dir * half,
                                         pbImage + shift, radius, colorB});
                else
                    cylinders.push_back({pbReal + shift, pbReal + shift - dir * half,
                                         radius, colorB});
            }
        }
    }

    if (in.style.showCell && in.structure->cell().isDefined()) {
        const auto corners = in.structure->cell().corners();
        const float radius = 0.015f * std::max(1.0f, in.style.cellLineWidth);
        for (const auto& [i, j] : core::UnitCell::edges())
            cylinders.push_back({toQt(corners[static_cast<std::size_t>(i)]),
                                 toQt(corners[static_cast<std::size_t>(j)]), radius,
                                 in.style.cellColor});
    }
}

QString vec(const QVector3D& v)
{
    return QStringLiteral("%1, %2, %3").arg(v.x()).arg(v.y()).arg(v.z());
}

QString rgb(const QColor& c)
{
    return QStringLiteral("%1, %2, %3").arg(c.redF()).arg(c.greenF()).arg(c.blueF());
}

} // namespace

QString RayTraceExporter::povray(const SceneInputs& in)
{
    std::vector<SceneSphere> spheres;
    std::vector<SceneCylinder> cylinders;
    collectGeometry(in, spheres, cylinders);

    const QVector3D eye = in.camera.worldPosition();
    const QVector3D target = in.camera.target();
    const QVector3D up = in.camera.worldUp();
    const QMatrix4x4 invView = in.camera.view().inverted();

    QString out;
    QTextStream s(&out);
    s << "// Scene exported from Calango — matches the active viewport.\n"
         "#version 3.7;\n"
         "global_settings { assumed_gamma 1.0 }\n"
      << "background { rgb <" << rgb(in.background) << "> }\n"
      << "camera {\n"
      << (in.camera.projectionMode() == CameraProjection::Orthographic
              ? "    orthographic\n"
              : "    perspective\n")
      << "    location <" << vec(eye) << ">\n"
      << "    look_at <" << vec(target) << ">\n"
      << "    sky <" << vec(up) << ">\n"
      // Negated right vector keeps POV-Ray's left-handed system aligned
      // with our right-handed OpenGL scene.
      << "    right -x*" << in.aspect << "\n"
      << "    angle 40\n"
      << "}\n";

    for (const auto& light : in.lights) {
        const QVector3D dirWorld =
            invView.mapVector(light.direction.normalized());
        const QVector3D position = target - dirWorld * 1000.0f;
        s << "light_source { <" << vec(position) << "> rgb <" << rgb(light.diffuse)
          << "> parallel point_at <" << vec(target) << "> }\n";
    }

    s << "#declare AtomFinish = finish { ambient 0.25 diffuse 0.75 specular 0.35 "
         "roughness 0.02 };\n";
    for (const auto& sphere : spheres) {
        s << "sphere { <" << vec(sphere.center) << ">, " << sphere.radius
          << " pigment { rgb <" << rgb(sphere.color) << "> } finish { AtomFinish } }\n";
    }
    for (const auto& cylinder : cylinders) {
        if (cylinder.from.distanceToPoint(cylinder.to) < 1e-5f)
            continue;
        s << "cylinder { <" << vec(cylinder.from) << ">, <" << vec(cylinder.to) << ">, "
          << cylinder.radius << " pigment { rgb <" << rgb(cylinder.color)
          << "> } finish { AtomFinish } }\n";
    }
    return out;
}

QString RayTraceExporter::tachyon(const SceneInputs& in)
{
    std::vector<SceneSphere> spheres;
    std::vector<SceneCylinder> cylinders;
    collectGeometry(in, spheres, cylinders);

    const QVector3D eye = in.camera.worldPosition();
    const QVector3D target = in.camera.target();
    const QVector3D up = in.camera.worldUp();
    const QVector3D viewDir = (target - eye).normalized();
    const QMatrix4x4 invView = in.camera.view().inverted();
    // Tachyon Zoom = 1 / tan(vertical_fov / 2); our FOV is 40°.
    const double zoom = 1.0 / std::tan(20.0 * M_PI / 180.0);

    QString out;
    QTextStream s(&out);
    s << "# Scene exported from Calango — matches the active viewport.\n"
         "Begin_Scene\n"
      << "Resolution " << in.width << " " << in.height << "\n"
      << "Begin_Camera\n"
      << "  Projection "
      << (in.camera.projectionMode() == CameraProjection::Orthographic
              ? "ORTHOGRAPHIC" : "PERSPECTIVE")
      << "\n"
      << "  Zoom " << zoom << "\n"
         "  Aspectratio 1.0\n"
         "  Antialiasing 8\n"
         "  Raydepth 8\n"
      << "  Center " << vec(eye).replace(',', ' ') << "\n"
      << "  Viewdir " << vec(viewDir).replace(',', ' ') << "\n"
      << "  Updir " << vec(up).replace(',', ' ') << "\n"
      << "End_Camera\n"
      << "Background " << rgb(in.background).replace(',', ' ') << "\n";

    for (const auto& light : in.lights) {
        const QVector3D dirWorld = invView.mapVector(light.direction.normalized());
        s << "Directional_Light Direction " << vec(dirWorld).replace(',', ' ')
          << " Color " << rgb(light.diffuse).replace(',', ' ') << "\n";
    }

    const auto texture = [](const QColor& color) {
        return QStringLiteral(
                   "  Texture Ambient 0.25 Diffuse 0.75 Specular 0.2 Opacity 1\n"
                   "  Phong Plastic 0.4 Phong_size 45 Color %1 TexFunc 0\n")
            .arg(rgb(color).replace(',', ' '));
    };
    for (const auto& sphere : spheres) {
        s << "Sphere Center " << vec(sphere.center).replace(',', ' ') << " Rad "
          << sphere.radius << "\n"
          << texture(sphere.color);
    }
    for (const auto& cylinder : cylinders) {
        if (cylinder.from.distanceToPoint(cylinder.to) < 1e-5f)
            continue;
        s << "FCylinder Base " << vec(cylinder.from).replace(',', ' ') << " Apex "
          << vec(cylinder.to).replace(',', ' ') << " Rad " << cylinder.radius << "\n"
          << texture(cylinder.color);
    }
    s << "End_Scene\n";
    return out;
}

} // namespace calango::render
