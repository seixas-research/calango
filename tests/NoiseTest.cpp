// Random Noise Setup's perturbation core: the deterministic pieces that
// don't need Qt or a GUI event loop — the linear-ramp amplitude formula
// (rampAmplitudeFactor) and applyRandomNoise's seed reproducibility.
//
// The GUI-level check (DialogConstructionTest.cpp, "Random Noise dialog")
// exercises the ramp end to end through the actual wizard, but only compares
// the endpoints of a random ensemble — comparing every adjacent frame there
// would be flaky, since each frame draws independent random displacements.
// The formula itself has no such excuse: it is pure arithmetic, and is
// pinned exactly here.

#include "core/Noise.hpp"
#include "core/Structure.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace calango::core;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

void checkClose(double got, double want, double tol, const std::string& what)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  %-4s %s (got %.10g, want %.10g, tol %g)\n", ok ? "ok" : "FAIL",
                what.c_str(), got, want, tol);
    if (!ok)
        ++failures;
}

Structure fourAtomSilicon()
{
    Structure structure;
    structure.setCell(UnitCell({8, 0, 0}, {0, 8, 0}, {0, 0, 8}));
    for (int i = 0; i < 4; ++i) {
        Atom atom;
        atom.atomicNumber = 14;
        atom.position = {i * 1.5, i * 0.7, i * 0.3};
        structure.addAtom(atom);
    }
    return structure;
}

void testRampAmplitudeFactorEndpoints()
{
    std::printf("Ramp amplitude factor: endpoints\n");
    // frame 0 (the untouched reference) is never passed through
    // applyRandomNoise at all -- it is the "i=0 -> factor 0" endpoint by
    // construction, with no call into this function. This function only
    // covers member indices 1..count -- the PERTURBED frames.
    checkClose(rampAmplitudeFactor(1, 1), 1.0, 1e-15,
               "count=1: the single perturbed member is also the LAST frame "
               "of the trajectory, so it gets full amplitude -- exactly the "
               "'first frame zero, last frame full' law at its smallest "
               "possible trajectory (N=2 total frames)");
    checkClose(rampAmplitudeFactor(20, 20), 1.0, 1e-15,
               "the last member always reaches exactly full amplitude");
    checkClose(rampAmplitudeFactor(1, 20), 0.05, 1e-15,
               "the first perturbed member gets 1/count of the amplitude");
    checkClose(rampAmplitudeFactor(10, 20), 0.5, 1e-15,
               "the middle member gets exactly half");
}

void testRampAmplitudeFactorLinearAndMonotonic()
{
    std::printf("Ramp amplitude factor: linear and monotonically "
                "increasing\n");
    const int count = 12;
    double previous = -1.0;
    bool monotonic = true;
    bool linear = true;
    for (int k = 1; k <= count; ++k) {
        const double factor = rampAmplitudeFactor(k, count);
        monotonic = monotonic && factor > previous;
        previous = factor;
        // f(k) = k/count is exactly linear in k -- check against the closed
        // form directly rather than only against its own neighbours.
        linear = linear
            && std::abs(factor - static_cast<double>(k) / count) < 1e-15;
    }
    check(monotonic, "strictly increasing across every member index");
    check(linear, "and exactly linear in the member index");
}

void testRampAmplitudeFactorDefensiveZeroCount()
{
    std::printf("Ramp amplitude factor: defensive count<=0 fallback\n");
    // The wizard's spin box enforces count >= 1, so this path is not
    // reachable from the UI -- it exists only so a caller outside that
    // guarantee gets full amplitude rather than a division by zero.
    checkClose(rampAmplitudeFactor(1, 0), 1.0, 1e-15,
               "count<=0 falls back to full amplitude, not a NaN/inf from "
               "dividing by zero");
}

void testApplyRandomNoiseReproducibleForSameSeed()
{
    std::printf("applyRandomNoise: same seed -> identical result\n");
    NoiseOptions options;
    options.amplitude = 0.05;
    options.seed = 12345;

    Structure a = fourAtomSilicon();
    Structure b = fourAtomSilicon();
    applyRandomNoise(a, options);
    applyRandomNoise(b, options);

    bool identical = a.size() == b.size();
    for (std::size_t i = 0; identical && i < a.size(); ++i)
        identical = (a.atoms()[i].position - b.atoms()[i].position).norm() < 1e-15;
    check(identical,
          "two independent perturbations with the same seed and amplitude "
          "produce bit-for-bit identical displacements -- the guarantee the "
          "ramp composes with, since it only ever rescales the amplitude "
          "passed in here, never the seed");

    Structure differentSeed = fourAtomSilicon();
    NoiseOptions otherOptions = options;
    otherOptions.seed = 54321;
    applyRandomNoise(differentSeed, otherOptions);
    bool anyDifferent = false;
    for (std::size_t i = 0; i < a.size(); ++i)
        anyDifferent = anyDifferent
            || (a.atoms()[i].position - differentSeed.atoms()[i].position).norm()
                > 1e-9;
    check(anyDifferent,
          "and a different seed actually produces a different displacement "
          "(the two are not silently the same draw)");
}

void testRampComposesWithSeedAcrossMembers()
{
    std::printf("Ramp composes with the per-member seed formula the wizard "
                "uses\n");
    // Mirrors RandomNoiseWizard::generateStructures()'s per-member loop
    // exactly (member.seed = baseSeed + k; member.amplitude scaled by the
    // ramp factor when ramping is on) without any Qt/GUI dependency, so the
    // composition of "ramp" with "same seed -> same trajectory" is checked
    // as pure core-library behaviour.
    const unsigned int baseSeed = 7;
    const double maxAmplitude = 0.08;
    const int count = 10;

    const auto buildEnsemble = [&](bool ramped) {
        std::vector<Structure> frames;
        const Structure original = fourAtomSilicon();
        for (int k = 1; k <= count; ++k) {
            NoiseOptions member;
            member.amplitude = maxAmplitude;
            member.seed = baseSeed + static_cast<unsigned int>(k);
            if (ramped)
                member.amplitude *= rampAmplitudeFactor(k, count);
            Structure frame = original;
            applyRandomNoise(frame, member);
            frames.push_back(std::move(frame));
        }
        return frames;
    };

    const auto ensembleA = buildEnsemble(true);
    const auto ensembleB = buildEnsemble(true);
    bool sameTrajectory = ensembleA.size() == ensembleB.size();
    for (std::size_t k = 0; sameTrajectory && k < ensembleA.size(); ++k)
        for (std::size_t i = 0; sameTrajectory && i < ensembleA[k].size(); ++i)
            sameTrajectory = (ensembleA[k].atoms()[i].position
                              - ensembleB[k].atoms()[i].position)
                                 .norm()
                < 1e-15;
    check(sameTrajectory,
          "regenerating the ramped ensemble with the same seed reproduces "
          "it exactly, member by member");

    const auto original = fourAtomSilicon();
    const auto displacement = [&](const Structure& frame) {
        double sum = 0.0;
        for (std::size_t i = 0; i < frame.size(); ++i)
            sum += (frame.atoms()[i].position - original.atoms()[i].position)
                       .norm();
        return sum;
    };
    const auto ramped = buildEnsemble(true);
    const auto constant = buildEnsemble(false);
    check(displacement(ramped.front()) < displacement(constant.front()),
          "the ramp's first member is displaced less than the constant-"
          "amplitude run's first member (same seed, so this isolates the "
          "amplitude scaling, not a different random draw)");
    check(std::abs(displacement(ramped.back()) - displacement(constant.back()))
              < 1e-9,
          "while the ramp's LAST member reaches the same full amplitude the "
          "constant run always used, and — same seed — the same draw");
}

} // namespace

int main()
{
    std::printf("Noise - linear ramp amplitude and seed reproducibility\n\n");
    testRampAmplitudeFactorEndpoints();
    testRampAmplitudeFactorLinearAndMonotonic();
    testRampAmplitudeFactorDefensiveZeroCount();
    testApplyRandomNoiseReproducibleForSameSeed();
    testRampComposesWithSeedAcrossMembers();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
