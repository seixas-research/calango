#include "core/Egqca.hpp"

#include "core/PhononThermodynamics.hpp"
#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>

namespace calango::core {

namespace {

/// x ln x, continuous at zero (0 ln 0 := 0, its limit). Every EGQCA sum runs
/// over probabilities that are routinely exactly zero — a strongly ordered
/// or strongly segregated alloy drives most cluster probabilities to zero —
/// and IEEE 0*log(0) is NaN, which would poison the whole (x, T) grid from
/// one cluster.
double xlnx(double x)
{
    return x > 0.0 ? x * std::log(x) : 0.0;
}

/// exp() with the argument clamped so a pathological (very negative or very
/// positive) reduced enthalpy cannot overflow to inf and turn every
/// downstream sum into NaN. +-700 is comfortably inside IEEE double's
/// ~+-745 range for exp().
double safeExp(double arg)
{
    return std::exp(std::clamp(arg, -700.0, 700.0));
}

/// One term of Eq. 15's polynomial-in-eta root equation, and of Eq. 13's
/// normalization sum, evaluated in log space via ln(eta) — g_j * eta^{n_j} *
/// exp(-(eps_j+Delta_j)/kT), computed as exp(ln g_j + n_j ln eta -
/// (eps_j+Delta_j)/kT) so a large n_j and a large |ln eta| never form an
/// intermediate eta^{n_j} that overflows before the exponential decay from
/// the energy term has a chance to bring it back down.
double clusterTerm(double lnDegeneracy, int bAtomCount, double lnEta,
                   double reducedEnthalpyPlusVibEv, double kT)
{
    return safeExp(lnDegeneracy + bAtomCount * lnEta
                   - reducedEnthalpyPlusVibEv / kT);
}

/// Eq. 15: F(eta) = sum_j (nx - n_j) g_j eta^{n_j} exp(-(eps_j+Delta_j)/kT).
/// The paper states this has a unique positive real root; F(eta->0+) > 0
/// (dominated by the nx * [n_j=0 term], positive since nx>0) and
/// F(eta->infinity) < 0 (dominated by the (nx-n) * [n_j=n term], negative
/// since nx<n) whenever the ensemble spans both pure endpoints, which is
/// exactly the sign change a bracketing solve needs.
double rootFunction(const std::vector<double>& lnDegeneracy,
                    const std::vector<int>& bAtomCounts,
                    const std::vector<double>& reducedEnthalpyPlusVibEv,
                    double nx, double lnEta, double kT)
{
    double sum = 0.0;
    for (std::size_t j = 0; j < bAtomCounts.size(); ++j) {
        const double term = clusterTerm(lnDegeneracy[j], bAtomCounts[j], lnEta,
                                        reducedEnthalpyPlusVibEv[j], kT);
        sum += (nx - bAtomCounts[j]) * term;
    }
    return sum;
}

/// Bracket and bisect Eq. 15 for ln(eta) (eta > 0 always — see
/// EgqcaCluster's eta = x e^{lambda_L/kT}/(1-x) definition in the paper —
/// so solving in log space keeps the search well-behaved across the many
/// orders of magnitude eta can span between a dilute and a concentrated
/// composition). Returns {ln(eta), converged, iterations}.
struct RootSolveResult {
    double lnEta = 0.0;
    bool converged = false;
    int iterations = 0;
};

RootSolveResult solveLnEta(const std::vector<double>& lnDegeneracy,
                           const std::vector<int>& bAtomCounts,
                           const std::vector<double>& reducedEnthalpyPlusVibEv,
                           double nx, double kT, int maxIterations,
                           double tolerance)
{
    RootSolveResult result;
    const auto f = [&](double lnEta) {
        return rootFunction(lnDegeneracy, bAtomCounts, reducedEnthalpyPlusVibEv,
                            nx, lnEta, kT);
    };

    // Expand outward from ln(eta) = 0 until the sign change Eq. 15
    // guarantees is bracketed. 60 halvings of a starting +-4 span already
    // covers ln(eta) in [-4*2^60, 4*2^60], far past anything a physical
    // (Delta_j, T) combination produces before safeExp's own clamp bites.
    double lo = -4.0, hi = 4.0;
    double fLo = f(lo), fHi = f(hi);
    int expand = 0;
    while (fLo <= 0.0 && expand < 60) {
        lo *= 2.0;
        fLo = f(lo);
        ++expand;
    }
    expand = 0;
    while (fHi >= 0.0 && expand < 60) {
        hi *= 2.0;
        fHi = f(hi);
        ++expand;
    }
    if (fLo <= 0.0 || fHi >= 0.0)
        return result; // no sign change found — refuses rather than guesses

    for (int iter = 0; iter < maxIterations; ++iter) {
        const double mid = 0.5 * (lo + hi);
        const double fMid = f(mid);
        if (std::abs(fMid) < tolerance || (hi - lo) < tolerance) {
            result.lnEta = mid;
            result.converged = true;
            result.iterations = iter + 1;
            return result;
        }
        if (fMid > 0.0)
            lo = mid;
        else
            hi = mid;
    }
    result.lnEta = 0.5 * (lo + hi);
    result.iterations = maxIterations;
    return result;
}

} // namespace

double egqcaIdealClusterProbability(int degeneracy, int bAtomCount,
                                    int sitesPerCluster, double composition)
{
    if (sitesPerCluster <= 0 || bAtomCount < 0 || bAtomCount > sitesPerCluster)
        return 0.0;
    const double x = std::clamp(composition, 0.0, 1.0);
    return degeneracy * std::pow(x, bAtomCount)
        * std::pow(1.0 - x, sitesPerCluster - bAtomCount);
}

EgqcaResult solveEgqca(const EgqcaInput& input)
{
    EgqcaResult result;
    const int n = input.sitesPerCluster;
    const std::size_t J = input.clusters.size();

    if (J == 0) {
        result.note = "no clusters supplied";
        return result;
    }
    if (n <= 0) {
        result.note = "sitesPerCluster must be positive";
        return result;
    }
    for (const auto& cluster : input.clusters) {
        if (cluster.bAtomCount < 0 || cluster.bAtomCount > n) {
            result.note = "a cluster's B-atom count is outside [0, n]";
            return result;
        }
    }
    int pureAIndex = -1, pureBIndex = -1;
    for (std::size_t j = 0; j < J; ++j) {
        if (input.clusters[j].bAtomCount == 0)
            pureAIndex = static_cast<int>(j);
        if (input.clusters[j].bAtomCount == n)
            pureBIndex = static_cast<int>(j);
    }
    if (pureAIndex < 0 || pureBIndex < 0) {
        result.note = "the cluster ensemble must include both pure "
                      "end-members (n_j = 0 and n_j = n) for the "
                      "composition range to be well posed";
        return result;
    }
    if (input.compositionSteps < 1 || input.temperatureSteps < 1) {
        result.note = "compositionSteps and temperatureSteps must be >= 1";
        return result;
    }

    // -- Delta_j (Eq. 4): reduced excess enthalpy, independent of T ---------
    std::vector<double> lnDegeneracy(J), delta(J);
    std::vector<int> bAtomCounts(J);
    for (std::size_t j = 0; j < J; ++j) {
        const EgqcaCluster& c = input.clusters[j];
        bAtomCounts[j] = c.bAtomCount;
        lnDegeneracy[j] = std::log(static_cast<double>(std::max(1, c.degeneracy)));
        const double hj = c.energyEv + input.pressureEv3 * c.volumeAng3;
        const double frac = static_cast<double>(c.bAtomCount) / n;
        delta[j] = hj - (1.0 - frac) * input.referenceEnthalpyA
            - frac * input.referenceEnthalpyB;
    }

    // -- Vibrational free energy per cluster, Eq. 9-12 -----------------------
    // All-or-nothing (see Egqca.hpp's class doc comment): mixing a
    // vibrational and a non-vibrational cluster in the same Delta G sum is
    // not thermodynamically consistent, and the paper's own practice for an
    // ensemble with dynamically unstable clusters is to omit vibrational
    // effects for the WHOLE calculation, not per cluster.
    result.vibrationalAvailable = true;
    for (const auto& c : input.clusters) {
        if (c.phononFrequenciesCm.empty() || c.phononDos.empty()) {
            result.vibrationalAvailable = false;
            result.warnings.push_back(
                "cluster '" + (c.label.empty() ? std::string("(unlabelled)") : c.label)
                + "' has no phonon DOS — vibrational effects (Delta A) are "
                  "omitted for the whole ensemble, as GQCA (not EGQCA)");
        }
    }
    // A_j(T) per cluster, on the input temperature grid — computed once,
    // reused for every composition, since A_j(T) does not depend on x.
    std::vector<std::vector<double>> vibFreeEnergyEv; // [cluster][T index]
    if (result.vibrationalAvailable) {
        vibFreeEnergyEv.resize(J);
        for (std::size_t j = 0; j < J; ++j) {
            const PhononThermoResult phonon = computePhononThermodynamics(
                input.clusters[j].phononFrequenciesCm, input.clusters[j].phononDos,
                input.minTemperatureK, input.maxTemperatureK,
                input.temperatureSteps);
            vibFreeEnergyEv[j].reserve(phonon.points.size());
            for (const auto& pt : phonon.points)
                vibFreeEnergyEv[j].push_back(pt.freeEnergyEv);
        }
    }

    result.propertyAvailable = std::all_of(
        input.clusters.begin(), input.clusters.end(),
        [](const EgqcaCluster& c) { return c.hasProperty; });
    result.propertyName = input.propertyName;

    result.compositionSteps = input.compositionSteps;
    result.temperatureSteps = input.temperatureSteps;
    result.points.reserve(static_cast<std::size_t>(input.compositionSteps)
                          * input.temperatureSteps);

    const double xSpan = input.compositionSteps > 1
        ? (input.maxComposition - input.minComposition)
            / (input.compositionSteps - 1)
        : 0.0;
    const double tSpan = input.temperatureSteps > 1
        ? (input.maxTemperatureK - input.minTemperatureK)
            / (input.temperatureSteps - 1)
        : 0.0;

    for (int ix = 0; ix < input.compositionSteps; ++ix) {
        const double x = input.compositionSteps > 1
            ? input.minComposition + ix * xSpan
            : input.minComposition;
        const double nx = x * n;

        for (int it = 0; it < input.temperatureSteps; ++it) {
            const double T = input.temperatureSteps > 1
                ? input.minTemperatureK + it * tSpan
                : input.minTemperatureK;
            const double kT = kBoltzmannEvPerK * T;

            EgqcaPoint point;
            point.composition = x;
            point.temperatureK = T;
            point.clusterProbabilities.assign(J, 0.0);

            // eps_j(T) + Delta_j — the SUM that drives the root solve and
            // Eq. 13's probabilities; eps_j and Delta_j otherwise stay
            // separate (Delta H, Eq. 3, uses Delta_j alone; Delta A, Eq. 10,
            // uses eps_j alone — only the Boltzmann weight combines them).
            std::vector<double> reducedEv(J);
            for (std::size_t j = 0; j < J; ++j) {
                const double eps = result.vibrationalAvailable
                    ? vibFreeEnergyEv[j][static_cast<std::size_t>(it)]
                        - (1.0 - static_cast<double>(bAtomCounts[j]) / n)
                            * vibFreeEnergyEv[static_cast<std::size_t>(pureAIndex)]
                                             [static_cast<std::size_t>(it)]
                        - (static_cast<double>(bAtomCounts[j]) / n)
                            * vibFreeEnergyEv[static_cast<std::size_t>(pureBIndex)]
                                             [static_cast<std::size_t>(it)]
                    : 0.0;
                reducedEv[j] = eps + delta[j];
            }

            // x = 0 / x = 1: Eq. 15 is singular there (nx - n_j = 0 exactly
            // for the surviving term), so the pure-element limit is handled
            // directly rather than solved — every cluster except the pure
            // one has p_j = 0 by construction, not by a numerical root that
            // happens to land there.
            if (x <= 1e-12 || x >= 1.0 - 1e-12) {
                const int pureIndex = x <= 1e-12 ? pureAIndex : pureBIndex;
                point.clusterProbabilities[static_cast<std::size_t>(pureIndex)] = 1.0;
                point.converged = true;
            } else {
                const RootSolveResult root =
                    solveLnEta(lnDegeneracy, bAtomCounts, reducedEv, nx, kT,
                              input.maxIterations, input.tolerance);
                point.converged = root.converged;
                point.iterations = root.iterations;
                if (root.converged) {
                    // Eq. 13: p_j = g_j eta^{n_j} exp(-(eps_j+Delta_j)/kT) /
                    // sum_j' [...]. Each numerator is exactly clusterTerm();
                    // the sum of all of them is the same normalization Eq.
                    // 15's derivation divides through by.
                    double normalization = 0.0;
                    std::vector<double> numerators(J);
                    for (std::size_t j = 0; j < J; ++j) {
                        numerators[j] = clusterTerm(lnDegeneracy[j], bAtomCounts[j],
                                                    root.lnEta, reducedEv[j], kT);
                        normalization += numerators[j];
                    }
                    if (normalization > 0.0)
                        for (std::size_t j = 0; j < J; ++j)
                            point.clusterProbabilities[j] = numerators[j] / normalization;
                    else
                        point.converged = false;
                }
            }

            if (point.converged) {
                // Delta H / M (Eq. 3).
                for (std::size_t j = 0; j < J; ++j)
                    point.mixingEnthalpyEv +=
                        point.clusterProbabilities[j] * delta[j];

                // Delta A / M (Eq. 10).
                if (result.vibrationalAvailable) {
                    for (std::size_t j = 0; j < J; ++j) {
                        const double eps =
                            reducedEv[j] - delta[j]; // recover eps_j alone
                        point.vibrationalFreeEnergyEv +=
                            point.clusterProbabilities[j] * eps;
                    }
                }

                // Delta_KL (Eq. 16) and (Delta S / M) / k_B, from Eq. 8
                // divided by M: -n[x ln x + (1-x) ln(1-x)] - Delta_KL.
                double klDivergence = 0.0;
                for (std::size_t j = 0; j < J; ++j) {
                    const double pj = point.clusterProbabilities[j];
                    if (pj <= 0.0)
                        continue;
                    const double pj0 = egqcaIdealClusterProbability(
                        input.clusters[j].degeneracy, bAtomCounts[j], n, x);
                    if (pj0 > 0.0)
                        klDivergence += pj * std::log(pj / pj0);
                }
                point.klDivergence = klDivergence;
                point.mixingEntropyKb =
                    -n * (xlnx(x) + xlnx(1.0 - x)) - klDivergence;

                // Delta G / M (Eq. 9): Delta H - T Delta S + Delta A.
                point.mixingFreeEnergyEv = point.mixingEnthalpyEv
                    - kT * point.mixingEntropyKb + point.vibrationalFreeEnergyEv;

                // Composition-dependent property, Eq. 17-18.
                if (result.propertyAvailable) {
                    double mean = 0.0, meanSquare = 0.0;
                    for (std::size_t j = 0; j < J; ++j) {
                        mean += point.clusterProbabilities[j]
                            * input.clusters[j].property;
                        meanSquare += point.clusterProbabilities[j]
                            * input.clusters[j].property * input.clusters[j].property;
                    }
                    point.propertyValue = mean;
                    const double variance = meanSquare - mean * mean;
                    point.propertyUncertainty =
                        variance > 0.0 ? std::sqrt(variance) : 0.0;
                }
            }

            result.points.push_back(std::move(point));
        }
    }

    result.ok = true;
    return result;
}

} // namespace calango::core
