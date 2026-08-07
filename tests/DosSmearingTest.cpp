// Live DOS smearing: the run stores an unbroadened eigenvalue histogram and
// the VIEWER convolves it, so a piece of physics now lives in a paint path.
// This pins that convolution against the analytic Gaussian it claims to be —
// peak height 1/(sigma*sqrt(2pi)), conserved spectral weight, FWHM 2.355*sigma
// — and against the one way it could corrupt data: re-broadening a file that
// already carries a width, which widens every peak to sqrt(s1^2 + s2^2) and
// looks exactly like a physical result.
#include "gui/BandPdosView.hpp"
#include <QApplication>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using calango::gui::BandPdosView;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    BandPdosView view;

    // A single unit-weight state at E = 0 on a fine grid.
    const int n = 4001;
    const double lo = -10.0, bin = 0.005;
    BandPdosView::PdosData data;
    data.energies.resize(n);
    for (int i = 0; i < n; ++i) data.energies[i] = lo + i * bin;
    std::vector<double> hist(n, 0.0);
    const int centre = static_cast<int>((0.0 - lo) / bin);
    hist[centre] = 1.0;
    data.projections.emplace_back(QStringLiteral("one state"), hist);
    data.binWidth = bin;
    data.broadened = false;
    data.suggestedWidth = 0.1;
    view.setPdosData(data);

    check(view.pdosSmearingAvailable(), "raw data is re-broadenable");
    check(std::abs(view.pdosSmearing() - 0.1) < 1e-12,
          "the view opens on the width the run suggested");

    for (double sigma : {0.05, 0.1, 0.3}) {
        view.setPdosSmearing(sigma);
        const auto& curve = view.pdosCurves().front().second;
        // Peak height of a normalized Gaussian is 1/(sigma*sqrt(2pi)).
        const double expectedPeak = 1.0 / (sigma * std::sqrt(2.0 * M_PI));
        const double peak = curve[static_cast<std::size_t>(centre)];
        // Integral over the grid must return the state's weight.
        double integral = 0.0;
        for (double v : curve) integral += v * bin;
        // Full width at half maximum = 2*sqrt(2 ln2)*sigma.
        int leftHalf = centre;
        while (leftHalf > 0 && curve[static_cast<std::size_t>(leftHalf)] > peak / 2.0)
            --leftHalf;
        const double fwhm = 2.0 * (centre - leftHalf) * bin;
        const double expectedFwhm = 2.0 * std::sqrt(2.0 * std::log(2.0)) * sigma;
        std::printf("  sigma=%.2f  peak %.4f (want %.4f)  integral %.5f  "
                    "fwhm %.3f (want %.3f)\n",
                    sigma, peak, expectedPeak, integral, fwhm, expectedFwhm);
        check(std::abs(peak - expectedPeak) / expectedPeak < 0.01,
              "peak height matches the normalized Gaussian");
        check(std::abs(integral - 1.0) < 0.01, "the state's weight is conserved");
        check(std::abs(fwhm - expectedFwhm) < 3 * bin, "FWHM matches 2.355 sigma");
    }

    // Two states 1 eV apart: resolved at small sigma, merged at large.
    std::vector<double> two(n, 0.0);
    two[static_cast<std::size_t>(((-0.5) - lo) / bin)] = 1.0;
    two[static_cast<std::size_t>((( 0.5) - lo) / bin)] = 1.0;
    data.projections.clear();
    data.projections.emplace_back(QStringLiteral("two states"), two);
    view.setPdosData(data);
    const auto dipAtCentre = [&](double sigma) {
        view.setPdosSmearing(sigma);
        const auto& c = view.pdosCurves().front().second;
        double peak = 0.0;
        for (double v : c) peak = std::max(peak, v);
        return c[static_cast<std::size_t>(centre)] / peak;
    };
    check(dipAtCentre(0.1) < 0.05, "sigma=0.1 resolves two states 1 eV apart");
    check(dipAtCentre(0.6) > 0.95, "sigma=0.6 merges them into one peak");

    // Already-broadened (legacy) data must be passed through untouched.
    BandPdosView::PdosData legacy = data;
    legacy.broadened = true;
    view.setPdosData(legacy);
    check(!view.pdosSmearingAvailable(), "legacy data reports no live smearing");
    view.setPdosSmearing(0.5);
    check(view.pdosCurves().front().second == two,
          "legacy curves are drawn exactly as stored, never re-broadened");

    std::printf(failures == 0 ? "\nAll DOS smearing checks passed.\n"
                              : "\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
