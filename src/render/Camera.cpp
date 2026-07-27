#include "render/Camera.hpp"

#include <algorithm>
#include <cmath>

namespace calango::render {

void OrbitCamera::rotate(float dxDeg, float dyDeg)
{
    yawDeg_ += dxDeg;
    pitchDeg_ = std::clamp(pitchDeg_ + dyDeg, -89.0f, 89.0f);
}

void OrbitCamera::pan(float dxPixels, float dyPixels, int viewportHeight)
{
    // World-space size of one pixel at the target depth (40° vertical FOV).
    const float worldPerPixel =
        2.0f * distance_ * std::tan(qDegreesToRadians(20.0f)) / std::max(1, viewportHeight);

    // The inverse of view()'s rotation chain, so a drag moves the scene the way
    // the cursor moves. Roll has to be undone here as well — without it a
    // horizontal drag on a rolled view slides the structure diagonally.
    QMatrix4x4 rotation;
    rotation.rotate(-yawDeg_, 0.0f, 1.0f, 0.0f);
    rotation.rotate(-pitchDeg_, 1.0f, 0.0f, 0.0f);
    rotation.rotate(-rollDeg_, 0.0f, 0.0f, 1.0f);
    // Undo the scene rotation too, so panning follows the cursor even
    // after fixed-angle axis rotations.
    const QQuaternion inverse = sceneRotation_.inverted();
    const QVector3D right =
        inverse.rotatedVector(rotation.map(QVector3D(1.0f, 0.0f, 0.0f)));
    const QVector3D up =
        inverse.rotatedVector(rotation.map(QVector3D(0.0f, 1.0f, 0.0f)));

    target_ += (-dxPixels * right + dyPixels * up) * worldPerPixel;
}

void OrbitCamera::zoom(float steps)
{
    distance_ = std::clamp(distance_ * std::pow(0.88f, steps), 0.5f, 5000.0f);
}

void OrbitCamera::frame(const QVector3D& center, float radius)
{
    target_ = center;
    distance_ = std::max(2.0f, radius * 2.8f);
}

void OrbitCamera::frameToFraction(const QVector3D& center, float radius,
                                  float verticalFraction)
{
    target_ = center;
    // The perspective FOV is 40° (half-angle 20°): at distance d the viewport
    // spans a vertical world height of 2·d·tan(20°). To make the sphere's
    // diameter (2·radius) occupy `verticalFraction` of that height:
    //   2·radius = fraction · 2·d·tan(20°)  ⇒  d = radius / (fraction·tan20°).
    constexpr float kHalfFovTan = 0.36397023f; // tan(20°)
    const float fraction = std::max(verticalFraction, 0.05f);
    const float d = radius / (fraction * kHalfFovTan);
    distance_ = std::clamp(d, 2.0f, 5000.0f);
}

QMatrix4x4 OrbitCamera::view() const
{
    QMatrix4x4 m;
    m.translate(0.0f, 0.0f, -distance_);
    // Roll goes OUTSIDE pitch/yaw. QMatrix4x4 post-multiplies, so a vertex is
    // transformed by these in reverse order — putting roll first here makes it
    // the LAST rotation applied, i.e. one about the finished view axis. That is
    // what "the camera tilts its head" means: the picture turns in the screen
    // plane and what is in front of the lens does not change. Inside pitch/yaw
    // it would instead re-orbit the camera and change the viewpoint.
    m.rotate(rollDeg_, 0.0f, 0.0f, 1.0f);
    m.rotate(pitchDeg_, 1.0f, 0.0f, 0.0f);
    m.rotate(yawDeg_, 0.0f, 1.0f, 0.0f);
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
    // Same composition as view(), roll included: the axes triad is a read-out
    // of the scene's orientation, and one that ignored roll would point the
    // wrong way the moment the view was tilted.
    m.rotate(rollDeg_, 0.0f, 0.0f, 1.0f);
    m.rotate(pitchDeg_, 1.0f, 0.0f, 0.0f);
    m.rotate(yawDeg_, 0.0f, 1.0f, 0.0f);
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
        // distance keeps apparent size constant across the toggle.
        const float halfHeight = distance_ * std::tan(qDegreesToRadians(20.0f));
        m.ortho(-halfHeight * aspectRatio, halfHeight * aspectRatio,
                -halfHeight, halfHeight, nearPlane, farPlane);
    } else {
        m.perspective(40.0f, aspectRatio, nearPlane, farPlane);
    }
    return m;
}

} // namespace calango::render
