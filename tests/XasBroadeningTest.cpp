// Live XAS broadening in the viewer.
//
// Unlike the Optics viewer's Lorentzian eta (OpticsBroadeningTest.cpp), this
// is not a convolution of an already-broadened curve — XasResultsWindow keeps
// the RAW discrete transitions (xas.json's stick_energy_eV / stick_isotropic /
// stick_polarization_x,y,z) and re-folds them from scratch at whatever FWHM
// the slider asks for. A stick is a delta function with no width of its own
// to deconvolve, so this is a well-defined forward computation at ANY width —
// narrower or wider than the run's own — not constrained to only widen the
// way Optics's convolution trick is.
//
// gpaw.xas.XAS.constant_broadening() (~/Codes/gpaw/gpaw/xas.py) folds each
// transition to a normalized Gaussian:
//
//     alpha = 4 ln(2) / fwhm^2
//     f(E) += intensity * sqrt(alpha/pi) * exp(-alpha (E - eps)^2)
//
// which is exactly XasResultsWindow::foldSticks(). This checks that
// reproduction against the CLOSED FORM of a single Gaussian: its peak height,
// its full width at half maximum, and its total area (a normalized Gaussian
// integrates to 1, so a stick of intensity I integrates to exactly I) — three
// independent, exactly-known properties a "smooth and plausible but wrong"
// implementation would not all satisfy at once.

#include "gui/XasResultsWindow.hpp"

#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSlider>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using calango::gui::XasResultsWindow;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

/// Trapezoidal integral of `y` over `x` — the grid is uniform in every test
/// below, but this makes no assumption of that.
double integrate(const std::vector<double>& x, const std::vector<double>& y)
{
    double total = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i)
        total += 0.5 * (y[i] + y[i - 1]) * (x[i] - x[i - 1]);
    return total;
}

std::vector<double> linspace(double lo, double hi, int n)
{
    std::vector<double> grid;
    grid.reserve(n);
    for (int i = 0; i < n; ++i)
        grid.push_back(lo + (hi - lo) * i / (n - 1));
    return grid;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // A fine grid, wide enough that a fwhm up to ~1 eV is fully captured —
    // the tails of a normalized Gaussian are negligible past ~5 sigma, and
    // 5 sigma at fwhm = 1 eV is about 2.1 eV.
    const std::vector<double> grid = linspace(0.0, 20.0, 20001);

    std::printf("A single transition folds to the exact Gaussian:\n");
    for (const double fwhm : {0.05, 0.20, 0.80}) {
        const std::vector<double> stickE = {10.0};
        const std::vector<double> stickI = {1.0};
        const std::vector<double> folded =
            XasResultsWindow::foldSticks(grid, stickE, stickI, fwhm);

        const double alpha = 4.0 * std::log(2.0) / (fwhm * fwhm);
        const double expectedPeak = std::sqrt(alpha / M_PI);
        const double peak =
            *std::max_element(folded.begin(), folded.end());
        const double peakError = std::abs(peak - expectedPeak) / expectedPeak;
        std::printf("       fwhm=%.2f: peak %.6f, expected %.6f (%.4f%% off)\n",
                    fwhm, peak, expectedPeak, peakError * 100.0);
        check(peakError < 1e-3,
              "the peak height matches sqrt(alpha/pi) to 0.1%");

        const double area = integrate(grid, folded);
        const double areaError = std::abs(area - 1.0);
        std::printf("       area under the curve: %.6f (stick intensity 1.0)\n",
                    area);
        check(areaError < 1e-3,
              "a normalized Gaussian integrates to the stick's own intensity");

        // Full width at half maximum: find where the curve crosses half the
        // peak on either side, by linear interpolation between the two grid
        // points that bracket it — independent of the peak-height check
        // above, so a correctly-normalized but wrongly-WIDE-OR-NARROW
        // Gaussian would still be caught here.
        const double half = peak / 2.0;
        long peakIndex = 0;
        for (long i = 1; i < static_cast<long>(folded.size()); ++i)
            if (folded[static_cast<std::size_t>(i)]
                > folded[static_cast<std::size_t>(peakIndex)])
                peakIndex = i;
        const long last = static_cast<long>(folded.size()) - 1;
        const auto crossing = [&](long start, long step) {
            long i = start;
            while (i + step >= 0 && i + step <= last
                  && folded[static_cast<std::size_t>(i + step)] > half)
                i += step;
            const long j = i + step;
            if (j < 0 || j > last)
                return grid[static_cast<std::size_t>(i)];
            const double f0 = folded[static_cast<std::size_t>(i)];
            const double f1 = folded[static_cast<std::size_t>(j)];
            const double t = (half - f0) / (f1 - f0);
            return grid[static_cast<std::size_t>(i)]
                + t * (grid[static_cast<std::size_t>(j)] - grid[static_cast<std::size_t>(i)]);
        };
        const double left = crossing(peakIndex, -1);
        const double right = crossing(peakIndex, 1);
        const double measuredFwhm = right - left;
        const double fwhmError = std::abs(measuredFwhm - fwhm) / fwhm;
        std::printf("       measured FWHM %.4f eV, expected %.4f eV (%.4f%% off)\n",
                    measuredFwhm, fwhm, fwhmError * 100.0);
        check(fwhmError < 1e-2,
              "the folded curve's own width matches the requested FWHM to 1%");
    }

    std::printf("\nMultiple transitions superpose linearly:\n");
    {
        const std::vector<double> stickE = {6.0, 9.0, 14.0};
        const std::vector<double> stickI = {0.5, 2.0, 1.2};
        const double fwhm = 0.3;
        const std::vector<double> combined =
            XasResultsWindow::foldSticks(grid, stickE, stickI, fwhm);

        std::vector<double> summed(grid.size(), 0.0);
        for (std::size_t s = 0; s < stickE.size(); ++s) {
            const std::vector<double> one = XasResultsWindow::foldSticks(
                grid, {stickE[s]}, {stickI[s]}, fwhm);
            for (std::size_t i = 0; i < grid.size(); ++i)
                summed[i] += one[i];
        }
        double worst = 0.0;
        for (std::size_t i = 0; i < grid.size(); ++i)
            worst = std::max(worst, std::abs(combined[i] - summed[i]));
        std::printf("       worst |Δ| between combined and summed folds: %.3e\n",
                    worst);
        check(worst < 1e-9,
              "folding three sticks at once equals summing three separate folds");

        const double totalArea = integrate(grid, combined);
        const double expectedArea = 0.5 + 2.0 + 1.2;
        std::printf("       total area %.4f, expected %.4f\n", totalArea,
                    expectedArea);
        check(std::abs(totalArea - expectedArea) < 1e-2,
              "the total area is the sum of the sticks' own intensities");
    }

    std::printf("\nAn intensity of exactly zero contributes nothing:\n");
    {
        const std::vector<double> folded = XasResultsWindow::foldSticks(
            grid, {10.0}, {0.0}, 0.3);
        const double peak = *std::max_element(folded.begin(), folded.end());
        check(peak == 0.0, "a zero-intensity stick folds to a flat zero");
    }

    // -- End-to-end: the viewer actually offers the control and moving it
    // does not crash, on data shaped like a real xas.json.
    std::printf("\nThe viewer's own broadening control re-folds a loaded run:\n");
    {
        QJsonArray energyArr, isoArr, stickEArr, stickIArr;
        QJsonArray pxArr, pyArr, pzArr, stickPxArr, stickPyArr, stickPzArr;
        const double storedFwhm = 0.5;
        for (double e : grid) {
            energyArr.append(e);
            // A single line at 10 eV, pre-folded at the "stored" fwhm — same
            // shape the generator itself would have written.
            const double alpha = 4.0 * std::log(2.0) / (storedFwhm * storedFwhm);
            const double value =
                std::sqrt(alpha / M_PI) * std::exp(-alpha * (e - 10.0) * (e - 10.0));
            isoArr.append(value);
            pxArr.append(value);
            pyArr.append(value);
            pzArr.append(value);
        }
        stickEArr.append(10.0);
        stickIArr.append(1.0);
        stickPxArr.append(1.0);
        stickPyArr.append(1.0);
        stickPzArr.append(1.0);

        QJsonObject root;
        root[QStringLiteral("element")] = QStringLiteral("O");
        root[QStringLiteral("absorbing_atom")] = 0;
        root[QStringLiteral("setup")] = QStringLiteral("hch1s");
        root[QStringLiteral("core_hole")] = 0.5;
        root[QStringLiteral("dks_energy_eV")] = 0.0;
        root[QStringLiteral("fwhm_eV")] = storedFwhm;
        root[QStringLiteral("linear_broadening")] = false;
        root[QStringLiteral("energy_eV")] = energyArr;
        root[QStringLiteral("isotropic")] = isoArr;
        root[QStringLiteral("polarization_x")] = pxArr;
        root[QStringLiteral("polarization_y")] = pyArr;
        root[QStringLiteral("polarization_z")] = pzArr;
        root[QStringLiteral("stick_energy_eV")] = stickEArr;
        root[QStringLiteral("stick_isotropic")] = stickIArr;
        root[QStringLiteral("stick_polarization_x")] = stickPxArr;
        root[QStringLiteral("stick_polarization_y")] = stickPyArr;
        root[QStringLiteral("stick_polarization_z")] = stickPzArr;

        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("xas.json"));
        QFile file(path);
        check(file.open(QIODevice::WriteOnly), "a results file can be staged");
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.close();

        XasResultsWindow window;
        check(window.loadResults(path), "the viewer reads it");

        auto* spin =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("xasBroadening"));
        auto* slider = window.findChild<QSlider*>();
        check(spin != nullptr, "the broadening spin box is present");
        check(slider != nullptr, "and its paired slider");
        if (spin) {
            check(spin->isEnabled(),
                  "enabled: this run recorded individual transitions");
            check(std::abs(spin->value() - storedFwhm) < 1e-9,
                  "opens at the run's own fwhm");
            // Move it — the window must not crash, and a real close-the-loop
            // numeric check belongs to the closed-form tests above, which
            // exercise the same foldSticks() this spin box drives.
            spin->setValue(1.2);
            check(std::abs(spin->value() - 1.2) < 1e-9,
                  "accepts a value both above AND below nothing in particular "
                  "— unlike Optics's eta, there is no floor to respect");
            spin->setValue(0.1);
            check(std::abs(spin->value() - 0.1) < 1e-9,
                  "including one NARROWER than the stored run — the raw "
                  "sticks make that a well-defined forward computation, not "
                  "a deconvolution");
        }
    }

    std::printf(failures == 0 ? "\nAll XAS broadening checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
