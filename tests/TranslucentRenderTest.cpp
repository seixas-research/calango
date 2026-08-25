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

/// Render one frame carrying ONLY the hydrogen-bond overlay: no atoms, no
/// bonds, no cell. The overlay is what is being counted, and a frame with
/// the structure in it would drown a few dozen thin marks in ten thousand
/// pixels of gold.
///
/// The contacts are two long horizontal lines across the middle of the cell,
/// far enough apart to stay separate at every width tested here.
QImage renderHydrogenBonds(QOpenGLFunctions_3_3_Core* gl,
                           render::HydrogenBondLineStyle style, float width,
                           bool enabled = true)
{
    QOpenGLFramebufferObject fbo(kSize, kSize,
                                 QOpenGLFramebufferObject::CombinedDepthStencil);
    fbo.bind();
    gl->glViewport(0, 0, kSize, kSize);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthFunc(GL_LESS);
    gl->glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    core::Structure empty;
    empty.setCell(core::UnitCell({8, 0, 0}, {0, 8, 0}, {0, 0, 8}));
    render::StructureRenderer renderer;
    renderer.initialize(gl);
    renderer.style().showCell = false;
    renderer.setStructure(&empty);

    std::vector<float> stream;
    if (enabled) {
        const std::vector<std::pair<QVector3D, QVector3D>> contacts = {
            {QVector3D(1.5f, 3.4f, 4.0f), QVector3D(6.5f, 3.4f, 4.0f)},
            {QVector3D(1.5f, 4.6f, 4.0f), QVector3D(6.5f, 4.6f, 4.0f)},
        };
        render::StructureRenderer::buildHydrogenBondDashes(
            contacts, QColor(120, 200, 255), 0.18f, style, width, stream);
    }
    renderer.setHydrogenBonds(stream);

    render::OrbitCamera camera;
    // Straight on, so a mark's drawn area is its length times its width and
    // nothing is foreshortened — the counts below are then comparable
    // between styles rather than between projections.
    camera.frame(QVector3D(4.0f, 4.0f, 4.0f), 3.6f);
    renderer.render(camera.view(), camera.projection(1.0f));

    gl->glFinish();
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

/// Whether this pixel of `image` has anything drawn on it at all.
bool isDrawn(const QImage& image, int x, int y)
{
    const QColor c = image.pixelColor(x, y);
    return c.red() > 40 || c.green() > 45 || c.blue() > 50;
}

/// Mean |Δ| over the pixels `mask` has something drawn on. Used to ask a
/// question the whole-frame mean cannot: did the picture change WHERE THE
/// STRUCTURE IS, as opposed to somewhere else in the frame.
double meanDifferenceWhereDrawn(const QImage& a, const QImage& b,
                                const QImage& mask)
{
    double total = 0.0;
    int counted = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            if (!isDrawn(mask, x, y))
                continue;
            const QColor lhs = a.pixelColor(x, y);
            const QColor rhs = b.pixelColor(x, y);
            total += std::abs(lhs.red() - rhs.red())
                + std::abs(lhs.green() - rhs.green())
                + std::abs(lhs.blue() - rhs.blue());
            ++counted;
        }
    return counted > 0 ? total / (3.0 * counted) : 0.0;
}

/// Mean channel value over the whole frame — how much light is in the
/// picture. Shadows can only take light away, so this is the direct read-out
/// of "something is being shadowed".
double meanBrightness(const QImage& image)
{
    double total = 0.0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            total += c.red() + c.green() + c.blue();
        }
    return total / (3.0 * image.width() * image.height());
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

    std::printf("A translucent cell edge survives too (Style::cellEdgeAlpha):\n");
    {
        // The exact same discard-in-the-opaque-pass failure mode as "The unit
        // cell survives translucency" above, but pinned against the OTHER way
        // a cell tube instance can end up with vColor.a < 0.999 in mesh.frag:
        // its own edge opacity (Unit Cell tab, "Cell color" row) rather than a
        // cast's. The cell-tube draw call used to gate its second, blended
        // pass on `surfaceFinish == Glassy` alone — cellEdgeAlpha below 1 with
        // any other finish discarded every instance in the opaque pass and
        // never reached a pass that would draw it, so the wireframe simply
        // vanished exactly as it did for the cast-opacity bug.
        const auto renderCellEdge = [gl](const core::Structure& s, float edgeAlpha) {
            QOpenGLFramebufferObject fbo(
                kSize, kSize, QOpenGLFramebufferObject::CombinedDepthStencil);
            fbo.bind();
            gl->glViewport(0, 0, kSize, kSize);
            gl->glEnable(GL_DEPTH_TEST);
            gl->glDepthFunc(GL_LESS);
            gl->glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
            gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            render::StructureRenderer renderer;
            renderer.initialize(gl);
            renderer.style().showCell = true;
            renderer.style().cellLineWidth = 3.0f; // > 1 -> the lit-tube path
            renderer.style().cellEdgeAlpha = edgeAlpha;
            // Standard finish, deliberately not Glassy: the bug is that a
            // faded edge needed the SAME second pass Glassy already used,
            // and testing with Glassy already selected would hide a
            // regression that only reappears once it is not.
            renderer.style().surfaceFinish = render::SurfaceFinish::Standard;
            renderer.setStructure(&s);

            render::OrbitCamera camera;
            camera.frame(QVector3D(4.0f, 4.0f, 4.0f), 3.6f);
            camera.rotate(30.0f, 20.0f);
            renderer.render(camera.view(), camera.projection(1.0f));

            gl->glFinish();
            QImage image(kSize, kSize, QImage::Format_RGB888);
            gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            gl->glReadPixels(0, 0, kSize, kSize, GL_RGB, GL_UNSIGNED_BYTE,
                            image.bits());
            fbo.release();
            return image;
        };

        // A near-empty cell — one small atom, tucked in a corner rather than
        // centred, so the wireframe dominates the frame rather than being
        // diluted by a large or central sphere. A wholly EMPTY structure
        // will not do: StructureRenderer's whole geometry build, cell
        // wireframe included, is gated on `structure && !structure->empty()`
        // (StructureRenderer.cpp), so zero atoms means no cell tubes either
        // — not the bug this test is about.
        core::Structure cellOnly;
        cellOnly.setCell(core::UnitCell({8, 0, 0}, {0, 8, 0}, {0, 0, 8}));
        {
            core::Atom corner;
            corner.atomicNumber = 1; // H: the smallest sphere radius
            corner.position = {0.3, 0.3, 0.3};
            cellOnly.addAtom(corner);
        }
        const QImage opaqueEdge = renderCellEdge(cellOnly, 1.0f);
        const QImage fadedEdge = renderCellEdge(cellOnly, 0.4f);
        const int opaqueEdgeDrawn = drawnPixels(opaqueEdge);
        const int fadedEdgeDrawn = drawnPixels(fadedEdge);
        std::printf("       drawn px: opaque edge %d, 0.40 alpha %d\n",
                    opaqueEdgeDrawn, fadedEdgeDrawn);
        check(opaqueEdgeDrawn > 200, "the opaque wireframe alone is a frame");
        check(fadedEdgeDrawn > opaqueEdgeDrawn / 4,
              "a 0.40-alpha edge still draws — not discarded into an empty "
              "frame the way it was before the second pass covered it");

        // The converse guard, same reasoning as "Translucency is still
        // visible as translucency" above: presence alone would also pass if
        // the fix had simply forced every cell edge back to opaque. Measured
        // WHERE DRAWN, not over the whole frame — the wireframe alone covers
        // only a few percent of it, which would dilute a whole-frame mean
        // into looking like no change at all even though every edge pixel
        // did change.
        const double edgeDifference =
            meanDifferenceWhereDrawn(opaqueEdge, fadedEdge, opaqueEdge);
        std::printf("       mean |Δ| where drawn = %.2f / 255\n", edgeDifference);
        check(edgeDifference > 2.0,
              "and a 0.40-alpha edge visibly differs from the opaque one — "
              "genuinely blended, not just present");
    }

    std::printf("A translucent vector arrow survives too (per-overlay opacity):\n");
    {
        // The identical mechanism once more, on a third style field: a
        // vector overlay's shaft and head share sphere_/cylinder_/cone_ with
        // the atoms and bonds (StructureRenderer.cpp's addArrows()), gated
        // by the SAME anyTranslucentCast() this pass extended to also ask
        // "is the active overlay's own opacity below 1?" — before that, a
        // faded Force/Velocity/Magnetic-moment overlay with every cast
        // otherwise opaque would have been discarded in the opaque pass with
        // no blended pass running to draw it either.
        core::Structure withForces = makeScene();
        std::vector<core::Vec3> forces(4, core::Vec3{0.0, 0.0, 3.0});
        withForces.setVectorField("forces", forces);

        const auto renderForceOverlay = [gl](const core::Structure& s,
                                             float overlayAlpha) {
            QOpenGLFramebufferObject fbo(
                kSize, kSize, QOpenGLFramebufferObject::CombinedDepthStencil);
            fbo.bind();
            gl->glViewport(0, 0, kSize, kSize);
            gl->glEnable(GL_DEPTH_TEST);
            gl->glDepthFunc(GL_LESS);
            gl->glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
            gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            render::StructureRenderer renderer;
            renderer.initialize(gl);
            renderer.style().vectorOverlay = render::VectorOverlay::Force;
            renderer.style().forceOpacity = overlayAlpha;
            renderer.style().surfaceFinish = render::SurfaceFinish::Standard;
            renderer.setStructure(&s);

            render::OrbitCamera camera;
            camera.frame(QVector3D(4.0f, 4.7f, 4.0f), 3.6f);
            camera.rotate(30.0f, 0.0f);
            renderer.render(camera.view(), camera.projection(1.0f));

            gl->glFinish();
            QImage image(kSize, kSize, QImage::Format_RGB888);
            gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            gl->glReadPixels(0, 0, kSize, kSize, GL_RGB, GL_UNSIGNED_BYTE,
                            image.bits());
            fbo.release();
            return image;
        };

        // Opacity 0.0 is the "no overlay" baseline: the arrow's own colour
        // fully vanishes into whatever is behind it (SRC_ALPHA blending with
        // alpha 0 is a no-op on the destination), leaving exactly the
        // atoms/bonds beneath — reusing the same render path rather than a
        // second one that sets vectorOverlay to None.
        const QImage noOverlay = renderForceOverlay(withForces, 0.0f);
        const QImage opaqueForce = renderForceOverlay(withForces, 1.0f);
        const QImage fadedForce = renderForceOverlay(withForces, 0.4f);
        const int opaqueForceDrawn = drawnPixels(opaqueForce);
        const int fadedForceDrawn = drawnPixels(fadedForce);
        std::printf("       drawn px: opaque overlay %d, 0.40 alpha %d\n",
                    opaqueForceDrawn, fadedForceDrawn);
        check(opaqueForceDrawn > 500, "the force overlay draws something");
        check(fadedForceDrawn > opaqueForceDrawn / 4,
              "and a 0.40-alpha overlay still draws — not discarded before "
              "anyTranslucentCast() knew to run the blended pass for it");

        // Measured over the ARROWS ALONE, not the whole frame: the atoms and
        // bonds beneath them are identical in both renders (only the
        // overlay's own opacity changed), and they cover far more of the
        // frame than the arrows do — a whole-frame mean dilutes exactly the
        // way the cell-edge check above would have without its own
        // drawn-only mask.
        double arrowDifference = 0.0;
        int arrowPixels = 0;
        for (int y = 0; y < opaqueForce.height(); ++y)
            for (int x = 0; x < opaqueForce.width(); ++x) {
                if (!isDrawn(opaqueForce, x, y) || !isDrawn(fadedForce, x, y))
                    continue;
                if (isDrawn(noOverlay, x, y))
                    continue; // an atom/bond pixel, identical in both renders
                const QColor lhs = opaqueForce.pixelColor(x, y);
                const QColor rhs = fadedForce.pixelColor(x, y);
                arrowDifference += std::abs(lhs.red() - rhs.red())
                    + std::abs(lhs.green() - rhs.green())
                    + std::abs(lhs.blue() - rhs.blue());
                ++arrowPixels;
            }
        arrowDifference = arrowPixels > 0 ? arrowDifference / (3.0 * arrowPixels) : 0.0;
        std::printf("       arrow-only px: %d, mean |Δ| = %.2f / 255\n",
                    arrowPixels, arrowDifference);
        check(arrowPixels > 50, "found arrow-only pixels to compare (not "
                                "just the atoms/bonds beneath them)");
        check(arrowDifference > 2.0,
              "and the arrow pixels visibly differ from the opaque overlay "
              "— genuinely blended, not just present");
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

    std::printf("The cell reacts to the scene lights:\n");
    {
        // Edges and fill are both shaded now: the edges are lit tubes at every
        // width (they used to drop to unlit GL_LINES below 1.01), and the fill
        // carries per-face normals through the lit surface program. The test
        // is to MOVE THE LIGHT and see the picture change — a flat, unlit
        // surface is invariant under that, which is exactly the old behaviour.
        const auto renderLit = [&](const QVector3D& lightDir, bool fill,
                                   float lineWidth) {
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
            renderer.style().fillCell = fill;
            renderer.style().cellFillAlpha = 0.85f;
            renderer.style().cellLineWidth = lineWidth;
            renderer.style().atomScaleFactor = 0.3f;
            // lights() hands back the live vector, so the direction is set
            // on it directly.
            if (!renderer.lights().empty())
                renderer.lights()[0].direction = lightDir;
            renderer.setStructure(&structure);
            render::OrbitCamera camera;
            camera.frame(QVector3D(4.0f, 4.0f, 4.0f), 9.0f);
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

        const QVector3D front(-0.4f, -0.5f, -1.0f);
        const QVector3D side(1.0f, 0.2f, -0.2f);

        // Hairline edges, no fill: the wireframe alone must respond.
        const QImage edgesA = renderLit(front, false, 1.0f);
        const QImage edgesB = renderLit(side, false, 1.0f);
        check(drawnPixels(edgesA) > 200, "thin cell edges are drawn at all");
        std::printf("       edges, mean |Δ| under a light move = %.2f\n",
                    meanDifference(edgesA, edgesB));
        check(meanDifference(edgesA, edgesB) > 0.2,
              "thin cell edges are lit — moving the light changes them");

        // And the filled faces.
        const QImage fillA = renderLit(front, true, 1.0f);
        const QImage fillB = renderLit(side, true, 1.0f);
        std::printf("       fill, mean |Δ| under a light move  = %.2f\n",
                    meanDifference(fillA, fillB));
        check(meanDifference(fillA, fillB) > 0.5,
              "the filled cell is lit — moving the light changes it");
        if (qEnvironmentVariableIsSet("CALANGO_DUMP_FRAMES")) {
            const QString dir = qEnvironmentVariable("CALANGO_DUMP_FRAMES");
            fillA.flipped(Qt::Vertical).save(dir + "/cell_lit_front.png");
            fillB.flipped(Qt::Vertical).save(dir + "/cell_lit_side.png");
        }
    }

    std::printf("The floor is placed by the geometry, not by a guess:\n");
    {
        // GL-free arithmetic, checked against closed forms rather than against
        // a previous run. This is the half of the feature that decides whether
        // the molecule rests on the plane or hovers over it / sinks into it,
        // and it is shared verbatim with the ray-trace and Alembic exporters —
        // so every one of them is wrong together or right together.
        using render::StructureRenderer;
        StructureRenderer::Style style;
        style.floorEnabled = true;
        style.showCell = false; // no cell: the atoms alone set the level

        const auto base = StructureRenderer::floorBase(&structure, style);
        check(base.visible, "a structure gets a floor");

        // The floor is a plane of constant Z — Calango is a Z-up application
        // (see FloorPlacement, and the default camera pinned in CameraTest).
        // The lowest Au in makeScene() is at z = 3.8; the plane clears the
        // BOTTOM OF ITS SPHERE by the fixed 0.25 Å margin, so no atom can
        // intersect it however the radii are scaled.
        const float radius = StructureRenderer::displayRadius(79, style);
        const float expected = 3.8f - radius - 0.25f;
        std::printf("       auto height %.4f, expected %.4f (r_Au = %.4f)\n",
                    static_cast<double>(base.center.z()),
                    static_cast<double>(expected),
                    static_cast<double>(radius));
        check(std::abs(base.center.z() - expected) < 1e-4f,
              "it sits one clearance below the lowest atom's SPHERE");

        // Extents scale with the footprint, and the fade starts well outside
        // it — that is what makes a finite quad read as ground rather than as
        // a tile the structure is standing on.
        check(base.reach >= 1.0f, "the reach is floored for a tiny structure");
        check(std::abs(base.solidRadius - base.reach * 3.5f) < 1e-4f,
              "solid out to 3.5 footprints");
        check(std::abs(base.fadeRadius - base.reach * 12.0f) < 1e-4f,
              "faded out by 12");
        check(std::abs(base.halfSize - base.fadeRadius) < 1e-4f,
              "and the quad reaches exactly as far as the fade does");

        // The manual adjustment is an OFFSET on top of that automatic level,
        // so it survives a structure change instead of being overwritten by
        // it — and it moves the plane by exactly what it says.
        style.floorOffset = 1.25f;
        const auto lifted = StructureRenderer::floorPlacement(&structure, style);
        check(lifted.visible, "the placement is visible when the floor is on");
        check(std::abs(lifted.center.z() - (expected + 1.25f)) < 1e-4f,
              "the offset shifts the plane by exactly its own value");
        style.floorOffset = 0.0f;

        style.floorEnabled = false;
        check(!StructureRenderer::floorPlacement(&structure, style).visible,
              "and nothing is placed when the floor is off");
        style.floorEnabled = true;
        check(!StructureRenderer::floorBase(nullptr, style).visible,
              "an empty scene has nothing to rest on a floor");

        // A hidden atom must not push the plane down: the user would see a
        // gap under the molecule with nothing in it.
        core::Structure withHydrogen = makeScene();
        {
            core::Atom h;
            h.atomicNumber = 1;
            h.position = {4.0, 4.0, 0.5}; // far below the gold
            withHydrogen.addAtom(h);
        }
        style.showHydrogens = false;
        const auto hidden = StructureRenderer::floorBase(&withHydrogen, style);
        check(std::abs(hidden.center.z() - expected) < 1e-4f,
              "a hidden hydrogen does not drag the floor down with it");
        style.showHydrogens = true;
        const auto shown = StructureRenderer::floorBase(&withHydrogen, style);
        check(shown.center.z() < expected - 1.0f,
              "a shown one does");
        style.showHydrogens = true;

        // Periodic case: the box is what visibly stands on the floor, and it
        // reaches below the atoms. makeScene()'s cell has its origin corner at
        // the origin, so the plane lands one clearance under z = 0.
        style.showCell = true;
        const auto withCell = StructureRenderer::floorBase(&structure, style);
        check(std::abs(withCell.center.z() - (-0.25f)) < 1e-4f,
              "with the cell drawn, the floor sits under the CELL");
    }

    std::printf("The floor can be turned, and the placement follows it:\n");
    {
        using render::StructureRenderer;
        StructureRenderer::Style style;
        style.floorEnabled = true;
        style.showCell = false;
        const float radius = StructureRenderer::displayRadius(79, style);

        // The frame. Every other property below is derived from it, and the
        // default has to reproduce the pre-orientation (x, y, z) axes exactly
        // or every existing placement shifts.
        QVector3D u, v, n;
        StructureRenderer::floorBasis(QVector3D(0.0f, 0.0f, 1.0f), u, v, n);
        check(u == QVector3D(1, 0, 0) && v == QVector3D(0, 1, 0)
                  && n == QVector3D(0, 0, 1),
              "the default +z normal gives back exactly the (x, y, z) frame");
        for (const QVector3D& axis : {QVector3D(1, 0, 0), QVector3D(0, 1, 0),
                                      QVector3D(0, 0, 1),
                                      QVector3D(-2.0f, 3.0f, 0.5f)}) {
            StructureRenderer::floorBasis(axis, u, v, n);
            const bool orthonormal =
                std::abs(u.length() - 1.0f) < 1e-5f
                && std::abs(v.length() - 1.0f) < 1e-5f
                && std::abs(n.length() - 1.0f) < 1e-5f
                && std::abs(QVector3D::dotProduct(u, v)) < 1e-5f
                && std::abs(QVector3D::dotProduct(u, n)) < 1e-5f
                && std::abs(QVector3D::dotProduct(v, n)) < 1e-5f;
            check(orthonormal, "the frame is orthonormal for any normal");
            // Right-handed, which is what lets ONE winding face along n
            // whichever way the plane is turned.
            check((QVector3D::crossProduct(u, v) - n).lengthSquared() < 1e-8f,
                  "and right-handed: u x v = n");
        }
        // A zero normal cannot throw NaNs into the geometry; the UI rejects it
        // at entry, this is the backstop.
        StructureRenderer::floorBasis(QVector3D(0, 0, 0), u, v, n);
        check(n == QVector3D(0, 0, 1), "a zero normal falls back to +z");

        // Placement in the turned frame. makeScene() spans x 2.2..5.8,
        // y 4.0..5.6, z 3.8..4.2, so each orientation has a different extreme
        // to sit under — which is the point: the plane must track the
        // structure along ITS OWN normal, not along z.
        struct Case { QVector3D normal; float extreme; const char* what; };
        const Case cases[] = {
            {{0, 0, 1}, 3.8f, "xy (normal z) rests under the lowest z"},
            {{0, 1, 0}, 4.0f, "xz (normal y) rests under the lowest y"},
            {{1, 0, 0}, 2.2f, "yz (normal x) rests under the lowest x"},
        };
        for (const Case& c : cases) {
            style.floorNormal = c.normal;
            const auto base = StructureRenderer::floorBase(&structure, style);
            const float along = QVector3D::dotProduct(base.center, c.normal);
            std::printf("       normal (%g %g %g): plane at %.4f, expected "
                        "%.4f\n", double(c.normal.x()), double(c.normal.y()),
                        double(c.normal.z()), double(along),
                        double(c.extreme - radius - 0.25f));
            check(std::abs(along - (c.extreme - radius - 0.25f)) < 1e-4f,
                  c.what);
        }

        // The extent is measured in the PLANE's axes. The scene is 3.6 A wide
        // in x and only 0.4 A deep in z, so the xy plane and the yz plane must
        // come out very different sizes — a footprint computed in world xy
        // regardless of orientation would make them identical.
        style.floorNormal = QVector3D(0, 0, 1);
        const float reachXY = StructureRenderer::floorBase(&structure, style).reach;
        style.floorNormal = QVector3D(1, 0, 0);
        const float reachYZ = StructureRenderer::floorBase(&structure, style).reach;
        std::printf("       reach: xy plane %.3f, yz plane %.3f\n",
                    double(reachXY), double(reachYZ));
        check(reachXY > reachYZ + 0.5f,
              "the plane is fitted in its own axes, not in world xy");

        // The height offset travels along the normal too, or "raise the floor"
        // would mean something different for every orientation.
        style.floorNormal = QVector3D(0, 1, 0);
        style.floorOffset = 1.5f;
        const auto lifted = StructureRenderer::floorPlacement(&structure, style);
        style.floorOffset = 0.0f;
        const auto resting = StructureRenderer::floorPlacement(&structure, style);
        check((lifted.center - resting.center - QVector3D(0, 1.5f, 0))
                  .lengthSquared() < 1e-6f,
              "and the height offset moves it along the normal");
        // Length is irrelevant: the normal is normalized before use.
        style.floorNormal = QVector3D(0, 7.0f, 0);
        const auto scaled = StructureRenderer::floorBase(&structure, style);
        style.floorNormal = QVector3D(0, 1, 0);
        const auto unit = StructureRenderer::floorBase(&structure, style);
        check((scaled.center - unit.center).lengthSquared() < 1e-8f,
              "a non-unit normal is the same plane as its unit version");

        // The rule the Floor tab's dropdown is a read-out of. It lives here
        // rather than in the panel so it can be checked without a GL context
        // and a window — and so there is exactly one answer to "is this normal
        // an axis?", which is what keeps the dropdown from disagreeing with
        // the numbers underneath it.
        using Preset = StructureRenderer::FloorPreset;
        const struct { QVector3D normal; Preset expected; const char* what; }
        presets[] = {
            {{0, 0, 1}, Preset::Xy, "(0,0,1) is the xy plane"},
            {{0, 1, 0}, Preset::Xz, "(0,1,0) is the xz plane"},
            {{1, 0, 0}, Preset::Yz, "(1,0,0) is the yz plane"},
            {{0, 0, 7}, Preset::Xy, "length is irrelevant: (0,0,7) is xy too"},
            {{1, 1, 0}, Preset::Custom, "a diagonal is Custom"},
            {{0, 0, 0}, Preset::Custom, "and so is a zero vector"},
            // Sign is NOT irrelevant: the plane goes on the other side of the
            // structure, which is a ceiling rather than a floor.
            {{0, 0, -1}, Preset::Custom, "a reversed normal is Custom, not xy"},
        };
        for (const auto& c : presets)
            check(StructureRenderer::floorPreset(c.normal) == c.expected,
                  c.what);
        for (const Preset preset : {Preset::Xy, Preset::Xz, Preset::Yz})
            check(StructureRenderer::floorPreset(
                      StructureRenderer::floorPresetNormal(preset)) == preset,
                  "every preset normal reads back as its own preset");
    }

    std::printf("The floor is drawn, receives the shadow, and stays out of "
                "the way:\n");
    {
        // The scene is deliberately molecule-like — no cell box — because that
        // is what the floor is for, and a wireframe cage over the plane would
        // dilute every pixel measurement below.
        const auto renderFloorOf = [&](const core::Structure& scene, bool floor,
                                       bool shadows, float offset,
                                       float pitchDeg) {
            QOpenGLFramebufferObject fbo(
                kSize, kSize, QOpenGLFramebufferObject::CombinedDepthStencil);
            fbo.bind();
            gl->glViewport(0, 0, kSize, kSize);
            gl->glEnable(GL_DEPTH_TEST);
            gl->glDepthFunc(GL_LESS);
            gl->glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
            gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            render::StructureRenderer renderer;
            renderer.initialize(gl);
            renderer.style().showCell = false;
            renderer.style().atomScaleFactor = 1.6f;
            renderer.style().bondWidthFactor = 2.0f;
            renderer.style().floorEnabled = floor;
            renderer.style().floorOffset = offset;
            renderer.style().shadowsEnabled = shadows;
            renderer.style().shadowStrength = 0.85f;
            renderer.setStructure(&scene);
            render::OrbitCamera camera;
            camera.frame(QVector3D(4.0f, 4.7f, 4.0f), 4.5f);
            // An ABSOLUTE orientation, not a rotate() delta: a fresh camera
            // already carries the default (0, -70, 20), so a relative step
            // would have to be read against that to know which side of the
            // plane the eye ends up on — which is exactly the mistake to
            // avoid in a test of a single-sided surface.
            //
            // The eye sits at z = target.z + distance·cos(pitch), so
            // |pitch| < 90 is ABOVE the floor and |pitch| > 90 is below it.
            camera.setOrientation(0.0f, pitchDeg, 0.0f);
            renderer.render(camera.view(), camera.projection(1.0f));
            gl->glFinish();
            QImage image(kSize, kSize, QImage::Format_RGB888);
            gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            gl->glReadPixels(0, 0, kSize, kSize, GL_RGB, GL_UNSIGNED_BYTE,
                             image.bits());
            fbo.release();
            return image;
        };
        const auto renderFloor = [&](bool floor, bool shadows, float offset,
                                     float pitchDeg) {
            return renderFloorOf(structure, floor, shadows, offset, pitchDeg);
        };

        const QImage bare = renderFloor(false, false, 0.0f, -60.0f);
        const QImage withFloor = renderFloor(true, false, 0.0f, -60.0f);
        const QImage bareShadow = renderFloor(false, true, 0.0f, -60.0f);
        const QImage floorShadow = renderFloor(true, true, 0.0f, -60.0f);
        if (qEnvironmentVariableIsSet("CALANGO_DUMP_FRAMES")) {
            const QString dir = qEnvironmentVariable("CALANGO_DUMP_FRAMES");
            bare.flipped(Qt::Vertical).save(dir + "/floor_bare.png");
            bareShadow.flipped(Qt::Vertical).save(dir + "/floor_bare_shadow.png");
            withFloor.flipped(Qt::Vertical).save(dir + "/floor.png");
            floorShadow.flipped(Qt::Vertical).save(dir + "/floor_shadow.png");
        }

        // 1. It is drawn at all. The failure this catches is a winding
        //    mistake: floor.frag discards the back face, so a quad wound the
        //    other way is invisible from every angle it should be seen from
        //    and looks exactly like the toggle doing nothing.
        const int bareDrawn = drawnPixels(bare);
        const int floorDrawn = drawnPixels(withFloor);
        std::printf("       drawn px: no floor %d, floor %d\n", bareDrawn,
                    floorDrawn);
        check(floorDrawn > bareDrawn + 5000,
              "switching the floor on covers a large part of the frame");

        // 2. It does not eat the structure. The plane writes depth (SSAO and
        //    depth-of-field both read it), so an occluding floor is a real
        //    possibility rather than a theoretical one — and it would be
        //    invisible to a whole-frame mean, which the new ground pixels
        //    dominate. Masking to what the structure drew asks the question
        //    directly.
        const double overStructure =
            meanDifferenceWhereDrawn(bare, withFloor, bare);
        std::printf("       mean |Δ| over the structure's own pixels = %.3f\n",
                    overStructure);
        check(overStructure < 1.0,
              "the floor leaves every pixel of the structure alone");

        // 3. The structure's shadow lands on it. Shadows only ever remove
        //    light, so a floor that receives one must darken; and it must
        //    darken MORE than the shadowless scene does, where the only
        //    surfaces available to shade are the atoms themselves.
        const double lostWithFloor =
            meanBrightness(withFloor) - meanBrightness(floorShadow);
        const double lostWithout =
            meanBrightness(bare) - meanBrightness(bareShadow);
        std::printf("       light lost to shadows: with floor %.3f, "
                    "without %.3f\n", lostWithFloor, lostWithout);
        check(lostWithFloor > 0.5, "the floor gets visibly darker in shadow");
        check(lostWithFloor > lostWithout * 1.5,
              "there is much more shadow to see once there is a floor to "
              "catch it");

        // 4. The height offset moves it. Lifting the plane a long way up puts
        //    it through the middle of the molecule, which no amount of
        //    ignoring the setting could reproduce.
        const QImage lifted = renderFloor(true, false, 3.0f, -60.0f);
        std::printf("       mean |Δ| under a 3 Å lift = %.3f\n",
                    meanDifference(withFloor, lifted));
        check(meanDifference(withFloor, lifted) > 2.0,
              "raising the floor moves it");

        // 5. Seen from below it is not there. An opaque plate that swallowed
        //    the scene whenever the camera passed under it would be correct
        //    and useless; floor.frag discards the back face for exactly this.
        const QImage underBare = renderFloor(false, false, 0.0f, -120.0f);
        const QImage underFloor = renderFloor(true, false, 0.0f, -120.0f);
        std::printf("       mean |Δ| from below = %.3f\n",
                    meanDifference(underBare, underFloor));
        check(meanDifference(underBare, underFloor) < 0.5,
              "from underneath, the floor does not hide the structure");

        // 6. Only what is DRAWN casts. The cell-tube instances are built
        //    whenever the structure has a cell, and `showCell` gates the draw
        //    rather than the build — so the depth pass used to cast twelve
        //    thin shadows of a wireframe nobody could see. It went unnoticed
        //    for as long as the only surfaces around to catch them were the
        //    atoms; on a ground plane it is a rectangle ruled across the
        //    floor. The same scene with the cell REMOVED is the reference:
        //    with the cell merely hidden, the picture has to match it.
        core::Structure noCell;
        for (const core::Atom& atom : structure.atoms())
            noCell.addAtom(atom);
        const QImage hiddenCell = renderFloor(true, true, 0.0f, -60.0f);
        const QImage noCellAtAll =
            renderFloorOf(noCell, true, true, 0.0f, -60.0f);
        std::printf("       mean |Δ| hidden cell vs no cell = %.3f\n",
                    meanDifference(hiddenCell, noCellAtAll));
        check(meanDifference(hiddenCell, noCellAtAll) < 0.2,
              "a hidden unit cell casts no shadow onto the floor");
    }

    std::printf("A turned floor is drawn and still catches the shadow:\n");
    {
        // A VERTICAL plane, used as a backdrop: normal +x, so it lands just
        // beyond the low-x end of the chain, and the light is re-aimed down
        // -x so the structure throws its shadow onto it. Both halves matter —
        // the plane has to be drawn facing the right way (one winding serves
        // every orientation only because the frame is right-handed), and the
        // shadow lookup has to work off the fragment's real normal rather than
        // an assumed +z.
        const auto renderTurned = [&](bool floor, bool shadows) {
            QOpenGLFramebufferObject fbo(
                kSize, kSize, QOpenGLFramebufferObject::CombinedDepthStencil);
            fbo.bind();
            gl->glViewport(0, 0, kSize, kSize);
            gl->glEnable(GL_DEPTH_TEST);
            gl->glDepthFunc(GL_LESS);
            gl->glClearColor(0.1f, 0.11f, 0.13f, 1.0f);
            gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            render::StructureRenderer renderer;
            renderer.initialize(gl);
            renderer.style().showCell = false;
            renderer.style().atomScaleFactor = 1.6f;
            renderer.style().bondWidthFactor = 2.0f;
            renderer.style().floorEnabled = floor;
            renderer.style().floorNormal = QVector3D(1.0f, 0.0f, 0.0f);
            renderer.style().shadowsEnabled = shadows;
            renderer.style().shadowStrength = 0.85f;
            if (!renderer.lights().empty())
                renderer.lights()[0].direction = QVector3D(-1.0f, -0.2f, -0.3f);
            renderer.setStructure(&structure);
            render::OrbitCamera camera;
            camera.frame(QVector3D(4.0f, 4.7f, 4.0f), 5.0f);
            // Yaw -90 looks along -x, so the eye is on the +x side — the side
            // the plane's front face points at. (+90 would put it behind.)
            camera.setOrientation(-90.0f, -12.0f, 0.0f);
            renderer.render(camera.view(), camera.projection(1.0f));
            gl->glFinish();
            QImage image(kSize, kSize, QImage::Format_RGB888);
            gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            gl->glReadPixels(0, 0, kSize, kSize, GL_RGB, GL_UNSIGNED_BYTE,
                             image.bits());
            fbo.release();
            return image;
        };

        const QImage none = renderTurned(false, true);
        const QImage plane = renderTurned(true, false);
        const QImage shadowed = renderTurned(true, true);
        if (qEnvironmentVariableIsSet("CALANGO_DUMP_FRAMES")) {
            const QString dir = qEnvironmentVariable("CALANGO_DUMP_FRAMES");
            shadowed.flipped(Qt::Vertical).save(dir + "/floor_vertical.png");
        }

        std::printf("       drawn px: no floor %d, vertical floor %d\n",
                    drawnPixels(none), drawnPixels(plane));
        check(drawnPixels(plane) > drawnPixels(none) + 5000,
              "a floor with a +x normal is drawn (the winding survives being "
              "turned)");
        const double lost = meanBrightness(plane) - meanBrightness(shadowed);
        std::printf("       light lost to shadows on it = %.3f\n", lost);
        check(lost > 0.5,
              "and the structure's shadow lands on it, off a normal that is "
              "not +z");
    }

    // --- The hydrogen-bond overlay actually reaches the framebuffer ------
    //
    // This overlay used to be GL_LINES; it is now triangles, because
    // core-profile GL gives no usable line width (1 px outright under
    // macOS's profile) and the Bond Editor offers a width control. That
    // change is exactly the kind a compile cannot see: a wrong winding, a
    // stale primitive mode or a zero-radius quad all produce a frame with
    // nothing in it, and a frame with nothing in it is still a frame.
    //
    // So the checks are on PIXELS, and they are the three statements the
    // control makes: the overlay is drawn at all; the width knob changes
    // how much of it is drawn; and the three line styles differ from each
    // other in the direction their names promise.
    std::printf("The hydrogen-bond overlay is drawn, and its width is a "
                "width:\n");
    {
        using render::HydrogenBondLineStyle;
        const QImage none =
            renderHydrogenBonds(gl, HydrogenBondLineStyle::Dashed, 1.5f, false);
        const QImage dashed =
            renderHydrogenBonds(gl, HydrogenBondLineStyle::Dashed, 1.5f);
        const QImage thick =
            renderHydrogenBonds(gl, HydrogenBondLineStyle::Dashed, 6.0f);
        const QImage solid =
            renderHydrogenBonds(gl, HydrogenBondLineStyle::Solid, 1.5f);
        const QImage dotted =
            renderHydrogenBonds(gl, HydrogenBondLineStyle::Dotted, 1.5f);
        const int emptyPixels = drawnPixels(none);
        const int dashedPixels = drawnPixels(dashed);
        const int thickPixels = drawnPixels(thick);
        const int solidPixels = drawnPixels(solid);
        const int dottedPixels = drawnPixels(dotted);
        std::printf("       drawn pixels: none=%d dashed=%d thick=%d "
                    "solid=%d dotted=%d\n",
                    emptyPixels, dashedPixels, thickPixels, solidPixels,
                    dottedPixels);
        check(emptyPixels == 0,
              "an empty overlay stream draws nothing — so every count below "
              "is the overlay and only the overlay");
        check(dashedPixels > 100,
              "the default dashed overlay is genuinely on screen (triangles, "
              "front and back faces both, since the main pass culls neither)");
        check(thickPixels > 2 * dashedPixels,
              "width 6 covers several times what width 1.5 does — the "
              "control is a width, not a no-op the way glLineWidth would be "
              "here");
        check(solidPixels > dashedPixels,
              "solid covers more than dashed: same marks, no gaps");
        check(dottedPixels < dashedPixels,
              "and dotted covers less: the same spacing with shorter marks");
    }

    std::printf(failures == 0 ? "\nAll translucent-render checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
