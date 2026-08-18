// Unit tests for the Extended Generalized Quasichemical Approximation
// (src/core/Egqca), following Ferreira et al., "Ab initio modeling of
// superconducting alloys", Materials Today Physics 48 (2024) 101547 —
// equation numbers in comments below are that paper's own.
//
// The paper's own worked examples (Al-doped MgB2, Nb-Ti, Nb-V, Y-Ca-H) use
// 20-256-configuration DFT+DFPT cluster ensembles whose numerical inputs
// (per-cluster energies, degeneracies) live in the paper's Supplementary
// Tables — not reproducible from the article text/figures alone, so this
// file cannot pin a literal figure curve. In its place: two exact checks the
// paper itself states as defining properties of the theory (the ideal-limit
// reduction and cluster-probability normalization — Task 1's own explicit
// minimum), plus an independent oracle for a small worked example: since
// n=2 leaves exactly one free parameter once the two EGQCA constraints
// (probabilities sum to 1, mean composition = x) are applied, the true
// Gibbs-minimizing point can be found by a brute-force 1D scan completely
// independent of Egqca.cpp's own Lagrange-multiplier solver — checking that
// EGQCA truly finds Eq. 9's global minimum, which Eq. 13-15 are DERIVED to
// be the stationarity condition of, rather than merely checking EGQCA
// against a second run of itself.
//
// Exit code 0 = pass.

#include "core/Egqca.hpp"
#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

} // namespace

int main()
{
    using namespace calango::core;

    std::printf("Ideal (regular-solution) limit — Delta_j = 0 everywhere:\n");
    {
        // n = 3, 4 clusters (nj = 0..3), degeneracies the binomial
        // coefficients C(3, nj) = 1, 3, 3, 1 — a random ternary-site cluster
        // ensemble with no genuine interaction (every H_j exactly equals
        // the linear interpolation of the pure references, so Delta_j = 0
        // for all j by construction).
        EgqcaInput in;
        in.sitesPerCluster = 3;
        in.referenceEnthalpyA = -1.0;
        in.referenceEnthalpyB = -2.0;
        const int degeneracy[4] = {1, 3, 3, 1};
        for (int nj = 0; nj <= 3; ++nj) {
            EgqcaCluster c;
            c.bAtomCount = nj;
            c.degeneracy = degeneracy[nj];
            // H_j = (1 - nj/n) H_A + (nj/n) H_B exactly => Delta_j = 0.
            c.energyEv = (1.0 - nj / 3.0) * in.referenceEnthalpyA
                + (nj / 3.0) * in.referenceEnthalpyB;
            in.clusters.push_back(c);
        }
        in.minComposition = 0.1;
        in.maxComposition = 0.9;
        in.compositionSteps = 9;
        in.minTemperatureK = 300.0;
        in.maxTemperatureK = 1200.0;
        in.temperatureSteps = 4;

        const EgqcaResult result = solveEgqca(in);
        check(result.ok, "the ideal-limit ensemble solves (" + result.note + ")");
        check(!result.vibrationalAvailable,
              "no phonon DOS supplied -> vibrational effects correctly "
              "report as unavailable (plain GQCA, not EGQCA)");

        double worstProbabilityError = 0.0, worstEnthalpy = 0.0,
               worstEntropyError = 0.0, worstKl = 0.0;
        for (const auto& point : result.points) {
            if (!point.converged)
                continue;
            for (std::size_t j = 0; j < in.clusters.size(); ++j) {
                const double p0 = egqcaIdealClusterProbability(
                    in.clusters[j].degeneracy, in.clusters[j].bAtomCount,
                    in.sitesPerCluster, point.composition);
                worstProbabilityError = std::max(
                    worstProbabilityError,
                    std::abs(point.clusterProbabilities[j] - p0));
            }
            worstEnthalpy = std::max(worstEnthalpy,
                                     std::abs(point.mixingEnthalpyEv));
            worstKl = std::max(worstKl, std::abs(point.klDivergence));
            const double idealEntropyPerSite =
                -(point.composition * std::log(point.composition)
                  + (1.0 - point.composition)
                      * std::log(1.0 - point.composition));
            worstEntropyError = std::max(
                worstEntropyError,
                std::abs(point.mixingEntropyKb / in.sitesPerCluster
                        - idealEntropyPerSite));
        }
        check(worstProbabilityError < 1e-8,
              "cluster probabilities converge to the ideal p_j^0 (Eq. 7) — "
              "worst error " + std::to_string(worstProbabilityError));
        check(worstEnthalpy < 1e-10,
              "mixing enthalpy is exactly zero with no genuine interaction");
        check(worstKl < 1e-8,
              "the Kullback-Leibler divergence (Eq. 16) vanishes — complete "
              "randomness, per the paper's own diagnostic");
        check(worstEntropyError < 1e-8,
              "mixing entropy per site reduces to the ideal configurational "
              "entropy -[x ln x + (1-x) ln(1-x)] (Eq. 5-6 in the random limit)");
    }

    std::printf("Normalization and the composition constraint, at every "
                "(x, T) — a genuinely interacting ensemble:\n");
    {
        EgqcaInput in;
        in.sitesPerCluster = 4;
        in.referenceEnthalpyA = 0.0;
        in.referenceEnthalpyB = 0.0;
        const int degeneracy[5] = {1, 4, 6, 4, 1};
        for (int nj = 0; nj <= 4; ++nj) {
            EgqcaCluster c;
            c.bAtomCount = nj;
            c.degeneracy = degeneracy[nj];
            // A real (nonzero) excess enthalpy, peaked at the middle
            // composition — an ordering tendency, positive Delta_j.
            const double frac = nj / 4.0;
            c.energyEv = 0.08 * frac * (1.0 - frac);
            in.clusters.push_back(c);
        }
        in.minComposition = 0.05;
        in.maxComposition = 0.95;
        in.compositionSteps = 19;
        in.minTemperatureK = 200.0;
        in.maxTemperatureK = 1500.0;
        in.temperatureSteps = 8;

        const EgqcaResult result = solveEgqca(in);
        check(result.ok, "an interacting ensemble solves (" + result.note + ")");

        int nonConverged = 0;
        double worstNormalization = 0.0, worstComposition = 0.0;
        for (const auto& point : result.points) {
            if (!point.converged) {
                ++nonConverged;
                continue;
            }
            double sumP = 0.0, meanNj = 0.0;
            for (std::size_t j = 0; j < in.clusters.size(); ++j) {
                sumP += point.clusterProbabilities[j];
                meanNj += point.clusterProbabilities[j] * in.clusters[j].bAtomCount;
            }
            worstNormalization = std::max(worstNormalization, std::abs(sumP - 1.0));
            worstComposition = std::max(
                worstComposition,
                std::abs(meanNj / in.sitesPerCluster - point.composition));
        }
        check(nonConverged == 0,
              "every (x, T) grid point converges (" + std::to_string(nonConverged)
                  + " did not)");
        check(worstNormalization < 1e-8,
              "sum_j p_j = 1 at every (x, T) point — worst deviation "
                  + std::to_string(worstNormalization));
        check(worstComposition < 1e-6,
              "sum_j n_j p_j / n = x at every point — the constraint Eq. 15 "
              "solves for is actually satisfied, worst deviation "
                  + std::to_string(worstComposition));
    }

    std::printf("Independent oracle: n=2, brute-force global minimum of "
                "Delta G (Eq. 9) vs. EGQCA's Lagrange-multiplier solve:\n");
    {
        // Three clusters: AA (nj=0), AB (nj=1, degeneracy 2 — the two
        // orderings of one A and one B on two distinguishable sites), BB
        // (nj=2). A genuine ordering interaction on the mixed cluster.
        EgqcaInput in;
        in.sitesPerCluster = 2;
        in.referenceEnthalpyA = 0.0;
        in.referenceEnthalpyB = 0.0;
        EgqcaCluster aa, ab, bb;
        aa.bAtomCount = 0;
        aa.degeneracy = 1;
        aa.energyEv = 0.0;
        ab.bAtomCount = 1;
        ab.degeneracy = 2;
        ab.energyEv = 0.05; // Delta_AB = 0.05 - 0 = 0.05 eV: an ordering cost
        bb.bAtomCount = 2;
        bb.degeneracy = 1;
        bb.energyEv = 0.0;
        in.clusters = {aa, ab, bb};
        in.minComposition = 0.5;
        in.maxComposition = 0.5;
        in.compositionSteps = 1;
        in.minTemperatureK = 500.0;
        in.maxTemperatureK = 500.0;
        in.temperatureSteps = 1;

        const EgqcaResult result = solveEgqca(in);
        check(result.ok && result.points.size() == 1,
              "the 3-cluster ensemble solves");
        const EgqcaPoint& solved = result.points.front();
        check(solved.converged, "...and converges at x=0.5, T=500 K");

        // Independent brute-force minimization of Eq. 9 over the ONE free
        // parameter this system has (p_AB; p_AA and p_BB are then fixed by
        // the two EGQCA constraints), with NO reference to Egqca.cpp at all.
        const double x = 0.5, T = 500.0;
        const double kT = kBoltzmannEvPerK * T;
        const double deltaAB = 0.05;
        const auto gibbs = [&](double pAB) {
            const double pAA = (1.0 - x) - pAB / 2.0;
            const double pBB = x - pAB / 2.0;
            if (pAA <= 0.0 || pBB <= 0.0 || pAB <= 0.0)
                return std::numeric_limits<double>::infinity();
            const double p0AA = egqcaIdealClusterProbability(1, 0, 2, x);
            const double p0AB = egqcaIdealClusterProbability(2, 1, 2, x);
            const double p0BB = egqcaIdealClusterProbability(1, 2, 2, x);
            const double enthalpy = pAB * deltaAB; // M * sum p_j Delta_j, M=1
            const double klDivergence = pAA * std::log(pAA / p0AA)
                + pAB * std::log(pAB / p0AB) + pBB * std::log(pBB / p0BB);
            const double entropyKb = -2.0 * (x * std::log(x)
                                             + (1.0 - x) * std::log(1.0 - x))
                - klDivergence;
            return enthalpy - kT * entropyKb;
        };
        double bestPAB = 0.0, bestGibbs = std::numeric_limits<double>::infinity();
        for (int i = 1; i < 20000; ++i) {
            // p_AB ranges over (0, 2*min(x, 1-x)) = (0, 1) at x=0.5.
            const double pAB = 2.0 * static_cast<double>(i) / 20000.0;
            const double g = gibbs(pAB);
            if (g < bestGibbs) {
                bestGibbs = g;
                bestPAB = pAB;
            }
        }
        check(std::abs(solved.clusterProbabilities[1] - bestPAB) < 2e-3,
              "EGQCA's solved p_AB (" + std::to_string(solved.clusterProbabilities[1])
                  + ") matches the independently brute-force-minimized value ("
                  + std::to_string(bestPAB) + ")");
        check(std::abs(solved.mixingFreeEnergyEv - bestGibbs) < 5e-4,
              "...and EGQCA's Delta G matches the independently found "
              "global minimum of Eq. 9");
    }

    std::printf("Vibrational wiring: an identical DOS on every cluster "
                "contributes exactly zero to Delta A (Eq. 11):\n");
    {
        EgqcaInput in;
        in.sitesPerCluster = 2;
        in.referenceEnthalpyA = 0.0;
        in.referenceEnthalpyB = 0.0;
        const std::vector<double> freq = {200.0, 300.0, 400.0};
        const std::vector<double> dos = {0.5, 1.0, 0.5};
        for (int nj = 0; nj <= 2; ++nj) {
            EgqcaCluster c;
            c.bAtomCount = nj;
            c.degeneracy = (nj == 1) ? 2 : 1;
            c.energyEv = 0.02 * nj * (2 - nj); // some ordinary interaction
            c.phononFrequenciesCm = freq;
            c.phononDos = dos; // the SAME DOS on every cluster
            in.clusters.push_back(c);
        }
        in.minComposition = 0.3;
        in.maxComposition = 0.7;
        in.compositionSteps = 3;
        in.minTemperatureK = 300.0;
        in.maxTemperatureK = 900.0;
        in.temperatureSteps = 3;

        const EgqcaResult result = solveEgqca(in);
        check(result.ok, "the vibrational ensemble solves");
        check(result.vibrationalAvailable,
              "every cluster has a DOS -> vibrational effects are available "
              "(genuinely EGQCA, not GQCA)");
        double worstVibrational = 0.0;
        for (const auto& point : result.points)
            if (point.converged)
                worstVibrational =
                    std::max(worstVibrational,
                            std::abs(point.vibrationalFreeEnergyEv));
        check(worstVibrational < 1e-10,
              "Delta A is exactly zero when every cluster shares one DOS "
              "(eps_j = A_j - linear interpolation of IDENTICAL A_A, A_B "
              "vanishes identically, Eq. 11)");
    }

    if (failures == 0) {
        std::printf("\nAll EGQCA checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d EGQCA check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
