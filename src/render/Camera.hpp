#pragma once

#include <QMatrix4x4>
#include <QVector3D>

namespace calango::render {

enum class CameraProjection {
    Perspective,
    Orthographic,
};

/// Orbit (turntable) camera: yaw/pitch around a target point, with pan
/// and dolly-zoom. All angles in degrees.
///
/// The orthographic frustum height is matched to the perspective FOV at
/// the target distance, so toggling projections keeps the structure at
/// the same apparent size and alignment; zoom keeps working in both modes
/// because both derive their extent from distance().
class OrbitCamera {
public:
    void setProjectionMode(CameraProjection mode) { projectionMode_ = mode; }
    CameraProjection projectionMode() const { return projectionMode_; }

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
    QVector3D target() const { return target_; }

    /// World-space eye position / up vector (for ray-tracer scene export).
    QVector3D worldPosition() const;
    QVector3D worldUp() const;

    /// View matrix with the translation removed — orientation only
    /// (used by the axes-triad overlay).
    QMatrix4x4 rotationOnlyView() const;

private:
    QVector3D target_{0.0f, 0.0f, 0.0f};
    float distance_ = 20.0f;
    float yawDeg_ = 0.0f;
    float pitchDeg_ = 20.0f;
    CameraProjection projectionMode_ = CameraProjection::Perspective;
};

} // namespace calango::render
