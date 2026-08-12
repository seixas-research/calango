// Linear-tetrahedron Brillouin-zone integration.
//
// Checked against free electrons, eps(k) = hbar^2 k^2 / 2m, whose density of
// states is known in closed form:
//
//     N(E) = (V / 4 pi^2) (2m / hbar^2)^{3/2} sqrt(E)   [per cell, per spin]
//
// so the test has an external reference rather than a previous run of this
// code.
//
// ONE TRAP, and it already caught out an earlier version of this test: the
// free-electron reference is only valid while the constant-energy sphere fits
// INSIDE the sampled grid. On a cubic grid the sphere first touches the zone
// face along a cube axis, and beyond that energy the grid samples only the
// corners — the tetrahedron result then correctly reports less than the
// infinite-medium formula, and comparing them measures the test's own
// geometry rather than the integrator. Probe energies here stay below that
// cutoff, which is computed and asserted rather than assumed.

#include "core/TetrahedronBz.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

constexpr double kPi = 3.14159265358979323846;
/// hbar^2 / 2m in eV * Angstrom^2.
constexpr double kHbar2Over2m = 3.80998212;

} // namespace

int main()
{
    using calango::core::TetrahedronBz;

    // A simple cubic cell of side a; its reciprocal cell is cubic with edge
    // 2 pi / a.
    const double a = 4.0;
    const double b = 2.0 * kPi / a;
    const std::array<std::array<double, 3>, 3> reciprocal{
        {{b, 0.0, 0.0}, {0.0, b, 0.0}, {0.0, 0.0, b}}};

    const int n = 24;
    TetrahedronBz bz({n, n, n}, reciprocal);

    std::printf("Grid and decomposition:\n");
    check(bz.pointCount() == static_cast<std::size_t>(n) * n * n,
          "the grid holds n^3 points");
    check(bz.tetrahedronCount() == bz.pointCount() * 6,
          "cut into six tetrahedra per microcell");
    check(std::abs(bz.brillouinZoneVolume() - b * b * b) < 1e-9 * b * b * b,
          "and the zone volume is |b1 . (b2 x b3)|");
    // The decomposition must tile the zone: six tetrahedra of a microcell fill
    // it exactly, so the total tetrahedron volume is the zone volume. This is
    // the check that catches a wrong corner-splitting table.
    {
        double total = 0.0;
        for (std::size_t t = 0; t < 6; ++t) {
            const auto& g = bz.tetrahedronGeometry(t);
            const double e1[3] = {g[1][0] - g[0][0], g[1][1] - g[0][1],
                                  g[1][2] - g[0][2]};
            const double e2[3] = {g[2][0] - g[0][0], g[2][1] - g[0][1],
                                  g[2][2] - g[0][2]};
            const double e3[3] = {g[3][0] - g[0][0], g[3][1] - g[0][1],
                                  g[3][2] - g[0][2]};
            const double det = e1[0] * (e2[1] * e3[2] - e2[2] * e3[1])
                - e1[1] * (e2[0] * e3[2] - e2[2] * e3[0])
                + e1[2] * (e2[0] * e3[1] - e2[1] * e3[0]);
            total += std::abs(det) / 6.0;
        }
        const double microcell = (b / n) * (b / n) * (b / n);
        check(std::abs(total - microcell) < 1e-9 * microcell,
              "the six tetrahedra tile the microcell exactly");
    }
    // Periodic wrap: the point one past the last is the first.
    check(bz.index(n, 0, 0) == bz.index(0, 0, 0)
              && bz.index(0, -1, 0) == bz.index(0, n - 1, 0),
          "grid indices wrap periodically");

    // -- The band ----------------------------------------------------------
    // Fractional coordinates folded into [-1/2, 1/2), which is the zone the
    // grid represents.
    std::vector<double> energies(bz.pointCount(), 0.0);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            for (int k = 0; k < n; ++k) {
                // Fold an index into [-n/2, n/2), i.e. the zone the grid
                // represents. All-integer on purpose; the cast is the last
                // step so the folding itself cannot pick up a rounding.
                const int half = n / 2;
                const auto fold = [n, half](int i) {
                    return static_cast<double>((i + half) % n - half)
                        / static_cast<double>(n);
                };
                const double kx = fold(i) * b;
                const double ky = fold(j) * b;
                const double kz = fold(k) * b;
                energies[bz.index(i, j, k)] =
                    kHbar2Over2m * (kx * kx + ky * ky + kz * kz);
            }

    // The sphere leaves the sampled region once it reaches the zone face,
    // i.e. at |k| = b/2 along a cube axis.
    const double cutoff = kHbar2Over2m * (b / 2.0) * (b / 2.0);
    std::printf("Free-electron DOS (valid below %.3f eV, where the sphere "
                "first touches the zone face):\n", cutoff);

    const double volume = a * a * a;
    const auto analytic = [volume](double energy) {
        return (volume / (4.0 * kPi * kPi))
            * std::pow(1.0 / kHbar2Over2m, 1.5) * std::sqrt(energy);
    };

    double worst = 0.0;
    for (const double energy : {0.4, 0.8, 1.2, 1.6, 2.0}) {
        check(energy < cutoff,
              "probe " + std::to_string(energy) + " eV is inside the grid");
        const double numeric = bz.dos(energies, energy);
        const double exact = analytic(energy);
        const double error = std::abs(numeric - exact) / exact;
        worst = std::max(worst, error);
        std::printf("    E = %.2f eV: tetrahedron %.6f, analytic %.6f, "
                    "error %5.2f %%\n", energy, numeric, exact, 100.0 * error);
    }
    // Linear interpolation on a 24^3 grid; a few percent is the discretization
    // error, not a defect. The libtetrabz reference reached 0.8 % on the same
    // grid, so this bound is generous enough to be stable and tight enough to
    // fail a wrong normalization (which would be off by a factor, not a
    // percent).
    check(worst < 0.05,
          "every probe is within 5 % of the closed-form DOS (worst "
              + std::to_string(static_cast<int>(100.0 * worst)) + " %)");

    // -- Normalization is a factor, so check it cannot hide ----------------
    // sqrt(E) scaling: the ratio between two energies is exact in the
    // continuum and insensitive to the prefactor, so it isolates the SHAPE
    // from the normalization the previous block already pinned.
    {
        const double low = bz.dos(energies, 0.5);
        const double high = bz.dos(energies, 2.0);
        const double ratio = high / low;
        check(std::abs(ratio - 2.0) < 0.1,
              "the DOS grows as sqrt(E): N(2.0)/N(0.5) = "
                  + std::to_string(ratio) + ", expected 2");
    }

    // -- Weights, not just their sum ---------------------------------------
    std::printf("Weight properties:\n");
    {
        std::vector<double> weights(bz.pointCount(), 0.0);
        bz.accumulateDeltaWeights(energies, 1.2, weights);
        const bool nonNegative =
            std::all_of(weights.begin(), weights.end(),
                        [](double w) { return w >= 0.0; });
        check(nonNegative, "every weight is non-negative");
        // A delta at 1.2 eV may only draw on points near that shell. A weight
        // on a point far off the surface would mean the barycentric
        // distribution is leaking.
        double maxFar = 0.0;
        for (std::size_t i = 0; i < weights.size(); ++i)
            if (std::abs(energies[i] - 1.2) > 0.6)
                maxFar = std::max(maxFar, weights[i]);
        check(maxFar == 0.0,
              "and only points bracketing the level carry any weight");

        // Accumulation adds, so summing two bands is summing their weights.
        std::vector<double> twice(bz.pointCount(), 0.0);
        bz.accumulateDeltaWeights(energies, 1.2, twice);
        bz.accumulateDeltaWeights(energies, 1.2, twice);
        double sumOnce = 0.0;
        double sumTwice = 0.0;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            sumOnce += weights[i];
            sumTwice += twice[i];
        }
        check(std::abs(sumTwice - 2.0 * sumOnce) < 1e-12 * sumOnce,
              "accumulate() adds rather than assigns, so bands sum");
    }

    // -- Levels outside the band -------------------------------------------
    {
        check(bz.dos(energies, -1.0) == 0.0,
              "a level below the band gives no states");
        const double top = *std::max_element(energies.begin(), energies.end());
        check(bz.dos(energies, top + 1.0) == 0.0,
              "and one above it gives none either");
    }

    // -- A flat band -------------------------------------------------------
    // |grad eps| = 0 makes the surface integral divergent. The routine drops
    // those tetrahedra rather than clamping, because clamping invents weight.
    {
        std::vector<double> flat(bz.pointCount(), 3.0);
        check(bz.dos(flat, 3.0) == 0.0,
              "a dispersionless band contributes nothing rather than "
              "infinity");
        check(bz.dos(flat, 1.0) == 0.0, "and nothing away from its energy");
    }

    // -- Convergence -------------------------------------------------------
    // The discretization error must fall as the grid is refined. A routine
    // that were merely self-consistent could pass everything above and still
    // not converge.
    std::printf("Convergence with grid density:\n");
    {
        double previous = 1.0;
        bool improving = true;
        for (const int size : {8, 16, 32}) {
            TetrahedronBz fine({size, size, size}, reciprocal);
            std::vector<double> band(fine.pointCount(), 0.0);
            for (int i = 0; i < size; ++i)
                for (int j = 0; j < size; ++j)
                    for (int k = 0; k < size; ++k) {
                        const int half = size / 2;
                        const auto fold = [size, half](int i) {
                            return static_cast<double>((i + half) % size - half)
                                / static_cast<double>(size);
                        };
                        const double kx = fold(i) * b;
                        const double ky = fold(j) * b;
                        const double kz = fold(k) * b;
                        band[fine.index(i, j, k)] =
                            kHbar2Over2m * (kx * kx + ky * ky + kz * kz);
                    }
            const double error =
                std::abs(fine.dos(band, 1.2) - analytic(1.2)) / analytic(1.2);
            std::printf("    %2d^3: error %5.2f %%\n", size, 100.0 * error);
            if (size > 8 && error > previous)
                improving = false;
            previous = error;
        }
        check(improving, "the error falls monotonically as the grid refines");
    }

    // -- The double delta: the nesting function -----------------------------
    //
    // zeta(q) = (1/V_BZ) integral dk delta(eps_k - E_F) delta(eps_{k+q} - E_F)
    //
    // For free electrons the two Fermi spheres — centred at 0 and at -q —
    // meet in a circle, and the two-constraint identity gives
    //
    //     integral = circumference / |grad1 x grad2| = 2 pi k_perp
    //                                                  / ((h2/m)^2 q k_perp)
    //
    // The circle's radius CANCELS, leaving a result independent of k_F:
    //
    //     zeta(q) = V_cell / (16 pi^2 A^2 q),   0 < q < 2 k_F
    //     zeta(q) = 0                            otherwise
    //
    // with A = hbar^2/2m. Both the 1/q shape and the hard cutoff at 2 k_F are
    // strong tests: the first is insensitive to any prefactor, and the second
    // is a property no smooth approximation reproduces.
    std::printf("Double delta — free-electron nesting function:\n");
    {
        const int m = 32;
        TetrahedronBz fine({m, m, m}, reciprocal);
        const int half = m / 2;
        const auto fold = [m, half](int i) {
            return static_cast<double>((i + half) % m - half)
                / static_cast<double>(m);
        };
        // The band, and its own values shifted by a grid vector q. Shifting by
        // rolling the array is exactly how the electron-phonon sums get
        // eps_{k+q}: q must be commensurate with the mesh for k+q to be a
        // sampled state at all.
        std::vector<double> band(fine.pointCount(), 0.0);
        for (int i = 0; i < m; ++i)
            for (int j = 0; j < m; ++j)
                for (int k = 0; k < m; ++k) {
                    const double kx = fold(i) * b;
                    const double ky = fold(j) * b;
                    const double kz = fold(k) * b;
                    band[fine.index(i, j, k)] =
                        kHbar2Over2m * (kx * kx + ky * ky + kz * kz);
                }
        const auto shifted = [&](int shift) {
            std::vector<double> out(fine.pointCount(), 0.0);
            for (int i = 0; i < m; ++i)
                for (int j = 0; j < m; ++j)
                    for (int k = 0; k < m; ++k)
                        out[fine.index(i, j, k)] =
                            band[fine.index(i + shift, j, k)];
            return out;
        };

        // E_F chosen so the sphere is comfortably inside the grid (the same
        // trap as the DOS block: outside it, the test measures its own
        // geometry).
        const double fermi = 1.2;
        const double kFermi = std::sqrt(fermi / kHbar2Over2m);
        const double volume = a * a * a;
        const auto analyticNesting = [volume](double q) {
            return volume / (16.0 * kPi * kPi * kHbar2Over2m * kHbar2Over2m * q);
        };
        std::printf("    E_F = %.2f eV -> k_F = %.4f 1/A, 2k_F = %.4f\n",
                    fermi, kFermi, 2.0 * kFermi);

        double worstNesting = 0.0;
        int probes = 0;
        for (const int shift : {2, 3, 4, 5}) {
            const double q = shift * b / m;
            if (q >= 2.0 * kFermi)
                continue; // beyond the cutoff; tested separately below
            const double numeric = fine.nesting(band, shifted(shift), fermi);
            const double exact = analyticNesting(q);
            const double error = std::abs(numeric - exact) / exact;
            worstNesting = std::max(worstNesting, error);
            ++probes;
            std::printf("    q = %.4f 1/A: tetrahedron %.6f, analytic %.6f, "
                        "error %5.2f %%\n", q, numeric, exact, 100.0 * error);
        }
        check(probes >= 3, "several q below the 2k_F cutoff were probed");
        check(worstNesting < 0.10,
              "the nesting function matches the closed form to better than "
              "10 % (worst "
                  + std::to_string(static_cast<int>(100.0 * worstNesting))
                  + " %)");

        // The 1/q shape, independent of every prefactor: doubling q halves
        // zeta. This is the check that a wrong measure — say dividing by
        // |grad1||grad2| instead of |grad1 x grad2| — cannot survive, since
        // that would carry an extra angular factor.
        const double atTwo = fine.nesting(band, shifted(2), fermi);
        const double atFour = fine.nesting(band, shifted(4), fermi);
        const double ratio = atTwo / atFour;
        std::printf("    zeta(q)/zeta(2q) = %.4f, expected 2\n", ratio);
        check(std::abs(ratio - 2.0) < 0.2,
              "and falls as 1/q, which no prefactor error can imitate");

        // The cutoff. Beyond 2 k_F the spheres do not intersect and the
        // nesting function is exactly zero — not small, zero.
        int beyond = 0;
        for (int shift = 2; shift < m / 2; ++shift) {
            const double q = shift * b / m;
            if (q <= 2.0 * kFermi * 1.05)
                continue;
            if (fine.nesting(band, shifted(shift), fermi) != 0.0)
                ++beyond;
        }
        check(beyond == 0,
              "and vanishes identically beyond 2k_F, where the two Fermi "
              "spheres no longer meet");

        // Weights, not just their sum.
        std::vector<double> weights(fine.pointCount(), 0.0);
        fine.accumulateDoubleDeltaWeights(band, shifted(3), fermi, weights);
        const bool nonNegative =
            std::all_of(weights.begin(), weights.end(),
                        [](double w) { return w >= 0.0; });
        check(nonNegative, "double-delta weights are non-negative");
        // Both constraints have to bite: a point far from E_F in EITHER band
        // must carry nothing.
        const std::vector<double> other = shifted(3);
        double leaked = 0.0;
        for (std::size_t i = 0; i < weights.size(); ++i)
            if (std::abs(band[i] - fermi) > 0.5
                || std::abs(other[i] - fermi) > 0.5)
                leaked = std::max(leaked, weights[i]);
        check(leaked == 0.0,
              "and only points near E_F in BOTH bands carry weight — the two "
              "constraints are not separable, and a leak would mean one was "
              "being ignored");

        // q = 0 is the degenerate case: the two surfaces coincide rather than
        // crossing, so there is no line to integrate over and the routine
        // declines rather than returning something enormous.
        check(fine.nesting(band, band, fermi) == 0.0,
              "identical bands give no nesting: coincident surfaces are a "
              "singularity, not a large number");
    }

    if (failures == 0) {
        std::printf("\nAll tetrahedron checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d tetrahedron check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
