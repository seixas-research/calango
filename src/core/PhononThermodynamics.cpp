#include "core/PhononThermodynamics.hpp"
#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>

namespace calango::core {

namespace {

/// cm⁻¹ → eV. ħω for ω in wavenumbers: hc/e × 100 m⁻¹.
constexpr double kCmToEv = 1.239841984e-4;

/// Trapezoidal weight of sample i on a (possibly non-uniform) grid.
double trapezoidWeight(const std::vector<double>& x, std::size_t i)
{
    const std::size_t n = x.size();
    if (n < 2)
        return 0.0;
    if (i == 0)
        return 0.5 * (x[1] - x[0]);
    if (i == n - 1)
        return 0.5 * (x[n - 1] - x[n - 2]);
    return 0.5 * (x[i + 1] - x[i - 1]);
}

} // namespace

PhononThermoResult computePhononThermodynamics(
    const std::vector<double>& frequenciesCm, const std::vector<double>& dos,
    double minTemperatureK, double maxTemperatureK, int steps)
{
    PhononThermoResult result;
    const std::size_t n = std::min(frequenciesCm.size(), dos.size());
    if (n < 2 || steps < 1)
        return result;

    // -- Pre-reduce the DOS to the modes that contribute ---------------------
    // Doing this once, outside the temperature loop, turns an O(N_T × N_ω)
    // filter into a single pass and keeps the loop body to pure arithmetic.
    struct Mode {
        double energyEv;  ///< ħω
        double weight;    ///< g(ω) dω
    };
    std::vector<Mode> modes;
    modes.reserve(n);
    double imaginaryWeight = 0.0;
    double totalWeight = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double weight = dos[i] * trapezoidWeight(frequenciesCm, i);
        if (weight <= 0.0)
            continue; // negative DOS is meaningless; zero contributes nothing
        // ω <= 0: imaginary (or the acoustic zero itself, which contributes
        // nothing to any of these integrals). Excluded — see the header.
        if (frequenciesCm[i] <= 0.0) {
            imaginaryWeight += weight;
            continue;
        }
        modes.push_back({frequenciesCm[i] * kCmToEv, weight});
        totalWeight += weight;
    }
    result.totalModes = totalWeight;
    const double allWeight = totalWeight + imaginaryWeight;
    result.imaginaryWeight = allWeight > 0.0 ? imaginaryWeight / allWeight : 0.0;

    double zpe = 0.0;
    for (const Mode& mode : modes)
        zpe += 0.5 * mode.energyEv * mode.weight;
    result.zeroPointEnergyEv = zpe;

    // -- Temperature sweep ---------------------------------------------------
    result.points.reserve(static_cast<std::size_t>(steps));
    const double span = maxTemperatureK - minTemperatureK;
    for (int step = 0; step < steps; ++step) {
        const double t = steps == 1
            ? minTemperatureK
            : minTemperatureK + span * static_cast<double>(step) / (steps - 1);

        PhononThermoPoint point;
        point.temperatureK = t;

        if (t <= 0.0) {
            // T = 0 exactly: every Bose factor is 0 and ln(2 sinh x) → x, so
            // both U and F collapse to the zero-point energy and S vanishes.
            // Evaluating the general expressions here would divide by zero.
            point.internalEnergyEv = zpe;
            point.freeEnergyEv = zpe;
            point.entropyEvPerK = 0.0;
            point.heatCapacityEvPerK = 0.0;
            result.points.push_back(point);
            continue;
        }

        const double kT = kBoltzmannEvPerK * t;
        double internal = 0.0;
        double freeEnergy = 0.0;
        double heatCapacity = 0.0;
        for (const Mode& mode : modes) {
            const double x = mode.energyEv / kT; // ħω / k_BT
            // A high-frequency mode at low T has e^x overflowing while its
            // contribution is negligible; the asymptotic forms below are exact
            // to double precision past x ~ 700 and avoid inf/NaN.
            if (x > 700.0) {
                internal += 0.5 * mode.energyEv * mode.weight;
                freeEnergy += 0.5 * mode.energyEv * mode.weight;
                continue;
            }
            const double expx = std::exp(x);
            const double bose = 1.0 / (expx - 1.0);
            internal += mode.energyEv * (0.5 + bose) * mode.weight;
            // ln(2 sinh(x/2)) written as x/2 + ln(1 − e^{−x}): algebraically
            // identical but numerically stable, since sinh(x/2) overflows for
            // large x while this form stays bounded.
            freeEnergy +=
                kT * (0.5 * x + std::log1p(-std::exp(-x))) * mode.weight;
            // C_v = k_B x² e^x / (e^x − 1)²
            const double denominator = (expx - 1.0) * (expx - 1.0);
            if (denominator > 0.0)
                heatCapacity +=
                    kBoltzmannEvPerK * x * x * expx / denominator * mode.weight;
        }

        point.internalEnergyEv = internal;
        point.freeEnergyEv = freeEnergy;
        point.entropyEvPerK = (internal - freeEnergy) / t;
        point.heatCapacityEvPerK = heatCapacity;
        result.points.push_back(point);
    }
    return result;
}

} // namespace calango::core
