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

    QMatrix4x4 rotation;
    rotation.rotate(-yawDeg_, 0.0f, 1.0f, 0.0f);
    rotation.rotate(-pitchDeg_, 1.0f, 0.0f, 0.0f);
    const QVector3D right = rotation.map(QVector3D(1.0f, 0.0f, 0.0f));
    const QVector3D up = rotation.map(QVector3D(0.0f, 1.0f, 0.0f));

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

QMatrix4x4 OrbitCamera::view() const
{
    QMatrix4x4 m;
    m.translate(0.0f, 0.0f, -distance_);
    m.rotate(pitchDeg_, 1.0f, 0.0f, 0.0f);
    m.rotate(yawDeg_, 0.0f, 1.0f, 0.0f);
    m.translate(-target_);
    return m;
}

QMatrix4x4 OrbitCamera::projection(float aspectRatio) const
{
    QMatrix4x4 m;
    m.perspective(40.0f, aspectRatio,
                  std::max(0.01f, distance_ * 0.01f),
                  distance_ * 50.0f);
    return m;
}

} // namespace calango::render
