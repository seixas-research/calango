#include "core/Vacf.hpp"

#include <cmath>

namespace calango::core {
namespace {

/// Trapezoidal integral of `y` sampled every `dx`.
double trapz(const std::vector<double>& y, double dx)
{
    if (y.size() < 2)
        return 0.0;
    double sum = 0.5 * (y.front() + y.back());
    for (std::size_t i = 1; i + 1 < y.size(); ++i)
        sum += y[i];
    return sum * dx;
}

} // namespace

VacfResult computeVacf(const std::vector<std::vector<Vec3>>& velocities,
                       double dtFs, int maxLag)
{
    VacfResult r;
    const int nFrames = static_cast<int>(velocities.size());
    if (nFrames < 2) {
        r.error = "Need at least two trajectory frames with velocities.";
        return r;
    }
    const std::size_t nAtoms = velocities[0].size();
    if (nAtoms == 0) {
        r.error = "The trajectory frames carry no atoms.";
        return r;
    }
    for (const auto& frame : velocities) {
        if (frame.size() != nAtoms) {
            r.error = "Frames have inconsistent atom counts.";
            return r;
        }
    }
    if (dtFs <= 0.0) {
        r.error = "The timestep must be positive.";
        return r;
    }

    const int lagCap = maxLag > 0 ? maxLag : nFrames / 2;
    const int maxL = std::min(lagCap, nFrames - 1);

    // Unnormalized VACF Z(lag) = <v(0)·v(lag)>, averaged over atoms and origins.
    std::vector<double> z(static_cast<std::size_t>(maxL) + 1, 0.0);
    for (int lag = 0; lag <= maxL; ++lag) {
        double acc = 0.0;
        const int origins = nFrames - lag;
        for (int t0 = 0; t0 < origins; ++t0) {
            const auto& a = velocities[static_cast<std::size_t>(t0)];
            const auto& b = velocities[static_cast<std::size_t>(t0 + lag)];
            for (std::size_t at = 0; at < nAtoms; ++at)
                acc += a[at].dot(b[at]);
        }
        z[static_cast<std::size_t>(lag)] =
            acc / (static_cast<double>(origins) * static_cast<double>(nAtoms));
    }

    r.z0 = z[0];
    r.time.resize(z.size());
    r.cv.resize(z.size());
    for (std::size_t lag = 0; lag < z.size(); ++lag) {
        r.time[lag] = static_cast<double>(lag) * dtFs;
        r.cv[lag] = r.z0 != 0.0 ? z[lag] / r.z0 : 0.0;
    }

    // Green-Kubo diffusion: D = (1/3) ∫ Z(t) dt (Å²/fs).
    r.diffusion = trapz(z, dtFs) / 3.0;
    // Momentum relaxation time: area under the normalized VACF (fs).
    r.relaxationTime = trapz(r.cv, dtFs);

    // Vibrational DOS: cosine transform of the Hann-windowed normalized VACF.
    // Frequency grid runs 0 .. Nyquist (1/(2·dt)); reported in THz.
    const int nFreq = maxL + 1;
    r.freq.resize(static_cast<std::size_t>(nFreq));
    r.vdos.resize(static_cast<std::size_t>(nFreq));
    const double twoPi = 2.0 * M_PI;
    for (int k = 0; k < nFreq; ++k) {
        // f in fs^-1: k / (2·maxL·dt) spans 0 .. 1/(2·dt).
        const double fFs = maxL > 0
            ? static_cast<double>(k) / (2.0 * maxL * dtFs)
            : 0.0;
        r.freq[static_cast<std::size_t>(k)] = fFs * 1000.0; // fs^-1 -> THz
        double sum = 0.0;
        for (int lag = 0; lag <= maxL; ++lag) {
            // Hann window suppresses truncation ringing.
            const double w = maxL > 0
                ? 0.5 * (1.0 + std::cos(M_PI * lag / maxL))
                : 1.0;
            const double weight = lag == 0 ? 1.0 : 2.0; // real, even signal
            sum += weight * w * r.cv[static_cast<std::size_t>(lag)]
                 * std::cos(twoPi * fFs * lag * dtFs);
        }
        r.vdos[static_cast<std::size_t>(k)] = std::abs(sum * dtFs);
    }

    r.valid = true;
    return r;
}

} // namespace calango::core
