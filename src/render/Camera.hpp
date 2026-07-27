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

/// The view every new viewport, every reset and every default-constructed
/// point-of-view starts from. Named constants rather than three separately
/// maintained literals: the initial view and the one the Reset Camera button
/// restores must be the same view, and they were previously kept in step by
/// hand across the struct defaults, the member defaults and resetOrientation().
///
/// A slightly rolled three-quarter view rather than a face-on one: it shows
/// all three axes at once, which is how a crystal structure is conventionally
/// presented.
inline constexpr float kDefaultYawDeg = 0.0f;
inline constexpr float kDefaultPitchDeg = -70.0f;
inline constexpr float kDefaultRollDeg = 20.0f;

/// Orthographic by default. A perspective view of a periodic cell makes the
/// far face smaller than the near one, so parallel lattice rows visibly
/// converge and equal-length cell edges measure differently on screen — an
/// artefact in exactly the images this application exists to produce.
/// Crystallography is drawn in parallel projection for that reason.
inline constexpr CameraProjection kDefaultProjection =
    CameraProjection::Orthographic;

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
    float pitchDeg = kDefaultPitchDeg;
    /// Rotation about the VIEW axis — the camera tilting its head, which turns
    /// the picture in the screen plane without changing what is in front of it.
    ///
    /// Distinct from yaw/pitch, which move the camera around the target, and
    /// from `sceneRotation`, which turns the structure itself. Roll is the one
    /// of the three that leaves both the viewpoint and the model alone, so it
    /// is what tilts a figure to a pleasing angle without re-orbiting it.
    float rollDeg = kDefaultRollDeg;
    QQuaternion sceneRotation;          ///< extra world-axis rotation
    CameraProjection projection = kDefaultProjection;

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
    /// buttons): yaw/pitch/roll in degrees, target and distance unchanged.
    /// Clears any scene rotation so the alignment is truly axis-aligned.
    ///
    /// Roll defaults to 0 because that is what "aligned with a plane" means —
    /// an axis-aligned view that arrived tilted would not be aligned.
    void setOrientation(float yawDeg, float pitchDeg, float rollDeg = 0.0f)
    {
        yawDeg_ = yawDeg;
        pitchDeg_ = pitchDeg;
        rollDeg_ = rollDeg;
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
    /// yaw/pitch/roll and an identity scene rotation. Target/distance are set
    /// separately by frame()/frameToFraction().
    void resetOrientation()
    {
        yawDeg_ = kDefaultYawDeg;
        pitchDeg_ = kDefaultPitchDeg;
        rollDeg_ = kDefaultRollDeg;
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
    float yaw() const { return yawDeg_; }
    float pitch() const { return pitchDeg_; }
    float roll() const { return rollDeg_; }
    /// Tilt the camera about its own view axis (turns the picture in the
    /// screen plane; the viewpoint and the model are unchanged).
    void setRoll(float degrees) { rollDeg_ = degrees; }

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
        return {target_,        distance_,      yawDeg_, pitchDeg_,
                rollDeg_,       sceneRotation_, projectionMode_, true};
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
        rollDeg_ = pov.rollDeg;
        sceneRotation_ = pov.sceneRotation;
        projectionMode_ = pov.projection;
    }

private:
    QVector3D target_{0.0f, 0.0f, 0.0f};
    float distance_ = 20.0f;
    float yawDeg_ = kDefaultYawDeg;
    float pitchDeg_ = kDefaultPitchDeg;
    float rollDeg_ = kDefaultRollDeg;
    QQuaternion sceneRotation_; ///< extra world-axis rotation (identity default)
    CameraProjection projectionMode_ = kDefaultProjection;
};

} // namespace calango::render
