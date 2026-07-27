#pragma once

#include "render/Camera.hpp"

#include <QString>

#include <vector>

namespace calango::render {

/// How the film gets from one point-of-view to the next.
enum class FilmTransition {
    /// Snap. The camera holds the outgoing shot for the whole segment and the
    /// next shot begins already in place — the editing cut, with no motion.
    HardCut,
    /// Fly. The camera eases from one shot to the other across the segment;
    /// the default, and the only transition that shows the structure from the
    /// angles BETWEEN the two keyframes.
    Interpolation,
    /// Cut through black. The camera snaps at the midpoint like a hard cut,
    /// but the image dips to black and back around it, which is what separates
    /// two shots that would otherwise read as one continuous take.
    FadeInOut,
    /// Dissolve. Both shots are rendered and mixed, so the outgoing image
    /// thins away as the incoming one appears through it.
    ///
    /// Unlike Interpolation, the camera never occupies the angles in between —
    /// which is exactly why it is useful. Two views with nothing sensible
    /// between them (opposite faces of a slab, before and after a reaction)
    /// can be joined without flying the camera through the structure, and
    /// unlike Fade in / Fade out the screen is never empty.
    Crossfade,
};

/// Which timeline sets the film's length when the workspace holds BOTH a
/// camera film and a trajectory.
enum class FilmTimelinePriority {
    /// The film's own duration wins; the trajectory is stretched or compressed
    /// to play exactly once across it.
    Film,
    /// The trajectory's natural duration wins; the camera moves are re-timed
    /// to fit it.
    Trajectory,
};

/// A cast's opacity at one keyframe, so a cast can be faded down (or up)
/// across a shot — the usual way to reveal a molecule sitting inside a
/// substrate without deleting the substrate.
struct FilmCastOpacity {
    int cast = 0;
    float opacity = 1.0f;
};

/// One keyframe of the film: where the camera is, how it leaves for the next
/// keyframe, and what the casts look like while it is there.
struct FilmShot {
    /// Name of the saved point-of-view this was taken from, for display. The
    /// camera state is copied into `pov` rather than looked up by name, so
    /// renaming or deleting a saved view cannot silently change a film.
    QString povName;
    PointOfView pov;
    /// The transition FROM this shot to the next. Ignored on the last shot.
    FilmTransition transitionToNext = FilmTransition::Interpolation;
    /// Cast opacities at this keyframe. Casts not listed are 1.0 (opaque), so
    /// a cast named on one shot only ramps down to it and back out again.
    std::vector<FilmCastOpacity> castOpacity;
};

/// A complete film: the keyframes plus the timing that turns them into frames.
struct FilmScript {
    std::vector<FilmShot> shots;
    /// The film's own length in seconds. Authoritative unless the priority is
    /// Trajectory and a trajectory is present.
    double duration = 10.0;
    /// Frames per second, for both the preview and the frame count.
    int fps = 30;
    FilmTimelinePriority priority = FilmTimelinePriority::Film;

    // -- Trajectory coupling, filled in by the host from the document -------

    /// Frames in the workspace's trajectory; 0 when there is none, which
    /// disables the priority rule entirely.
    int trajectoryFrames = 0;
    /// The rate the trajectory plays at on its own, used to work out its
    /// natural duration when it has priority.
    double trajectoryFps = 10.0;

    /// At least one keyframe and a positive rate — the minimum to produce a
    /// single frame of film.
    bool isValid() const;
    /// True when the priority dropdown means anything (there is a trajectory
    /// to be re-timed against).
    bool hasTrajectory() const { return trajectoryFrames > 0; }
    /// The length actually played, after the priority rule. With Trajectory
    /// priority and a trajectory present this is the trajectory's natural
    /// duration; otherwise it is `duration`.
    double effectiveDuration() const;
    /// Frames the film renders end to end, at least 1.
    int frameCount() const;
};

/// Everything the viewport needs to draw one instant of the film.
struct FilmSample {
    PointOfView camera;
    /// 1 = fully visible, 0 = black. Only a Fade in / Fade out transition ever
    /// drives this below 1.
    float fade = 1.0f;

    // -- Crossfade ---------------------------------------------------------
    //
    // A dissolve is the one transition that cannot be expressed as a single
    // camera: the frame is a MIX of two complete renders. Both sides are
    // reported so the caller can render each and blend them.
    //
    // Both endpoints are static camera positions — a shot is a fixed pose —
    // so the outgoing render is constant for the whole segment and a caller
    // that caches it pays for one extra render per dissolve, not per frame.

    /// True while a Crossfade transition is running.
    bool crossfading = false;
    /// The outgoing shot's camera. Only meaningful when `crossfading`.
    PointOfView crossfadeFrom;
    /// Cast opacities for the outgoing render, which are the outgoing SHOT's
    /// own — the two sides of a dissolve are each rendered whole, so neither
    /// takes a blended value.
    std::vector<FilmCastOpacity> crossfadeFromCastOpacity;
    /// Weight of `camera` (the incoming shot) in the mix: 0 shows only the
    /// outgoing render, 1 only the incoming one.
    float crossfadeWeight = 1.0f;
    /// Cast opacities at this instant; casts absent from every keyframe are
    /// absent here too and keep whatever the Representation panel set.
    std::vector<FilmCastOpacity> castOpacity;
    /// Trajectory frame to display, or -1 when the film has no trajectory.
    /// Already re-timed by the priority rule.
    int trajectoryFrame = -1;
};

/// The film's state at `timeSeconds`, clamped to [0, effectiveDuration()].
///
/// Shots are spaced evenly: N keyframes make N-1 equal segments, so adding a
/// keyframe subdivides the film rather than lengthening it. That keeps the
/// total duration exactly what the user typed, which is the number they
/// actually care about when a film has to fit a slide or a talk.
FilmSample sampleFilm(const FilmScript& script, double timeSeconds);

/// Blend two camera states, `t` in [0,1]. Exposed for testing and for the
/// dialog's preview scrubbing.
///
/// Distance blends geometrically rather than linearly — zoom reads as
/// multiplicative, and a linear ramp from 5 Å to 500 Å spends almost the whole
/// transition far away. Yaw takes the short way round, so 350 degrees to 10
/// turns 20 degrees forward instead of 340 back.
PointOfView blendPointOfView(const PointOfView& from, const PointOfView& to,
                             double t);

} // namespace calango::render
