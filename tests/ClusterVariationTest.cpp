// Configurational entropy by the Cluster Variation Method.
//
// The pair (Bethe-Peierls-Guggenheim) approximation is EXACT on a
// one-dimensional chain, and the 1D Ising chain has a closed-form solution by
// transfer matrix. That coincidence is the whole test strategy: it gives an
// exact analytic answer to check the solver against, at every temperature and
// every interaction strength, rather than a tolerance around a previous run.
//
// For the symmetric equiatomic binary with e_AA = e_BB = 0 and e_AB = J, the
// transfer matrix T = [[1, u], [u, 1]] with u = exp(-J/kT) has largest
// eigenvalue 1 + u, so per site
//
//     E   = J u / (1 + u)
//     S/k = ln(1 + u) + (J/kT) u / (1 + u)
//
// derived here independently of the module. Both limits are also fixed:
// S -> ln 2 as T -> infinity (ideal), and S -> 0 as T -> 0 in EITHER sign of
// J, because the alloy either phase-separates or orders and both are single
// arrangements.

#include "core/ClusterVariation.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

constexpr double kBoltzmannEvPerK = 8.617333262e-5;

/// The exact 1D chain result, written from the transfer matrix.
void exactChain(double j, double temperatureK, double* entropy, double* energy)
{
    const double beta = 1.0 / (kBoltzmannEvPerK * temperatureK);
    const double u = std::exp(-beta * j);
    *energy = j * u / (1.0 + u);
    *entropy = std::log(1.0 + u) + beta * j * u / (1.0 + u);
}

} // namespace

int main()
{
    using calango::core::CvmApproximation;
    using calango::core::CvmInput;
    using calango::core::CvmLattice;

    // -- The ideal baseline --------------------------------------------------
    std::printf("Ideal configurational entropy:\n");
    {
        const double binary =
            calango::core::idealConfigurationalEntropy({0.5, 0.5});
        check(std::abs(binary - std::log(2.0)) < 1e-15,
              "an equiatomic binary gives exactly ln 2");
        const double quinary = calango::core::idealConfigurationalEntropy(
            {0.2, 0.2, 0.2, 0.2, 0.2});
        check(std::abs(quinary - std::log(5.0)) < 1e-15,
              "and an equiatomic quinary exactly ln 5 = 1.609 k_B — the "
              "number quoted for high-entropy alloys");
        check(calango::core::idealConfigurationalEntropy({1.0, 0.0}) == 0.0,
              "a pure element has zero configurational entropy, without a "
              "NaN from 0 ln 0");
        // Unnormalized input must be normalized, not mis-scaled.
        check(std::abs(calango::core::idealConfigurationalEntropy({2.0, 2.0})
                       - std::log(2.0))
                  < 1e-15,
              "and the composition is normalized on entry");
    }

    // -- The pair approximation against the exact 1D chain -------------------
    //
    // This is the load-bearing test. It runs BOTH signs of J: negative favours
    // unlike neighbours (ordering), positive favours like (clustering). A sign
    // error in the energy convention passes one and fails the other, which is
    // why both are here.
    std::printf("Pair (BPG) vs the exact 1D Ising chain:\n");
    for (const double j : {-0.05, -0.01, 0.01, 0.05}) {
        CvmInput in;
        in.lattice = CvmLattice::Chain;
        in.approximation = CvmApproximation::Pair;
        in.species = {"A", "B"};
        in.composition = {0.5, 0.5};
        in.pairEnergiesEv = {0.0, j, j, 0.0};
        in.minTemperatureK = 200.0;
        in.maxTemperatureK = 2000.0;
        in.temperatureSteps = 7;
        const auto out = calango::core::solveClusterVariation(in);
        if (!out.ok) {
            check(false, "the chain solves for J = " + std::to_string(j));
            continue;
        }
        double worstEntropy = 0.0;
        double worstEnergy = 0.0;
        for (const auto& point : out.points) {
            double s = 0.0;
            double e = 0.0;
            exactChain(j, point.temperatureK, &s, &e);
            worstEntropy =
                std::max(worstEntropy, std::abs(point.entropyPerSiteKb - s));
            worstEnergy =
                std::max(worstEnergy, std::abs(point.energyPerSiteEv - e));
        }
        std::printf("    J = %+.3f eV: max |dS| = %.2e k_B, "
                    "max |dE| = %.2e eV\n",
                    j, worstEntropy, worstEnergy);
        check(worstEntropy < 1e-9,
              "S matches the transfer-matrix solution to 1e-9 k_B at J = "
                  + std::to_string(j));
        check(worstEnergy < 1e-12,
              "and E matches it to 1e-12 eV — the pair approximation is "
              "EXACT in 1D, so this is an identity, not an approximation");
    }

    // -- Limits --------------------------------------------------------------
    std::printf("Limits that must hold for any lattice:\n");
    {
        CvmInput in;
        in.lattice = CvmLattice::Fcc;
        in.approximation = CvmApproximation::Pair;
        in.species = {"A", "B"};
        in.composition = {0.5, 0.5};
        in.pairEnergiesEv = {0.0, -0.02, -0.02, 0.0};
        in.minTemperatureK = 300.0;
        in.maxTemperatureK = 100000.0; // far above any ordering scale
        in.temperatureSteps = 40;
        const auto out = calango::core::solveClusterVariation(in);
        check(out.ok, "an FCC binary solves");
        const auto& hottest = out.points.back();
        check(std::abs(hottest.entropyPerSiteKb - out.idealEntropyKb) < 1e-4,
              "S -> S_ideal as T -> infinity: interactions stop mattering, "
              "which is the identity that fixes the -(z/2)/(z-1) prefactors");
        // The residual SRO is FIRST order in beta*J while the entropy
        // deviation is SECOND order — which is why the entropy above already
        // matched to 1e-4 while alpha here is ~1e-3. That difference in order
        // is the physics, so what is asserted is the SCALING: alpha must fall
        // like 1/T. A constant offset would pass a loose magnitude bound and
        // fail this.
        double worstAlpha = 0.0;
        for (const double alpha : hottest.warrenCowley)
            worstAlpha = std::max(worstAlpha, std::abs(alpha));
        CvmInput hotter = in;
        hotter.minTemperatureK = hotter.maxTemperatureK = 1000000.0;
        hotter.temperatureSteps = 1;
        const auto tenTimes = calango::core::solveClusterVariation(hotter);
        double worstHotter = 0.0;
        for (const double alpha : tenTimes.points.front().warrenCowley)
            worstHotter = std::max(worstHotter, std::abs(alpha));
        std::printf("    max|alpha| = %.3e at 1e5 K, %.3e at 1e6 K "
                    "(ratio %.2f)\n",
                    worstAlpha, worstHotter, worstAlpha / worstHotter);
        check(worstAlpha < 5e-3 && worstHotter < worstAlpha,
              "the Warren-Cowley alpha decay towards zero with temperature");
        check(std::abs(worstAlpha / worstHotter - 10.0) < 0.5,
              "and do so as 1/T — alpha is first order in beta*J, which is "
              "why it is still ~1e-3 where the entropy has already converged "
              "to 1e-4");

        // Below the ideal value everywhere: correlations can only REMOVE
        // arrangements, never add them.
        bool everAbove = false;
        for (const auto& point : out.points)
            if (point.entropyPerSiteKb > out.idealEntropyKb + 1e-9)
                everAbove = true;
        check(!everAbove,
              "S never exceeds S_ideal — order can only reduce the number of "
              "arrangements, so the ideal value is an upper bound");

        // Ordering (negative e_AB here) must show as NEGATIVE alpha for the
        // unlike pair: A prefers B.
        const auto& coldest = out.points.front();
        check(coldest.warrenCowley[1] < 0.0,
              "with e_AB < 0 the unlike-pair alpha is negative — the alloy "
              "orders rather than clusters");
        check(coldest.entropyPerSiteKb < out.idealEntropyKb,
              "and its entropy falls below the ideal baseline, which is the "
              "whole point of computing it");
    }

    // -- The point approximation IS the ideal entropy ------------------------
    {
        CvmInput in;
        in.lattice = CvmLattice::Fcc;
        in.approximation = CvmApproximation::Point;
        in.species = {"A", "B"};
        in.composition = {0.3, 0.7};
        in.pairEnergiesEv = {0.0, -0.05, -0.05, 0.0};
        in.minTemperatureK = 100.0;
        in.maxTemperatureK = 2000.0;
        in.temperatureSteps = 5;
        const auto out = calango::core::solveClusterVariation(in);
        check(out.ok, "the point approximation solves");
        bool constant = true;
        for (const auto& point : out.points)
            if (std::abs(point.entropyPerSiteKb - out.idealEntropyKb) > 1e-15)
                constant = false;
        check(constant,
              "Bragg-Williams entropy is the ideal one at every temperature, "
              "however strong the interaction — it lets the alloy lower its "
              "energy without paying entropy for the order that requires, "
              "which is precisely why it is wrong rather than merely coarse");
    }

    // -- Zero interaction is random at every temperature ---------------------
    {
        CvmInput in;
        in.lattice = CvmLattice::Fcc;
        in.approximation = CvmApproximation::Pair;
        in.species = {"Co", "Cr", "Fe", "Ni"};
        in.composition = {0.25, 0.25, 0.25, 0.25};
        in.pairEnergiesEv.assign(16, 0.0);
        in.minTemperatureK = 300.0;
        in.maxTemperatureK = 1800.0;
        in.temperatureSteps = 6;
        const auto out = calango::core::solveClusterVariation(in);
        check(out.ok, "a four-component equiatomic HEA solves");
        check(std::abs(out.idealEntropyKb - std::log(4.0)) < 1e-15,
              "whose ideal entropy is ln 4 = 1.386 k_B");
        for (const auto& point : out.points)
            check(std::abs(point.entropyPerSiteKb - out.idealEntropyKb) < 1e-9,
                  "and with zero interaction the pair result equals it "
                  "exactly, at every temperature");
        check(out.sroVanishingTemperatureK == 0.0,
              "with no short-range order reported anywhere");
    }

    // -- The tetrahedron approximation ---------------------------------------
    //
    // The sharp test is the IDEAL LIMIT AT A NON-EQUIATOMIC COMPOSITION.
    // With zero interaction the tetrahedron distribution must be exactly the
    // product form and S exactly S_ideal — and that identity is what fixes
    // the Kikuchi-Barker coefficients (2, 0, -6, 5) AND the composition
    // multiplier together. An earlier derivation here dropped the composition
    // multiplier, which gives w ~ (prod x)^(7/8): completely invisible at
    // x = 0.5 (every tuple picks up the same factor and normalization hides
    // it) and wrong everywhere else. So this runs at x = 0.3, not 0.5.
    std::printf("Kikuchi tetrahedron:\n");
    {
        CvmInput in;
        in.lattice = CvmLattice::Fcc;
        in.approximation = CvmApproximation::Tetrahedron;
        in.species = {"A", "B"};
        in.composition = {0.3, 0.7};
        in.pairEnergiesEv.assign(4, 0.0);
        in.minTemperatureK = 300.0;
        in.maxTemperatureK = 1500.0;
        in.temperatureSteps = 4;
        const auto out = calango::core::solveClusterVariation(in);
        check(out.ok, "a non-equiatomic FCC binary solves");
        double worst = 0.0;
        for (const auto& point : out.points)
            worst = std::max(worst,
                             std::abs(point.entropyPerSiteKb
                                      - out.idealEntropyKb));
        std::printf("    x = 0.3, zero interaction: max |S - S_ideal| = "
                    "%.2e k_B\n",
                    worst);
        check(worst < 1e-10,
              "with zero interaction S is EXACTLY S_ideal at x = 0.3 — the "
              "identity that pins the Kikuchi coefficients and the "
              "composition multiplier together");
        for (const auto& point : out.points)
            check(point.converged, "and the natural iteration converges");
    }

    // The composition constraint must survive the iteration.
    {
        CvmInput in;
        in.lattice = CvmLattice::Fcc;
        in.approximation = CvmApproximation::Tetrahedron;
        in.species = {"A", "B"};
        in.composition = {0.35, 0.65};
        in.pairEnergiesEv = {0.0, -0.03, -0.03, 0.0};
        in.minTemperatureK = 400.0;
        in.maxTemperatureK = 400.0;
        in.temperatureSteps = 1;
        const auto out = calango::core::solveClusterVariation(in);
        check(out.ok, "an interacting non-equiatomic tetrahedron solves");
        // Pair marginals must reproduce the imposed composition.
        const auto& y = out.points.front().pairProbabilities;
        double xa = 0.0;
        for (int j = 0; j < 2; ++j)
            xa += y[static_cast<std::size_t>(0) * 2 + j];
        std::printf("    imposed x_A = 0.35, recovered %.12f\n", xa);
        check(std::abs(xa - 0.35) < 1e-9,
              "and the pair marginals still carry the imposed composition — "
              "the iterative proportional fitting is not drifting");
    }

    // High temperature must return to ideal, as for any approximation.
    {
        CvmInput in;
        in.lattice = CvmLattice::Fcc;
        in.approximation = CvmApproximation::Tetrahedron;
        in.species = {"A", "B"};
        in.composition = {0.5, 0.5};
        in.pairEnergiesEv = {0.0, -0.02, -0.02, 0.0};
        in.minTemperatureK = 500.0;
        in.maxTemperatureK = 200000.0;
        in.temperatureSteps = 30;
        const auto out = calango::core::solveClusterVariation(in);
        check(out.ok, "the interacting tetrahedron solves over a wide range");
        check(std::abs(out.points.back().entropyPerSiteKb - out.idealEntropyKb)
                  < 1e-4,
              "S -> S_ideal as T -> infinity");
        check(out.points.front().entropyPerSiteKb < out.idealEntropyKb,
              "and falls below it where the interaction bites");

        // The hierarchy: the tetrahedron sees the frustration of the FCC
        // nearest-neighbour tetrahedron that the pair approximation cannot,
        // so on the SAME interaction it must not simply reproduce the pair
        // answer. Asserting they DIFFER is what proves the tetrahedron
        // machinery is actually running rather than silently falling back.
        CvmInput pairInput = in;
        pairInput.approximation = CvmApproximation::Pair;
        const auto pairOut = calango::core::solveClusterVariation(pairInput);
        std::printf("    at %.0f K: S_tet = %.6f, S_pair = %.6f, "
                    "S_ideal = %.6f k_B\n",
                    out.points.front().temperatureK,
                    out.points.front().entropyPerSiteKb,
                    pairOut.points.front().entropyPerSiteKb,
                    out.idealEntropyKb);
        check(std::abs(out.points.front().entropyPerSiteKb
                       - pairOut.points.front().entropyPerSiteKb)
                  > 1e-6,
              "and differs from the pair result — the tetrahedron is really "
              "being solved, not falling back");
    }

    // -- The ECI basis transform --------------------------------------------
    {
        bool ok = false;
        const auto energies = calango::core::pairEnergiesFromEci(0.03, &ok);
        check(ok && energies.size() == 4,
              "a pair ECI converts to a species-pair energy matrix");
        check(energies[0] == 0.03 && energies[3] == 0.03
                  && energies[1] == -0.03 && energies[2] == -0.03,
              "as e_AA = e_BB = +J and e_AB = -J, the +/-1 correlation basis "
              "— J > 0 therefore ORDERS, which is the convention half the "
              "literature writes the other way round");
    }

    // -- Refusals ------------------------------------------------------------
    {
        CvmInput in;
        in.species = {"A"};
        in.composition = {1.0};
        check(!calango::core::solveClusterVariation(in).ok,
              "a single species is refused: a pure element has one "
              "arrangement, not a configurational entropy");

        CvmInput bad;
        bad.species = {"A", "B"};
        bad.composition = {0.5, 0.5};
        bad.pairEnergiesEv = {0.0, 0.1}; // not 2x2
        check(!calango::core::solveClusterVariation(bad).ok,
              "and a pair energy matrix that is not species x species is "
              "refused rather than read past");
    }

    // -- Coordination numbers ------------------------------------------------
    check(calango::core::cvmCoordination(CvmLattice::Fcc) == 12
              && calango::core::cvmCoordination(CvmLattice::Bcc) == 8
              && calango::core::cvmCoordination(CvmLattice::Chain) == 2,
          "the coordination numbers are the lattices' own");

    if (failures == 0) {
        std::printf("\nAll cluster-variation checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d cluster-variation check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
