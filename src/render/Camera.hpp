#pragma once

#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>

#include <algorithm>

namespace calango::render {

enum class CameraProjection {
    Perspective,
    Orthographic,
};

/// A complete, restorable camera state — "where you were looking from".
///
/// Everything the orbit camera needs and nothing it does not: the target the
/// pan has moved to, the dolly distance (zoom), the two orbit angles, the extra
/// world-axis scene rotation, and the projection. Restoring one reproduces a
/// view exactly, which is what makes a saved point-of-view reusable across
/// sessions and what lets a workspace tab keep its own view while the user
/// works in another.
///
/// Deliberately a plain aggregate: it is stored per tab, serialized to the
/// project file, and listed in the Set Point-of-View dialog, so it must copy
/// and compare like data rather than behave like an object.
struct PointOfView {
    QVector3D target{0.0f, 0.0f, 0.0f}; ///< what the camera looks at (pan)
    float distance = 20.0f;             ///< dolly distance (zoom)
    float yawDeg = 0.0f;
    float pitchDeg = 20.0f;
    QQuaternion sceneRotation;          ///< extra world-axis rotation
    CameraProjection projection = CameraProjection::Perspective;

    /// True when this holds a real captured view rather than a default-
    /// constructed placeholder. A tab that has never been shown has no view to
    /// restore, and restoring the default would yank the camera to an
    /// arbitrary place the first time the user switched to it.
    bool valid = false;
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

    /// Restore the default view orientation (Reset Camera): the default
    /// yaw/pitch and an identity scene rotation. Target/distance are set
    /// separately by frame()/frameToFraction().
    void resetOrientation()
    {
        yawDeg_ = 0.0f;
        pitchDeg_ = 20.0f;
        sceneRotation_ = QQuaternion();
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

    /// Capture / restore the whole camera state. `apply` is exact: after it,
    /// view() and projection() reproduce the captured frame.
    PointOfView pointOfView() const
    {
        return {target_, distance_, yawDeg_, pitchDeg_, sceneRotation_,
                projectionMode_, true};
    }
    void setPointOfView(const PointOfView& pov)
    {
        if (!pov.valid)
            return; // nothing was captured; leave the camera alone
        target_ = pov.target;
        // A non-positive distance would put the eye at the target and make the
        // view matrix singular; clamp rather than trust a hand-edited value.
        distance_ = std::max(pov.distance, 1e-3f);
        yawDeg_ = pov.yawDeg;
        pitchDeg_ = pov.pitchDeg;
        sceneRotation_ = pov.sceneRotation;
        projectionMode_ = pov.projection;
    }

private:
    QVector3D target_{0.0f, 0.0f, 0.0f};
    float distance_ = 20.0f;
    float yawDeg_ = 0.0f;
    float pitchDeg_ = 20.0f;
    QQuaternion sceneRotation_; ///< extra world-axis rotation (identity default)
    CameraProjection projectionMode_ = CameraProjection::Perspective;
};

} // namespace calango::render
