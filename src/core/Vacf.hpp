#pragma once

#include "core/Vec3.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Velocity-autocorrelation-function analysis of an MD trajectory.
struct VacfResult {
    std::vector<double> time; ///< lag times (fs)
    std::vector<double> cv;   ///< normalized VACF C_v(t) = <v(0)·v(t)>/<v(0)·v(0)>
    std::vector<double> freq; ///< frequencies (THz) for the VDOS
    std::vector<double> vdos; ///< vibrational DOS (arb. units), |FFT of C_v|

    /// Self-diffusion coefficient D = (1/3) ∫ <v(0)·v(t)> dt (Green-Kubo),
    /// in Å²/fs (assumes velocities are in Å/fs).
    double diffusion = 0.0;
    /// Momentum relaxation time τ = ∫ C_v(t) dt (area under the normalized
    /// VACF), in fs.
    double relaxationTime = 0.0;
    double z0 = 0.0; ///< <v(0)·v(0)> (Å²/fs²)

    bool valid = false;
    std::string error;
};

/// Compute the VACF, its Green-Kubo diffusion coefficient, the vibrational DOS
/// (cosine transform of the normalized VACF, Hann-windowed) and the momentum
/// relaxation time from a trajectory's per-atom velocities.
///
/// `velocities[frame][atom]` are Cartesian velocities (Å/fs) at each MD step;
/// `dtFs` is the time between stored frames (fs). `maxLag` caps the correlation
/// length (<= 0 defaults to half the trajectory). Averages over all atoms and
/// all time origins.
VacfResult computeVacf(const std::vector<std::vector<Vec3>>& velocities,
                       double dtFs, int maxLag = 0);

} // namespace calango::core
