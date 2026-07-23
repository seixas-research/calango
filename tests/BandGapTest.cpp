// Automated band-gap detection for the Electronic Structure viewer.
//
// Pins the three classifications the summary box reports — direct, indirect,
// metallic — plus the distinction between the fundamental gap (CBM − VBM,
// possibly at different k) and the minimum DIRECT gap (smallest same-k
// separation), which is the optical onset and is strictly larger for an
// indirect material.

#include "core/BandGap.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using calango::core::analyzeBandGap;

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
    std::printf("  %s %s  (got %.4f, want %.4f)\n", ok ? "ok  " : "FAIL",
                what.c_str(), got, want);
    if (!ok)
        ++failures;
}

/// One spin channel from a per-k-point list of {valence, conduction}.
std::vector<std::vector<std::vector<double>>> single(
    std::vector<std::vector<double>> perK)
{
    return {std::move(perK)};
}

} // namespace

int main()
{
    // E_F sits in the gap, as every DFT code reports for a semiconductor.
    const double ef = 0.0;

    std::printf("Direct gap (VBM and CBM at the same k):\n");
    {
        // k = 1 has both the highest valence and the lowest conduction state.
        const auto info = analyzeBandGap(single({
                                             {-2.0, 3.0},
                                             {-0.5, 1.5}, // <- VBM and CBM here
                                             {-1.2, 2.2},
                                         }),
                                         ef);
        check(info.valid, "analysis is valid");
        check(!info.metallic, "not metallic");
        checkNear(info.gap, 2.0, 1e-9, "fundamental gap = 1.5 - (-0.5)");
        check(info.direct, "classified as DIRECT");
        check(info.vbmKPoint == 1 && info.cbmKPoint == 1, "both extrema at k = 1");
        checkNear(info.directGap, info.gap, 1e-9,
                  "direct gap equals the fundamental gap");
    }

    std::printf("Indirect gap (VBM and CBM at different k):\n");
    {
        // VBM at k = 0, CBM at k = 2 — the silicon-like arrangement.
        const auto info = analyzeBandGap(single({
                                             {-0.2, 3.0}, // VBM here
                                             {-1.5, 2.5},
                                             {-2.0, 0.9}, // CBM here
                                         }),
                                         ef);
        check(!info.metallic, "not metallic");
        checkNear(info.gap, 1.1, 1e-9, "fundamental gap = 0.9 - (-0.2)");
        check(!info.direct, "classified as INDIRECT");
        check(info.vbmKPoint == 0, "VBM at k = 0");
        check(info.cbmKPoint == 2, "CBM at k = 2");
        // Vertical separations: 3.2, 4.0, 2.9 -> the smallest is at k = 2.
        checkNear(info.directGap, 2.9, 1e-9,
                  "minimum direct gap is the optical onset");
        check(info.directGap > info.gap,
              "direct gap strictly exceeds the fundamental gap (indirect)");
        check(info.directKPoint == 2, "optical onset located at k = 2");
    }

    std::printf("Metal (band index 1 crosses E_F):\n");
    {
        // Band 1 runs -0.05 -> +0.02 -> +0.30 across the path: it dips below
        // E_F at k=0 and rises above it afterwards. Splitting raw eigenvalues
        // at E_F would call this a 0.07 eV semiconductor — a pure artefact of
        // how far apart the k-points are.
        const auto info = analyzeBandGap(single({
                                             {-1.0, -0.05, 2.0},
                                             {-0.9, 0.02, 2.1},
                                             {-1.1, 0.30, 2.2},
                                         }),
                                         ef);
        check(info.valid, "analysis is valid");
        check(info.metallic, "classified as METALLIC (band crosses, not a gap)");
        checkNear(info.gap, 0.0, 1e-12, "gap reported as exactly zero");
    }

    std::printf("Semimetal (bands touch without crossing):\n");
    {
        const auto info = analyzeBandGap(single({
                                             {-1.0, 0.5},
                                             {-0.0000001, 0.0000002}, // touch
                                         }),
                                         ef);
        check(info.metallic, "a vanishing gap is reported as metallic");
    }

    std::printf("Spin-polarized (gap in one channel, smaller in the other):\n");
    {
        // Channel 1 carries the true CBM; the analysis must search both.
        const std::vector<std::vector<std::vector<double>>> energies = {
            {{-0.4, 2.5}, {-0.6, 2.7}},  // spin up
            {{-0.9, 1.0}, {-1.1, 1.2}},  // spin down: lower CBM
        };
        const auto info = analyzeBandGap(energies, ef);
        checkNear(info.gap, 1.4, 1e-9, "gap spans the two spin channels");
        check(info.vbmSpin == 0 && info.cbmSpin == 1,
              "VBM in spin up, CBM in spin down");
        check(!info.direct || info.vbmKPoint == info.cbmKPoint,
              "direct flag agrees with the k indices");
    }

    std::printf("Degenerate / unusable input:\n");
    {
        check(!analyzeBandGap({}, ef).valid, "empty band data is not valid");
        // Every state below E_F: the plotted window does not bracket the gap.
        check(!analyzeBandGap(single({{-3.0, -2.0}}), ef).valid,
              "all-occupied window reports invalid rather than a fake gap");
        check(!analyzeBandGap(single({{1.0, 2.0}}), ef).valid,
              "all-empty window reports invalid");
        // NaN eigenvalues (a failed k-point) must not poison the result.
        const auto withNan = analyzeBandGap(
            single({{-0.5, std::nan("")}, {-0.5, 1.5}}), ef);
        check(withNan.valid && std::abs(withNan.gap - 2.0) < 1e-9,
              "NaN eigenvalues are skipped, gap still found");
    }

    std::printf(failures == 0 ? "\nAll band-gap checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
