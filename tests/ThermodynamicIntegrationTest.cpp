// Thermodynamic integration: every assertion below is against a CLOSED FORM or
// an independently computed quantity, never against a previous run of this
// code. That rule is what makes the test able to fail.
//
// What is pinned, and against what:
//
//   thermal wavelength      recomputed from SI constants (h, k_B, m_u), a path
//                           that shares nothing with the eV·Å² constant used
//   ideal gas               Sackur-Tetrode, with the exact ln N! residual
//                           ½ln(2πN)/N asserted rather than tolerated — that
//                           residual IS the indistinguishability term
//   Einstein crystal        3N k_BT ln(βħω) recomputed in SI
//   fixed-CM correction     the constrained Gaussian integral, checked against
//                           the N = 2 and N = 3 cases done by hand
//   Gauss-Legendre          exact for every polynomial of degree ≤ 2n − 1, and
//                           demonstrably NOT exact at 2n
//   trapezoid / Simpson     the O(h²) and O(h⁴) error orders, by refinement
//   the TI itself           a harmonic → harmonic transformation, whose ΔF is
//                           (3N/2)k_BT ln(α₁/α₀) analytically
//   LJ second virial        the HCB series against direct numerical quadrature
//                           of −2π∫(e^{−βu} − 1)r²dr, plus the published Boyle
//                           temperature T* = 3.418
//   autocorrelation         an AR(1) process, whose τ_int = (1+φ)/(1−φ)
//   partial failure         a dead window must produce NO free energy

#include "core/ThermodynamicIntegration.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace calango::core;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  [%s] %s\n", condition ? "ok" : "FAIL", what);
    if (!condition)
        ++failures;
}

void checkClose(double actual, double expected, double tolerance,
                const char* what)
{
    const bool ok = std::abs(actual - expected) <= tolerance;
    std::printf("  [%s] %s (got %.12g, expected %.12g, tol %.3g)\n",
                ok ? "ok" : "FAIL", what, actual, expected, tolerance);
    if (!ok)
        ++failures;
}

void checkRelative(double actual, double expected, double relativeTolerance,
                   const char* what)
{
    const double scale = std::max(std::abs(expected), 1.0e-300);
    checkClose(actual, expected, relativeTolerance * scale, what);
}

// ---------------------------------------------------------------------------
// Independent (SI) recomputation of the constants the module works in
// ---------------------------------------------------------------------------

constexpr double kPlanckJs = 6.62607015e-34;      // exact, SI 2019
constexpr double kHbarJs = 1.054571817e-34;
constexpr double kBoltzmannJPerK = 1.380649e-23;  // exact, SI 2019
constexpr double kAmuKg = 1.66053906660e-27;
constexpr double kElectronVoltJ = 1.602176634e-19; // exact, SI 2019

/// Λ = h/√(2π m k_B T), computed entirely in SI and converted to Å at the end.
double thermalWavelengthFromSi(double massAmu, double temperatureK)
{
    const double m = massAmu * kAmuKg;
    const double lambdaMetres =
        kPlanckJs / std::sqrt(2.0 * M_PI * m * kBoltzmannJPerK * temperatureK);
    return lambdaMetres * 1.0e10;
}

/// βħω for an oscillator of spring constant α (eV/Å²) and mass m (amu),
/// computed in SI.
double reducedOscillatorFrequencyFromSi(double alphaEvPerA2, double massAmu,
                                        double temperatureK)
{
    // 1 eV/Å² = e / 1e-20 m² = 16.02176634 J/m².
    const double kSi = alphaEvPerA2 * kElectronVoltJ / 1.0e-20;
    const double m = massAmu * kAmuKg;
    const double omega = std::sqrt(kSi / m);
    return kHbarJs * omega / (kBoltzmannJPerK * temperatureK);
}

// ---------------------------------------------------------------------------
// Direct numerical B₂ for the Lennard-Jones 12-6 potential
// ---------------------------------------------------------------------------

/// B₂/σ³ = −2π ∫₀^∞ (e^{−u/kT} − 1) x² dx with x = r/σ, by composite Simpson
/// on a fine grid. Independent of the Gamma-function series entirely: it never
/// expands anything, it just integrates the Mayer function.
double secondVirialByQuadrature(double reducedTemperature)
{
    const auto mayer = [reducedTemperature](double x) {
        if (x <= 0.0)
            return -1.0; // e^{−∞} − 1
        const double x6 = std::pow(x, -6.0);
        const double u = 4.0 * (x6 * x6 - x6) / reducedTemperature;
        // Guard the hard core: exp(−u) underflows to 0 well before x = 0.7,
        // and the integrand there is exactly −1.
        if (u > 700.0)
            return -1.0;
        return std::exp(-u) - 1.0;
    };
    // The integrand is −x² below the core and decays like x²·(4/T*)x^{−6} above
    // it, so 60σ is far past where it matters at any T* used here.
    const double upper = 60.0;
    const int intervals = 4000000;
    const double h = upper / intervals;
    double total = mayer(0.0) * 0.0; // x²·f at x = 0 is 0
    for (int i = 1; i < intervals; ++i) {
        const double x = i * h;
        const double weight = (i % 2 == 1) ? 4.0 : 2.0;
        total += weight * x * x * mayer(x);
    }
    total += upper * upper * mayer(upper);
    total *= h / 3.0;
    return -2.0 * M_PI * total;
}

/// One λ window built from an EXACT integrand value — the test supplies the
/// physics, the module supplies the quadrature.
TiWindowSample makeWindow(int index, double lambda, double value, double error,
                          double variance = 0.0)
{
    TiWindowSample window;
    window.index = index;
    window.lambda = lambda;
    window.dudlEv = value;
    window.dudlErrorEv = error;
    window.dudlVarianceEv2 = variance > 0.0 ? variance : error * error;
    window.samples = 1000;
    window.ok = true;
    return window;
}

} // namespace

int main()
{
    std::printf("Thermal de Broglie wavelength (against SI):\n");
    {
        for (const double mass : {1.008, 12.011, 39.948, 195.084}) {
            for (const double temperature : {77.0, 300.0, 1500.0}) {
                const double fromModule =
                    thermalDeBroglieWavelengthA(mass, temperature);
                const double fromSi = thermalWavelengthFromSi(mass, temperature);
                checkRelative(fromModule, fromSi, 1.0e-6,
                              "Lambda matches the SI computation");
            }
        }
        // Hydrogen at room temperature is ~1 Å — the number every textbook
        // quotes, and a sanity net under the constant above.
        checkClose(thermalDeBroglieWavelengthA(1.008, 300.0), 1.0, 0.05,
                   "H at 300 K is about 1 Angstrom");
        check(thermalDeBroglieWavelengthA(0.0, 300.0) == 0.0,
              "a massless particle has no classical wavelength");
        check(thermalDeBroglieWavelengthA(1.0, 0.0) == 0.0,
              "and neither does anything at T = 0");
    }

    std::printf("Ideal gas — Sackur-Tetrode and the N! term:\n");
    {
        const double kB = ti_constants::kBoltzmannEvPerK;

        // N = 1: 0! = 1, so F = −kT ln(V/Λ³) with no approximation anywhere.
        {
            TiSystem system;
            system.atomCount = 1;
            system.volumeA3 = 1000.0;
            system.temperatureK = 300.0;
            system.uniformMassAmu = 39.948;
            const auto result = idealGasFreeEnergy(system);
            const double lambda = thermalDeBroglieWavelengthA(39.948, 300.0);
            const double expected = -kB * 300.0
                * std::log(system.volumeA3 / (lambda * lambda * lambda));
            check(result.valid, "a one-particle ideal gas evaluates");
            checkRelative(result.freeEnergyEv, expected, 1.0e-12,
                          "F = -kT ln(V/Lambda^3) exactly for N = 1");
        }

        // Large N: F/(NkT) − [ln(ρΛ³) − 1] must be EXACTLY Stirling's residual
        // [½ln(2πN) + 1/(12N)]/N. Asserting the residual to two orders, rather
        // than allowing a loose tolerance, is what makes this a test of the
        // ln N! term instead of a test that the answer is roughly right — at
        // N = 100 the second order is 8e-6, far above the tolerance below.
        for (const int count : {100, 1000, 10000}) {
            TiSystem system;
            system.atomCount = count;
            system.volumeA3 = 30.0 * count; // fixed density
            system.temperatureK = 300.0;
            system.uniformMassAmu = 39.948;
            const auto result = idealGasFreeEnergy(system);
            const double lambda = thermalDeBroglieWavelengthA(39.948, 300.0);
            const double density = count / system.volumeA3;
            const double sackurTetrode =
                std::log(density * lambda * lambda * lambda) - 1.0;
            const double perParticle =
                result.freeEnergyEv / (count * kB * 300.0);
            const double residual =
                (0.5 * std::log(2.0 * M_PI * count) + 1.0 / (12.0 * count))
                / count;
            checkClose(perParticle - sackurTetrode, residual, 1.0e-9,
                       "F/NkT minus Sackur-Tetrode is Stirling's residual");
        }

        // The negative control: this is how big the mistake is if the N! is
        // dropped. It has to be huge, or nobody would ever have made it.
        {
            const int count = 1000;
            TiSystem system;
            system.atomCount = count;
            system.volumeA3 = 30.0 * count;
            system.temperatureK = 300.0;
            system.uniformMassAmu = 39.948;
            const auto result = idealGasFreeEnergy(system);
            const double lambda = thermalDeBroglieWavelengthA(39.948, 300.0);
            const double withoutFactorial = -kB * 300.0 * count
                * std::log(system.volumeA3 / (lambda * lambda * lambda));
            // F carries +kT ln N! relative to the factorial-free expression, so
            // dropping the term makes the free energy LOWER by exactly that.
            const double gap = result.freeEnergyEv - withoutFactorial;
            const double expectedGap = kB * 300.0 * std::lgamma(count + 1.0);
            checkRelative(gap, expectedGap, 1.0e-12,
                          "dropping N! shifts F by exactly kT ln N!");
            check(std::abs(gap) > 0.5 * std::abs(result.freeEnergyEv),
                  "which is the same order as F itself, not a correction");
        }

        // A mixture is the sum of its species at the same volume, exactly.
        {
            TiSystem mixture;
            mixture.atomCount = 5;
            mixture.volumeA3 = 500.0;
            mixture.temperatureK = 400.0;
            mixture.massesAmu = {4.0026, 4.0026, 20.1797, 20.1797, 20.1797};
            const auto both = idealGasFreeEnergy(mixture);

            TiSystem helium = mixture;
            helium.atomCount = 2;
            helium.massesAmu = {4.0026, 4.0026};
            TiSystem neon = mixture;
            neon.atomCount = 3;
            neon.massesAmu = {20.1797, 20.1797, 20.1797};
            const double separate = idealGasFreeEnergy(helium).freeEnergyEv
                + idealGasFreeEnergy(neon).freeEnergyEv;
            checkRelative(both.freeEnergyEv, separate, 1.0e-12,
                          "a mixture is the sum of its species (one N! each)");

            // And it is NOT what a single averaged mass would give — the trap
            // the per-species grouping exists to avoid.
            TiSystem averaged = mixture;
            averaged.massesAmu.clear();
            averaged.uniformMassAmu = (2 * 4.0026 + 3 * 20.1797) / 5.0;
            check(std::abs(idealGasFreeEnergy(averaged).freeEnergyEv
                           - both.freeEnergyEv)
                      > 1.0e-3,
                  "and differs measurably from an average-mass ideal gas");
        }
    }

    std::printf("Einstein crystal (against the SI oscillator):\n");
    {
        const double kB = ti_constants::kBoltzmannEvPerK;
        for (const double alpha : {0.05, 0.5, 2.0}) {
            for (const double mass : {12.011, 63.546}) {
                const double temperature = 800.0;
                TiSystem system;
                system.atomCount = 216;
                system.volumeA3 = 216 * 20.0;
                system.temperatureK = temperature;
                system.uniformMassAmu = mass;
                const auto result =
                    einsteinCrystalFreeEnergy(system, alpha, false);
                const double betaHbarOmega =
                    reducedOscillatorFrequencyFromSi(alpha, mass, temperature);
                const double expected = 3.0 * system.atomCount * kB * temperature
                    * std::log(betaHbarOmega);
                check(result.valid, "the Einstein crystal evaluates");
                checkRelative(result.freeEnergyEv, expected, 1.0e-6,
                              "F = 3NkT ln(hbar*omega/kT)");
            }
        }

        // The fixed-CM correction, rebuilt in the test from its two pieces.
        //
        //   piece 1: the constrained Gaussian integral loses 3 dof:
        //            (2π/βα)^{3(N−1)/2} N^{−3/2} against (2π/βα)^{3N/2}
        //   piece 2: restoring the free translation, −kT ln(V N³)
        //
        // Piece 1's 1D form is checked against two integrals done by hand:
        //   N = 2: ∫δ(x₁+x₂)e^{−c(x₁²+x₂²)/2} = √(π/c)
        //   N = 3: ∫δ(Σx)e^{−c Σx²/2}         = 2π/(c√3)
        {
            const double c = 3.7; // βα, arbitrary
            const double formula2 = std::pow(2.0 * M_PI / c, (2 - 1) / 2.0)
                / std::sqrt(2.0);
            checkRelative(formula2, std::sqrt(M_PI / c), 1.0e-12,
                          "constrained Gaussian, N = 2, matches sqrt(pi/c)");
            const double formula3 = std::pow(2.0 * M_PI / c, (3 - 1) / 2.0)
                / std::sqrt(3.0);
            checkRelative(formula3, 2.0 * M_PI / (c * std::sqrt(3.0)), 1.0e-12,
                          "constrained Gaussian, N = 3, matches 2pi/(c sqrt3)");

            TiSystem system;
            system.atomCount = 108;
            system.volumeA3 = 108 * 16.0;
            system.temperatureK = 500.0;
            system.uniformMassAmu = 26.9815;
            const double alpha = 1.3;
            const double kT = kB * system.temperatureK;
            const double n = system.atomCount;
            const double piece1 =
                1.5 * kT * (std::log(2.0 * M_PI * kT / alpha) + std::log(n));
            const double piece2 =
                -kT * std::log(system.volumeA3 * n * n * n);
            checkRelative(
                einsteinFixedCenterOfMassCorrection(system, alpha),
                piece1 + piece2, 1.0e-12,
                "the fixed-CM correction is the sum of its two derived pieces");

            const auto free = einsteinCrystalFreeEnergy(system, alpha, false);
            const auto fixed = einsteinCrystalFreeEnergy(system, alpha, true);
            checkRelative(fixed.freeEnergyEv - free.freeEnergyEv,
                          piece1 + piece2, 1.0e-12,
                          "and is exactly what the fixed-CM flag adds");
        }
    }

    std::printf("Gauss-Legendre quadrature:\n");
    {
        for (int points = 1; points <= 8; ++points) {
            const auto rule = gaussLegendreRule(points, 0.0, 1.0);
            double weightSum = 0.0;
            for (const double w : rule.weights)
                weightSum += w;
            checkRelative(weightSum, 1.0, 1.0e-13,
                          "weights sum to the interval length");
            bool ascending = true;
            for (std::size_t i = 1; i < rule.nodes.size(); ++i)
                ascending = ascending && rule.nodes[i] > rule.nodes[i - 1];
            check(ascending, "nodes come back ascending");

            // Exact for every polynomial up to degree 2n − 1, to machine
            // precision. Nothing weaker is Gauss-Legendre.
            bool exact = true;
            for (int degree = 0; degree <= 2 * points - 1; ++degree) {
                double integral = 0.0;
                for (std::size_t i = 0; i < rule.nodes.size(); ++i)
                    integral +=
                        rule.weights[i] * std::pow(rule.nodes[i], degree);
                exact = exact
                    && std::abs(integral - 1.0 / (degree + 1)) < 1.0e-13;
            }
            check(exact, "integrates every polynomial of degree <= 2n-1 exactly");

            // And is NOT exact one degree higher — otherwise the check above
            // would pass for any rule that happened to be very accurate.
            double overshoot = 0.0;
            for (std::size_t i = 0; i < rule.nodes.size(); ++i)
                overshoot +=
                    rule.weights[i] * std::pow(rule.nodes[i], 2 * points);
            check(std::abs(overshoot - 1.0 / (2 * points + 1)) > 1.0e-12,
                  "and is measurably inexact at degree 2n");
        }
    }

    std::printf("Trapezoid and Simpson error orders:\n");
    {
        // A smooth, non-polynomial integrand with a known integral:
        // ∫₀¹ e^λ dλ = e − 1.
        const double exact = std::exp(1.0) - 1.0;
        const auto errorAt = [exact](TiQuadrature rule, int points) {
            const auto nodes = lambdaSchedule(TiLambdaSchedule::Uniform, points);
            std::vector<double> values;
            values.reserve(nodes.size());
            for (const double lambda : nodes)
                values.push_back(std::exp(lambda));
            const auto weights = quadratureWeights(rule, nodes);
            return std::abs(integrateWithWeights(weights.weights, values) - exact);
        };

        const double t1 = errorAt(TiQuadrature::Trapezoid, 9);
        const double t2 = errorAt(TiQuadrature::Trapezoid, 17);
        const double t3 = errorAt(TiQuadrature::Trapezoid, 33);
        checkClose(t1 / t2, 4.0, 0.05, "trapezoid error falls by 4x per halving");
        checkClose(t2 / t3, 4.0, 0.05, "and again");

        const double s1 = errorAt(TiQuadrature::Simpson, 9);
        const double s2 = errorAt(TiQuadrature::Simpson, 17);
        const double s3 = errorAt(TiQuadrature::Simpson, 33);
        checkClose(s1 / s2, 16.0, 0.6, "Simpson error falls by 16x per halving");
        checkClose(s2 / s3, 16.0, 0.6, "and again");
        check(s1 < t1, "and Simpson beats the trapezoid at equal cost");

        // Simpson must stay exact for cubics even at EVEN point counts, where
        // it falls back on the 3/8 rule for the last three intervals.
        for (const int points : {4, 5, 6, 7, 8, 9}) {
            const auto nodes = lambdaSchedule(TiLambdaSchedule::Uniform, points);
            std::vector<double> values;
            for (const double lambda : nodes)
                values.push_back(lambda * lambda * lambda);
            const auto weights =
                quadratureWeights(TiQuadrature::Simpson, nodes);
            checkClose(integrateWithWeights(weights.weights, values), 0.25,
                       1.0e-12, "Simpson is exact for a cubic at any n >= 4");
            // The most basic invariant of any quadrature, and the one an even
            // point count (which falls back on the 3/8 rule for the last three
            // intervals) is most likely to break: the weights must sum to the
            // length of the interval, or every integral is scaled.
            double weightSum = 0.0;
            for (const double w : weights.weights)
                weightSum += w;
            checkClose(weightSum, 1.0, 1.0e-12,
                       "and its weights sum to the interval length");
        }

        // A non-uniform grid is not silently Simpson-ed: the rule downgrades
        // and says so.
        {
            const auto nodes =
                lambdaSchedule(TiLambdaSchedule::PowerLaw, 9, 2.0);
            const auto weights =
                quadratureWeights(TiQuadrature::Simpson, nodes);
            check(weights.ruleUsed == TiQuadrature::Trapezoid
                      && !weights.note.empty(),
                  "Simpson refuses a non-uniform grid and reports the fallback");
        }
        // Gauss weights on non-Gauss nodes are refused for the same reason.
        {
            const auto nodes = lambdaSchedule(TiLambdaSchedule::Uniform, 6);
            const auto weights =
                quadratureWeights(TiQuadrature::GaussLegendre, nodes);
            check(weights.ruleUsed == TiQuadrature::Trapezoid
                      && !weights.note.empty(),
                  "Gauss-Legendre refuses non-Gauss nodes");
        }
    }

    std::printf("Lambda schedules:\n");
    {
        for (const auto schedule :
             {TiLambdaSchedule::Uniform, TiLambdaSchedule::GaussLegendre,
              TiLambdaSchedule::PowerLaw, TiLambdaSchedule::ClusteredEnds}) {
            const auto lambdas = lambdaSchedule(schedule, 12, 2.0);
            bool ok = lambdas.size() == 12;
            for (std::size_t i = 1; i < lambdas.size(); ++i)
                ok = ok && lambdas[i] > lambdas[i - 1];
            for (const double lambda : lambdas)
                ok = ok && lambda >= 0.0 && lambda <= 1.0;
            check(ok, "schedule is ascending and inside [0, 1]");
        }
        const auto gauss = lambdaSchedule(TiLambdaSchedule::GaussLegendre, 12);
        check(gauss.front() > 0.0 && gauss.back() < 1.0,
              "Gauss-Legendre never samples the singular endpoints");
        const auto power = lambdaSchedule(TiLambdaSchedule::PowerLaw, 12, 3.0);
        const auto uniform = lambdaSchedule(TiLambdaSchedule::Uniform, 12);
        check(power[1] < uniform[1],
              "the power law clusters windows towards lambda = 0");
        const auto clustered =
            lambdaSchedule(TiLambdaSchedule::ClusteredEnds, 13, 3.0);
        const auto uniform13 = lambdaSchedule(TiLambdaSchedule::Uniform, 13);
        check(clustered[1] < uniform13[1] && clustered[11] > uniform13[11],
              "and ClusteredEnds clusters at both ends");
        checkClose(clustered[6], 0.5, 1.0e-12,
                   "staying symmetric about the midpoint");
    }

    std::printf("Thermodynamic integration of a harmonic -> harmonic path:\n");
    {
        // The one path in classical statistical mechanics whose ΔF is exact.
        //
        //   U(λ) = ½[(1−λ)α₀ + λα₁] Σ r²
        //   ⟨∂U/∂λ⟩_λ = (3N/2) k_BT (α₁ − α₀) / [(1−λ)α₀ + λα₁]   (equipartition)
        //   ΔF = (3N/2) k_BT ln(α₁/α₀)
        //
        // So the module is handed the exact integrand and asked to reproduce
        // the exact integral.
        const double kB = ti_constants::kBoltzmannEvPerK;
        const int atoms = 64;
        const double temperature = 300.0;
        const double alpha0 = 0.4;
        const double alpha1 = 1.6;
        const double kT = kB * temperature;
        const double exact = 1.5 * atoms * kT * std::log(alpha1 / alpha0);

        const auto integrand = [&](double lambda) {
            const double alpha = (1.0 - lambda) * alpha0 + lambda * alpha1;
            return 1.5 * atoms * kT * (alpha1 - alpha0) / alpha;
        };

        const auto run = [&](TiLambdaSchedule schedule, TiQuadrature rule,
                             int windows) {
            const auto lambdas = lambdaSchedule(schedule, windows);
            std::vector<TiWindowSample> samples;
            for (std::size_t i = 0; i < lambdas.size(); ++i)
                samples.push_back(makeWindow(static_cast<int>(i), lambdas[i],
                                             integrand(lambdas[i]), 0.0));
            return integrateThermodynamicPath(samples, rule, windows);
        };

        const auto gauss = run(TiLambdaSchedule::GaussLegendre,
                               TiQuadrature::GaussLegendre, 12);
        check(gauss.complete, "the complete path integrates");
        checkRelative(gauss.deltaFEv, exact, 1.0e-10,
                      "12-point Gauss-Legendre reproduces the analytic dF");

        const auto trapezoid =
            run(TiLambdaSchedule::Uniform, TiQuadrature::Trapezoid, 12);
        checkRelative(trapezoid.deltaFEv, exact, 5.0e-3,
                      "the 12-point trapezoid gets close");
        check(std::abs(gauss.deltaFEv - exact)
                  < std::abs(trapezoid.deltaFEv - exact),
              "and Gauss-Legendre is far more accurate at the same cost");

        const auto simpson =
            run(TiLambdaSchedule::Uniform, TiQuadrature::Simpson, 13);
        check(std::abs(simpson.deltaFEv - exact)
                  < std::abs(trapezoid.deltaFEv - exact),
              "Simpson beats the trapezoid on the same uniform grid");

        // The quadrature-error estimate must actually track the real error:
        // bigger than it for the coarse rule, and not absurdly larger.
        check(trapezoid.quadratureErrorEv > 0.0
                  && trapezoid.quadratureErrorEv
                      >= 0.2 * std::abs(trapezoid.deltaFEv - exact),
              "the reported grid error is of the size of the real grid error");
    }

    std::printf("Error propagation and partial failure:\n");
    {
        const auto lambdas = lambdaSchedule(TiLambdaSchedule::Uniform, 9);
        const double sigma = 0.02;
        std::vector<TiWindowSample> samples;
        for (std::size_t i = 0; i < lambdas.size(); ++i)
            samples.push_back(
                makeWindow(static_cast<int>(i), lambdas[i], lambdas[i], sigma));

        const auto weights =
            quadratureWeights(TiQuadrature::Trapezoid, lambdas);
        double expectedVariance = 0.0;
        for (const double w : weights.weights)
            expectedVariance += w * w * sigma * sigma;
        const auto result =
            integrateThermodynamicPath(samples, TiQuadrature::Trapezoid, 9);
        checkRelative(result.statisticalErrorEv, std::sqrt(expectedVariance),
                      1.0e-12,
                      "sigma_I = sqrt(sum w_i^2 sigma_i^2), exactly");

        // One dead window. This must NOT produce a free energy.
        auto broken = samples;
        broken[4].ok = false;
        broken[4].failure = "the MD diverged";
        const auto partial =
            integrateThermodynamicPath(broken, TiQuadrature::Trapezoid, 9);
        check(!partial.complete, "a dead window makes the path incomplete");
        check(partial.deltaFEv == 0.0,
              "and no free energy is reported at all");
        check(partial.missingWindows.size() == 1
                  && partial.missingWindows.front() == 4,
              "the missing window is named");
        check(!partial.warnings.empty(),
              "with the failure carried through as a warning");

        // A window that was never even submitted is just as fatal.
        auto truncated = samples;
        truncated.pop_back();
        const auto short_ =
            integrateThermodynamicPath(truncated, TiQuadrature::Trapezoid, 9);
        check(!short_.complete && short_.missingWindows.size() == 1
                  && short_.missingWindows.front() == 8,
              "a window that never reported is missing, not absent");
        // And the same list WITHOUT the expectation of a ninth window is fine —
        // the expected count is the only thing that knows.
        const auto reinterpreted =
            integrateThermodynamicPath(truncated, TiQuadrature::Trapezoid, 8);
        check(reinterpreted.complete,
              "eight windows are complete when eight were expected");
    }

    std::printf("Endpoint singularity detection:\n");
    {
        const auto lambdas = lambdaSchedule(TiLambdaSchedule::Uniform, 11);
        std::vector<TiWindowSample> smooth;
        for (std::size_t i = 0; i < lambdas.size(); ++i)
            smooth.push_back(makeWindow(static_cast<int>(i), lambdas[i],
                                        1.0 + lambdas[i], 0.01, 0.01));
        check(!endpointDiagnostics(smooth).suspected,
              "a smooth path raises no endpoint flag");

        // The signature of the singularity: the λ → 0 window's integrand and
        // its variance both blow up while the rest of the path is quiet.
        auto singular = smooth;
        singular.front().dudlEv = 400.0;
        singular.front().dudlVarianceEv2 = 900.0;
        const auto diagnostics = endpointDiagnostics(singular);
        check(diagnostics.suspected, "a divergent end window is flagged");
        check(diagnostics.varianceRatio > 8.0
                  && diagnostics.magnitudeRatio > 8.0,
              "with both ratios reported");
        check(diagnostics.message.find("ENDPOINT SINGULARITY")
                  != std::string::npos,
              "and named in the message");

        const auto integrated =
            integrateThermodynamicPath(singular, TiQuadrature::Trapezoid, 11);
        check(integrated.complete && integrated.endpoint.suspected,
              "the integration still runs but carries the flag");
        bool warned = false;
        for (const auto& warning : integrated.warnings)
            warned = warned
                || warning.find("ENDPOINT SINGULARITY") != std::string::npos;
        check(warned, "and the warning reaches the result");
    }

    std::printf("Autocorrelation and block averaging (AR(1)):\n");
    {
        // x_t = phi x_{t-1} + eps  has tau_int = (1 + phi)/(1 - phi) exactly.
        // A fixed seed makes the sequence reproducible; the assertion is
        // against that closed form, not against the sequence.
        for (const double phi : {0.0, 0.5, 0.8}) {
            std::mt19937 rng(20260812u);
            std::normal_distribution<double> noise(0.0, 1.0);
            std::vector<double> series;
            series.reserve(400000);
            double x = 0.0;
            for (int i = 0; i < 400000; ++i) {
                x = phi * x + noise(rng);
                if (i >= 2000) // discard the burn-in of the process itself
                    series.push_back(x);
            }
            const double expected = (1.0 + phi) / (1.0 - phi);
            const double measured = integratedAutocorrelationTime(series);
            checkClose(measured, expected, 0.25 * expected + 0.15,
                       "tau_int recovers (1+phi)/(1-phi)");

            const auto stats = analyseSeries(series);
            check(stats.valid && stats.samples == (long long)series.size(),
                  "the series is analysed");
            // The whole point of the correction: the naive sigma/sqrt(N)
            // under-reports by sqrt(tau).
            const double naive =
                std::sqrt(stats.variance / static_cast<double>(series.size()));
            checkClose(stats.standardError / naive, std::sqrt(expected),
                       0.2 * std::sqrt(expected),
                       "the corrected error is sqrt(tau) times the naive one");
            // The independent block estimate has to agree with it.
            if (stats.blocks >= 2)
                check(stats.blockStandardError > 0.5 * stats.standardError
                          && stats.blockStandardError < 2.0 * stats.standardError,
                      "block averaging agrees with the autocorrelation estimate");
        }
    }

    std::printf("Lennard-Jones second virial coefficient:\n");
    {
        // The Gamma-function series against direct quadrature of the Mayer
        // function. Two computations with nothing in common but the potential.
        for (const double tReduced : {0.8, 1.0, 2.0, 3.418, 10.0}) {
            bool converged = false;
            const double series =
                lennardJonesSecondVirialSigma3(tReduced, &converged);
            const double quadrature = secondVirialByQuadrature(tReduced);
            check(converged, "the HCB series converges");
            checkClose(series, quadrature,
                       1.0e-4 * std::max(1.0, std::abs(quadrature)),
                       "the HCB series matches direct quadrature of the Mayer "
                       "function");
        }
        // The Boyle temperature, where B₂ = 0, is T* = 3.418 for LJ 12-6 — a
        // published number this implementation must land on.
        {
            const double atBoyle = lennardJonesSecondVirialSigma3(3.418);
            checkClose(atBoyle, 0.0, 2.0e-3,
                       "B2 vanishes at the published Boyle temperature 3.418");
            check(lennardJonesSecondVirialSigma3(2.0) < 0.0
                      && lennardJonesSecondVirialSigma3(6.0) > 0.0,
                  "and changes sign across it");
        }
        // The reference refuses at liquid density rather than extrapolating.
        {
            TiSystem system;
            system.atomCount = 500;
            system.volumeA3 = 500 * 30.0;
            system.temperatureK = 120.0;
            system.uniformMassAmu = 39.948;
            TiReferenceParameters parameters;
            parameters.ljEpsilonEv = 0.0104;
            parameters.ljSigmaA = 3.4;
            const auto dense = lennardJonesFreeEnergy(system, parameters);
            check(!dense.valid,
                  "the LJ reference refuses at liquid density");
            bool explained = false;
            for (const auto& warning : dense.warnings)
                explained = explained
                    || warning.find("NO exact closed form") != std::string::npos;
            check(explained, "and says why, rather than extrapolating a fit");

            // Supplied excess: the reference becomes usable and is exactly
            // ideal + N × excess.
            parameters.ljExcessSupplied = true;
            parameters.ljExcessFreeEnergyEvPerAtom = -0.031;
            const auto supplied = lennardJonesFreeEnergy(system, parameters);
            check(supplied.valid, "a supplied excess makes it usable");
            checkRelative(supplied.freeEnergyEv
                              - idealGasFreeEnergy(system).freeEnergyEv,
                          -0.031 * 500, 1.0e-12,
                          "and is ideal + N x the supplied excess, exactly");

            // Dilute: the virial route works and is close to ideal.
            TiSystem dilute = system;
            dilute.volumeA3 = 500 * 2000.0; // rho* ~ 0.01
            TiReferenceParameters virialOnly;
            virialOnly.ljEpsilonEv = 0.0104;
            virialOnly.ljSigmaA = 3.4;
            const auto thin = lennardJonesFreeEnergy(dilute, virialOnly);
            check(thin.valid, "the virial route works at low density");
            check(!thin.warnings.empty(),
                  "and states its own truncation error");
        }
    }

    std::printf("Assembly of the absolute free energy:\n");
    {
        TiSystem system;
        system.atomCount = 100;
        system.volumeA3 = 3000.0;
        system.temperatureK = 300.0;
        system.pressureGPa = 0.5;
        system.uniformMassAmu = 39.948;

        const auto lambdas = lambdaSchedule(TiLambdaSchedule::GaussLegendre, 8);
        std::vector<TiWindowSample> samples;
        for (std::size_t i = 0; i < lambdas.size(); ++i)
            samples.push_back(makeWindow(static_cast<int>(i), lambdas[i],
                                         -1.25, 0.01));
        // A constant integrand integrates to itself over a unit interval.
        const auto assembly = assembleThermodynamicIntegration(
            system, TiReference::IdealGas, {}, samples,
            TiQuadrature::GaussLegendre, 8);
        check(assembly.valid, "the assembly succeeds");
        checkRelative(assembly.integration.deltaFEv, -1.25, 1.0e-12,
                      "a constant integrand integrates to itself over [0,1]");
        checkRelative(assembly.helmholtzEv,
                      idealGasFreeEnergy(system).freeEnergyEv - 1.25, 1.0e-12,
                      "F = F_ref + dF");
        // PV: 0.5 GPa x 3000 A^3 = 1500/160.2176634 eV.
        checkRelative(assembly.pvEv, 1500.0 / 160.2176634, 1.0e-12,
                      "PV converts GPa.A^3 to eV");
        checkRelative(assembly.gibbsEv, assembly.helmholtzEv + assembly.pvEv,
                      1.0e-12, "G = F + PV");
        checkRelative(assembly.gibbsEvPerAtom, assembly.gibbsEv / 100.0,
                      1.0e-12, "per-atom is per-cell divided by N");
        check(std::abs(assembly.gibbsEv - assembly.gibbsEvPerAtom) > 1.0,
              "and the two are different numbers, as the field names say");

        // Zero pressure: G is F, not a placeholder.
        TiSystem nvt = system;
        nvt.pressureGPa = 0.0;
        const auto isochoric = assembleThermodynamicIntegration(
            nvt, TiReference::IdealGas, {}, samples, TiQuadrature::GaussLegendre,
            8);
        check(isochoric.pvEv == 0.0
                  && isochoric.gibbsEv == isochoric.helmholtzEv,
              "at P = 0, G = F exactly");

        // An incomplete path must not assemble anything.
        auto broken = samples;
        broken[2].ok = false;
        const auto refused = assembleThermodynamicIntegration(
            system, TiReference::IdealGas, {}, broken,
            TiQuadrature::GaussLegendre, 8);
        check(!refused.valid && refused.helmholtzEv == 0.0
                  && refused.gibbsEv == 0.0,
              "an incomplete path assembles to nothing, not to an estimate");
    }

    std::printf("Hysteresis:\n");
    {
        const auto lambdas = lambdaSchedule(TiLambdaSchedule::Uniform, 9);
        const auto build = [&](double offset, double sigma) {
            std::vector<TiWindowSample> samples;
            for (std::size_t i = 0; i < lambdas.size(); ++i)
                samples.push_back(makeWindow(static_cast<int>(i), lambdas[i],
                                             1.0 + offset, sigma));
            return integrateThermodynamicPath(samples, TiQuadrature::Trapezoid, 9);
        };
        const auto reversible =
            compareHysteresis(build(0.0, 0.05), build(0.005, 0.05));
        check(reversible.valid && !reversible.significant,
              "a reversible path shows no significant hysteresis");
        const auto irreversible =
            compareHysteresis(build(0.0, 0.001), build(0.5, 0.001));
        check(irreversible.significant,
              "a path that does not close is flagged");
        checkRelative(irreversible.differenceEv, -0.5, 1.0e-9,
                      "and the gap is reported as forward minus backward");

        // An incomplete sweep cannot be compared at all.
        TiIntegrationResult empty;
        check(!compareHysteresis(build(0.0, 0.05), empty).valid,
              "an incomplete sweep produces no hysteresis verdict");
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASSED" : "FAILED",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
