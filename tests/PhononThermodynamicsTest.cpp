// Harmonic phonon thermodynamics test.
//
// The integrals have exact analytic limits, so this checks against physics
// rather than against a stored snapshot: a single Einstein mode has closed-form
// U, F, S and C_v at every temperature, and the T -> 0 and high-T limits are
// laws (Third Law, Dulong-Petit) that any correct implementation must satisfy.
//
// GUI-free, Python-free.

#include "core/PhononThermodynamics.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace calango::core;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

void checkClose(double actual, double expected, double tolerance,
                const std::string& what)
{
    const bool ok = std::abs(actual - expected) <= tolerance;
    std::printf("  %s %s  (got %.6g, expected %.6g)\n", ok ? "ok  " : "FAIL",
                what.c_str(), actual, expected);
    if (!ok)
        ++failures;
}

constexpr double kCmToEv = 1.239841984e-4;
constexpr double kBoltzmannEvPerK = 8.617333262e-5;

/// A narrow triangular peak at `centreCm` carrying unit total weight — a
/// numerical stand-in for one Einstein oscillator, g(ω) = δ(ω − ω₀).
void einsteinDos(double centreCm, std::vector<double>& frequencies,
                 std::vector<double>& dos)
{
    // Fine grid so the trapezoidal integration of the peak is accurate; the
    // peak itself spans several samples so it is resolved rather than aliased.
    constexpr int kPoints = 4001;
    constexpr double kMaxCm = 2000.0;
    const double step = kMaxCm / (kPoints - 1);
    const double halfWidth = 4.0 * step;
    frequencies.resize(kPoints);
    dos.assign(kPoints, 0.0);
    for (int i = 0; i < kPoints; ++i) {
        const double omega = i * step;
        frequencies[i] = omega;
        const double distance = std::abs(omega - centreCm);
        if (distance < halfWidth)
            dos[i] = (1.0 - distance / halfWidth) / halfWidth; // area = 1
    }
}

/// Closed-form Einstein-oscillator properties for one mode of energy `ev`.
struct Einstein {
    double u, f, s, cv;
};
Einstein einsteinExact(double ev, double t)
{
    if (t <= 0.0)
        return {0.5 * ev, 0.5 * ev, 0.0, 0.0};
    const double kT = kBoltzmannEvPerK * t;
    const double x = ev / kT;
    const double expx = std::exp(x);
    const double u = ev * (0.5 + 1.0 / (expx - 1.0));
    const double f = kT * (0.5 * x + std::log1p(-std::exp(-x)));
    return {u, f, (u - f) / t,
            kBoltzmannEvPerK * x * x * expx / ((expx - 1.0) * (expx - 1.0))};
}

} // namespace

int main()
{
    std::vector<double> frequencies;
    std::vector<double> dos;
    const double centreCm = 500.0;
    const double modeEv = centreCm * kCmToEv;
    einsteinDos(centreCm, frequencies, dos);

    std::printf("Single Einstein mode at %.0f cm^-1 (%.4f eV):\n", centreCm,
                modeEv);
    const auto result =
        computePhononThermodynamics(frequencies, dos, 0.0, 1000.0, 101);
    check(result.points.size() == 101, "the requested temperature grid is returned");
    checkClose(result.totalModes, 1.0, 1e-3, "the DOS integrates to one mode");

    // -- T = 0: the Third Law -----------------------------------------------
    const auto& zero = result.points.front();
    checkClose(zero.temperatureK, 0.0, 1e-12, "grid starts at 0 K");
    checkClose(result.zeroPointEnergyEv, 0.5 * modeEv, 1e-6,
               "zero-point energy is hw/2");
    checkClose(zero.internalEnergyEv, 0.5 * modeEv, 1e-6, "U(0) = E_ZPE");
    checkClose(zero.freeEnergyEv, 0.5 * modeEv, 1e-6, "F(0) = E_ZPE");
    checkClose(zero.entropyEvPerK, 0.0, 1e-12, "S(0) = 0 (Third Law)");
    checkClose(zero.heatCapacityEvPerK, 0.0, 1e-12, "Cv(0) = 0");

    // -- Finite T against the closed form ------------------------------------
    for (const double t : {100.0, 300.0, 700.0, 1000.0}) {
        const auto single =
            computePhononThermodynamics(frequencies, dos, t, t, 1);
        const Einstein exact = einsteinExact(modeEv, t);
        const auto& p = single.points.front();
        // 1e-4 eV absorbs the finite width of the numerical delta peak.
        checkClose(p.internalEnergyEv, exact.u, 1e-4,
                   "U(" + std::to_string(int(t)) + " K)");
        checkClose(p.freeEnergyEv, exact.f, 1e-4,
                   "F(" + std::to_string(int(t)) + " K)");
        checkClose(p.entropyEvPerK, exact.s, 1e-7,
                   "S(" + std::to_string(int(t)) + " K)");
        checkClose(p.heatCapacityEvPerK, exact.cv, 1e-7,
                   "Cv(" + std::to_string(int(t)) + " K)");
    }

    // -- High-temperature limit: Dulong-Petit --------------------------------
    // Well above the mode temperature every oscillator contributes k_B to Cv.
    {
        const auto hot =
            computePhononThermodynamics(frequencies, dos, 20000.0, 20000.0, 1);
        checkClose(hot.points.front().heatCapacityEvPerK, kBoltzmannEvPerK,
                   1e-8, "Cv -> k_B per mode at high T (Dulong-Petit)");
    }

    // -- Monotonicity --------------------------------------------------------
    // U and S rise with temperature and F falls: these hold for any physical
    // phonon spectrum, and catching a sign error here is the point.
    {
        bool uRises = true, sRises = true, fFalls = true;
        for (std::size_t i = 1; i < result.points.size(); ++i) {
            uRises = uRises && result.points[i].internalEnergyEv
                >= result.points[i - 1].internalEnergyEv - 1e-12;
            sRises = sRises && result.points[i].entropyEvPerK
                >= result.points[i - 1].entropyEvPerK - 1e-12;
            fFalls = fFalls && result.points[i].freeEnergyEv
                <= result.points[i - 1].freeEnergyEv + 1e-12;
        }
        check(uRises, "U increases with temperature");
        check(sRises, "S increases with temperature");
        check(fFalls, "F decreases with temperature");
        check(result.points.back().freeEnergyEv
                  < result.points.back().internalEnergyEv,
              "F < U at finite temperature (F = U - TS with S > 0)");
    }

    // -- Imaginary modes ------------------------------------------------------
    // Negative frequencies must be excluded and REPORTED: the harmonic
    // expressions are undefined there, and a structure carrying them is not at
    // a minimum, so quietly integrating over the rest would hand back
    // authoritative-looking numbers for a meaningless quantity.
    std::printf("Imaginary modes:\n");
    {
        std::vector<double> f = {-100.0, -50.0, 0.0, 250.0, 500.0};
        std::vector<double> g = {1.0, 1.0, 0.0, 1.0, 1.0};
        const auto mixed = computePhononThermodynamics(f, g, 300.0, 300.0, 1);
        check(mixed.imaginaryWeight > 0.0, "the discarded weight is reported");
        check(mixed.zeroPointEnergyEv > 0.0,
              "the remaining positive modes still integrate");
        check(std::isfinite(mixed.points.front().freeEnergyEv),
              "F stays finite (no log of a negative argument)");
    }

    // -- Degenerate input -----------------------------------------------------
    {
        const auto empty = computePhononThermodynamics({}, {}, 0.0, 300.0, 10);
        check(empty.points.empty(), "an empty DOS yields no points");
        const auto single = computePhononThermodynamics({100.0}, {1.0}, 0.0,
                                                        300.0, 10);
        check(single.points.empty(), "a one-sample DOS cannot be integrated");
    }

    std::printf(failures == 0 ? "\nAll thermodynamics checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
