// Translucent-rendering test: renders real frames and compares pixels.
//
// Dialling a cast's opacity from 1.00 down to 0.95 used to change the picture
// out of all proportion to the 5 % it asked for: the whole unit-cell wireframe
// vanished, and every bond painted itself over the atoms it ends inside. Two
// separate causes, both invisible to a compile and to every other test here,
// because a wrong frame is still a frame.
//
//   * the cell tubes go through the same shader program as the atoms, and the
//     atom pass leaves uFinishPass at 1 whenever anything is translucent —
//     which makes mesh.frag discard every opaque instance, the cell among them;
//   * the blended pass ran with depth writes off and no depth pre-pass, so
//     within the translucent set the draw ORDER decided occlusion, and bonds
//     are drawn after spheres.
//
// The invariant asserted here is the one a user would state: at 99 % opacity
// the picture must look essentially like the opaque one. Both bugs violate it
// enormously, and neither can be fixed by accident.
//
// Self-skips (exit 0) when no GL 3.3 context can be created — the offscreen Qt
// platform plugin and headless CI boxes have no GL, and that is not a
// rendering defect.

#include "core/Structure.hpp"
#include "core/UnitCell.hpp"
#include "render/Camera.hpp"
#include "render/StructureRenderer.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace calango;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

constexpr int kSize = 256;

/// A four-atom gold chain inside a cubic cell. Gold for its bright CPK colour
/// (a dark species blends into the background and makes every difference here
/// unmeasurably small) and a chain rather than a cluster so that from the
/// chosen angle bonds pass BEHIND spheres — which is the configuration the
/// depth ordering has to get right.
core::Structure makeScene()
{
    core::Structure structure;
    structure.setCell(core::UnitCell({8, 0, 0}, {0, 8, 0}, {0, 0, 8}));
    const double positions[4][3] = {
        {2.2, 4.0, 4.0}, {4.0, 4.0, 4.2}, {4.0, 5.6, 3.8}, {5.8, 5.6, 4.0}};
    for (const auto& p : positions) {
        core::Atom atom;
        atom.atomicNumber = 79; // Au
        atom.position = {p[0], p[1], p[2]};
        structure.addAtom(atom);
    }
    return structure;
}

/// Render one frame at the given cast-0 opacity, optionally with the unit
/// cell's faces filled.
QImage renderAt(QOpenGLFunctions_3_3_Core* gl, const core::Structure& structure,
                float opacity, bool fillCell = false,
                float fillAlpha = 0.15f)
{
    QOpenGLFramebufferObject fbo(kSize, kSize,
                                 QOpenGLFramebufferObject::CombinedDepthStencil);
    fbo.bind();
    gl->glViewport(0, 0, kSize, kSize);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthFunc(GL_LESS);
    gl->glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    render::StructureRenderer renderer;
    renderer.initialize(gl);
    renderer.style().opacity = opacity;
    renderer.style().showCell = true;
    renderer.style().cellLineWidth = 2.0f;   // > 1 → the lit-tube path
    renderer.style().fillCell = fillCell;
    renderer.style().cellFillAlpha = fillAlpha;
    // Big spheres and fat bonds: the whole point is to put a lot of overlapping
    // translucent geometry on screen, since a scene that barely covers any
    // pixels cannot show an ordering error however wrong the ordering is.
    renderer.style().atomScaleFactor = 2.0f;
    renderer.style().bondWidthFactor = 3.0f;
    renderer.setStructure(&structure);

    render::OrbitCamera camera;
    camera.frame(QVector3D(4.0f, 4.7f, 4.0f), 3.6f);
    camera.rotate(30.0f, 0.0f); // an angle where bonds pass behind spheres
    renderer.render(camera.view(), camera.projection(1.0f));

    gl->glFinish();
    // glReadPixels rather than QOpenGLFramebufferObject::toImage(): blending
    // writes the destination ALPHA as well as the colour (dst_a = a² + (1-a)a
    // for a source alpha a), and toImage() un-premultiplies by that alpha on
    // the way out — which divides the very blend this test is measuring back
    // out again and reports a translucent frame as identical to an opaque one.
    // Reading RGB straight out of the attachment has no such convention.
    QImage image(kSize, kSize, QImage::Format_RGB888);
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl->glReadPixels(0, 0, kSize, kSize, GL_RGB, GL_UNSIGNED_BYTE,
                     image.bits());
    fbo.release();
    return image;
}

/// Mean absolute per-channel difference, 0-255.
double meanDifference(const QImage& a, const QImage& b)
{
    double total = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const QColor lhs = a.pixelColor(x, y);
            const QColor rhs = b.pixelColor(x, y);
            total += std::abs(lhs.red() - rhs.red())
                + std::abs(lhs.green() - rhs.green())
                + std::abs(lhs.blue() - rhs.blue());
        }
    }
    return total / (3.0 * a.width() * a.height());
}

/// Pixels differing from the clear colour — i.e. how much was actually drawn.
int drawnPixels(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.red() > 40 || c.green() > 45 || c.blue() > 50)
                ++count;
        }
    return count;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();
    QOpenGLContext context;
    context.setFormat(format);
    if (!surface.isValid() || !context.create() || !context.makeCurrent(&surface)) {
        std::printf("no OpenGL 3.3 context available — skipping\n");
        return EXIT_SUCCESS;
    }
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(
        &context);
    if (!gl || !gl->initializeOpenGLFunctions()) {
        std::printf("no GL 3.3 core function table — skipping\n");
        return EXIT_SUCCESS;
    }

    const core::Structure structure = makeScene();
    const QImage opaque = renderAt(gl, structure, 1.00f);
    const QImage almost = renderAt(gl, structure, 0.99f);
    const QImage translucent = renderAt(gl, structure, 0.50f);
    // CALANGO_DUMP_FRAMES=<dir> writes the three frames out. When this test
    // fails, the numbers say "the picture changed" and only the images say
    // how. GL's origin is bottom-left, so they are flipped on the way out.
    if (qEnvironmentVariableIsSet("CALANGO_DUMP_FRAMES")) {
        const QString dir = qEnvironmentVariable("CALANGO_DUMP_FRAMES");
        opaque.flipped(Qt::Vertical).save(dir + "/opaque.png");
        almost.flipped(Qt::Vertical).save(dir + "/almost.png");
        translucent.flipped(Qt::Vertical).save(dir + "/translucent.png");
    }

    std::printf("The opaque frame is a frame at all:\n");
    const int opaqueDrawn = drawnPixels(opaque);
    check(opaqueDrawn > 2000, "the opaque render draws the scene");

    std::printf("99%% opacity looks like 100%% opacity:\n");
    {
        // The bug this pins: at 0.95 the cell disappeared and every bond moved
        // in front of its atoms, which is a wholesale change of the picture for
        // a 1-5 % change of alpha. A mean difference of a couple of levels is
        // the blend itself; tens of levels is a different image.
        const double difference = meanDifference(opaque, almost);
        std::printf("       mean |Δ| = %.2f / 255\n", difference);
        check(difference < 6.0,
              "a 1 % opacity change moves the picture by ~1 %");
    }

    std::printf("The unit cell survives translucency:\n");
    {
        // The cell tubes carry no cast opacity, so they must still be drawn —
        // whatever the atoms' alpha. Counting drawn pixels catches the
        // discard-everything failure directly: losing the whole wireframe
        // removes a large, thin, high-contrast structure from the frame.
        const int almostDrawn = drawnPixels(almost);
        const int translucentDrawn = drawnPixels(translucent);
        std::printf("       drawn px: opaque %d, 0.99 %d, 0.50 %d\n",
                    opaqueDrawn, almostDrawn, translucentDrawn);
        check(almostDrawn > opaqueDrawn * 9 / 10,
              "0.99 opacity keeps essentially every drawn pixel");
        check(translucentDrawn > opaqueDrawn * 3 / 4,
              "0.50 opacity still draws the cell and the atoms");
    }

    std::printf("Translucency is still visible as translucency:\n");
    {
        // The converse guard: if the fix had simply forced everything opaque,
        // every check above would pass and the feature would be gone.
        const double difference = meanDifference(opaque, translucent);
        std::printf("       mean |Δ| = %.2f / 255\n", difference);
        check(difference > 2.0, "0.50 opacity does visibly differ from opaque");
    }

    std::printf("The filled unit cell shades the box without hiding it:\n");
    {
        // Three properties, and the failure modes behind each one:
        //
        //  * it draws something — the faces stream must actually reach the GPU
        //    (an unregistered VAO or an unrebuilt buffer draws nothing and
        //    looks exactly like the toggle being off);
        //  * it does NOT write depth — a filled box that occludes is the whole
        //    point of using a blended pass, and getting that wrong hides every
        //    atom inside the cell, which is every atom;
        //  * it scales with the alpha control — a fill wired to a constant
        //    would pass the first two checks and ignore the slider.
        const QImage unfilled = renderAt(gl, structure, 1.00f, false);
        const QImage filled = renderAt(gl, structure, 1.00f, true, 0.25f);
        const QImage heavier = renderAt(gl, structure, 1.00f, true, 0.60f);
        if (qEnvironmentVariableIsSet("CALANGO_DUMP_FRAMES")) {
            const QString dir = qEnvironmentVariable("CALANGO_DUMP_FRAMES");
            filled.flipped(Qt::Vertical).save(dir + "/cell_filled.png");
            heavier.flipped(Qt::Vertical).save(dir + "/cell_filled_heavy.png");
        }

        const double lightDifference = meanDifference(unfilled, filled);
        std::printf("       mean |Δ| vs unfilled: 0.25 alpha %.2f\n",
                    lightDifference);
        check(lightDifference > 1.0, "the fill is actually drawn");

        // The faces cover far more of the frame than the wireframe does, so
        // switching them on can only ADD lit pixels. A drop means the fill is
        // writing depth and eating the scene behind it.
        const int unfilledDrawn = drawnPixels(unfilled);
        const int filledDrawn = drawnPixels(filled);
        std::printf("       drawn px: unfilled %d, filled %d\n", unfilledDrawn,
                    filledDrawn);
        check(filledDrawn >= unfilledDrawn,
              "filling the cell never removes drawn pixels — it blends "
              "without writing depth");

        const double heavyDifference = meanDifference(unfilled, heavier);
        std::printf("       mean |Δ| vs unfilled: 0.60 alpha %.2f\n",
                    heavyDifference);
        check(heavyDifference > lightDifference,
              "a higher fill alpha shades the box more strongly");
    }

    std::printf("The Voronoi cell inherits the unit cell's styling:\n");
    {
        // The Wigner-Seitz cell replaces the parallelepiped in the SAME style
        // pipeline: dash pattern, tube threshold, colour and the translucent
        // fill all come from the cell settings. What is pinned here is that
        // (a) it draws something different from the box, and (b) each of those
        // settings still moves the picture once it is on — the failure mode
        // being a second geometry path that quietly ignores half of them.
        // An FCC PRIMITIVE cell, not the cubic scene above. For simple cubic
        // the Wigner-Seitz cell IS the box — correct, and useless as a test,
        // since a comparison of the two images would be measuring nothing. The
        // fcc primitive rhombohedron has a rhombic dodecahedron for its
        // Wigner-Seitz cell: 12 faces against the parallelepiped's 6, which is
        // the case the toggle exists for.
        core::Structure fcc;
        constexpr double kA = 8.0;
        fcc.setCell(core::UnitCell({0.0, kA / 2, kA / 2},
                                   {kA / 2, 0.0, kA / 2},
                                   {kA / 2, kA / 2, 0.0}));
        {
            core::Atom atom;
            atom.atomicNumber = 79; // Au, for the bright CPK colour
            atom.position = {kA / 2, kA / 2, kA / 2};
            fcc.addAtom(atom);
        }

        const auto renderCell = [&](bool voronoi, bool fill, float lineWidth,
                                    render::CellLineStyle lineStyle) {
            QOpenGLFramebufferObject fbo(
                kSize, kSize, QOpenGLFramebufferObject::CombinedDepthStencil);
            fbo.bind();
            gl->glViewport(0, 0, kSize, kSize);
            gl->glEnable(GL_DEPTH_TEST);
            gl->glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
            gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            render::StructureRenderer renderer;
            renderer.initialize(gl);
            renderer.style().showCell = true;
            renderer.style().showVoronoiCell = voronoi;
            renderer.style().fillCell = fill;
            renderer.style().cellFillAlpha = 0.5f;
            renderer.style().cellLineWidth = lineWidth;
            renderer.style().cellLineStyle = lineStyle;
            renderer.style().atomScaleFactor = 0.4f;
            renderer.setStructure(&fcc);
            render::OrbitCamera camera;
            camera.frame(QVector3D(4.0f, 4.0f, 4.0f), 7.0f);
            camera.rotate(25.0f, 15.0f);
            renderer.render(camera.view(), camera.projection(1.0f));
            gl->glFinish();
            QImage image(kSize, kSize, QImage::Format_RGB888);
            gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            gl->glReadPixels(0, 0, kSize, kSize, GL_RGB, GL_UNSIGNED_BYTE,
                             image.bits());
            fbo.release();
            return image;
        };

        const QImage box =
            renderCell(false, false, 2.0f, render::CellLineStyle::Solid);
        const QImage voronoi =
            renderCell(true, false, 2.0f, render::CellLineStyle::Solid);
        if (qEnvironmentVariableIsSet("CALANGO_DUMP_FRAMES")) {
            const QString dir = qEnvironmentVariable("CALANGO_DUMP_FRAMES");
            box.flipped(Qt::Vertical).save(dir + "/cell_box.png");
            voronoi.flipped(Qt::Vertical).save(dir + "/cell_voronoi.png");
        }
        check(drawnPixels(voronoi) > 1000, "the Voronoi cell draws something");
        std::printf("       mean |Δ| box vs voronoi = %.2f\n",
                    meanDifference(box, voronoi));
        check(meanDifference(box, voronoi) > 1.0,
              "and is a different shape from the parallelepiped");

        // Each inherited setting has to reach it.
        const QImage dashed =
            renderCell(true, false, 2.0f, render::CellLineStyle::Dashed);
        check(meanDifference(voronoi, dashed) > 0.5,
              "the cell line style applies to it");
        const QImage thin =
            renderCell(true, false, 1.0f, render::CellLineStyle::Solid);
        check(meanDifference(voronoi, thin) > 0.5,
              "the cell line width applies to it");
        const QImage filled =
            renderCell(true, true, 2.0f, render::CellLineStyle::Solid);
        check(meanDifference(voronoi, filled) > 1.0,
              "and so does the fill, at its own opacity");
        if (qEnvironmentVariableIsSet("CALANGO_DUMP_FRAMES")) {
            filled.flipped(Qt::Vertical).save(
                qEnvironmentVariable("CALANGO_DUMP_FRAMES")
                + "/cell_voronoi_filled.png");
        }
    }

    std::printf(failures == 0 ? "\nAll translucent-render checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
