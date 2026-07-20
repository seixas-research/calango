#pragma once

#include "core/Structure.hpp"

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

} // namespace calango::core
