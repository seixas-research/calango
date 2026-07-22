#pragma once

#include <QMatrix4x4>
#include <QQuaternion>
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

    /// Jump to an absolute orientation (used by the view-alignment
    /// buttons): yaw/pitch in degrees, target and distance unchanged.
    /// Clears any scene rotation so the alignment is truly axis-aligned.
    void setOrientation(float yawDeg, float pitchDeg)
    {
        yawDeg_ = yawDeg;
        pitchDeg_ = pitchDeg;
        sceneRotation_ = QQuaternion();
    }

    /// Rotate the scene about a world-space axis through the camera
    /// target (the fixed-angle X/Y/Z toolbar buttons). Composes with the
    /// orbit yaw/pitch; picking and overlays stay consistent because
    /// every consumer goes through view()/projection().
    void rotateScene(const QVector3D& axis, float degrees)
    {
        sceneRotation_ =
            QQuaternion::fromAxisAndAngle(axis, degrees) * sceneRotation_;
    }

    /// Pan in screen space; dx/dy are in pixels, scaled by distance so the
    /// scene appears to follow the cursor.
    void pan(float dxPixels, float dyPixels, int viewportHeight);

    /// steps > 0 zooms in (each step scales distance by 0.88).
    void zoom(float steps);

    /// Center on a bounding sphere and back off far enough to see it all.
    void frame(const QVector3D& center, float radius);

    /// Center on a bounding sphere and set the distance so the sphere's
    /// diameter spans `verticalFraction` (0..1) of the viewport's vertical
    /// extent, given the fixed 40° field of view. Used for intelligent
    /// auto-zoom: ~0.9 fits a unit cell, 0.5 sizes a molecule to half-height.
    void frameToFraction(const QVector3D& center, float radius,
                         float verticalFraction);

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
    QQuaternion sceneRotation_; ///< extra world-axis rotation (identity default)
    CameraProjection projectionMode_ = CameraProjection::Perspective;
};

} // namespace calango::render
