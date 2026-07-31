#include "render/Camera.hpp"

#include <QMatrix3x3>

#include <algorithm>
#include <cmath>

namespace calango::render {

QQuaternion OrbitCamera::fromEuler(float yawDeg, float pitchDeg, float rollDeg)
{
    // The exact order view() composes them in: R = Rz(roll)·Rx(pitch)·Ry(yaw),
    // which as quaternions is q_roll * q_pitch * q_yaw. Qt's own
    // fromEulerAngles uses a different order, so it is deliberately not used
    // here — a mismatch would silently mirror every restored view.
    return QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, rollDeg)
        * QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, pitchDeg)
        * QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, yawDeg);
}

QVector3D OrbitCamera::toEuler(const QQuaternion& q)
{
    // Inverse of fromEuler, read off the rotation matrix of R =
    // Rz(r)·Rx(p)·Ry(y):
    //   m(2,1) =  sin p
    //   m(2,0) = -cos p · sin y      m(2,2) = cos p · cos y
    //   m(0,1) = -sin r · cos p      m(1,1) = cos r · cos p
    QMatrix3x3 m = q.normalized().toRotationMatrix();
    const float sp = std::clamp(m(2, 1), -1.0f, 1.0f);
    const float pitch = std::asin(sp);
    const float cp = std::cos(pitch);
    float yaw = 0.0f;
    float roll = 0.0f;
    if (std::abs(cp) > 1e-4f) {
        yaw = std::atan2(-m(2, 0), m(2, 2));
        roll = std::atan2(-m(0, 1), m(1, 1));
    } else {
        // Gimbal lock: looking straight up or down, roll and yaw turn about
        // the same screen axis and the split between them is arbitrary.
        // Attributing it all to yaw keeps roll at 0, which is what the
        // point-of-view dialog can then show without a meaningless number.
        yaw = std::atan2(m(0, 2), m(0, 0));
        roll = 0.0f;
    }
    return {qRadiansToDegrees(pitch), qRadiansToDegrees(yaw),
            qRadiansToDegrees(roll)};
}

QVector3D OrbitCamera::mapToSphere(const QPointF& p, int width, int height)
{
    // Normalized device-ish coordinates with y up, scaled so the virtual ball
    // fills the SHORTER side. Using the shorter side keeps the gesture
    // isotropic: on a wide viewport a ball sized to the width would make
    // vertical drags rotate far more than horizontal ones.
    const float radius = 0.5f * static_cast<float>(std::min(width, height));
    const float cx = 0.5f * static_cast<float>(width);
    const float cy = 0.5f * static_cast<float>(height);
    const float x = (static_cast<float>(p.x()) - cx) / std::max(radius, 1.0f);
    const float y = (cy - static_cast<float>(p.y())) / std::max(radius, 1.0f);
    const float square = x * x + y * y;
    if (square <= 1.0f)
        return QVector3D(x, y, std::sqrt(1.0f - square));
    // Outside the ball: fall to the rim. The drag then turns about the view
    // axis, which is how an arcball gives roll without a separate control.
    const float inv = 1.0f / std::sqrt(square);
    return QVector3D(x * inv, y * inv, 0.0f);
}

void OrbitCamera::rotateArcball(const QPointF& from, const QPointF& to,
                                int width, int height)
{
    if (width <= 0 || height <= 0 || from == to)
        return;
    const QVector3D a = mapToSphere(from, width, height);
    const QVector3D b = mapToSphere(to, width, height);
    // The rotation that carries the grabbed point to the cursor. Composed on
    // the LEFT because it is expressed in camera space, where the drag
    // happened: orientation_ maps world -> camera, so applying delta after it
    // moves the point the user grabbed to where the pointer now is.
    const QQuaternion delta = QQuaternion::rotationTo(a, b);
    orientation_ = (delta * orientation_).normalized();
}

void OrbitCamera::rotate(float dxDeg, float dyDeg)
{
    // Turntable semantics for the programmatic callers: yaw about the WORLD up
    // axis so a film turntable circles the structure rather than tumbling it,
    // and pitch about the camera's own right axis.
    const QQuaternion yaw =
        QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, dxDeg);
    const QQuaternion pitch =
        QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, dyDeg);
    orientation_ = (pitch * orientation_ * yaw).normalized();
}

void OrbitCamera::pan(float dxPixels, float dyPixels, int viewportHeight)
{
    // World-space size of one pixel at the target depth (40° vertical FOV).
    const float worldPerPixel =
        2.0f * distance_ * std::tan(qDegreesToRadians(20.0f)) / std::max(1, viewportHeight);

    // The inverse of view()'s whole rotation chain, so a drag moves the scene
    // the way the cursor moves. One quaternion inversion now that the
    // orientation is a quaternion — the old version undid three Euler
    // rotations by hand and had to remember to include roll.
    const QQuaternion inverse = (orientation_ * sceneRotation_).inverted();
    const QVector3D right = inverse.rotatedVector(QVector3D(1.0f, 0.0f, 0.0f));
    const QVector3D up = inverse.rotatedVector(QVector3D(0.0f, 1.0f, 0.0f));

    target_ += (-dxPixels * right + dyPixels * up) * worldPerPixel;
}

void OrbitCamera::zoom(float steps)
{
    distance_ = std::clamp(distance_ * std::pow(0.88f, steps), 0.5f, 5000.0f);
}

void OrbitCamera::frame(const QVector3D& center, float radius)
{
    // Deliberately not a bare `radius * 2.8`. That constant was the right
    // distance only for the 40-degree default: it put the bounding sphere at
    // very nearly the full height of the viewport. With the angle now under
    // the user's control it has to be re-derived, or framing a newly opened
    // structure after a turn of the perspective slider would leave it a speck
    // at a wide angle and overflowing the edges at a narrow one.
    //
    // 0.98 is that same framing expressed as a screen fraction, so at the
    // default field of view this reproduces the old distance to three figures.
    frameToFraction(center, radius, 0.98f);
    distance_ = std::max(2.0f, distance_);
}

void OrbitCamera::frameToFraction(const QVector3D& center, float radius,
                                  float verticalFraction)
{
    target_ = center;
    // At distance d the viewport spans a vertical world height of
    // 2·d·tan(fov/2). To make the sphere's diameter (2·radius) occupy
    // `verticalFraction` of that height:
    //   2·radius = fraction · 2·d·tan(fov/2)  ⇒  d = radius/(fraction·tan(fov/2))
    //
    // Derived from the CURRENT field of view rather than a baked tan(20°): with
    // the FOV adjustable, a constant here would frame a structure to the wrong
    // size the moment the user moved the slider — auto-zoom would overshoot at
    // a narrow FOV and undershoot at a wide one.
    const float halfFovTan =
        std::tan(qDegreesToRadians(fieldOfViewDeg_ * 0.5f));
    const float fraction = std::max(verticalFraction, 0.05f);
    const float d = radius / (fraction * std::max(halfFovTan, 1e-4f));
    distance_ = std::clamp(d, 2.0f, 5000.0f);
}

QMatrix4x4 OrbitCamera::view() const
{
    QMatrix4x4 m;
    m.translate(0.0f, 0.0f, -distance_);
    // One quaternion where three Euler rotations used to be. The arcball
    // accumulates directly into it, so there is no longer an order to get
    // wrong and no pitch clamp: the scene can be turned completely over.
    m.rotate(orientation_);
    m.rotate(sceneRotation_); // world-axis rotation about the target
    m.translate(-target_);
    return m;
}

QVector3D OrbitCamera::worldPosition() const
{
    return view().inverted().map(QVector3D(0.0f, 0.0f, 0.0f));
}

QVector3D OrbitCamera::worldUp() const
{
    return view().inverted().mapVector(QVector3D(0.0f, 1.0f, 0.0f)).normalized();
}

QMatrix4x4 OrbitCamera::rotationOnlyView() const
{
    QMatrix4x4 m;
    // Same composition as view(): the axes triad is a read-out of the scene's
    // orientation, so it has to use the identical rotation.
    m.rotate(orientation_);
    m.rotate(sceneRotation_); // keep the axes triad in sync
    return m;
}

QMatrix4x4 OrbitCamera::projection(float aspectRatio) const
{
    QMatrix4x4 m;
    const float nearPlane = std::max(0.01f, distance_ * 0.01f);
    const float farPlane = distance_ * 50.0f;
    if (projectionMode_ == CameraProjection::Orthographic) {
        // Half-height matching the perspective frustum at the target
        // distance keeps apparent size constant across the toggle — so it
        // tracks the field of view rather than a fixed 20° half-angle.
        const float halfHeight =
            distance_ * std::tan(qDegreesToRadians(fieldOfViewDeg_ * 0.5f));
        m.ortho(-halfHeight * aspectRatio, halfHeight * aspectRatio,
                -halfHeight, halfHeight, nearPlane, farPlane);
    } else {
        m.perspective(fieldOfViewDeg_, aspectRatio, nearPlane, farPlane);
    }
    return m;
}

} // namespace calango::render
