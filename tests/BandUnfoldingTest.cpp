// Popescu-Zunger band unfolding: the geometry/bookkeeping half.
//
// Pins the three things that silently produce a plausible-but-wrong effective
// band structure: the supercell matrix deduction (and its commensurability
// check), the reciprocal-space fold k -> K (which uses M TRANSPOSE, not M —
// the classic error, invisible for diagonal supercells), and the spectral
// broadening normalization.

#include "core/BandUnfolding.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace calango::core;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

void checkNear(double got, double want, double tol, const std::string& what)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  %s %s  (got %.6f, want %.6f)\n", ok ? "ok  " : "FAIL",
                what.c_str(), got, want);
    if (!ok)
        ++failures;
}

UnitCell cubic(double a)
{
    return UnitCell({a, 0, 0}, {0, a, 0}, {0, 0, a}, {true, true, true});
}

} // namespace

int main()
{
    std::printf("Supercell matrix deduction:\n");
    {
        // 2x2x2 of a 3.6 A cube.
        double residual = 1.0;
        const auto m = deduceSupercellMatrix(cubic(3.6), cubic(7.2), &residual);
        check(m.m[0][0] == 2 && m.m[1][1] == 2 && m.m[2][2] == 2,
              "diagonal 2x2x2 recovered");
        check(m.determinant() == 8, "|det M| = 8 primitive cells");
        checkNear(residual, 0.0, 1e-12, "exact commensurate fit");
        check(m.valid(), "matrix is usable");
    }
    {
        // Non-diagonal: an fcc conventional cell built from the fcc primitive.
        const UnitCell primitive({0.0, 1.8, 1.8}, {1.8, 0.0, 1.8},
                                 {1.8, 1.8, 0.0}, {true, true, true});
        double residual = 1.0;
        const auto m = deduceSupercellMatrix(primitive, cubic(3.6), &residual);
        checkNear(residual, 0.0, 1e-9, "non-diagonal fit is exact");
        check(std::abs(m.determinant()) == 4,
              "conventional fcc holds 4 primitive cells");
    }
    {
        // Incommensurate: 1.5x is not an integer multiple, so the residual
        // must expose it rather than the deduction quietly rounding to 2.
        double residual = 0.0;
        deduceSupercellMatrix(cubic(3.6), cubic(5.4), &residual);
        check(residual > 0.1,
              "incommensurate cells report a large residual (not rounded away)");
    }
    {
        double residual = 0.0;
        const UnitCell degenerate({0, 0, 0}, {0, 0, 0}, {0, 0, 0});
        deduceSupercellMatrix(degenerate, cubic(3.6), &residual);
        check(!std::isfinite(residual), "singular primitive cell is rejected");
    }

    std::printf("Folding k -> K:\n");
    {
        SupercellMatrix m;
        m.m[0][0] = m.m[1][1] = m.m[2][2] = 2; // 2x2x2
        // X of the primitive zone, (0.5, 0, 0), folds onto Gamma of a 2x
        // supercell: K = 2 * 0.5 = 1.0 == 0 (mod 1).
        const Vec3 folded = foldToSupercell({0.5, 0.0, 0.0}, m);
        checkNear(folded.x, 0.0, 1e-12, "primitive X folds onto supercell Gamma");
        // A quarter-zone point lands on the supercell zone boundary.
        const Vec3 quarter = foldToSupercell({0.25, 0.0, 0.0}, m);
        checkNear(quarter.x, -0.5, 1e-12, "k = 1/4 folds to the K boundary");
        // Gamma always folds to Gamma.
        const Vec3 gamma = foldToSupercell({0.0, 0.0, 0.0}, m);
        check(std::abs(gamma.x) < 1e-12 && std::abs(gamma.y) < 1e-12
                  && std::abs(gamma.z) < 1e-12,
              "Gamma folds to Gamma");
    }
    {
        // Transpose sensitivity: a shear supercell distinguishes M from M^T.
        SupercellMatrix m;
        m.m[0][0] = 1; m.m[0][1] = 0; m.m[0][2] = 0;
        m.m[1][0] = 2; m.m[1][1] = 1; m.m[1][2] = 0;   // <- off-diagonal
        m.m[2][0] = 0; m.m[2][1] = 0; m.m[2][2] = 1;
        // With M^T, k = (0.25, 0, 0) gives K_x = 1*0.25 + 2*0 = 0.25 and
        // K_y = 0*0.25 + 1*0 = 0. Using M instead would put the 2 into K_y.
        const Vec3 folded = foldToSupercell({0.25, 0.0, 0.0}, m);
        checkNear(folded.x, 0.25, 1e-12, "shear fold uses M-transpose (x)");
        checkNear(folded.y, 0.0, 1e-12, "shear fold uses M-transpose (y)");
        // And a k along y picks up the off-diagonal term: K_x = 2 * 0.25.
        // That lands exactly on the zone boundary, where +0.5 and -0.5 are the
        // same point by periodicity — the reduction uses the half-open
        // convention [-0.5, 0.5), so it reports -0.5. Compare the magnitude.
        const Vec3 alongY = foldToSupercell({0.0, 0.25, 0.0}, m);
        checkNear(std::abs(alongY.x), 0.5, 1e-12,
                  "off-diagonal term enters K_x (zone boundary, sign-agnostic)");
        checkNear(alongY.y, 0.25, 1e-12, "diagonal term still gives K_y");
    }

    std::printf("Spectral function:\n");
    {
        // One state, unit weight: the Gaussian must integrate to that weight,
        // so columns stay comparable regardless of the bin count.
        UnfoldedColumn column;
        column.pathCoordinate = 0.0;
        column.states.push_back({0.0, 1.0});
        SpectralFunctionOptions options;
        options.energyMin = -2.0;
        options.energyMax = 2.0;
        options.energyBins = 801;
        options.sigma = 0.05;
        const auto a = computeSpectralFunction({column}, options);
        check(a.valid(), "spectral function produced");
        check(a.energies.size() == 801, "energy grid has the requested bins");
        const double step = 4.0 / 800.0;
        double integral = 0.0;
        for (const double v : a.intensity[0])
            integral += v * step;
        checkNear(integral, 1.0, 5e-3, "integral of A over E equals the weight");
        checkNear(a.maxIntensity, 1.0 / (0.05 * std::sqrt(2.0 * M_PI)), 1e-3,
                  "peak height is the Gaussian normalization");
    }
    {
        // Weight scaling is linear, and sub-threshold states vanish entirely.
        UnfoldedColumn column;
        column.states.push_back({0.0, 0.25});
        column.states.push_back({1.0, 1e-9}); // below threshold
        SpectralFunctionOptions options;
        options.energyMin = -2.0;
        options.energyMax = 2.0;
        options.energyBins = 801;
        options.sigma = 0.05;
        options.weightThreshold = 1e-4;
        const auto a = computeSpectralFunction({column}, options);
        checkNear(a.maxIntensity,
                  0.25 / (0.05 * std::sqrt(2.0 * M_PI)), 1e-3,
                  "peak scales linearly with spectral weight");
        // The discarded state sat at E = 1.0; that region must be empty.
        const std::size_t nearOne = static_cast<std::size_t>((1.0 + 2.0) / 4.0 * 800);
        checkNear(a.intensity[0][nearOne], 0.0, 1e-9,
                  "sub-threshold state contributes nothing");
    }
    {
        check(!computeSpectralFunction({}, {}).valid(), "no columns -> invalid");
        SpectralFunctionOptions bad;
        bad.energyMin = 1.0;
        bad.energyMax = 1.0; // zero span
        UnfoldedColumn column;
        column.states.push_back({1.0, 1.0});
        check(!computeSpectralFunction({column}, bad).valid(),
              "zero energy span -> invalid rather than a divide by zero");
    }

    std::printf(failures == 0 ? "\nAll band-unfolding checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
