// Film timing and sampling test.
//
// The film model is where the two things a user can actually get wrong live:
// the transition semantics (a hard cut must NOT drift, a fade must reach black
// exactly at the cut) and the timeline priority rule (whichever timeline wins,
// the trajectory still plays exactly once across the film). Both are pure
// arithmetic, so they are checked here rather than by watching a preview.
//
// GUI-free (Qt Gui math types only, no widgets, no GL).

#include "render/Film.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace calango::render;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

void checkNear(double value, double expected, double tolerance,
               const std::string& what)
{
    const bool ok = std::fabs(value - expected) <= tolerance;
    std::printf("  %s %s (got %.4f, expected %.4f +- %.4f)\n",
                ok ? "ok  " : "FAIL", what.c_str(), value, expected, tolerance);
    if (!ok)
        ++failures;
}

FilmShot shot(float yaw, float distance, FilmTransition transition)
{
    FilmShot s;
    s.pov.valid = true;
    s.pov.yawDeg = yaw;
    s.pov.pitchDeg = 0.0f;
    s.pov.distance = distance;
    s.transitionToNext = transition;
    return s;
}

// -- Timing ---------------------------------------------------------------

void testFrameCount()
{
    std::printf("frame count from duration and fps\n");
    FilmScript script;
    script.shots = {shot(0.0f, 10.0f, FilmTransition::Interpolation)};
    script.duration = 10.0;
    script.fps = 30;
    check(script.frameCount() == 300, "10 s at 30 fps is 300 frames");

    script.fps = 24;
    check(script.frameCount() == 240, "10 s at 24 fps is 240 frames");

    script.duration = 2.5;
    check(script.frameCount() == 60, "2.5 s at 24 fps is 60 frames");

    FilmScript empty;
    check(!empty.isValid(), "a film with no shots is invalid");
}

/// The worked example from the request: a 10 s trajectory and a 20 s film.
/// With the film in charge the answer is 20 s and the trajectory is stretched;
/// with the trajectory in charge it is 10 s and the camera moves compress.
void testTimelinePriority()
{
    std::printf("timeline priority (10 s trajectory vs 20 s film)\n");
    FilmScript script;
    script.shots = {shot(0.0f, 10.0f, FilmTransition::Interpolation),
                    shot(90.0f, 10.0f, FilmTransition::Interpolation)};
    script.duration = 20.0;
    script.fps = 30;
    script.trajectoryFrames = 100;
    script.trajectoryFps = 10.0; // 100 frames / 10 fps = 10 s natural

    script.priority = FilmTimelinePriority::Film;
    checkNear(script.effectiveDuration(), 20.0, 1e-9,
              "film priority keeps the 20 s film length");
    check(script.frameCount() == 600, "600 frames at 30 fps");
    // Stretched: the trajectory's halfway frame lands at the film's halfway
    // point, i.e. at 10 s rather than at its natural 5 s.
    check(sampleFilm(script, 10.0).trajectoryFrame == 50,
          "at 10 s the stretched trajectory is at its own midpoint");
    check(sampleFilm(script, 0.0).trajectoryFrame == 0, "starts at frame 0");
    check(sampleFilm(script, 20.0).trajectoryFrame == 99,
          "ends on the last frame, not one past it");

    script.priority = FilmTimelinePriority::Trajectory;
    checkNear(script.effectiveDuration(), 10.0, 1e-9,
              "trajectory priority shortens the film to 10 s");
    check(script.frameCount() == 300, "300 frames at 30 fps");
    check(sampleFilm(script, 5.0).trajectoryFrame == 50,
          "the trajectory plays at its natural rate");
    // The camera is what gets re-timed now: the halfway pose arrives at 5 s.
    checkNear(sampleFilm(script, 5.0).camera.yawDeg, 45.0, 1e-3,
              "the camera move is compressed into the trajectory's length");

    // No trajectory: the priority setting must not change anything.
    script.trajectoryFrames = 0;
    checkNear(script.effectiveDuration(), 20.0, 1e-9,
              "with no trajectory the film's own duration always wins");
    check(sampleFilm(script, 5.0).trajectoryFrame == -1,
          "no trajectory frame is reported");
}

// -- Transitions ----------------------------------------------------------

/// Interpolation must actually move, ease at both ends, and land exactly on
/// the keyframes at t=0 and t=duration.
void testInterpolation()
{
    std::printf("interpolation transition\n");
    FilmScript script;
    script.shots = {shot(0.0f, 10.0f, FilmTransition::Interpolation),
                    shot(90.0f, 10.0f, FilmTransition::Interpolation)};
    script.duration = 10.0;

    checkNear(sampleFilm(script, 0.0).camera.yawDeg, 0.0, 1e-3,
              "starts on the first keyframe");
    checkNear(sampleFilm(script, 10.0).camera.yawDeg, 90.0, 1e-3,
              "ends on the last keyframe");
    checkNear(sampleFilm(script, 5.0).camera.yawDeg, 45.0, 1e-3,
              "the midpoint is halfway round");
    // Easing: a quarter of the way through, a smoothstep is BEHIND linear.
    const double quarter = sampleFilm(script, 2.5).camera.yawDeg;
    check(quarter < 22.5, "eases in rather than starting at full speed");
    checkNear(sampleFilm(script, 5.0).fade, 1.0, 1e-6,
              "interpolation never fades");
}

/// A hard cut must hold its outgoing keyframe for the whole segment — any
/// drift means it is secretly an interpolation.
void testHardCut()
{
    std::printf("hard cut transition\n");
    FilmScript script;
    script.shots = {shot(0.0f, 10.0f, FilmTransition::HardCut),
                    shot(90.0f, 10.0f, FilmTransition::HardCut),
                    shot(180.0f, 10.0f, FilmTransition::HardCut)};
    script.duration = 10.0; // two segments of 5 s

    for (const double t : {0.0, 1.0, 2.5, 4.9}) {
        checkNear(sampleFilm(script, t).camera.yawDeg, 0.0, 1e-6,
                  "first segment holds the first shot");
    }
    for (const double t : {5.0, 7.5, 9.9}) {
        checkNear(sampleFilm(script, t).camera.yawDeg, 90.0, 1e-6,
                  "second segment holds the second shot");
    }
    checkNear(sampleFilm(script, 10.0).camera.yawDeg, 180.0, 1e-6,
              "the final instant lands on the last shot");
    checkNear(sampleFilm(script, 2.5).fade, 1.0, 1e-6, "a hard cut never fades");
}

/// The fade has to reach black exactly where the camera snaps, and be fully
/// open at both ends of the segment — otherwise the film opens or closes on a
/// half-dark frame.
void testFadeInOut()
{
    std::printf("fade in / fade out transition\n");
    FilmScript script;
    script.shots = {shot(0.0f, 10.0f, FilmTransition::FadeInOut),
                    shot(90.0f, 10.0f, FilmTransition::FadeInOut)};
    script.duration = 10.0;

    checkNear(sampleFilm(script, 0.0).fade, 1.0, 1e-6, "opens fully visible");
    checkNear(sampleFilm(script, 10.0).fade, 1.0, 1e-6, "closes fully visible");
    checkNear(sampleFilm(script, 5.0).fade, 0.0, 1e-6, "black at the cut");
    checkNear(sampleFilm(script, 2.5).fade, 0.5, 1e-6, "half way down");
    checkNear(sampleFilm(script, 7.5).fade, 0.5, 1e-6, "half way back up");

    // The camera snaps at the midpoint, under cover of the black frame.
    checkNear(sampleFilm(script, 4.9).camera.yawDeg, 0.0, 1e-6,
              "still on the outgoing shot just before the cut");
    checkNear(sampleFilm(script, 5.1).camera.yawDeg, 90.0, 1e-6,
              "on the incoming shot just after it");
}

/// A crossfade is the one transition that is a MIX of two renders rather than
/// one camera, so the sample has to hand out both sides and a weight — and the
/// screen must never be empty (that is what distinguishes it from a fade).
void testCrossfade()
{
    std::printf("crossfade transition\n");
    FilmScript script;
    script.shots = {shot(0.0f, 10.0f, FilmTransition::Crossfade),
                    shot(90.0f, 20.0f, FilmTransition::Crossfade)};
    script.duration = 10.0;

    for (const double t : {0.0, 2.5, 5.0, 7.5, 10.0}) {
        const FilmSample s = sampleFilm(script, t);
        check(s.crossfading, "the whole segment is a dissolve");
        // The live camera is the INCOMING shot throughout, so a caller that
        // renders the sample and mixes the cached outgoing image over it
        // finishes on the incoming shot with nothing left to composite.
        checkNear(s.camera.yawDeg, 90.0, 1e-6, "the live camera is the incoming shot");
        checkNear(s.crossfadeFrom.yawDeg, 0.0, 1e-6,
                  "the outgoing shot is reported for the second render");
        checkNear(s.fade, 1.0, 1e-6, "a dissolve never goes to black");
    }

    checkNear(sampleFilm(script, 0.0).crossfadeWeight, 0.0, 1e-6,
              "opens showing only the outgoing shot");
    checkNear(sampleFilm(script, 10.0).crossfadeWeight, 1.0, 1e-6,
              "closes showing only the incoming shot");
    checkNear(sampleFilm(script, 5.0).crossfadeWeight, 0.5, 1e-6,
              "an even mix at the midpoint");
    // Eased like the camera fly, so the dissolve starts and ends gently.
    check(sampleFilm(script, 2.5).crossfadeWeight < 0.25,
          "the mix eases in rather than ramping linearly");

    // Neither side of a dissolve takes a blended cast opacity: each is a whole
    // render of its own shot, and the mix is what blends them.
    script.shots[0].castOpacity = {{1, 0.2f}};
    script.shots[1].castOpacity = {{1, 0.8f}};
    const FilmSample mid = sampleFilm(script, 5.0);
    check(mid.castOpacity.size() == 1 && mid.castOpacity[0].opacity > 0.79f,
          "the incoming render uses the incoming shot's own cast opacity");
    check(mid.crossfadeFromCastOpacity.size() == 1
              && mid.crossfadeFromCastOpacity[0].opacity < 0.21f,
          "the outgoing render uses the outgoing shot's own cast opacity");
}

/// Every other transition must leave the crossfade fields inert, or a caller
/// would composite a stale second render over an ordinary frame.
void testNonCrossfadeTransitionsAreInert()
{
    std::printf("crossfade fields stay inert elsewhere\n");
    for (const FilmTransition transition :
         {FilmTransition::HardCut, FilmTransition::Interpolation,
          FilmTransition::FadeInOut}) {
        FilmScript script;
        script.shots = {shot(0.0f, 10.0f, transition),
                        shot(90.0f, 10.0f, transition)};
        script.duration = 10.0;
        bool inert = true;
        for (const double t : {0.0, 2.5, 5.0, 7.5, 10.0})
            inert = inert && !sampleFilm(script, t).crossfading;
        check(inert, "no dissolve is reported");
    }
    // A single-shot film has no transition at all.
    FilmScript still;
    still.shots = {shot(0.0f, 10.0f, FilmTransition::Crossfade)};
    check(!sampleFilm(still, 0.0).crossfading,
          "a one-shot film has nothing to dissolve into");
}

// -- Camera blending ------------------------------------------------------

/// Yaw must take the short way round: 350 -> 10 is 20 degrees forward, not
/// 340 back. A film that spins the long way is the classic symptom.
void testShortestArc()
{
    std::printf("yaw blends the short way round\n");
    PointOfView a;
    a.yawDeg = 350.0f;
    a.distance = 10.0f;
    PointOfView b;
    b.yawDeg = 10.0f;
    b.distance = 10.0f;

    const PointOfView mid = blendPointOfView(a, b, 0.5);
    // 350 + 20*0.5 = 360, i.e. the same place as 0 degrees.
    const double wrapped = std::fmod(mid.yawDeg + 720.0, 360.0);
    check(wrapped < 1.0 || wrapped > 359.0,
          "the midpoint of 350 -> 10 is 0, not 180");
}

/// Zoom is multiplicative: the midpoint of 1 A and 100 A is 10 A, not 50.5.
void testGeometricZoom()
{
    std::printf("distance blends geometrically\n");
    PointOfView a;
    a.distance = 1.0f;
    PointOfView b;
    b.distance = 100.0f;
    checkNear(blendPointOfView(a, b, 0.5).distance, 10.0, 1e-3,
              "the midpoint of a 1 A -> 100 A dolly is 10 A");
    checkNear(blendPointOfView(a, b, 0.0).distance, 1.0, 1e-6, "starts at 1 A");
    checkNear(blendPointOfView(a, b, 1.0).distance, 100.0, 1e-3, "ends at 100 A");
}

// -- Cast transformations --------------------------------------------------

/// A cast faded on one keyframe ramps down to it and back out, so the same
/// setting works whether the user wanted a dip or a permanent change (which
/// they express by setting it on both keyframes).
void testCastOpacityRamp()
{
    std::printf("cast opacity keyframes\n");
    FilmScript script;
    script.shots = {shot(0.0f, 10.0f, FilmTransition::Interpolation),
                    shot(0.0f, 10.0f, FilmTransition::Interpolation),
                    shot(0.0f, 10.0f, FilmTransition::Interpolation)};
    script.shots[1].castOpacity = {{1, 0.0f}}; // cast 1 vanishes mid-film
    script.duration = 10.0; // two 5 s segments

    const auto opacityOf = [](const FilmSample& s, int cast) {
        for (const FilmCastOpacity& entry : s.castOpacity)
            if (entry.cast == cast)
                return entry.opacity;
        return 1.0f; // absent means untouched
    };

    checkNear(opacityOf(sampleFilm(script, 0.0), 1), 1.0, 1e-3,
              "opaque at the start");
    checkNear(opacityOf(sampleFilm(script, 5.0), 1), 0.0, 1e-3,
              "fully transparent at the middle keyframe");
    checkNear(opacityOf(sampleFilm(script, 10.0), 1), 1.0, 1e-3,
              "opaque again at the end");
    // Untouched casts are never reported, so they keep the panel's setting.
    for (const FilmCastOpacity& entry : sampleFilm(script, 5.0).castOpacity)
        check(entry.cast == 1, "only the keyframed cast is reported");
}

/// A single keyframe is a legal film: a static shot over a trajectory is the
/// most common thing anyone actually renders.
void testSingleShot()
{
    std::printf("single-keyframe film\n");
    FilmScript script;
    script.shots = {shot(42.0f, 7.0f, FilmTransition::Interpolation)};
    script.duration = 4.0;
    script.fps = 25;
    script.trajectoryFrames = 40;

    check(script.isValid(), "one keyframe is a valid film");
    check(script.frameCount() == 100, "4 s at 25 fps is 100 frames");
    checkNear(sampleFilm(script, 0.0).camera.yawDeg, 42.0, 1e-6,
              "the camera holds still");
    checkNear(sampleFilm(script, 4.0).camera.yawDeg, 42.0, 1e-6,
              "...for the whole film");
    check(sampleFilm(script, 2.0).trajectoryFrame == 20,
          "the trajectory still plays across it");
}

/// Time outside the film is clamped rather than extrapolated — a scrubber
/// dragged past either end must not produce a camera that never existed.
void testClamping()
{
    std::printf("out-of-range times clamp\n");
    FilmScript script;
    script.shots = {shot(0.0f, 10.0f, FilmTransition::Interpolation),
                    shot(90.0f, 10.0f, FilmTransition::Interpolation)};
    script.duration = 10.0;

    checkNear(sampleFilm(script, -5.0).camera.yawDeg, 0.0, 1e-6,
              "before the start clamps to the first shot");
    checkNear(sampleFilm(script, 99.0).camera.yawDeg, 90.0, 1e-6,
              "after the end clamps to the last shot");
}

} // namespace

int main()
{
    testFrameCount();
    testTimelinePriority();
    testInterpolation();
    testHardCut();
    testFadeInOut();
    testCrossfade();
    testNonCrossfadeTransitionsAreInert();
    testShortestArc();
    testGeometricZoom();
    testCastOpacityRamp();
    testSingleShot();
    testClamping();

    if (failures == 0) {
        std::printf("\nAll film checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d film check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
