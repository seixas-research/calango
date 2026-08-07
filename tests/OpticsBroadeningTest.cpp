// Live optical broadening in the viewer.
//
// eta is not a resolution blur, it is a LIFETIME: it sits inside the response
// function as 1/(E - w - i*eta), so unlike a DOS smearing it cannot be left out
// of the calculation and applied later. What makes a slider possible anyway is
// that Lorentzian widths ADD under convolution,
//
//     L(eta_1) * L(eta_2) = L(eta_1 + eta_2),
//
// so a spectrum stored at eta_0 convolved with a Lorentzian of width
// (eta - eta_0) is exactly the spectrum the calculation would have produced at
// eta. This checks that claim against the analytic two-level resolvent — the
// same closed form GPAW's response module evaluates — because a broadening that
// is merely plausible produces a smooth, convincing, wrong spectrum.
//
// Two details the naive implementation gets wrong and this pins:
//
//   * The spectrum is stored on a grid starting at w = 0, so the kernel runs
//     off the front. eps2 is ODD in w and (eps1 - 1) is EVEN, and reflecting
//     through zero with those parities is what keeps the low-energy edge
//     correct — without it the error there is ~3000x larger.
//
//   * The Lorentzian tail falls as 1/x^2, so a truncated kernel is missing real
//     weight. That weight must NOT be renormalized back in: the truncation is
//     part of the honest answer, and renormalizing measurably worsens the
//     agreement.

#include "gui/OpticsResultsWindow.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDoubleSpinBox>
#include <QTemporaryDir>

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <vector>

using calango::gui::OpticsResultsWindow;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

/// One oscillator: transition energy (eV) and strength.
struct Level {
    double energy;
    double strength;
};

const std::vector<Level> kLevels = {{3.0, 1.7}, {5.5, 0.9}, {8.2, 2.3}};

/// The analytic dielectric function of that oscillator set at broadening eta,
/// resonant + anti-resonant, which is the form the response module produces.
std::complex<double> analytic(double omega, double eta)
{
    std::complex<double> eps(1.0, 0.0);
    for (const Level& level : kLevels) {
        eps += level.strength
            / std::complex<double>(level.energy - omega, -eta);
        eps += level.strength
            / std::complex<double>(level.energy + omega, eta);
    }
    return eps;
}

/// Write an optics.json the viewer will load: the analytic spectrum at `eta`,
/// with the derived quantities the generator would have written.
void writeOptics(const QString& path, double eta, const std::vector<double>& grid,
                 double vacuumLengthA)
{
    QJsonArray energies;
    QJsonArray eps1;
    QJsonArray eps2;
    for (const double omega : grid) {
        const std::complex<double> eps = analytic(omega, eta);
        energies.append(omega);
        eps1.append(eps.real());
        eps2.append(eps.imag());
    }
    QJsonObject direction;
    direction[QStringLiteral("eps1")] = eps1;
    direction[QStringLiteral("eps2")] = eps2;

    QJsonObject root;
    root[QStringLiteral("energy_eV")] = energies;
    root[QStringLiteral("eta_eV")] = eta;
    root[QStringLiteral("xx")] = direction;
    if (vacuumLengthA > 0.0) {
        root[QStringLiteral("L_z_A")] = vacuumLengthA;
        // Presence of the twod_ block is what marks the run as 2D.
        QJsonObject sheet;
        QJsonArray zeros;
        for (std::size_t i = 0; i < grid.size(); ++i)
            zeros.append(0.0);
        sheet[QStringLiteral("absorbance")] = zeros;
        sheet[QStringLiteral("alpha_2D_re_A")] = zeros;
        sheet[QStringLiteral("alpha_2D_im_A")] = zeros;
        sheet[QStringLiteral("sigma_2D_re")] = zeros;
        sheet[QStringLiteral("sigma_2D_im")] = zeros;
        root[QStringLiteral("twod_xx")] = sheet;
    }

    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // A realistic optics grid: 0-20 eV, the resolution a wizard run produces.
    std::vector<double> grid;
    const int points = 2001;
    for (int i = 0; i < points; ++i)
        grid.push_back(20.0 * i / (points - 1));

    const double etaStored = 0.05;

    QTemporaryDir tmp;
    const QString job = tmp.path();
    writeOptics(job + QStringLiteral("/optics.json"), etaStored, grid,
                /*vacuumLengthA=*/20.0);

    std::printf("Loading and re-deriving:\n");
    OpticsResultsWindow window(job);
    check(window.hasData(), "the viewer read the spectrum");


    std::printf("\nBroadened spectra match the analytic form:\n");
    auto* broadening =
        window.findChild<QDoubleSpinBox*>(QStringLiteral("opticsBroadening"));
    check(broadening != nullptr, "the broadening control is present");
    if (!broadening) {
        std::printf("\n%d check(s) FAILED.\n", failures);
        return EXIT_FAILURE;
    }

    for (const double target : {0.10, 0.20, 0.35}) {
        broadening->setValue(target);
        const auto derived = window.derivedSpectra(0);
        if (derived.eps1.size() != grid.size()) {
            check(false, "the viewer re-derived a full spectrum");
            continue;
        }

        double worst1 = 0.0;
        double worst2 = 0.0;
        double peak1 = 0.0;
        double peak2 = 0.0;
        for (std::size_t i = 0; i < grid.size(); ++i) {
            // Away from both edges: the reflection makes w -> 0 exact, but the
            // top of the grid is genuinely missing the spectrum above it, and
            // no amount of care recovers information that was never stored.
            if (grid[i] < 0.5 || grid[i] > 15.0)
                continue;
            const std::complex<double> exact = analytic(grid[i], target);
            worst1 = std::max(worst1, std::abs(derived.eps1[i] - exact.real()));
            worst2 = std::max(worst2, std::abs(derived.eps2[i] - exact.imag()));
            peak1 = std::max(peak1, std::abs(exact.real()));
            peak2 = std::max(peak2, std::abs(exact.imag()));
        }
        const double rel1 = peak1 > 0.0 ? worst1 / peak1 : 1.0;
        const double rel2 = peak2 > 0.0 ? worst2 / peak2 : 1.0;
        std::printf("    eta=%.2f eV:  eps1 rel err %.2e   eps2 rel err %.2e\n",
                    target, rel1, rel2);
        check(rel1 < 5e-3 && rel2 < 5e-3,
              "the convolved spectrum reproduces the analytic one");
    }

    std::printf("\nThe derived curves follow the broadened epsilon:\n");
    {
        broadening->setValue(0.30);
        const auto d = window.derivedSpectra(0);
        bool consistent = d.n.size() == grid.size();
        // n and k must be sqrt(eps) of the CURRENT eps, not of the stored one.
        for (std::size_t i = 0; consistent && i < grid.size(); ++i) {
            const std::complex<double> refractive =
                std::sqrt(std::complex<double>(d.eps1[i], d.eps2[i]));
            consistent = std::abs(d.n[i] - refractive.real()) < 1e-9
                && std::abs(d.k[i] - refractive.imag()) < 1e-9;
        }
        check(consistent, "n and k are sqrt of the broadened eps, not stale");

        // The sheet observables too: A(w) = (w L_z / hbar c) eps2.
        bool sheetOk = d.absorbance.size() == grid.size();
        for (std::size_t i = 0; sheetOk && i < grid.size(); ++i) {
            const double expected =
                (grid[i] / 1973.269804) * 20.0 * d.eps2[i];
            sheetOk = std::abs(d.absorbance[i] - expected) < 1e-9;
        }
        check(sheetOk, "the 2D absorbance is re-derived from the broadened eps");
    }

    std::printf("\nBroadening is one-way:\n");
    check(std::abs(broadening->minimum() - etaStored) < 1e-9,
          "the control cannot go below the run's own eta");
    check(std::abs(window.storedBroadening() - etaStored) < 1e-9,
          "and the stored value is reported as the floor");

    std::printf(failures == 0 ? "\nAll optics broadening checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
