#include "core/ThermodynamicIntegration.hpp"

#include "core/LocaleSafeNumber.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>

namespace calango::core {

namespace {

using ti_constants::kBoltzmannEvPerK;
using ti_constants::kGpaInEvPerA3;
using ti_constants::kHbarSqOverTwoAmuEvA2;

/// Numbers written into a warning string go through the locale-safe formatter.
/// std::to_string and printf both follow LC_NUMERIC, which QApplication sets
/// from the environment — on this project's own development machine (pt_BR)
/// that turns "3.14" into "3,14" inside a message that is later parsed or
/// compared. Two real bugs here, one of them silent file corruption.
///
/// Rounded to six significant figures first, because localeSafeFormat produces
/// the shortest text that ROUND-TRIPS — right for a file, and wrong for a
/// sentence: "rho* = 3.3696844993141277" is a diagnostic nobody reads to the
/// end of. Nothing here is parsed back, so the precision buys nothing.
std::string num(double value)
{
    if (value != 0.0 && std::isfinite(value)) {
        const double magnitude =
            std::pow(10.0, std::floor(std::log10(std::abs(value))) - 5.0);
        value = std::round(value / magnitude) * magnitude;
    }
    return localeSafeFormat(value);
}

double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t half = values.size() / 2;
    if (values.size() % 2 == 1)
        return values[half];
    return 0.5 * (values[half - 1] + values[half]);
}

/// Trapezoid over `nodes` ⊂ [0, 1], with the integrand held CONSTANT outside
/// the outermost node.
///
/// The end extension is what makes this usable as a cross-check for
/// Gauss-Legendre, whose nodes are strictly interior: a plain trapezoid over
/// them integrates only [x₀, x_{n-1}] and would report the two missing end
/// panels as a quadrature "error" of the wrong order of magnitude. Holding the
/// end values constant across those panels is the crudest honest closure and is
/// exactly right when the nodes already include 0 and 1 (the panels have zero
/// width and this reduces to the ordinary trapezoid).
double trapezoidClosed(const std::vector<double>& nodes,
                       const std::vector<double>& values, double a, double b)
{
    if (nodes.empty() || nodes.size() != values.size())
        return 0.0;
    double total = values.front() * (nodes.front() - a)
        + values.back() * (b - nodes.back());
    for (std::size_t i = 1; i < nodes.size(); ++i)
        total += 0.5 * (values[i] + values[i - 1]) * (nodes[i] - nodes[i - 1]);
    return total;
}

/// True when the node spacing is uniform to within a relative tolerance.
bool uniformSpacing(const std::vector<double>& nodes)
{
    if (nodes.size() < 3)
        return true;
    const double h = nodes[1] - nodes[0];
    if (h <= 0.0)
        return false;
    for (std::size_t i = 2; i < nodes.size(); ++i)
        if (std::abs((nodes[i] - nodes[i - 1]) - h) > 1.0e-9 * std::abs(h))
            return false;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Reference systems
// ---------------------------------------------------------------------------

double thermalDeBroglieWavelengthA(double massAmu, double temperatureK)
{
    if (massAmu <= 0.0 || temperatureK <= 0.0)
        return 0.0;
    // Λ² = 2πħ²/(m k_B T) = 4π · [ħ²/(2m)] / (k_B T).
    const double hbarSqOverTwoM = kHbarSqOverTwoAmuEvA2 / massAmu; // eV·Å²
    const double lambdaSq =
        4.0 * M_PI * hbarSqOverTwoM / (kBoltzmannEvPerK * temperatureK);
    return std::sqrt(lambdaSq);
}

TiReferenceFreeEnergy idealGasFreeEnergy(const TiSystem& system)
{
    TiReferenceFreeEnergy result;
    if (system.atomCount <= 0 || system.volumeA3 <= 0.0
        || system.temperatureK <= 0.0) {
        result.warnings.push_back(
            "ideal gas: needs a positive atom count, volume and temperature");
        return result;
    }

    // Group by mass: each SPECIES carries its own thermal wavelength and its
    // own N!. Averaging the masses first is the mistake that makes a mixture's
    // free energy look reasonable and be wrong.
    std::map<double, int> speciesCount;
    for (int i = 0; i < system.atomCount; ++i) {
        const double mass = i < static_cast<int>(system.massesAmu.size())
            ? system.massesAmu[static_cast<std::size_t>(i)]
            : system.uniformMassAmu;
        if (mass <= 0.0) {
            result.warnings.push_back("ideal gas: atom with non-positive mass");
            return result;
        }
        // Bin to 1e-6 amu so numerically identical masses are one species.
        speciesCount[std::round(mass * 1.0e6) / 1.0e6] += 1;
    }

    const double kT = kBoltzmannEvPerK * system.temperatureK;
    double lnQ = 0.0;
    for (const auto& [mass, count] : speciesCount) {
        const double lambda = thermalDeBroglieWavelengthA(mass, system.temperatureK);
        if (lambda <= 0.0) {
            result.warnings.push_back("ideal gas: degenerate thermal wavelength");
            return result;
        }
        // ln Q_s = N_s ln V − 3 N_s ln Λ_s − ln(N_s!).
        //
        // ln(N!) via lgamma rather than Stirling: it is EXACT for every N,
        // including the N = 1 case where Stirling is off by 100 %, and it costs
        // the same. The Sackur-Tetrode form quoted in the header is the large-N
        // limit of this, differing by ½ln(2πN) — which is what the test
        // measures rather than assumes.
        lnQ += count * std::log(system.volumeA3)
            - 3.0 * count * std::log(lambda)
            - std::lgamma(static_cast<double>(count) + 1.0);
    }

    result.freeEnergyEv = -kT * lnQ;
    result.freeEnergyEvPerAtom = result.freeEnergyEv / system.atomCount;
    result.valid = true;
    result.description = "ideal gas (Sackur-Tetrode, exact ln N!)";

    // The classical treatment assumes ρ_s Λ_s³ ≪ 1 for every species. Above ~1
    // the gas is degenerate and this expression is not merely inaccurate, it is
    // the wrong statistics. Checked per species, since the lightest one reaches
    // degeneracy first and an average over the mixture would hide it.
    double degeneracy = 0.0;
    for (const auto& [mass, count] : speciesCount) {
        const double lambda =
            thermalDeBroglieWavelengthA(mass, system.temperatureK);
        degeneracy = std::max(
            degeneracy, count * lambda * lambda * lambda / system.volumeA3);
    }
    if (degeneracy > 0.1)
        result.warnings.push_back(
            "ideal gas: rho*Lambda^3 = " + num(degeneracy)
            + " — the classical (Maxwell-Boltzmann) limit needs this well "
              "below 1; quantum statistics are not included");
    return result;
}

double einsteinFixedCenterOfMassCorrection(const TiSystem& system,
                                           double springConstantEvPerA2)
{
    if (system.atomCount <= 0 || system.volumeA3 <= 0.0
        || system.temperatureK <= 0.0 || springConstantEvPerA2 <= 0.0)
        return 0.0;
    const double kT = kBoltzmannEvPerK * system.temperatureK;
    const double n = static_cast<double>(system.atomCount);
    // 2π/(βα) = 2π k_B T / α, the Gaussian width² of one Einstein oscillator
    // times 2π. See the header for the two-step derivation; the combination
    // below is the one that is dimensionless.
    const double gaussianVolume =
        std::pow(2.0 * M_PI * kT / springConstantEvPerA2, 1.5); // Å³
    return -kT
        * std::log(system.volumeA3 * std::pow(n, 1.5) / gaussianVolume);
}

TiReferenceFreeEnergy einsteinCrystalFreeEnergy(const TiSystem& system,
                                                double springConstantEvPerA2,
                                                bool fixedCenterOfMass)
{
    TiReferenceFreeEnergy result;
    if (system.atomCount <= 0 || system.temperatureK <= 0.0
        || springConstantEvPerA2 <= 0.0) {
        result.warnings.push_back(
            "Einstein crystal: needs a positive atom count, temperature and "
            "spring constant");
        return result;
    }
    const double kT = kBoltzmannEvPerK * system.temperatureK;

    // F = (3 k_B T / 2) Σ_i ln(β α Λ_i² / 2π), summed per ATOM so a mixture
    // gets one ω per mass. The identity with 3N k_BT ln(βħω) is pinned by a
    // test: the two forms come from different derivations and agreeing is
    // evidence that neither lost a 2π.
    double total = 0.0;
    double maxQuantumness = 0.0;
    for (int i = 0; i < system.atomCount; ++i) {
        const double mass = i < static_cast<int>(system.massesAmu.size())
            ? system.massesAmu[static_cast<std::size_t>(i)]
            : system.uniformMassAmu;
        const double lambda = thermalDeBroglieWavelengthA(mass, system.temperatureK);
        if (lambda <= 0.0) {
            result.warnings.push_back("Einstein crystal: atom with a "
                                      "non-positive mass");
            return result;
        }
        const double x = springConstantEvPerA2 * lambda * lambda
            / (kT * 2.0 * M_PI); // = (βħω)²
        total += 1.5 * kT * std::log(x);
        maxQuantumness = std::max(maxQuantumness, std::sqrt(x));
    }

    result.description = "classical Einstein crystal, alpha = "
        + num(springConstantEvPerA2) + " eV/A^2";
    if (fixedCenterOfMass) {
        total += einsteinFixedCenterOfMassCorrection(system, springConstantEvPerA2);
        result.description += ", fixed centre of mass";
    }
    result.freeEnergyEv = total;
    result.freeEnergyEvPerAtom = total / system.atomCount;
    result.valid = true;

    // βħω ≥ 1 means the oscillator's level spacing is at or above k_BT: the
    // CLASSICAL partition function used here is then the wrong one, and a
    // reference free energy that is wrong is not a reference at all.
    if (maxQuantumness > 0.3)
        result.warnings.push_back(
            "Einstein crystal: hbar*omega/kT = " + num(maxQuantumness)
            + " — the classical oscillator free energy needs this well below "
              "1; soften the spring or raise the temperature");
    return result;
}

double lennardJonesSecondVirialSigma3(double reducedTemperature,
                                      bool* converged)
{
    if (converged)
        *converged = false;
    if (reducedTemperature <= 0.0)
        return 0.0;

    // B₂*(T*) = −Σ_k [2^{(2k+1)/2} / (4 k!)] Γ((2k−1)/4) T*^{−(2k+1)/4}
    //           (Hirschfelder, Curtiss & Bird 1954, §3.6)
    //
    // k = 0 is the only term whose Γ argument is negative (Γ(−1/4) < 0), so it
    // is evaluated directly; every k ≥ 1 term is positive and is built in log
    // space, because Γ((2k−1)/4) overflows a double long before 1/k! has
    // brought the term back down.
    const double lnT = std::log(reducedTemperature);
    double sum = std::pow(2.0, 0.5) / 4.0 * std::tgamma(-0.25)
        * std::pow(reducedTemperature, -0.25);

    constexpr int kMaxTerms = 400;
    double lastTerm = std::abs(sum);
    int smallRun = 0;
    for (int k = 1; k < kMaxTerms; ++k) {
        const double lnTerm = 0.5 * (2.0 * k + 1.0) * std::log(2.0)
            - std::log(4.0) - std::lgamma(k + 1.0)
            + std::lgamma((2.0 * k - 1.0) / 4.0)
            - 0.25 * (2.0 * k + 1.0) * lnT;
        const double term = std::exp(lnTerm);
        sum += term;
        lastTerm = term;
        // Two consecutive negligible terms, and never before k = 5: at low T*
        // the series RISES for the first few terms, and a single-term stopping
        // rule would truncate it at the top of the rise.
        if (k >= 5 && term < 1.0e-14 * std::max(1.0, std::abs(sum))) {
            if (++smallRun >= 2) {
                if (converged)
                    *converged = true;
                break;
            }
        } else {
            smallRun = 0;
        }
    }
    if (converged && lastTerm > 1.0e-10 * std::max(1.0, std::abs(sum)))
        *converged = false;

    // B₂* is in units of b₀ = (2/3)πσ³; the caller wants B₂/σ³.
    return -sum * (2.0 * M_PI / 3.0);
}

TiReferenceFreeEnergy lennardJonesFreeEnergy(
    const TiSystem& system, const TiReferenceParameters& parameters)
{
    TiReferenceFreeEnergy result;
    const TiReferenceFreeEnergy ideal = idealGasFreeEnergy(system);
    if (!ideal.valid) {
        result.warnings = ideal.warnings;
        return result;
    }
    if (parameters.ljEpsilonEv <= 0.0 || parameters.ljSigmaA <= 0.0) {
        result.warnings.push_back(
            "Lennard-Jones reference: epsilon and sigma must be positive");
        return result;
    }

    const double kT = kBoltzmannEvPerK * system.temperatureK;
    const double sigma3 = std::pow(parameters.ljSigmaA, 3.0);
    const double rhoReduced = system.atomCount * sigma3 / system.volumeA3;
    const double tReduced = kT / parameters.ljEpsilonEv;

    // An externally supplied excess free energy always wins: it is either a
    // previous ideal-gas → LJ integration from this very module, or an equation
    // of state the user picked and is accountable for.
    if (parameters.ljExcessSupplied) {
        result.freeEnergyEv = ideal.freeEnergyEv
            + parameters.ljExcessFreeEnergyEvPerAtom * system.atomCount;
        result.freeEnergyEvPerAtom = result.freeEnergyEv / system.atomCount;
        result.valid = true;
        result.description = "Lennard-Jones fluid, ideal gas + supplied excess "
            + num(parameters.ljExcessFreeEnergyEvPerAtom) + " eV/atom";
        result.warnings = ideal.warnings;
        return result;
    }

    if (rhoReduced > kVirialDensityLimit) {
        result.warnings.push_back(
            "Lennard-Jones reference: rho* = " + num(rhoReduced)
            + " is above the second-virial limit of "
            + num(kVirialDensityLimit)
            + ". There is NO exact closed form for the LJ fluid at liquid "
              "densities. Supply an excess free energy (from an ideal-gas -> "
              "LJ integration, or from a published equation of state) or use "
              "the ideal gas as the reference and put the LJ fluid on the "
              "path instead.");
        return result;
    }

    bool converged = false;
    const double b2 = lennardJonesSecondVirialSigma3(tReduced, &converged) * sigma3;
    if (!converged) {
        result.warnings.push_back(
            "Lennard-Jones reference: the second-virial series did not "
            "converge at T* = " + num(tReduced));
        return result;
    }

    // βA_ex/N = B₂ρ + O(ρ²). Exact to first order in the density, and nothing
    // beyond it is claimed.
    const double density = system.atomCount / system.volumeA3;
    const double excessPerAtom = kT * b2 * density;
    result.freeEnergyEv = ideal.freeEnergyEv + excessPerAtom * system.atomCount;
    result.freeEnergyEvPerAtom = result.freeEnergyEv / system.atomCount;
    result.valid = true;
    result.description = "Lennard-Jones fluid, ideal gas + exact B2 at rho* = "
        + num(rhoReduced);
    result.warnings = ideal.warnings;
    result.warnings.push_back(
        "Lennard-Jones reference: excess free energy truncated at the second "
        "virial coefficient; the neglected O(rho^2) term is of order "
        + num(rhoReduced) + " relative to the term kept");
    return result;
}

TiReferenceFreeEnergy referenceFreeEnergy(
    TiReference kind, const TiSystem& system,
    const TiReferenceParameters& parameters)
{
    switch (kind) {
    case TiReference::EinsteinCrystal:
        return einsteinCrystalFreeEnergy(system,
                                         parameters.einsteinSpringEvPerA2,
                                         parameters.einsteinFixedCenterOfMass);
    case TiReference::LennardJonesFluid:
        return lennardJonesFreeEnergy(system, parameters);
    case TiReference::IdealGas:
        break;
    }
    return idealGasFreeEnergy(system);
}

std::string toString(TiReference reference)
{
    switch (reference) {
    case TiReference::EinsteinCrystal:   return "einstein_crystal";
    case TiReference::LennardJonesFluid: return "lennard_jones_fluid";
    case TiReference::IdealGas:          break;
    }
    return "ideal_gas";
}

// ---------------------------------------------------------------------------
// λ scheduling and Gauss-Legendre
// ---------------------------------------------------------------------------

GaussLegendreRule gaussLegendreRule(int points, double a, double b)
{
    GaussLegendreRule rule;
    if (points < 1)
        return rule;
    rule.nodes.resize(static_cast<std::size_t>(points));
    rule.weights.resize(static_cast<std::size_t>(points));

    // Newton iteration on P_n(x) with the standard cos(π(i − ¼)/(n + ½))
    // starting guess, and the three-term recurrence for both P_n and P_n'.
    // Converges in a handful of iterations at every n this module uses.
    const int m = (points + 1) / 2;
    for (int i = 0; i < m; ++i) {
        double x = std::cos(M_PI * (i + 0.75) / (points + 0.5));
        double dp = 0.0;
        for (int iteration = 0; iteration < 100; ++iteration) {
            double p0 = 1.0;
            double p1 = 0.0;
            for (int j = 0; j < points; ++j) {
                const double p2 = p1;
                p1 = p0;
                p0 = ((2.0 * j + 1.0) * x * p1 - j * p2) / (j + 1.0);
            }
            dp = points * (x * p0 - p1) / (x * x - 1.0);
            const double dx = p0 / dp;
            x -= dx;
            if (std::abs(dx) < 1.0e-15)
                break;
        }
        // Symmetric pair. The nodes come out descending in x, so index from the
        // top to keep the mapped λ list ascending.
        const double weight = 2.0 / ((1.0 - x * x) * dp * dp);
        const std::size_t lo = static_cast<std::size_t>(i);
        const std::size_t hi = static_cast<std::size_t>(points - 1 - i);
        const double half = 0.5 * (b - a);
        const double mid = 0.5 * (a + b);
        rule.nodes[lo] = mid - half * x;
        rule.nodes[hi] = mid + half * x;
        rule.weights[lo] = weight * half;
        rule.weights[hi] = weight * half;
    }
    return rule;
}

std::vector<double> lambdaSchedule(TiLambdaSchedule schedule, int windows,
                                   double exponent)
{
    std::vector<double> lambdas;
    if (windows < 1)
        return lambdas;
    // A single window can only be a midpoint rule; putting it at λ = 0 or 1
    // would sample an endpoint and integrate nothing.
    if (windows == 1)
        return {0.5};

    switch (schedule) {
    case TiLambdaSchedule::GaussLegendre:
        return gaussLegendreRule(windows, 0.0, 1.0).nodes;
    case TiLambdaSchedule::PowerLaw: {
        const double p = std::max(exponent, 1.0e-3);
        lambdas.reserve(static_cast<std::size_t>(windows));
        for (int i = 0; i < windows; ++i)
            lambdas.push_back(std::pow(static_cast<double>(i) / (windows - 1), p));
        return lambdas;
    }
    case TiLambdaSchedule::ClusteredEnds: {
        // λ = uᵖ / (uᵖ + (1 − u)ᵖ): symmetric, endpoint-exact, identity at
        // p = 1, and increasingly bunched at BOTH ends as p grows.
        const double p = std::max(exponent, 1.0e-3);
        lambdas.reserve(static_cast<std::size_t>(windows));
        for (int i = 0; i < windows; ++i) {
            const double u = static_cast<double>(i) / (windows - 1);
            const double up = std::pow(u, p);
            const double vp = std::pow(1.0 - u, p);
            lambdas.push_back(up + vp > 0.0 ? up / (up + vp) : u);
        }
        return lambdas;
    }
    case TiLambdaSchedule::Uniform:
        break;
    }
    lambdas.reserve(static_cast<std::size_t>(windows));
    for (int i = 0; i < windows; ++i)
        lambdas.push_back(static_cast<double>(i) / (windows - 1));
    return lambdas;
}

// ---------------------------------------------------------------------------
// Quadrature
// ---------------------------------------------------------------------------

namespace {

std::vector<double> trapezoidWeights(const std::vector<double>& nodes)
{
    std::vector<double> weights(nodes.size(), 0.0);
    if (nodes.size() == 1) {
        weights[0] = 1.0; // midpoint rule over the unit interval
        return weights;
    }
    for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
        const double h = nodes[i + 1] - nodes[i];
        weights[i] += 0.5 * h;
        weights[i + 1] += 0.5 * h;
    }
    return weights;
}

} // namespace

TiQuadratureWeights quadratureWeights(TiQuadrature rule,
                                      const std::vector<double>& nodes)
{
    TiQuadratureWeights result;
    if (nodes.empty())
        return result;
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        if (nodes[i] <= nodes[i - 1]) {
            result.note = "quadrature nodes must be strictly ascending";
            return result;
        }
    }

    switch (rule) {
    case TiQuadrature::GaussLegendre: {
        const GaussLegendreRule gl =
            gaussLegendreRule(static_cast<int>(nodes.size()), 0.0, 1.0);
        // Gauss weights are meaningful ONLY on Gauss nodes. Reweighting an
        // arbitrary node set with them is not an approximation of the integral,
        // it is a different linear functional — so this refuses rather than
        // producing a number.
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (std::abs(nodes[i] - gl.nodes[i]) > 1.0e-9) {
                result.note = "Gauss-Legendre quadrature needs the "
                              "Gauss-Legendre nodes (lambdaSchedule("
                              "GaussLegendre, n)); falling back to trapezoid";
                result.weights = trapezoidWeights(nodes);
                result.ruleUsed = TiQuadrature::Trapezoid;
                result.valid = true;
                return result;
            }
        }
        result.weights = gl.weights;
        result.ruleUsed = TiQuadrature::GaussLegendre;
        result.valid = true;
        return result;
    }
    case TiQuadrature::Simpson: {
        const std::size_t n = nodes.size();
        if (n < 3 || !uniformSpacing(nodes)) {
            result.note = n < 3
                ? "Simpson needs at least 3 points; using the trapezoid"
                : "Simpson needs a uniformly spaced lambda grid; using the "
                  "trapezoid";
            result.weights = trapezoidWeights(nodes);
            result.ruleUsed = TiQuadrature::Trapezoid;
            result.valid = true;
            return result;
        }
        const double h = nodes[1] - nodes[0];
        std::vector<double> weights(n, 0.0);
        // Composite Simpson needs an EVEN number of intervals, i.e. an odd
        // number of points. With an even count the last three intervals take
        // Simpson's 3/8 rule (also O(h⁴)) and the rest take the 1/3 rule — the
        // textbook fix, and the alternative to silently dropping a point.
        std::size_t simpsonPoints = n;
        if (n % 2 == 0) {
            // n even ⇒ n − 1 intervals, an odd number. The last three take the
            // 3/8 rule, leaving n − 4 intervals (n − 3 points) for the 1/3 rule
            // — even, as it must be. At n = 4 that leaves a single point and
            // the 3/8 panel is the whole grid, which the loop below handles by
            // simply not running.
            simpsonPoints = n - 3;
            const std::size_t start = simpsonPoints - 1;
            weights[start] += 3.0 * h / 8.0;
            weights[start + 1] += 9.0 * h / 8.0;
            weights[start + 2] += 9.0 * h / 8.0;
            weights[start + 3] += 3.0 * h / 8.0;
        }
        for (std::size_t i = 0; i + 2 < simpsonPoints; i += 2) {
            weights[i] += h / 3.0;
            weights[i + 1] += 4.0 * h / 3.0;
            weights[i + 2] += h / 3.0;
        }
        result.weights = std::move(weights);
        result.ruleUsed = TiQuadrature::Simpson;
        result.valid = true;
        return result;
    }
    case TiQuadrature::Trapezoid:
        break;
    }
    result.weights = trapezoidWeights(nodes);
    result.ruleUsed = TiQuadrature::Trapezoid;
    result.valid = true;
    return result;
}

double integrateWithWeights(const std::vector<double>& weights,
                            const std::vector<double>& values)
{
    if (weights.size() != values.size())
        return 0.0;
    double total = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i)
        total += weights[i] * values[i];
    return total;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

double integratedAutocorrelationTime(const std::vector<double>& series)
{
    const std::size_t n = series.size();
    if (n < 4)
        return 1.0;
    const double mean =
        std::accumulate(series.begin(), series.end(), 0.0) / n;
    double variance = 0.0;
    for (const double value : series)
        variance += (value - mean) * (value - mean);
    variance /= n;
    if (variance <= 0.0)
        return 1.0;

    // Sokal's automatic windowing: accumulate ρ(t) and stop at the first W with
    // W ≥ 5 τ_int(W). The tail of the correlogram is noise whose variance grows
    // with the number of terms summed, so the naive full sum does not converge
    // — it wanders, and the wandering is what would be reported as the error.
    double tau = 1.0;
    const std::size_t maxLag = n / 2;
    for (std::size_t lag = 1; lag < maxLag; ++lag) {
        double covariance = 0.0;
        for (std::size_t i = 0; i + lag < n; ++i)
            covariance += (series[i] - mean) * (series[i + lag] - mean);
        covariance /= static_cast<double>(n - lag);
        const double rho = covariance / variance;
        tau += 2.0 * rho;
        if (static_cast<double>(lag) >= 5.0 * tau)
            break;
    }
    return std::max(1.0, tau);
}

double blockStandardError(const std::vector<double>& series, int blocks)
{
    if (blocks < 2 || series.size() < static_cast<std::size_t>(2 * blocks))
        return 0.0;
    const std::size_t blockSize = series.size() / static_cast<std::size_t>(blocks);
    std::vector<double> means;
    means.reserve(static_cast<std::size_t>(blocks));
    for (int b = 0; b < blocks; ++b) {
        const std::size_t begin = static_cast<std::size_t>(b) * blockSize;
        double sum = 0.0;
        for (std::size_t i = begin; i < begin + blockSize; ++i)
            sum += series[i];
        means.push_back(sum / blockSize);
    }
    const double mean = std::accumulate(means.begin(), means.end(), 0.0) / blocks;
    double variance = 0.0;
    for (const double value : means)
        variance += (value - mean) * (value - mean);
    variance /= (blocks - 1);
    return std::sqrt(variance / blocks);
}

TiSeriesStatistics analyseSeries(const std::vector<double>& series)
{
    TiSeriesStatistics stats;
    const std::size_t n = series.size();
    if (n == 0)
        return stats;
    stats.samples = static_cast<long long>(n);
    stats.mean = std::accumulate(series.begin(), series.end(), 0.0) / n;
    if (n < 2) {
        stats.valid = true;
        return stats;
    }
    double variance = 0.0;
    for (const double value : series)
        variance += (value - stats.mean) * (value - stats.mean);
    stats.variance = variance / (n - 1);
    stats.correlationTime = integratedAutocorrelationTime(series);
    stats.standardError =
        std::sqrt(stats.variance * stats.correlationTime / static_cast<double>(n));

    // Block averaging as an INDEPENDENT estimate: pick the largest block count
    // whose blocks still hold ~20 τ_int samples each, so the block means are
    // effectively uncorrelated (2 τ would leave ~50 % residual correlation and
    // the "independent" estimate would just be the correlated one again). Two
    // estimators that disagree by more than a factor of ~2 mean the run is too
    // short for either.
    const double minBlockSize = std::max(20.0 * stats.correlationTime, 4.0);
    stats.blocks = static_cast<int>(std::floor(n / minBlockSize));
    stats.blocks = std::min(stats.blocks, static_cast<int>(n / 2));
    if (stats.blocks >= 2)
        stats.blockStandardError = blockStandardError(series, stats.blocks);
    else
        stats.blocks = 0;
    stats.valid = true;
    return stats;
}

// ---------------------------------------------------------------------------
// Endpoint diagnostics
// ---------------------------------------------------------------------------

TiEndpointDiagnostics endpointDiagnostics(
    const std::vector<TiWindowSample>& windows, double ratioThreshold)
{
    TiEndpointDiagnostics diagnostics;
    std::vector<const TiWindowSample*> good;
    for (const auto& window : windows)
        if (window.ok)
            good.push_back(&window);
    if (good.size() < 4)
        return diagnostics;
    std::sort(good.begin(), good.end(),
              [](const TiWindowSample* a, const TiWindowSample* b) {
                  return a->lambda < b->lambda;
              });

    std::vector<double> variances;
    std::vector<double> magnitudes;
    variances.reserve(good.size());
    magnitudes.reserve(good.size());
    for (const auto* window : good) {
        variances.push_back(window->dudlVarianceEv2);
        magnitudes.push_back(std::abs(window->dudlEv));
    }
    const double medianVariance = median(variances);
    const double medianMagnitude = median(magnitudes);

    const double endVariance =
        std::max(good.front()->dudlVarianceEv2, good.back()->dudlVarianceEv2);
    const double endMagnitude =
        std::max(std::abs(good.front()->dudlEv), std::abs(good.back()->dudlEv));
    diagnostics.varianceRatio =
        medianVariance > 0.0 ? endVariance / medianVariance : 0.0;
    diagnostics.magnitudeRatio =
        medianMagnitude > 0.0 ? endMagnitude / medianMagnitude : 0.0;

    if (diagnostics.varianceRatio > ratioThreshold
        || diagnostics.magnitudeRatio > ratioThreshold) {
        diagnostics.suspected = true;
        // Ratios rounded to one decimal for the message. localeSafeFormat is
        // the shortest text that ROUND-TRIPS, which is what a file needs and
        // not what a sentence needs — "x166.4661890384224" is a diagnostic
        // nobody reads to the end of.
        const auto ratio = [](double value) {
            return num(std::round(value * 10.0) / 10.0);
        };
        diagnostics.message =
            "dU/dlambda blows up at an endpoint (variance x"
            + ratio(diagnostics.varianceRatio) + ", magnitude x"
            + ratio(diagnostics.magnitudeRatio)
            + " against the median over the path). This is the ENDPOINT "
              "SINGULARITY: with linear coupling and a repulsive target core "
              "the integrand diverges at lambda -> 0, and the quadrature is "
              "integrating the shoulder of a divergence. Cluster the windows "
              "towards the endpoints (Gauss-Legendre or the power-law "
              "schedule), or use a reference that already has a core "
              "(Einstein crystal, Lennard-Jones) instead of the ideal gas.";
    }
    return diagnostics;
}

// ---------------------------------------------------------------------------
// Integration and assembly
// ---------------------------------------------------------------------------

TiIntegrationResult integrateThermodynamicPath(
    const std::vector<TiWindowSample>& windows, TiQuadrature rule,
    int expectedWindows)
{
    TiIntegrationResult result;
    result.ruleUsed = rule;

    // Completeness FIRST, and it is not a warning.
    //
    // Quadrature weights are a property of the NODE SET. Dropping a dead window
    // and reweighting the survivors integrates a different curve over a
    // different grid and reports it with the same name — the exact failure this
    // check exists to make impossible. So a path with a missing window produces
    // no free energy at all.
    std::vector<bool> present(
        static_cast<std::size_t>(std::max(expectedWindows, 0)), false);
    std::vector<TiWindowSample> usable;
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const TiWindowSample& window = windows[i];
        const int index = window.index >= 0 ? window.index : static_cast<int>(i);
        if (index >= 0 && index < expectedWindows && window.ok)
            present[static_cast<std::size_t>(index)] = true;
        if (window.ok)
            usable.push_back(window);
    }
    for (std::size_t i = 0; i < present.size(); ++i)
        if (!present[i])
            result.missingWindows.push_back(static_cast<int>(i));

    for (const auto& window : windows)
        if (!window.ok && !window.failure.empty())
            result.warnings.push_back("window " + std::to_string(window.index)
                                      + ": " + window.failure);

    if (!result.missingWindows.empty()) {
        std::string list;
        for (const int index : result.missingWindows)
            list += (list.empty() ? "" : ", ") + std::to_string(index);
        result.warnings.push_back(
            "incomplete lambda path: window(s) " + list + " of "
            + std::to_string(expectedWindows)
            + " produced no average. No free energy is reported — an integral "
              "over the surviving windows is a different integral, not a "
              "noisier one.");
        return result;
    }
    if (usable.empty() || expectedWindows <= 0) {
        result.warnings.push_back("no lambda windows to integrate");
        return result;
    }

    std::sort(usable.begin(), usable.end(),
              [](const TiWindowSample& a, const TiWindowSample& b) {
                  return a.lambda < b.lambda;
              });

    std::vector<double> nodes;
    std::vector<double> values;
    std::vector<double> errors;
    nodes.reserve(usable.size());
    values.reserve(usable.size());
    errors.reserve(usable.size());
    for (const auto& window : usable) {
        nodes.push_back(window.lambda);
        values.push_back(window.dudlEv);
        errors.push_back(window.dudlErrorEv);
    }

    const TiQuadratureWeights weights = quadratureWeights(rule, nodes);
    if (!weights.valid) {
        result.warnings.push_back(weights.note.empty()
                                      ? "quadrature failed"
                                      : weights.note);
        return result;
    }
    if (!weights.note.empty())
        result.warnings.push_back(weights.note);
    result.ruleUsed = weights.ruleUsed;

    result.deltaFEv = integrateWithWeights(weights.weights, values);

    // σ_I² = Σ wᵢ² σᵢ². Exact for a linear rule, and the reason the weights are
    // returned rather than the integral: guessing this propagation is how a
    // free energy ends up quoted with an error bar that has nothing to do with
    // the sampling that produced it.
    double variance = 0.0;
    for (std::size_t i = 0; i < errors.size(); ++i)
        variance += weights.weights[i] * weights.weights[i] * errors[i] * errors[i];
    result.statisticalErrorEv = std::sqrt(variance);

    // Discretization sensitivity: the chosen rule against a closed trapezoid on
    // the same nodes, and — when the grid is fine enough — against the same
    // trapezoid on every SECOND node. A λ grid that is too coarse, or one whose
    // integrand is singular at an end, shows up here as a difference far larger
    // than the statistical error.
    const double coarse = trapezoidClosed(nodes, values, 0.0, 1.0);
    double discretization = std::abs(result.deltaFEv - coarse);
    if (nodes.size() >= 5) {
        std::vector<double> halfNodes;
        std::vector<double> halfValues;
        for (std::size_t i = 0; i < nodes.size(); i += 2) {
            halfNodes.push_back(nodes[i]);
            halfValues.push_back(values[i]);
        }
        if (halfNodes.size() >= 2)
            discretization = std::max(
                discretization,
                std::abs(result.deltaFEv
                         - trapezoidClosed(halfNodes, halfValues, 0.0, 1.0)));
    }
    result.quadratureErrorEv = discretization;
    result.totalErrorEv = std::sqrt(result.statisticalErrorEv
                                        * result.statisticalErrorEv
                                    + result.quadratureErrorEv
                                        * result.quadratureErrorEv);

    result.endpoint = endpointDiagnostics(usable);
    if (result.endpoint.suspected)
        result.warnings.push_back(result.endpoint.message);
    if (result.quadratureErrorEv > 3.0 * result.statisticalErrorEv
        && result.statisticalErrorEv > 0.0)
        result.warnings.push_back(
            "the lambda grid, not the sampling, dominates the error ("
            + num(result.quadratureErrorEv) + " eV against "
            + num(result.statisticalErrorEv)
            + " eV): add windows rather than MD steps");

    result.complete = true;
    return result;
}

TiHysteresis compareHysteresis(const TiIntegrationResult& forward,
                               const TiIntegrationResult& backward)
{
    TiHysteresis hysteresis;
    if (!forward.complete || !backward.complete)
        return hysteresis;
    hysteresis.forwardEv = forward.deltaFEv;
    hysteresis.backwardEv = backward.deltaFEv;
    hysteresis.differenceEv = forward.deltaFEv - backward.deltaFEv;
    hysteresis.combinedErrorEv =
        std::sqrt(forward.totalErrorEv * forward.totalErrorEv
                  + backward.totalErrorEv * backward.totalErrorEv);
    hysteresis.significant =
        std::abs(hysteresis.differenceEv) > 2.0 * hysteresis.combinedErrorEv;
    hysteresis.valid = true;
    return hysteresis;
}

TiAssembly assembleThermodynamicIntegration(
    const TiSystem& system, TiReference referenceKind,
    const TiReferenceParameters& parameters,
    const std::vector<TiWindowSample>& windows, TiQuadrature rule,
    int expectedWindows)
{
    TiAssembly assembly;
    assembly.integration =
        integrateThermodynamicPath(windows, rule, expectedWindows);
    assembly.reference = referenceFreeEnergy(referenceKind, system, parameters);
    assembly.warnings = assembly.integration.warnings;
    assembly.warnings.insert(assembly.warnings.end(),
                             assembly.reference.warnings.begin(),
                             assembly.reference.warnings.end());

    if (!assembly.integration.complete || !assembly.reference.valid)
        return assembly;

    assembly.helmholtzEv =
        assembly.reference.freeEnergyEv + assembly.integration.deltaFEv;
    // PV in eV: P[GPa] × (1 eV/Å³ / 160.2 GPa) × V[Å³]. Zero pressure gives
    // G = F exactly, which is the right answer for an NVT path rather than a
    // placeholder.
    assembly.pvEv = system.pressureGPa * kGpaInEvPerA3 * system.volumeA3;
    assembly.gibbsEv = assembly.helmholtzEv + assembly.pvEv;
    assembly.errorEv = assembly.integration.totalErrorEv;

    if (system.atomCount > 0) {
        const double n = static_cast<double>(system.atomCount);
        assembly.helmholtzEvPerAtom = assembly.helmholtzEv / n;
        assembly.gibbsEvPerAtom = assembly.gibbsEv / n;
        assembly.errorEvPerAtom = assembly.errorEv / n;
    }
    assembly.valid = true;
    return assembly;
}

} // namespace calango::core
