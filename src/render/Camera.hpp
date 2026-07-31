#pragma once

#include <QMatrix4x4>
#include <QPointF>
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

/// Vertical field of view, in degrees: the default and the range the UI offers.
///
/// 40° is what the projection was hard-coded to before it became adjustable, so
/// the default view is unchanged. The bounds are where the projection stops
/// being useful rather than where it stops being defined — below ~5° the near
/// and far planes crowd together, and above ~120° the edges of the frustum
/// distort past the point of reading a structure.
inline constexpr float kDefaultFieldOfViewDeg = 40.0f;
inline constexpr float kMinFieldOfViewDeg = 5.0f;
inline constexpr float kMaxFieldOfViewDeg = 120.0f;

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
    /// The camera orientation as a quaternion — the authoritative value since
    /// the rotation model became an arcball.
    ///
    /// yaw/pitch/roll above are kept, derived from this, because they are what
    /// the Set Point-of-View dialog edits and what older saved views and
    /// project files contain. An identity quaternion means "this view predates
    /// the arcball", and the Euler triple is used instead — which is exact,
    /// because a stored identity can only have come from yaw = pitch = roll =
    /// 0 and those rebuild the identity anyway.
    QQuaternion orientation;
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

    /// Vertical field of view, in degrees.
    ///
    /// Governs BOTH projections, and deliberately so: the orthographic frustum
    /// height is matched to the perspective one at the target distance, which
    /// is what keeps the structure the same apparent size across the toggle. A
    /// FOV that only applied to perspective would make that toggle jump.
    ///
    /// Narrow (~10°) is a long lens: near-parallel, almost orthographic, and
    /// the honest choice for a lattice. Wide (~90°) exaggerates depth, which is
    /// occasionally what a figure of a pore or a channel wants.
    void setFieldOfView(float degrees)
    {
        fieldOfViewDeg_ = std::clamp(degrees, kMinFieldOfViewDeg,
                                     kMaxFieldOfViewDeg);
    }
    float fieldOfView() const { return fieldOfViewDeg_; }

    /// Change the field of view and walk the camera in or out to compensate —
    /// the "dolly zoom", or Vertigo shot.
    ///
    /// Plain setFieldOfView() is a zoom: everything grows or shrinks together
    /// and the picture says nothing new. The interesting operation moves the
    /// eye at the same time, holding whatever sits at the orbit target at a
    /// fixed size while the perspective around it opens out or flattens. What
    /// changes is the depth relationship alone — a narrow angle from far away
    /// compresses the near and far faces of a cell onto each other, a wide
    /// angle from close in throws them apart and drives the eye down a channel
    /// or a pore. That is the one thing an orthographic figure cannot show.
    ///
    /// The invariant held is distance·tan(fov/2), the world half-height
    /// spanned at the target. Note that this is exactly the orthographic
    /// frustum height too, so under an orthographic camera this is a no-op by
    /// construction — correctly, since a parallel projection has no depth
    /// relationship to alter. Callers wanting the effect visible must be in
    /// perspective mode.
    void setFieldOfViewDolly(float degrees)
    {
        const float before = std::tan(qDegreesToRadians(fieldOfViewDeg_ * 0.5f));
        setFieldOfView(degrees);
        const float after = std::tan(qDegreesToRadians(fieldOfViewDeg_ * 0.5f));
        if (after > 1e-4f && before > 1e-4f)
            distance_ = std::clamp(distance_ * before / after, 0.5f, 5000.0f);
    }

    /// Arcball (trackball) rotation from one cursor position to another.
    ///
    /// Both points are in widget pixels; `width`/`height` size the virtual
    /// sphere. This replaced the yaw/pitch model, which had two defects a user
    /// feels immediately: dragging in a circle did not return the object to
    /// where it started (Euler composition is not commutative, so the net
    /// rotation depended on the path), and pitch had to be clamped at ±89° to
    /// avoid gimbal lock — so the scene could never be turned fully over.
    ///
    /// Inside the sphere the drag rotates about an axis in the screen plane;
    /// outside it, the projection falls to the rim and the motion becomes pure
    /// roll about the view axis. That is the conventional arcball behaviour and
    /// it means roll no longer needs a control of its own to reach.
    void rotateArcball(const QPointF& from, const QPointF& to, int width,
                       int height);

    /// Programmatic turntable step: yaw about the world up axis, pitch about
    /// the camera's own right axis. Kept for the animation paths (the film
    /// turntable), which want a reproducible angular step rather than a
    /// pointer gesture.
    void rotate(float dxDeg, float dyDeg);

    /// Jump to an absolute orientation (used by the view-alignment
    /// buttons): yaw/pitch/roll in degrees, target and distance unchanged.
    /// Clears any scene rotation so the alignment is truly axis-aligned.
    ///
    /// Roll defaults to 0 because that is what "aligned with a plane" means —
    /// an axis-aligned view that arrived tilted would not be aligned.
    void setOrientation(float yawDeg, float pitchDeg, float rollDeg = 0.0f)
    {
        orientation_ = fromEuler(yawDeg, pitchDeg, rollDeg);
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
        orientation_ =
            fromEuler(kDefaultYawDeg, kDefaultPitchDeg, kDefaultRollDeg);
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
    /// extent, at the CURRENT field of view. Used for intelligent
    /// auto-zoom: ~0.9 fits a unit cell, 0.5 sizes a molecule to half-height.
    void frameToFraction(const QVector3D& center, float radius,
                         float verticalFraction);

    QMatrix4x4 view() const;
    QMatrix4x4 projection(float aspectRatio) const;

    float distance() const { return distance_; }
    QVector3D target() const { return target_; }
    /// Euler read-outs, DERIVED from the orientation quaternion.
    ///
    /// They exist for the UI (the Set Point-of-View dialog's three spin boxes)
    /// and for serialization. Any rotation can be written as this ZXY triple,
    /// so the round trip is exact except exactly at pitch = ±90°, where roll
    /// and yaw become the same axis and the split between them is arbitrary.
    float yaw() const { return toEuler(orientation_).y(); }
    float pitch() const { return toEuler(orientation_).x(); }
    float roll() const { return toEuler(orientation_).z(); }
    /// Tilt the camera about its own view axis (turns the picture in the
    /// screen plane; the viewpoint and the model are unchanged).
    void setRoll(float degrees)
    {
        const QVector3D e = toEuler(orientation_);
        orientation_ = fromEuler(e.y(), e.x(), degrees);
    }
    /// The orientation itself, for callers that want it without the Euler
    /// round trip.
    QQuaternion orientation() const { return orientation_; }
    void setOrientation(const QQuaternion& q) { orientation_ = q.normalized(); }

    /// Build the view rotation from an Euler triple, in exactly the order
    /// view() composes them: roll about z, then pitch about x, then yaw about
    /// y. Public and static so the point-of-view plumbing can convert without
    /// duplicating the convention — getting the order wrong here silently
    /// mirrors a restored view.
    static QQuaternion fromEuler(float yawDeg, float pitchDeg, float rollDeg);
    /// The inverse: (pitch, yaw, roll) in degrees.
    static QVector3D toEuler(const QQuaternion& q);

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
        // Both representations are written: the quaternion is what restores
        // exactly, the Euler triple is what the dialog shows and what an older
        // build would read.
        const QVector3D e = toEuler(orientation_);
        PointOfView pov;
        pov.target = target_;
        pov.distance = distance_;
        pov.pitchDeg = e.x();
        pov.yawDeg = e.y();
        pov.rollDeg = e.z();
        pov.sceneRotation = sceneRotation_;
        pov.orientation = orientation_;
        pov.projection = projectionMode_;
        pov.valid = true;
        return pov;
    }
    void setPointOfView(const PointOfView& pov)
    {
        if (!pov.valid)
            return; // nothing was captured; leave the camera alone
        target_ = pov.target;
        // A non-positive distance would put the eye at the target and make the
        // view matrix singular; clamp rather than trust a hand-edited value.
        distance_ = std::max(pov.distance, 1e-3f);
        // An identity quaternion means the view predates the arcball (or is a
        // genuine zero rotation, which the Euler path reproduces identically),
        // so the Euler triple is the right source in both cases.
        orientation_ = pov.orientation.isIdentity()
            ? fromEuler(pov.yawDeg, pov.pitchDeg, pov.rollDeg)
            : pov.orientation.normalized();
        sceneRotation_ = pov.sceneRotation;
        projectionMode_ = pov.projection;
    }

private:
    /// Map a cursor position onto the virtual arcball. Inside the ball the
    /// point lies on the sphere; outside it falls to the rim, which turns the
    /// drag into pure roll.
    static QVector3D mapToSphere(const QPointF& p, int width, int height);

    QVector3D target_{0.0f, 0.0f, 0.0f};
    float distance_ = 20.0f;
    /// The camera rotation. Single source of truth — yaw/pitch/roll are
    /// derived from it, not stored beside it, so the two can never disagree.
    QQuaternion orientation_ =
        fromEuler(kDefaultYawDeg, kDefaultPitchDeg, kDefaultRollDeg);
    QQuaternion sceneRotation_; ///< extra world-axis rotation (identity default)
    CameraProjection projectionMode_ = kDefaultProjection;
    float fieldOfViewDeg_ = kDefaultFieldOfViewDeg;
};

} // namespace calango::render
