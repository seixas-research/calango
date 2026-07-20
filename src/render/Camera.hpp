#pragma once

#include <QMatrix4x4>
#include <QVector3D>

namespace calango::render {

/// Orbit (turntable) camera: yaw/pitch around a target point, with pan
/// and dolly-zoom. All angles in degrees.
class OrbitCamera {
public:
    void rotate(float dxDeg, float dyDeg);

    /// Pan in screen space; dx/dy are in pixels, scaled by distance so the
    /// scene appears to follow the cursor.
    void pan(float dxPixels, float dyPixels, int viewportHeight);

    /// steps > 0 zooms in (each step scales distance by 0.88).
    void zoom(float steps);

    /// Center on a bounding sphere and back off far enough to see it all.
    void frame(const QVector3D& center, float radius);

    QMatrix4x4 view() const;
    QMatrix4x4 projection(float aspectRatio) const;

    float distance() const { return distance_; }

private:
    QVector3D target_{0.0f, 0.0f, 0.0f};
    float distance_ = 20.0f;
    float yawDeg_ = 0.0f;
    float pitchDeg_ = 20.0f;
};

} // namespace calango::render
