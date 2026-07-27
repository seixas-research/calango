#include "render/Film.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace calango::render {

namespace {

/// Ease in and out. A camera that starts and stops abruptly reads as a glitch
/// rather than as a move, and this is the cheapest curve that fixes it.
double smoothstep(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/// Wrap an angle difference into (-180, 180] so a blend always takes the short
/// way round the circle.
double shortestArc(double degrees)
{
    degrees = std::fmod(degrees + 180.0, 360.0);
    if (degrees < 0.0)
        degrees += 360.0;
    return degrees - 180.0;
}

/// Cast opacities of one keyframe as a lookup, defaulting absent casts to
/// fully opaque.
std::map<int, float> opacityMap(const std::vector<FilmCastOpacity>& entries)
{
    std::map<int, float> out;
    for (const FilmCastOpacity& entry : entries)
        out[entry.cast] = entry.opacity;
    return out;
}

/// Blend the two keyframes' cast opacities. The union of both sides is taken,
/// with a cast missing from one side counting as opaque there — that is what
/// makes an opacity set on a single keyframe ramp down to it and back out
/// instead of latching.
std::vector<FilmCastOpacity> blendCastOpacity(
    const std::vector<FilmCastOpacity>& from,
    const std::vector<FilmCastOpacity>& to, double t)
{
    const std::map<int, float> a = opacityMap(from);
    const std::map<int, float> b = opacityMap(to);
    std::vector<FilmCastOpacity> out;
    std::map<int, bool> casts;
    for (const auto& [cast, value] : a) {
        (void)value;
        casts[cast] = true;
    }
    for (const auto& [cast, value] : b) {
        (void)value;
        casts[cast] = true;
    }
    for (const auto& [cast, present] : casts) {
        (void)present;
        const auto ia = a.find(cast);
        const auto ib = b.find(cast);
        const float va = ia == a.end() ? 1.0f : ia->second;
        const float vb = ib == b.end() ? 1.0f : ib->second;
        out.push_back({cast, static_cast<float>(va + (vb - va) * t)});
    }
    return out;
}

} // namespace

bool FilmScript::isValid() const
{
    return !shots.empty() && fps > 0 && duration > 0.0;
}

double FilmScript::effectiveDuration() const
{
    // The priority rule, and the only place it lives: with the trajectory in
    // charge, the film runs exactly as long as the trajectory naturally does,
    // and every camera move is re-timed to fit. With the film in charge (or
    // with no trajectory at all) the typed duration stands and the trajectory
    // is the thing that gets stretched — see sampleFilm(), which spreads the
    // trajectory's frames across whatever this returns.
    if (priority == FilmTimelinePriority::Trajectory && hasTrajectory()
        && trajectoryFps > 0.0) {
        return static_cast<double>(trajectoryFrames) / trajectoryFps;
    }
    return duration;
}

std::vector<double> FilmScript::segmentDurations() const
{
    const auto count = static_cast<int>(shots.size());
    if (count < 2)
        return {};
    const int segments = count - 1;
    const double total = effectiveDuration();

    std::vector<double> raw(static_cast<std::size_t>(segments), 0.0);
    double sumSet = 0.0;
    int countSet = 0;
    for (int i = 0; i < segments; ++i) {
        const double value = shots[static_cast<std::size_t>(i)].segmentSeconds;
        if (value > 0.0) {
            raw[static_cast<std::size_t>(i)] = value;
            sumSet += value;
            ++countSet;
        }
    }

    if (countSet == 0) {
        // Nothing timed: the even split, unchanged.
        return std::vector<double>(static_cast<std::size_t>(segments),
                                   total / static_cast<double>(segments));
    }

    // A partly-timed film still has to play. An unset segment takes the mean
    // of the timed ones rather than zero, which would make it a hard cut the
    // user never asked for.
    const double mean = sumSet / static_cast<double>(countSet);
    double sum = 0.0;
    for (double& value : raw) {
        if (value <= 0.0)
            value = mean;
        sum += value;
    }
    const double scale = sum > 0.0 ? total / sum : 1.0;
    for (double& value : raw)
        value *= scale;
    return raw;
}

int FilmScript::frameCount() const
{
    if (!isValid())
        return 1;
    const double frames = effectiveDuration() * static_cast<double>(fps);
    return std::max(1, static_cast<int>(std::lround(frames)));
}

PointOfView blendPointOfView(const PointOfView& from, const PointOfView& to,
                             double t)
{
    t = std::clamp(t, 0.0, 1.0);
    PointOfView out = from;
    out.target = from.target * static_cast<float>(1.0 - t)
        + to.target * static_cast<float>(t);
    // Geometric blend: a dolly reads as a ratio, not a difference.
    if (from.distance > 0.0f && to.distance > 0.0f) {
        out.distance = static_cast<float>(
            from.distance
            * std::pow(static_cast<double>(to.distance / from.distance), t));
    } else {
        out.distance = static_cast<float>(from.distance
                                          + (to.distance - from.distance) * t);
    }
    out.yawDeg = static_cast<float>(
        from.yawDeg + shortestArc(to.yawDeg - from.yawDeg) * t);
    out.pitchDeg = static_cast<float>(
        from.pitchDeg + shortestArc(to.pitchDeg - from.pitchDeg) * t);
    // Roll takes the same shortest-arc path as the other two: a shot that
    // tilts from -170 deg to +170 deg should roll 20 deg through the wrap, not
    // 340 deg the long way round.
    out.rollDeg = static_cast<float>(
        from.rollDeg + shortestArc(to.rollDeg - from.rollDeg) * t);
    out.sceneRotation = QQuaternion::slerp(from.sceneRotation, to.sceneRotation,
                                           static_cast<float>(t));
    // Projection is discrete — there is no half-orthographic camera — so it
    // switches at the midpoint rather than blending.
    out.projection = t < 0.5 ? from.projection : to.projection;
    out.valid = from.valid || to.valid;
    return out;
}

FilmSample sampleFilm(const FilmScript& script, double timeSeconds)
{
    FilmSample sample;
    if (!script.isValid())
        return sample;

    const double total = script.effectiveDuration();
    const double time = std::clamp(timeSeconds, 0.0, total);

    // -- Camera and casts ---------------------------------------------------
    const auto shotCount = static_cast<int>(script.shots.size());
    // Which shot's overlay set is on screen. Overlays cannot be blended — a
    // label is either shown or not — so this is always a hard switch to
    // whichever shot the frame most belongs to, unlike the cast opacities
    // beside it.
    const FilmShot* overlaySource = nullptr;

    if (shotCount == 1) {
        sample.camera = script.shots.front().pov;
        sample.castOpacity = script.shots.front().castOpacity;
        overlaySource = &script.shots.front();
    } else {
        const int segments = shotCount - 1;
        // Segments are no longer necessarily equal, so the shot is found by
        // walking the cumulative times rather than by dividing.
        const std::vector<double> lengths = script.segmentDurations();
        int index = 0;
        double start = 0.0;
        // The final instant belongs to the last segment, not to a segment one
        // past the end — without this, t == duration indexes out of range.
        while (index < segments - 1
               && time >= start + lengths[static_cast<std::size_t>(index)]) {
            start += lengths[static_cast<std::size_t>(index)];
            ++index;
        }
        const double segmentLength = lengths[static_cast<std::size_t>(index)];
        const double u = segmentLength > 0.0
            ? std::clamp((time - start) / segmentLength, 0.0, 1.0)
            : 1.0;

        const FilmShot& from = script.shots[static_cast<std::size_t>(index)];
        const FilmShot& to = script.shots[static_cast<std::size_t>(index + 1)];

        switch (from.transitionToNext) {
        case FilmTransition::HardCut:
            // Hold the outgoing shot for the whole segment. The next segment
            // opens on its own keyframe, which IS the cut.
            sample.camera = u >= 1.0 ? to.pov : from.pov;
            sample.castOpacity = u >= 1.0 ? to.castOpacity : from.castOpacity;
            overlaySource = u >= 1.0 ? &to : &from;
            break;
        case FilmTransition::Interpolation: {
            const double eased = smoothstep(u);
            sample.camera = blendPointOfView(from.pov, to.pov, eased);
            sample.castOpacity =
                blendCastOpacity(from.castOpacity, to.castOpacity, eased);
            overlaySource = u < 0.5 ? &from : &to;
            break;
        }
        case FilmTransition::Crossfade: {
            // The live camera is the INCOMING shot, so a caller that renders
            // the sample normally and mixes the cached outgoing image over it
            // ends on the incoming shot with nothing left to composite.
            const double eased = smoothstep(u);
            sample.camera = to.pov;
            sample.castOpacity = to.castOpacity;
            sample.crossfading = true;
            sample.crossfadeFrom = from.pov;
            sample.crossfadeFromCastOpacity = from.castOpacity;
            sample.crossfadeWeight = static_cast<float>(eased);
            overlaySource = u < 0.5 ? &from : &to;
            break;
        }
        case FilmTransition::FadeInOut:
            // Snap at the midpoint, with the image dipping to black around it:
            // fade runs 1 -> 0 -> 1 so the cut itself is never visible.
            sample.camera = u < 0.5 ? from.pov : to.pov;
            sample.castOpacity = u < 0.5 ? from.castOpacity : to.castOpacity;
            sample.fade = static_cast<float>(std::fabs(2.0 * u - 1.0));
            // Switched under cover of the black, like the camera itself.
            overlaySource = u < 0.5 ? &from : &to;
            break;
        }
    }

    if (overlaySource && overlaySource->overridesOverlays) {
        sample.overridesOverlays = true;
        sample.overlayIds = overlaySource->overlayIds;
    }

    // -- Trajectory ---------------------------------------------------------
    //
    // The trajectory always plays exactly once across the effective duration,
    // whichever timeline set that duration. That single line is the whole
    // stretch/compress behaviour: with Film priority the duration is the typed
    // one and a 10 s trajectory spread over 20 s plays at half speed; with
    // Trajectory priority the duration IS the trajectory's own, so it plays at
    // its natural rate and the camera moves are what get re-timed.
    if (script.hasTrajectory()) {
        const double position = total > 0.0 ? time / total : 0.0;
        const int frame = static_cast<int>(
            position * static_cast<double>(script.trajectoryFrames));
        sample.trajectoryFrame =
            std::clamp(frame, 0, script.trajectoryFrames - 1);
    }

    return sample;
}

} // namespace calango::render
