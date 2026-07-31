// Orbit-camera test: defaults, plane alignment and the roll axis.
//
// The camera's angles are not decoration — they decide what a saved figure
// looks like, and every one of them is silently wrong-able. A pitch of the
// wrong sign still produces a picture, just of the other side of the crystal;
// a roll composed on the inside of pitch/yaw still rotates something, just the
// viewpoint rather than the image. Neither throws, so both need checking
// against the geometry the matrices actually produce.
//
// GUI-free, GL-free, Python-free.

#include "render/Camera.hpp"
#include "render/Film.hpp"

#include <QVector3D>
#include <QVector4D>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace calango;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

bool near(float a, float b, float tolerance = 1e-4f)
{
    return std::abs(a - b) < tolerance;
}

bool nearVector(const QVector3D& a, const QVector3D& b, float tolerance = 1e-4f)
{
    return (a - b).length() < tolerance;
}

/// The world direction the camera looks ALONG, derived from the view matrix
/// rather than re-deriving the angle convention here — the point is to check
/// what the matrix does, not to restate how it is built.
QVector3D viewDirection(const render::OrbitCamera& camera)
{
    // A camera looks down -z in view space.
    return camera.view().inverted().mapVector(QVector3D(0, 0, -1)).normalized();
}

/// The world direction that appears as "up" on screen.
QVector3D screenUp(const render::OrbitCamera& camera)
{
    return camera.view().inverted().mapVector(QVector3D(0, 1, 0)).normalized();
}

/// The world direction that appears as "right" on screen.
QVector3D screenRight(const render::OrbitCamera& camera)
{
    return camera.view().inverted().mapVector(QVector3D(1, 0, 0)).normalized();
}

} // namespace

int main()
{
    std::printf("Defaults:\n");
    {
        render::OrbitCamera camera;
        check(near(camera.yaw(), 0.0f), "a fresh camera has yaw 0");
        check(near(camera.pitch(), -70.0f), "pitch -70");
        check(near(camera.roll(), 20.0f), "roll 20");
        // Perspective makes the far face of a periodic cell smaller than the
        // near one, so parallel lattice rows converge and equal cell edges
        // measure differently — an artefact in exactly the figures this
        // application produces.
        check(camera.projectionMode() == render::CameraProjection::Orthographic,
              "and an orthographic projection");

        const render::PointOfView pov;
        check(near(pov.pitchDeg, camera.pitch())
                  && near(pov.rollDeg, camera.roll())
                  && pov.projection == camera.projectionMode(),
              "a default-constructed point-of-view agrees with the camera");
    }

    std::printf("Reset restores the default view:\n");
    {
        render::OrbitCamera camera;
        camera.rotate(37.0f, 11.0f);
        camera.setRoll(-100.0f);
        camera.rotateScene(QVector3D(0, 0, 1), 45.0f);
        camera.resetOrientation();
        check(near(camera.yaw(), 0.0f) && near(camera.pitch(), -70.0f)
                  && near(camera.roll(), 20.0f),
              "Reset Camera returns to (yaw, pitch, roll) = (0, -70, 20)");
        // The scene rotation is separate state; leaving it behind would make a
        // "reset" view that is still turned.
        render::OrbitCamera fresh;
        check(nearVector(viewDirection(camera), viewDirection(fresh)),
              "and clears the accumulated scene rotation with it");
    }

    std::printf("Plane alignment:\n");
    {
        // XZ: pitch -90 looks along +y, which puts +x to the right and +z UP.
        // The other sign (+90) also shows the XZ plane, but mirrored — +z
        // points down the screen — which is not how the plane is drawn.
        render::OrbitCamera camera;
        camera.setOrientation(0.0f, -90.0f, 0.0f);
        check(near(camera.yaw(), 0.0f) && near(camera.pitch(), -90.0f)
                  && near(camera.roll(), 0.0f),
              "the XZ button sets (0, -90, 0)");
        check(nearVector(viewDirection(camera), QVector3D(0, 1, 0)),
              "the camera then looks along +y");
        check(nearVector(screenUp(camera), QVector3D(0, 0, 1)),
              "with +z up the screen");
        check(nearVector(screenRight(camera), QVector3D(1, 0, 0)),
              "and +x to the right");
    }
    {
        render::OrbitCamera camera;
        camera.setOrientation(0.0f, 0.0f, 0.0f);
        check(nearVector(viewDirection(camera), QVector3D(0, 0, -1)),
              "XY looks along -z");
        check(nearVector(screenUp(camera), QVector3D(0, 1, 0)),
              "with +y up the screen");
        // +x, not -x: Qt's rotate() is right-handed, so a yaw of +90 turns the
        // camera to look back along the positive x axis. The measured triads
        // for the three buttons are
        //     XY (0, 0)    look -z   up +y   right +x
        //     XZ (0, -90)  look +y   up +z   right +x
        //     YZ (90, 0)   look +x   up +y   right +z
        // each of which is a right-handed screen basis, which is what makes
        // the three read as the same scene from three sides.
        camera.setOrientation(90.0f, 0.0f, 0.0f);
        check(nearVector(viewDirection(camera), QVector3D(1, 0, 0)),
              "YZ looks along +x");
        check(nearVector(screenUp(camera), QVector3D(0, 1, 0)),
              "with +y up the screen");
        check(nearVector(screenRight(camera), QVector3D(0, 0, 1)),
              "and +z to the right");
    }
    {
        // An alignment must not inherit the previous view's tilt, or it is not
        // aligned with anything.
        render::OrbitCamera camera;
        camera.setRoll(33.0f);
        camera.setOrientation(0.0f, -90.0f);
        check(near(camera.roll(), 0.0f),
              "aligning clears any roll that was set before it");
    }

    std::printf("Roll is a camera tilt, not an orbit:\n");
    {
        render::OrbitCamera unrolled;
        unrolled.setOrientation(0.0f, 0.0f, 0.0f);
        render::OrbitCamera rolled;
        rolled.setOrientation(0.0f, 0.0f, 30.0f);

        // THE defining property: rolling changes the picture's orientation but
        // not what the camera is pointed at. Composing roll on the wrong side
        // of pitch/yaw breaks exactly this and nothing else.
        check(nearVector(viewDirection(unrolled), viewDirection(rolled)),
              "rolling leaves the viewing direction untouched");
        check(!nearVector(screenUp(unrolled), screenUp(rolled)),
              "but does turn the image in the screen plane");

        // 30° of roll must move screen-up by 30°, not some other angle.
        const float cosAngle = QVector3D::dotProduct(screenUp(unrolled),
                                                     screenUp(rolled));
        check(near(cosAngle, std::cos(qDegreesToRadians(30.0f)), 1e-3f),
              "by exactly the angle asked for");
        // The eye stays put: roll is not a move around the target.
        check(nearVector(unrolled.worldPosition(), rolled.worldPosition()),
              "and does not move the eye");
    }

    std::printf("Point-of-view round trip:\n");
    {
        render::OrbitCamera camera;
        camera.setOrientation(15.0f, -42.0f, 63.0f);
        camera.setProjectionMode(render::CameraProjection::Perspective);
        const render::PointOfView pov = camera.pointOfView();
        check(near(pov.rollDeg, 63.0f), "roll is captured");

        render::OrbitCamera restored;
        restored.setPointOfView(pov);
        check(near(restored.roll(), 63.0f) && near(restored.pitch(), -42.0f)
                  && near(restored.yaw(), 15.0f),
              "and restored");
        // "Exact" means the frame is reproduced, not merely the numbers.
        check(nearVector(viewDirection(restored), viewDirection(camera))
                  && nearVector(screenUp(restored), screenUp(camera)),
              "restoring reproduces the frame, roll included");
    }

    std::printf("Film blending carries roll:\n");
    {
        // A film that interpolated yaw and pitch but snapped roll would show a
        // shot rotating smoothly and then jumping at the cut.
        render::PointOfView from;
        from.rollDeg = 0.0f;
        from.valid = true;
        render::PointOfView to;
        to.rollDeg = 90.0f;
        to.valid = true;
        const render::PointOfView mid = render::blendPointOfView(from, to, 0.5);
        check(near(mid.rollDeg, 45.0f), "half-way is half the roll");

        // Shortest arc: -170 -> +170 is 20 degrees through the wrap, not 340.
        render::PointOfView wrapFrom;
        wrapFrom.rollDeg = -170.0f;
        wrapFrom.valid = true;
        render::PointOfView wrapTo;
        wrapTo.rollDeg = 170.0f;
        wrapTo.valid = true;
        const render::PointOfView wrapped =
            render::blendPointOfView(wrapFrom, wrapTo, 0.5);
        check(std::abs(wrapped.rollDeg) > 175.0f,
              "and a wrap takes the short way round, not 340 degrees the long way");
    }

    // --- Arcball rotation ---------------------------------------------------
    // The Euler model it replaced had two defects a user feels directly: a
    // drag depended on the path taken (Euler composition does not commute),
    // and pitch was clamped at +/-89 to dodge gimbal lock, so the scene could
    // never be turned over.
    std::printf("Arcball rotation:\n");
    {
        using render::OrbitCamera;
        constexpr int W = 800;
        constexpr int H = 600;
        const auto sameRotation = [](const QQuaternion& a, const QQuaternion& b) {
            // q and -q name the same rotation.
            const float d = std::abs(
                QQuaternion::dotProduct(a.normalized(), b.normalized()));
            return std::abs(d - 1.0f) < 1e-3f;
        };

        // The Euler triple is what the point-of-view dialog edits and what
        // older project files hold, so the conversion has to be exact.
        for (const QVector3D e : {QVector3D(0.0f, -70.0f, 20.0f),
                                  QVector3D(30.0f, 45.0f, -15.0f),
                                  QVector3D(0.0f, 0.0f, 0.0f),
                                  QVector3D(-120.0f, 10.0f, 170.0f)}) {
            const QQuaternion q = OrbitCamera::fromEuler(e.x(), e.y(), e.z());
            const QVector3D back = OrbitCamera::toEuler(q); // (pitch, yaw, roll)
            check(sameRotation(
                      q, OrbitCamera::fromEuler(back.y(), back.x(), back.z())),
                  "Euler <-> quaternion round trip reproduces the rotation");
        }

        // Reversibility: dragging out and back must land exactly where it
        // started. Under the old model it did not, because the yaw and pitch
        // increments composed in a fixed order regardless of the path.
        {
            OrbitCamera c;
            const QQuaternion start = c.orientation();
            c.rotateArcball(QPointF(400, 300), QPointF(560, 380), W, H);
            c.rotateArcball(QPointF(560, 380), QPointF(400, 300), W, H);
            check(sameRotation(c.orientation(), start),
                  "a drag out and back returns exactly to the start");
        }

        // A drag beyond where the old clamp sat must actually get there.
        {
            OrbitCamera c;
            c.setOrientation(0.0f, 0.0f, 0.0f);
            c.rotateArcball(QPointF(400, 300), QPointF(400, 595), W, H);
            check(std::abs(c.pitch()) > 60.0f,
                  "a vertical drag passes freely through the old +/-89 clamp");
        }

        // Outside the ball the projection falls to the rim, which turns the
        // drag into roll about the view axis — the arcball's way of offering
        // roll without a control of its own.
        {
            OrbitCamera c;
            c.setOrientation(0.0f, 0.0f, 0.0f);
            // Two points well outside the sphere, swept around the centre.
            c.rotateArcball(QPointF(790, 300), QPointF(400, 10), W, H);
            check(std::abs(c.roll()) > 10.0f,
                  "dragging outside the ball rolls the view");
        }

        // Capture/restore must carry the exact orientation, including one no
        // Euler triple was ever asked to represent.
        {
            OrbitCamera c;
            c.rotateArcball(QPointF(300, 200), QPointF(520, 410), W, H);
            const render::PointOfView pov = c.pointOfView();
            check(!pov.orientation.isIdentity(),
                  "a captured view carries the quaternion");
            render::OrbitCamera restored;
            restored.setPointOfView(pov);
            check(sameRotation(restored.orientation(), c.orientation()),
                  "and restores it exactly");
        }

        // A point-of-view saved before the arcball has no quaternion; it must
        // still load, from its Euler triple.
        {
            render::PointOfView legacy;
            legacy.yawDeg = 15.0f;
            legacy.pitchDeg = -70.0f;
            legacy.rollDeg = 20.0f;
            legacy.distance = 12.0f;
            legacy.valid = true;
            OrbitCamera c;
            c.setPointOfView(legacy);
            check(sameRotation(c.orientation(),
                               OrbitCamera::fromEuler(15.0f, -70.0f, 20.0f)),
                  "a pre-arcball saved view falls back to its Euler triple");
        }
    }

    // --- Field of view ------------------------------------------------------
    // The toolbar's perspective control is a slider over this one number, so
    // it is worth pinning what the number means. Two things can go quietly
    // wrong: the slider can be built around a default the camera does not
    // actually start at (the readout then lies the moment it opens), and the
    // angle can be wired to the perspective branch alone — leaving it inert in
    // the orthographic mode this application starts in.
    std::printf("Field of view:\n");
    {
        render::OrbitCamera camera;
        check(near(camera.fieldOfView(), render::kDefaultFieldOfViewDeg),
              "a fresh camera opens at the documented default");
        check(render::kMinFieldOfViewDeg < render::kDefaultFieldOfViewDeg
                  && render::kDefaultFieldOfViewDeg < render::kMaxFieldOfViewDeg,
              "which lies inside the slider's range");

        camera.setFieldOfView(1000.0f);
        check(near(camera.fieldOfView(), render::kMaxFieldOfViewDeg),
              "a wild value clamps to the maximum");
        camera.setFieldOfView(-5.0f);
        check(near(camera.fieldOfView(), render::kMinFieldOfViewDeg),
              "and a negative one to the minimum");

        // A wider angle must widen the frustum, in both projection modes.
        const auto horizontalExtentAtUnitDepth = [](const render::OrbitCamera& c) {
            // Map a point one unit ahead of the eye and read how far off-axis
            // it has to sit to land on the right edge of the clip cube.
            const QMatrix4x4 proj = c.projection(1.0f);
            const QVector4D probe = proj * QVector4D(1.0f, 0.0f, -1.0f, 1.0f);
            return probe.w() != 0.0f ? probe.x() / probe.w() : probe.x();
        };
        for (const auto mode : {render::CameraProjection::Perspective,
                                render::CameraProjection::Orthographic}) {
            render::OrbitCamera narrow;
            narrow.setProjectionMode(mode);
            narrow.setFieldOfView(20.0f);
            render::OrbitCamera wide;
            wide.setProjectionMode(mode);
            wide.setFieldOfView(80.0f);
            const bool perspective = mode == render::CameraProjection::Perspective;
            check(horizontalExtentAtUnitDepth(wide)
                      < horizontalExtentAtUnitDepth(narrow),
                  perspective
                      ? "widening the angle widens the perspective frustum"
                      : "and the orthographic one, which follows it too");
        }

        // Framing has to follow the angle as well: a fit computed against a
        // hard-coded angle parks the structure at the wrong distance the
        // moment the slider moves.
        render::OrbitCamera narrow;
        narrow.setFieldOfView(20.0f);
        narrow.frame(QVector3D(0, 0, 0), 5.0f);
        render::OrbitCamera wide;
        wide.setFieldOfView(80.0f);
        wide.frame(QVector3D(0, 0, 0), 5.0f);
        check(wide.distance() < narrow.distance(),
              "a wider angle fits the same structure from closer in");
        // ...without disturbing what framing does at the default angle, which
        // is every existing figure and every freshly opened file.
        render::OrbitCamera fresh;
        fresh.frame(QVector3D(0, 0, 0), 5.0f);
        check(near(fresh.distance(), 5.0f * 2.8f, 0.02f),
              "and at the default angle it still frames exactly as before");
    }

    std::printf("Dolly zoom:\n");
    {
        // The Vertigo shot: the angle opens while the eye walks in, so the
        // subject holds its size and only the depth relationship moves. The
        // failure mode is a slider that merely re-zooms — visually it looks
        // like something is happening, but every distance in the picture
        // scales together and no new information appears.
        render::OrbitCamera camera;
        camera.setProjectionMode(render::CameraProjection::Perspective);
        camera.frame(QVector3D(0, 0, 0), 5.0f);
        const float startDistance = camera.distance();
        const float startHalfHeight =
            startDistance
            * std::tan(qDegreesToRadians(camera.fieldOfView() * 0.5f));

        camera.setFieldOfViewDolly(90.0f);
        check(near(camera.fieldOfView(), 90.0f), "the angle follows the slider");
        check(camera.distance() < startDistance,
              "and a wider angle walks the eye IN, not out");
        const float halfHeight =
            camera.distance()
            * std::tan(qDegreesToRadians(camera.fieldOfView() * 0.5f));
        check(near(halfHeight, startHalfHeight, 1e-3f),
              "leaving the subject exactly its original size on screen");

        // Which is the whole point: same size, different depth. A point one
        // radius BEHIND the target must not project where it did before.
        const auto depthOfBackPoint = [](const render::OrbitCamera& c) {
            const QVector4D behind =
                c.projection(1.0f) * c.view() * QVector4D(0, 5, 0, 1);
            return behind.w() != 0.0f ? behind.y() / behind.w() : behind.y();
        };
        render::OrbitCamera unchanged;
        unchanged.setProjectionMode(render::CameraProjection::Perspective);
        unchanged.frame(QVector3D(0, 0, 0), 5.0f);
        check(std::abs(depthOfBackPoint(camera) - depthOfBackPoint(unchanged))
                  > 0.02f,
              "while everything off the target plane moves — the actual effect");

        // Narrowing has to be the exact inverse, or the slider drifts the
        // structure smaller or larger every time it is swept back and forth.
        camera.setFieldOfViewDolly(render::kDefaultFieldOfViewDeg);
        check(near(camera.distance(), startDistance, 1e-2f),
              "and sweeping back returns the eye where it started");

        // Under a parallel projection there is no depth relationship to alter,
        // and the compensation cancels the angle exactly. Not a bug — but it
        // is why the toolbar control leaves orthographic when it is moved.
        render::OrbitCamera ortho;
        ortho.frame(QVector3D(0, 0, 0), 5.0f);
        // Where a fixed world point lands on screen — the frustum's x/y, which
        // is the whole of what a parallel projection shows. The near and far
        // planes do move with the eye, but they only decide what is clipped.
        const auto screenPosition = [](const render::OrbitCamera& c) {
            const QVector4D p =
                c.projection(1.5f) * c.view() * QVector4D(2, 1, 0, 1);
            return QVector3D(p.x() / p.w(), p.y() / p.w(), 0.0f);
        };
        const QVector3D before = screenPosition(ortho);
        ortho.setFieldOfViewDolly(100.0f);
        check(nearVector(before, screenPosition(ortho), 1e-3f),
              "an orthographic camera shows the identical picture either way");
    }

    std::printf(failures == 0 ? "\nAll camera checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
