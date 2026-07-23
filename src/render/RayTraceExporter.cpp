#include "render/RayTraceExporter.hpp"
#include "render/RenderGeometry.hpp"

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

// Tachyon's parser reads whitespace-separated floats. Emitting the triples
// directly (rather than building "a, b, c" and replacing the commas) keeps
// the two backends' formatting independent and avoids any chance of a
// stray separator ending up in a .dat scene.
QString xyz(const QVector3D& v)
{
    return QStringLiteral("%1 %2 %3")
        .arg(static_cast<double>(v.x()), 0, 'f', 6)
        .arg(static_cast<double>(v.y()), 0, 'f', 6)
        .arg(static_cast<double>(v.z()), 0, 'f', 6);
}

QString rgbTriple(const QColor& c)
{
    return QStringLiteral("%1 %2 %3")
        .arg(c.redF(), 0, 'f', 4)
        .arg(c.greenF(), 0, 'f', 4)
        .arg(c.blueF(), 0, 'f', 4);
}

/// Half-angle of the shared 40° vertical field of view (OrbitCamera).
constexpr double kHalfFovDeg = 20.0;

double halfFovTan()
{
    return std::tan(kHalfFovDeg * M_PI / 180.0);
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

    // POV-Ray's `angle` is the *horizontal* field of view measured across the
    // `right` vector, whereas OrbitCamera's 40° is vertical. Emitting a flat
    // 40 therefore cropped every non-square render (the wider the image, the
    // more it zoomed in). Convert: tan(h/2) = aspect · tan(v/2).
    const double horizontalFovDeg =
        2.0 * std::atan(static_cast<double>(in.aspect) * halfFovTan()) * 180.0 / M_PI;
    // Orthographic cameras ignore `angle`; POV-Ray sizes them from the
    // up/right vector lengths instead, matched here to OrbitCamera's frustum
    // (half-height = distance · tan 20°).
    const double orthoHalfHeight =
        static_cast<double>(in.camera.distance()) * halfFovTan();
    const bool orthographic =
        in.camera.projectionMode() == CameraProjection::Orthographic;

    QString out;
    QTextStream s(&out);
    s << "// Scene exported from Calango — matches the active viewport.\n"
         "#version 3.7;\n"
         "global_settings { assumed_gamma 1.0 }\n"
      << "background { rgb <" << rgb(in.background) << "> }\n"
      << "camera {\n"
      << (orthographic ? "    orthographic\n" : "    perspective\n")
      << "    location <" << vec(eye) << ">\n"
      << "    look_at <" << vec(target) << ">\n"
      << "    sky <" << vec(up) << ">\n";
    if (orthographic) {
        // An orthographic POV camera is sized by the *lengths* of up/right
        // (angle is ignored). `look_at` reorients both while preserving those
        // lengths, so the same -x / +y convention as below still applies and
        // the visible extent matches OrbitCamera's frustum
        // (half-height = distance · tan 20°).
        s << "    right -x*" << 2.0 * orthoHalfHeight * static_cast<double>(in.aspect)
          << "\n"
          << "    up y*" << 2.0 * orthoHalfHeight << "\n";
    } else {
        // Negated right vector keeps POV-Ray's left-handed system aligned
        // with our right-handed OpenGL scene.
        s << "    right -x*" << in.aspect << "\n"
          << "    angle " << horizontalFovDeg << "\n";
    }
    s << "}\n";

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
    const QVector3D viewDir = (target - eye).normalized();
    const QMatrix4x4 invView = in.camera.view().inverted();

    // Tachyon wants an Updir orthogonal to Viewdir; re-orthogonalize instead
    // of trusting the camera's up vector (they agree today, but a future
    // free-look camera would silently skew the image plane otherwise).
    QVector3D up = in.camera.worldUp();
    up = (up - viewDir * QVector3D::dotProduct(up, viewDir)).normalized();
    if (up.isNull())
        up = perpendicularTo(viewDir);

    const bool orthographic =
        in.camera.projectionMode() == CameraProjection::Orthographic;

    // Tachyon places its image plane one `Zoom` unit down Viewdir with a
    // half-height of 0.5, so the vertical field of view is 2·atan(0.5/zoom)
    // — i.e. zoom = 0.5 / tan(fov/2), NOT 1/tan(fov/2). The old formula was
    // exactly 2× too large and cropped every render to the middle of the
    // viewport. (Sanity check: VMD's 53.13° default FOV ⇒ zoom 1.0.)
    //
    // Under an orthographic camera Tachyon reads the same field as a direct
    // world-space extent, so it has to be matched to OrbitCamera's frustum
    // (half-height = distance · tan 20°) rather than to the FOV.
    const double orthoHalfHeight =
        static_cast<double>(in.camera.distance()) * halfFovTan();
    const double zoom = orthographic
        ? 0.5 / std::max(1e-6, orthoHalfHeight)
        : 0.5 / halfFovTan();

    // Ambient/diffuse weights come from the scene lights so the ray-traced
    // image reproduces the viewport's lighting balance instead of a fixed
    // guess. Tachyon applies Ambient as a flat term, matching our shader.
    double ambient = 0.0;
    double diffuse = 0.0;
    for (const auto& light : in.lights) {
        ambient = std::max(ambient, static_cast<double>(light.ambient.valueF()));
        diffuse = std::max(diffuse, static_cast<double>(light.diffuse.valueF()));
    }
    if (in.lights.empty()) { // no lights configured: fall back to a headlight
        ambient = 0.25;
        diffuse = 0.75;
    }
    ambient = std::clamp(ambient, 0.05, 0.9);
    diffuse = std::clamp(diffuse, 0.1, 1.0);

    QString out;
    QTextStream s(&out);
    s << "# Scene exported from Calango — matches the active viewport.\n"
         "Begin_Scene\n"
      << "Resolution " << in.width << " " << in.height << "\n"
      // Full shading (shadows, Phong highlights, transparency) — the default
      // shader mode skips the specular term our textures rely on.
      << "Shader_Mode Full\n"
         "End_Shader_Mode\n"
      // The block keyword is `Camera`, not `Begin_Camera`: Tachyon's parser
      // treats an unknown leading token as an object type and aborts the
      // whole file with a parse error, which is why Tachyon renders used to
      // fail outright while the POV-Ray path worked.
      << "Camera\n"
      << "  Projection " << (orthographic ? "Orthographic" : "Perspective") << "\n"
      << "  Zoom " << QString::number(zoom, 'f', 6) << "\n"
      // Aspectratio is a *pixel* aspect (1.0 = square pixels); the image
      // aspect already follows from Resolution, so this must stay 1.0 —
      // passing width/height here would stretch the render.
      << "  Aspectratio 1.0\n"
      << "  Antialiasing " << std::max(0, in.antialiasing) << "\n"
         "  Raydepth 8\n"
      << "  Center " << xyz(eye) << "\n"
      << "  Viewdir " << xyz(viewDir) << "\n"
      << "  Updir " << xyz(up) << "\n"
      << "End_Camera\n"
      << "Background " << rgbTriple(in.background) << "\n";

    if (in.lights.empty()) {
        s << "Directional_Light Direction " << xyz(viewDir) << " Color 1 1 1\n";
    } else {
        for (const auto& light : in.lights) {
            // Lights are stored in view space; Direction is the direction the
            // light *travels*, matching both our shader and Tachyon.
            const QVector3D dirWorld =
                invView.mapVector(light.direction.normalized()).normalized();
            s << "Directional_Light Direction " << xyz(dirWorld) << " Color "
              << rgbTriple(light.diffuse) << "\n";
        }
    }

    const auto texture = [ambient, diffuse](const QColor& color) {
        return QStringLiteral(
                   "  Texture Ambient %1 Diffuse %2 Specular 0.2 Opacity 1\n"
                   "  Phong Plastic 0.4 Phong_size 45 Color %3 TexFunc 0\n")
            .arg(ambient, 0, 'f', 3)
            .arg(diffuse, 0, 'f', 3)
            .arg(rgbTriple(color));
    };
    for (const auto& sphere : spheres) {
        // A zero/negative radius is a hard parse error in Tachyon; the
        // viewport just draws nothing, so guard rather than emit it.
        if (sphere.radius <= 1e-6f)
            continue;
        s << "Sphere Center " << xyz(sphere.center) << " Rad "
          << QString::number(static_cast<double>(sphere.radius), 'f', 6) << "\n"
          << texture(sphere.color);
    }
    for (const auto& cylinder : cylinders) {
        if (cylinder.from.distanceToPoint(cylinder.to) < 1e-5f
            || cylinder.radius <= 1e-6f)
            continue;
        s << "FCylinder Base " << xyz(cylinder.from) << " Apex "
          << xyz(cylinder.to) << " Rad "
          << QString::number(static_cast<double>(cylinder.radius), 'f', 6) << "\n"
          << texture(cylinder.color);
    }
    s << "End_Scene\n";
    return out;
}

} // namespace calango::render
