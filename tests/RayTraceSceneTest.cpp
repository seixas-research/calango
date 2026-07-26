// Scene-file generation test for the external ray-tracer backends.
//
// The renderers themselves are optional third-party binaries, so CI cannot
// render an image — but the failure mode that actually bit us was a *scene
// file* Tachyon refused to parse (`Begin_Camera` is not a keyword) and a
// camera zoom that was a factor of two off. Both are checkable from the
// generated text alone, which is what this test does. GUI-free: the
// exporter's geometry collection uses StructureRenderer's static color/radius
// helpers, neither of which touches an OpenGL context.

#include "core/Structure.hpp"
#include "render/RayTraceExporter.hpp"

#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using calango::render::CameraProjection;
using calango::render::RayTraceExporter;

namespace {

int failures = 0;

void check(bool condition, const QString& what)
{
    if (condition) {
        std::printf("  ok   %s\n", qPrintable(what));
    } else {
        std::printf("  FAIL %s\n", qPrintable(what));
        ++failures;
    }
}

/// First capture group of `pattern` in `text`, or "" when it doesn't match.
QString capture(const QString& text, const QString& pattern)
{
    const QRegularExpression re(pattern);
    const auto match = re.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

calango::core::Structure diatomic()
{
    calango::core::Structure structure;
    calango::core::Atom a;
    a.atomicNumber = 6;
    a.position = {0.0, 0.0, 0.0};
    structure.addAtom(a);
    calango::core::Atom b;
    b.atomicNumber = 8;
    b.position = {1.2, 0.0, 0.0};
    structure.addAtom(b);
    structure.setCell(calango::core::UnitCell({10.0, 0.0, 0.0}, {0.0, 10.0, 0.0},
                                              {0.0, 0.0, 10.0}, {true, true, true}));
    return structure;
}

RayTraceExporter::SceneInputs makeInputs(const calango::core::Structure& structure)
{
    RayTraceExporter::SceneInputs in;
    in.structure = &structure;
    in.style.showCell = true;
    in.lights.push_back({});
    in.camera.frame(QVector3D(0.6f, 0.0f, 0.0f), 5.0f);
    in.width = 1920;
    in.height = 1080;
    in.aspect = 1920.0f / 1080.0f;
    return in;
}

} // namespace

int main(int argc, char** argv)
{
    const auto structure = diatomic();

    // `--dump tachyon|povray` prints the generated scene instead of running
    // the checks — handy when adapting to a renderer version's syntax.
    if (argc >= 2 && QString::fromLatin1(argv[1]) == QStringLiteral("--dump")) {
        auto in = makeInputs(structure);
        const bool pov = argc >= 3
            && QString::fromLatin1(argv[2]) == QStringLiteral("povray");
        std::printf("%s", qPrintable(pov ? RayTraceExporter::povray(in)
                                         : RayTraceExporter::tachyon(in)));
        return EXIT_SUCCESS;
    }

    // -- Tachyon ------------------------------------------------------------
    std::printf("Tachyon scene:\n");
    auto inputs = makeInputs(structure);
    const QString scene = RayTraceExporter::tachyon(inputs);

    check(scene.startsWith(QStringLiteral("# Scene exported")),
          "starts with a comment line");
    check(scene.contains(QStringLiteral("\nBegin_Scene\n")), "has Begin_Scene");
    check(scene.trimmed().endsWith(QStringLiteral("End_Scene")), "ends with End_Scene");

    // The regression that made every Tachyon render fail outright.
    check(!scene.contains(QStringLiteral("Begin_Camera")),
          "does NOT emit the invalid 'Begin_Camera' keyword");
    check(scene.contains(QRegularExpression(QStringLiteral("(?m)^Camera$"))),
          "opens the camera block with 'Camera'");
    check(scene.contains(QStringLiteral("End_Camera")), "closes the camera block");
    check(scene.contains(QStringLiteral("Resolution 1920 1080")),
          "carries the requested resolution");
    check(scene.contains(QStringLiteral("Aspectratio 1.0")),
          "keeps the pixel aspect at 1.0 (image aspect comes from Resolution)");

    // Zoom = 0.5/tan(fov/2) for the shared 40° vertical FOV. The old
    // 1/tan(fov/2) was exactly 2x and cropped the image.
    const double expectedZoom = 0.5 / std::tan(20.0 * M_PI / 180.0);
    const QString zoomText =
        capture(scene, QStringLiteral("Zoom\\s+([0-9.eE+-]+)"));
    check(!zoomText.isEmpty(), "emits a Zoom value");
    check(std::abs(zoomText.toDouble() - expectedZoom) < 1e-4,
          QStringLiteral("perspective Zoom %1 == 0.5/tan(20°) = %2")
              .arg(zoomText)
              .arg(expectedZoom, 0, 'f', 6));

    check(scene.contains(QStringLiteral("Projection Perspective")),
          "declares a perspective projection");
    check(scene.contains(QStringLiteral("Directional_Light Direction")),
          "emits the scene lights");
    check(scene.contains(QStringLiteral("Sphere Center")), "emits atoms as spheres");
    check(scene.contains(QStringLiteral("FCylinder Base")),
          "emits bonds/cell edges as finite cylinders");
    check(scene.contains(QStringLiteral("TexFunc 0")), "closes every texture block");

    // Commas would be a parse error: Tachyon reads whitespace-separated floats.
    const QStringList geometryLines =
        scene.split(QLatin1Char('\n')).filter(QRegularExpression(
            QStringLiteral("^(Sphere|FCylinder|Background|Directional_Light|  (Center|Viewdir|Updir))")));
    check(!geometryLines.isEmpty(), "found geometry/camera lines to inspect");
    check(!geometryLines.join(QLatin1Char(' ')).contains(QLatin1Char(',')),
          "no commas in any numeric triple");
    check(!scene.contains(QStringLiteral("nan")) && !scene.contains(QStringLiteral("inf")),
          "no nan/inf in the scene");

    // Every sphere/cylinder must carry a strictly positive radius.
    const QRegularExpression radiusRe(QStringLiteral("Rad\\s+([0-9.eE+-]+)"));
    auto radii = radiusRe.globalMatch(scene);
    int radiusCount = 0;
    bool allPositive = true;
    while (radii.hasNext()) {
        ++radiusCount;
        if (radii.next().captured(1).toDouble() <= 0.0)
            allPositive = false;
    }
    check(radiusCount > 0 && allPositive,
          QStringLiteral("all %1 radii are positive").arg(radiusCount));

    // -- Tachyon, orthographic ----------------------------------------------
    std::printf("Tachyon scene (orthographic):\n");
    inputs.camera.setProjectionMode(CameraProjection::Orthographic);
    const QString ortho = RayTraceExporter::tachyon(inputs);
    check(ortho.contains(QStringLiteral("Projection Orthographic")),
          "declares an orthographic projection");
    // Orthographic Zoom is a world-space extent, so it must differ from the
    // perspective (FOV-derived) value.
    const double orthoHalfHeight =
        static_cast<double>(inputs.camera.distance()) * std::tan(20.0 * M_PI / 180.0);
    const QString orthoZoom = capture(ortho, QStringLiteral("Zoom\\s+([0-9.eE+-]+)"));
    check(std::abs(orthoZoom.toDouble() - 0.5 / orthoHalfHeight) < 1e-4,
          QStringLiteral("orthographic Zoom %1 == 0.5/(d·tan20°) = %2")
              .arg(orthoZoom)
              .arg(0.5 / orthoHalfHeight, 0, 'f', 6));

    // -- Casts (per-atom representation groups) ------------------------------
    //
    // The exporter shares StructureRenderer::atomModes with the viewport, so
    // this pins the resolution rule itself: a figure that splits a substrate
    // and an adsorbate into two casts must export with that split, and a stale
    // assignment left over from a replaced structure must fall back to the
    // uniform mode rather than styling arbitrary atoms.
    std::printf("Casts:\n");
    {
        using calango::render::RepresentationMode;
        using calango::render::StructureRenderer;

        auto casted = makeInputs(structure);
        // C in cast 0 (space-filling), O in cast 1 (ball-and-stick).
        casted.style.mode = RepresentationMode::SpaceFilling;
        casted.style.castModes = {RepresentationMode::BallAndStick};
        casted.style.atomCasts = {0, 1};

        const auto modes = StructureRenderer::atomModes(&structure, casted.style);
        check(modes.size() == 2 && modes[0] == RepresentationMode::SpaceFilling
                  && modes[1] == RepresentationMode::BallAndStick,
              "each atom resolves to its own cast's representation");

        // The two atoms must now get DIFFERENT radii — one vdW-sized, one a
        // ball-and-stick node. A shared radius would mean the cast never
        // reached the geometry.
        const float carbonR = StructureRenderer::displayRadius(6, casted.style,
                                                               modes[0]);
        const float oxygenR = StructureRenderer::displayRadius(8, casted.style,
                                                               modes[1]);
        check(carbonR > oxygenR * 2.0f,
              "the space-filling cast is drawn several times larger");

        // A bond touching a space-filling atom is dropped, so this scene keeps
        // only the 12 cell edges — without that rule a CPK surface grows sticks
        // through its own vdW spheres.
        const QString castScene = RayTraceExporter::tachyon(casted);
        check(castScene.count(QStringLiteral("FCylinder Base")) == 12,
              "no bond cylinders survive a space-filling endpoint");

        // Stale assignment (wrong length): fall back to the uniform mode.
        auto stale = casted;
        stale.style.atomCasts = {0, 1, 0, 1};
        const auto staleModes = StructureRenderer::atomModes(&structure,
                                                             stale.style);
        check(staleModes.size() == 2
                  && staleModes[0] == RepresentationMode::SpaceFilling
                  && staleModes[1] == RepresentationMode::SpaceFilling,
              "an assignment that does not match the atom count is ignored");

        // A cast index with no mode entry falls back to cast 0's rather than
        // reading past the end of castModes.
        auto outOfRange = casted;
        outOfRange.style.atomCasts = {0, 7};
        const auto clamped = StructureRenderer::atomModes(&structure,
                                                          outOfRange.style);
        check(clamped[1] == RepresentationMode::SpaceFilling,
              "an out-of-range cast index falls back to cast 0");

        // With no casts at all nothing changes: this is the default every
        // structure loads with.
        auto plain = makeInputs(structure);
        const auto defaults = StructureRenderer::atomModes(&structure,
                                                           plain.style);
        check(defaults.size() == 2
                  && defaults[0] == plain.style.mode
                  && defaults[1] == plain.style.mode
                  && plain.style.castCount() == 1,
              "the default is one cast holding every atom");
    }

    // -- POV-Ray ------------------------------------------------------------
    std::printf("POV-Ray scene:\n");
    auto povInputs = makeInputs(structure);
    const QString pov = RayTraceExporter::povray(povInputs);
    check(pov.contains(QStringLiteral("camera {")), "has a camera block");
    check(pov.contains(QStringLiteral("perspective")), "is perspective by default");
    // POV's `angle` is horizontal; a 16:9 render of a 40° vertical FOV needs
    // ~65.8°, not the 40 that used to be hardcoded.
    const double expectedAngle =
        2.0 * std::atan(static_cast<double>(povInputs.aspect)
                        * std::tan(20.0 * M_PI / 180.0)) * 180.0 / M_PI;
    const QString angleText = capture(pov, QStringLiteral("angle\\s+([0-9.eE+-]+)"));
    check(!angleText.isEmpty(), "emits a camera angle");
    check(std::abs(angleText.toDouble() - expectedAngle) < 1e-2,
          QStringLiteral("horizontal angle %1 == %2 (not the vertical 40)")
              .arg(angleText)
              .arg(expectedAngle, 0, 'f', 3));
    check(pov.contains(QStringLiteral("sphere {")), "emits atoms");
    check(pov.contains(QStringLiteral("cylinder {")), "emits bonds/cell edges");

    povInputs.camera.setProjectionMode(CameraProjection::Orthographic);
    const QString povOrtho = RayTraceExporter::povray(povInputs);
    check(povOrtho.contains(QStringLiteral("orthographic")),
          "orthographic mode is declared");
    // Orthographic POV cameras are sized by up/right length, not by `angle`.
    check(!povOrtho.contains(QStringLiteral("angle ")),
          "orthographic camera omits the meaningless angle");
    // Sized by the up/right *lengths* (POV ignores angle in ortho mode);
    // they must span 2 · distance · tan 20° vertically.
    const double povOrthoHeight = 2.0 * static_cast<double>(povInputs.camera.distance())
        * std::tan(20.0 * M_PI / 180.0);
    const QString upLength = capture(povOrtho, QStringLiteral("up y\\*([0-9.eE+-]+)"));
    check(!upLength.isEmpty(), "orthographic camera sets an explicit up length");
    check(std::abs(upLength.toDouble() - povOrthoHeight) < 1e-3,
          QStringLiteral("orthographic up length %1 == 2·d·tan20° = %2")
              .arg(upLength)
              .arg(povOrthoHeight, 0, 'f', 4));
    check(povOrtho.contains(QStringLiteral("right -x*")),
          "orthographic camera keeps the right-handed -x convention");

    std::printf(failures == 0 ? "\nAll scene checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
