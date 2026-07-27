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
    // Explicitly perspective. The viewport's DEFAULT projection is
    // orthographic (render::kDefaultProjection), and the checks below are
    // about the perspective code path — the Zoom = 0.5/tan(fov/2) formula and
    // the "Projection Perspective" declaration — so the fixture has to state
    // which path it means rather than inherit whatever the default happens to
    // be. The orthographic path has its own fixture further down.
    in.camera.setProjectionMode(calango::render::CameraProjection::Perspective);
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

        using CastStyle = StructureRenderer::CastStyle;

        auto casted = makeInputs(structure);
        // C in cast 0 (space-filling, matte, half-scale bonds);
        // O in cast 1 (ball-and-stick, glassy, double-scale atoms).
        casted.style.mode = RepresentationMode::SpaceFilling;
        casted.style.surfaceFinish = calango::render::SurfaceFinish::Matte;
        casted.style.bondWidthFactor = 0.5f;
        CastStyle second;
        second.mode = RepresentationMode::BallAndStick;
        second.surfaceFinish = calango::render::SurfaceFinish::Glassy;
        second.atomScaleFactor = 2.0f;
        casted.style.castStyles = {second};
        casted.style.atomCasts = {0, 1};

        const auto casts = StructureRenderer::atomCastStyles(&structure,
                                                             casted.style);
        check(casts.size() == 2 && casts[0].mode == RepresentationMode::SpaceFilling
                  && casts[1].mode == RepresentationMode::BallAndStick,
              "each atom resolves to its own cast's representation");
        // Every per-cast setting has to travel, not just the mode — that is
        // the whole point of a cast owning its own style.
        check(casts[0].surfaceFinish == calango::render::SurfaceFinish::Matte
                  && casts[1].surfaceFinish == calango::render::SurfaceFinish::Glassy,
              "each cast keeps its own surface finish");
        check(casts[0].atomScaleFactor == 1.0f && casts[1].atomScaleFactor == 2.0f,
              "each cast keeps its own atom radius scale");
        check(casts[0].bondWidthFactor == 0.5f && casts[1].bondWidthFactor == 1.0f,
              "each cast keeps its own bond width scale");
        check(casted.style.anyTranslucentCast(),
              "a glassy cast is detected wherever it sits");
        // Opacity is the other route into the blended pass, and it is per cast.
        auto faded = makeInputs(structure);
        check(!faded.style.anyTranslucentCast(),
              "a fully opaque scene needs no blended pass");
        CastStyle ghost;
        ghost.opacity = 0.35f;
        faded.style.castStyles = {ghost};
        faded.style.atomCasts = {0, 1};
        check(faded.style.anyTranslucentCast(),
              "a cast faded below 1 does need one");
        const auto fadedCasts =
            StructureRenderer::atomCastStyles(&structure, faded.style);
        check(fadedCasts[0].opacity == 1.0f && fadedCasts[1].opacity == 0.35f,
              "each cast keeps its own opacity");

        // The two atoms must now get DIFFERENT radii — one vdW-sized, one a
        // ball-and-stick node. A shared radius would mean the cast never
        // reached the geometry.
        const float carbonR = StructureRenderer::displayRadius(6, casts[0]);
        const float oxygenR = StructureRenderer::displayRadius(8, casts[1]);
        check(carbonR > oxygenR, "the space-filling cast is drawn larger");

        // A bond touching a space-filling atom is dropped, so this scene keeps
        // only the 12 cell edges — without that rule a CPK surface grows sticks
        // through its own vdW spheres.
        const QString castScene = RayTraceExporter::tachyon(casted);
        check(castScene.count(QStringLiteral("FCylinder Base")) == 12,
              "no bond cylinders survive a space-filling endpoint");

        // Stale assignment (wrong length): fall back to the uniform style.
        auto stale = casted;
        stale.style.atomCasts = {0, 1, 0, 1};
        const auto staleCasts = StructureRenderer::atomCastStyles(&structure,
                                                                  stale.style);
        check(staleCasts.size() == 2
                  && staleCasts[0].mode == RepresentationMode::SpaceFilling
                  && staleCasts[1].mode == RepresentationMode::SpaceFilling,
              "an assignment that does not match the atom count is ignored");

        // A cast index with no entry falls back to cast 0's rather than
        // reading past the end of castStyles.
        auto outOfRange = casted;
        outOfRange.style.atomCasts = {0, 7};
        const auto clamped = StructureRenderer::atomCastStyles(&structure,
                                                               outOfRange.style);
        check(clamped[1].mode == RepresentationMode::SpaceFilling,
              "an out-of-range cast index falls back to cast 0");

        // With no casts at all nothing changes: this is the default every
        // structure loads with.
        auto plain = makeInputs(structure);
        const auto defaults = StructureRenderer::atomCastStyles(&structure,
                                                                plain.style);
        check(defaults.size() == 2
                  && defaults[0].mode == plain.style.mode
                  && defaults[1].mode == plain.style.mode
                  && plain.style.castCount() == 1,
              "the default is one cast holding every atom");

        // Cast 0 writes through to the plain members, so the two ways of
        // reading it can never drift apart.
        auto writeThrough = makeInputs(structure);
        CastStyle edited = writeThrough.style.castStyle(0);
        edited.atomScaleFactor = 1.75f;
        edited.colorMode = calango::render::ColorMode::CoordinationNumber;
        writeThrough.style.setCastStyle(0, edited);
        check(writeThrough.style.atomScaleFactor == 1.75f
                  && writeThrough.style.colorMode
                      == calango::render::ColorMode::CoordinationNumber,
              "cast 0 is stored in the style's own members, not a copy");

        // A macromolecular cast contributes no per-atom geometry at all.
        auto macro = makeInputs(structure);
        macro.style.mode = RepresentationMode::Ribbon;
        const QString macroScene = RayTraceExporter::tachyon(macro);
        check(!macroScene.contains(QStringLiteral("Sphere Center")),
              "a ribbon cast exports no atom spheres");
    }

    // -- Macromolecular representations --------------------------------------
    //
    // Both builders are pure geometry over a Structure, so they can be checked
    // directly rather than only through a rendered frame.
    std::printf("Ribbon diagram:\n");
    {
        using calango::render::RepresentationMode;
        using calango::render::StructureRenderer;
        using CastStyle = StructureRenderer::CastStyle;

        // Two chains of α-carbons at the real 3.8 Å spacing, with a deliberate
        // break in chain A: residues 4 and 5 sit 12 Å apart, which is what a
        // disordered loop the model omits looks like.
        calango::core::Structure protein;
        std::vector<calango::core::ResidueInfo> residues;
        const auto addCa = [&](const char* chain, int seq, double x, double y,
                               double z) {
            calango::core::Atom atom;
            atom.atomicNumber = 6;
            atom.position = {x, y, z};
            protein.addAtom(atom);
            calango::core::ResidueInfo info;
            info.chain = chain;
            info.residue = "ALA";
            info.residueSeq = seq;
            info.atomName = "CA";
            residues.push_back(info);
        };
        for (int i = 0; i < 5; ++i)
            addCa("A", i, 3.8 * i, 0.0, 0.0);
        addCa("A", 5, 3.8 * 4 + 12.0, 0.0, 0.0); // chain break
        for (int i = 0; i < 4; ++i)
            addCa("B", i, 3.8 * i, 10.0, 0.0);
        // A non-CA atom, which the trace must ignore.
        calango::core::Atom sidechain;
        sidechain.atomicNumber = 8;
        sidechain.position = {1.0, 1.0, 1.0};
        protein.addAtom(sidechain);
        calango::core::ResidueInfo sideInfo;
        sideInfo.chain = "A";
        sideInfo.residue = "ALA";
        sideInfo.atomName = "CB";
        residues.push_back(sideInfo);
        protein.setResidues(residues);
        check(protein.hasResidues(), "the synthetic protein carries residues");

        StructureRenderer renderer;
        std::vector<CastStyle> casts(protein.size());
        for (CastStyle& cast : casts)
            cast.mode = RepresentationMode::Ribbon;

        std::vector<float> tubes;
        std::vector<float> joints;
        renderer.buildRibbon(&protein, casts, nullptr, tubes, joints);
        check(!tubes.empty(), "the α-carbon trace produces tube geometry");
        check(!joints.empty(), "and a sphere at every joint");

        // 4 subdivisions per segment: chain A contributes 4 joined segments
        // (the 5th is the break, which must NOT be bridged) and chain B three.
        // A builder that bridged the gap would produce 4 more.
        constexpr int kFloats = 25;
        const int tubeCount = static_cast<int>(tubes.size()) / kFloats;
        check(tubeCount == (4 + 3) * 4,
              QStringLiteral("the chain break is not bridged (%1 segments)")
                  .arg(tubeCount));

        // A structure with no residue annotation has no backbone to trace, so
        // the ribbon must be empty rather than guessing one.
        calango::core::Structure plain = diatomic();
        std::vector<CastStyle> plainCasts(plain.size());
        for (CastStyle& cast : plainCasts)
            cast.mode = RepresentationMode::Ribbon;
        std::vector<float> noTubes;
        std::vector<float> noJoints;
        renderer.buildRibbon(&plain, plainCasts, nullptr, noTubes, noJoints);
        check(noTubes.empty() && noJoints.empty(),
              "a structure without residues yields no ribbon");
    }

    std::printf("Molecular surface:\n");
    {
        using calango::render::RepresentationMode;
        using calango::render::StructureRenderer;
        using CastStyle = StructureRenderer::CastStyle;

        StructureRenderer renderer;
        const auto structureCopy = diatomic();
        std::vector<CastStyle> casts(structureCopy.size());
        for (CastStyle& cast : casts)
            cast.mode = RepresentationMode::MolecularSurface;

        std::vector<float> faces;
        renderer.buildMolecularSurface(&structureCopy, casts, faces);
        check(!faces.empty(), "an envelope is extracted");
        check(faces.size() % (3 * 6) == 0,
              "the output is whole pos+color triangles");

        // The envelope must ENCLOSE the atoms: every vertex further from the
        // C-O midpoint than the atoms themselves, and none absurdly far. A
        // surface that collapsed onto the nuclei, or that leaked to the padded
        // box faces, fails this.
        float minRadius = 1e9f;
        float maxRadius = 0.0f;
        const QVector3D center(0.6f, 0.0f, 0.0f); // midpoint of the diatomic
        for (std::size_t i = 0; i + 5 < faces.size(); i += 6) {
            const QVector3D vertex(faces[i], faces[i + 1], faces[i + 2]);
            const float radius = (vertex - center).length();
            minRadius = std::min(minRadius, radius);
            maxRadius = std::max(maxRadius, radius);
        }
        check(minRadius > 1.0f,
              QStringLiteral("the surface stands off the nuclei (min %1 A)")
                  .arg(static_cast<double>(minRadius), 0, 'f', 2));
        check(maxRadius < 4.5f,
              QStringLiteral("and hugs the molecule rather than the box (max "
                             "%1 A)")
                  .arg(static_cast<double>(maxRadius), 0, 'f', 2));

        // Only the atoms in a MolecularSurface cast contribute.
        std::vector<CastStyle> mixed(structureCopy.size());
        mixed[0].mode = RepresentationMode::BallAndStick;
        mixed[1].mode = RepresentationMode::MolecularSurface;
        std::vector<float> partial;
        renderer.buildMolecularSurface(&structureCopy, mixed, partial);
        check(!partial.empty() && partial.size() < faces.size(),
              "a cast holding one of the two atoms wraps only that atom");
    }

    // -- Point-of-view round trip --------------------------------------------
    //
    // This is what makes a workspace tab keep its own view: the state is
    // captured on every camera move and re-applied when the tab is shown
    // again, so capture->restore has to reproduce the view EXACTLY, not
    // approximately.
    std::printf("Point-of-view:\n");
    {
        using calango::render::CameraProjection;
        using calango::render::OrbitCamera;
        using calango::render::PointOfView;

        OrbitCamera camera;
        check(!PointOfView{}.valid,
              "a default-constructed point-of-view is not valid");

        camera.frame(QVector3D(1.0f, 2.0f, 3.0f), 4.0f);
        camera.rotate(37.0f, -12.0f);
        camera.rotateScene(QVector3D(0.0f, 0.0f, 1.0f), 25.0f);
        camera.zoom(2.0f);
        camera.pan(15.0f, -8.0f, 600);
        camera.setProjectionMode(CameraProjection::Orthographic);

        const PointOfView saved = camera.pointOfView();
        check(saved.valid, "a captured point-of-view is valid");
        const QMatrix4x4 savedView = camera.view();
        const QMatrix4x4 savedProjection = camera.projection(1.6f);

        // Move the camera somewhere else entirely — the "other tab".
        OrbitCamera other;
        other.frame(QVector3D(-9.0f, 4.0f, 0.5f), 30.0f);
        other.rotate(-100.0f, 40.0f);
        other.setProjectionMode(CameraProjection::Perspective);
        check(other.view() != savedView, "the other view really differs");

        // Switch back.
        other.setPointOfView(saved);
        const auto identical = [](const QMatrix4x4& a, const QMatrix4x4& b) {
            for (int i = 0; i < 16; ++i)
                if (std::abs(a.constData()[i] - b.constData()[i]) > 1e-5f)
                    return false;
            return true;
        };
        check(identical(other.view(), savedView),
              "restoring reproduces the view matrix exactly");
        check(identical(other.projection(1.6f), savedProjection),
              "and the projection, including the orthographic mode");

        // An invalid point-of-view must be ignored rather than snapping the
        // camera to a default — that is what lets a never-shown tab frame
        // normally the first time it is opened.
        const QMatrix4x4 before = other.view();
        other.setPointOfView(PointOfView{});
        check(identical(other.view(), before),
              "an invalid point-of-view leaves the camera untouched");

        // A hand-edited or corrupt distance must not produce a singular view.
        PointOfView degenerate = saved;
        degenerate.distance = 0.0f;
        other.setPointOfView(degenerate);
        check(other.distance() > 0.0f, "a zero distance is clamped, not applied");
    }

    // -- POV-Ray ------------------------------------------------------------
    std::printf("POV-Ray scene:\n");
    auto povInputs = makeInputs(structure);
    const QString pov = RayTraceExporter::povray(povInputs);
    check(pov.contains(QStringLiteral("camera {")), "has a camera block");
    check(pov.contains(QStringLiteral("perspective")),
          "is perspective when the camera says so");
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
