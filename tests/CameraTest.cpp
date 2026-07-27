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

    std::printf(failures == 0 ? "\nAll camera checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
