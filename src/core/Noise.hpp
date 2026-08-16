#pragma once

#include "core/Structure.hpp"

#include <memory>
#include <vector>

namespace calango::core {

/// Thermal / structural random perturbation of a structure.
struct NoiseOptions {
    enum class Distribution { Gaussian, Uniform };

    Distribution distribution = Distribution::Gaussian;
    /// Gaussian: standard deviation per Cartesian component (Å).
    /// Uniform: half-width of the [-amplitude, amplitude] interval (Å).
    double amplitude = 0.05;
    unsigned int seed = 42; ///< reproducible for a given seed
    bool perturbPositions = true;
    /// Perturb the cell vectors (each Cartesian component). Atoms follow
    /// the cell affinely (fractional coordinates preserved), so the noise
    /// acts as a random strain rather than tearing the structure apart.
    bool perturbCell = false;
};

void applyRandomNoise(Structure& structure, const NoiseOptions& options);

/// Linear-ramp amplitude scale for ensemble member `memberIndex` (1-based:
/// 1..count) of a `count`-member noisy ensemble.
///
/// The ensemble's frame 0 is always the untouched reference (never passed
/// through applyRandomNoise at all), so it is already exactly the "zero
/// noise" endpoint of the ramp without needing a special case here — this
/// function only has to place the `count` PERTURBED members between it and
/// the full-amplitude endpoint. For a trajectory of N = count + 1 total
/// frames, member `memberIndex` is frame `memberIndex`, and the requested
/// law "frame i of N gets amplitude i/(N-1)" becomes
/// memberIndex / (N - 1) = memberIndex / count — which is what this returns.
///
/// `count` is always >= 1 in the wizard (the spin box's minimum), so the
/// division never sees count == 0; the defensive fallback below exists only
/// for callers outside that guarantee, and returns full amplitude rather
/// than dividing by zero. At count == 1 the single perturbed member gets
/// memberIndex/count == 1/1 == full amplitude — exactly the two-frame edge
/// case "first frame zero noise, last (only) member full amplitude" the
/// ramp is specified to produce, with no separate N == 1 branch needed.
constexpr double rampAmplitudeFactor(int memberIndex, int count)
{
    return count > 0 ? static_cast<double>(memberIndex) / static_cast<double>(count)
                     : 1.0;
}

/// The full noisy ensemble: frame 0 the untouched `reference`, then `count`
/// perturbed members, one seed per member (options.seed + memberIndex) so
/// the whole ensemble stays reproducible from the one seed on the page
/// regardless of `ramped`. `cumulative` walks each member from the PREVIOUS
/// one instead of always restarting at `reference` — a random walk rather
/// than `count` independent draws around the same centre.
///
/// Shared between the standalone Random Noise Setup wizard
/// (RandomNoiseWizard::generateStructures()) and the same-named
/// Orchestration node (OrchestrationTransforms.cpp's RandomNoiseSetup case),
/// so the two paths can never disagree about what "20 noisy frames" means —
/// the Orchestration node calls this once per pass and keeps the one frame
/// that pass needs, which costs nothing extra: this whole function is a few
/// hundred microseconds of in-process array work even at three-digit counts.
std::vector<std::shared_ptr<Structure>> buildNoiseEnsemble(
    const Structure& reference, const NoiseOptions& options, int count,
    bool cumulative, bool ramped);

} // namespace calango::core
