// The visible-spectrum overlay in the optics viewer.
//
// A gradient is one of the few things a review genuinely cannot check: the
// code compiles and draws *something* whether or not the colours land where
// the physics puts them. So this renders the widget and reads pixels back.
//
// The claim under test is not "a coloured band appears". It is that the band
// spans 380-750 nm (= 3.26-1.65 eV) on whichever axis is shown, that the
// colours run red at low energy to violet at high energy, and — the part that
// is easy to get wrong and invisible afterwards — that the placement is
// NON-LINEAR on an energy axis, because colour follows wavelength and
// wavelength goes as 1/E.

#include "gui/SpectrumPlotWidget.hpp"

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

using calango::gui::SpectrumPlotWidget;

/// Render a plot of `series` over [xMin, xMax] with the band on or off.
QImage render(bool band, SpectrumPlotWidget::SpectralAxis axis, double xMin,
              double xMax)
{
    SpectrumPlotWidget widget;
    std::vector<double> x;
    std::vector<double> y;
    for (int i = 0; i <= 200; ++i) {
        x.push_back(xMin + (xMax - xMin) * i / 200.0);
        y.push_back(1.0); // flat, so the band is what varies
    }
    widget.setSeries(x, {{QStringLiteral("s"), y}}, QStringLiteral("x"),
                     QStringLiteral("y"));
    widget.setXRange(xMin, xMax);
    widget.setVisibleSpectrum(band, axis);
    QImage image(700, 400, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    widget.renderTo(painter, image.size());
    painter.end();
    return image;
}

/// Pixel column for an x value, matching renderTo's plot rectangle.
int columnFor(double value, double xMin, double xMax, int width)
{
    const double left = 70.0;
    const double right = width - 20.0;
    return static_cast<int>(std::lround(
        left + (value - xMin) / (xMax - xMin) * (right - left)));
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // -- 1. The colour of a wavelength -------------------------------------
    std::printf("Wavelength to colour:\n");
    const QColor red = SpectrumPlotWidget::wavelengthColor(700.0);
    const QColor green = SpectrumPlotWidget::wavelengthColor(550.0);
    const QColor blue = SpectrumPlotWidget::wavelengthColor(450.0);
    const QColor violet = SpectrumPlotWidget::wavelengthColor(400.0);
    check(red.red() > red.green() && red.red() > red.blue(),
          "700 nm is red-dominant");
    check(green.green() > green.red() && green.green() > green.blue(),
          "550 nm is green-dominant");
    check(blue.blue() > blue.red(), "450 nm is blue-dominant");
    check(violet.blue() > violet.green() && violet.red() > violet.green(),
          "400 nm is violet (blue with a red component), not pure blue");
    // Hue must advance monotonically red -> violet across the range. Qt's hue
    // wheel puts red at 0 and violet near 280, so sampling from long to short
    // wavelength should climb.
    bool monotonic = true;
    int previous = -1;
    for (double nm = 680.0; nm >= 420.0; nm -= 20.0) {
        const int hue = SpectrumPlotWidget::wavelengthColor(nm).hue();
        if (previous >= 0 && hue < previous)
            monotonic = false;
        previous = hue;
    }
    check(monotonic, "hue advances monotonically from red to violet");

    // -- 2. The band is drawn, and only inside the visible range -----------
    std::printf("Band placement on an energy axis (0-6 eV):\n");
    const double xMin = 0.0;
    const double xMax = 6.0;
    const QImage without =
        render(false, SpectrumPlotWidget::SpectralAxis::EnergyEv, xMin, xMax);
    const QImage with =
        render(true, SpectrumPlotWidget::SpectralAxis::EnergyEv, xMin, xMax);
    check(without.size() == with.size(), "both renders share a size");

    const int row = 200; // mid-height, inside the plot rectangle
    const int colRed = columnFor(1.75, xMin, xMax, with.width());   // in band
    const int colViolet = columnFor(3.15, xMin, xMax, with.width()); // in band
    const int colIR = columnFor(1.0, xMin, xMax, with.width());     // below
    const int colUV = columnFor(5.0, xMin, xMax, with.width());     // above

    check(with.pixelColor(colRed, row) != without.pixelColor(colRed, row),
          "the band changes pixels inside the visible range");
    check(with.pixelColor(colIR, row) == without.pixelColor(colIR, row),
          "and leaves the infrared side untouched (below 1.65 eV)");
    check(with.pixelColor(colUV, row) == without.pixelColor(colUV, row),
          "and the ultraviolet side too (above 3.26 eV)");

    // -- 3. Red at low energy, violet at high energy -----------------------
    const QColor lowE = with.pixelColor(colRed, row);
    const QColor highE = with.pixelColor(colViolet, row);
    check(lowE.red() > lowE.blue(),
          "the low-energy end of the band is red-dominant");
    check(highE.blue() > highE.red(),
          "and the high-energy end is blue/violet-dominant");

    // -- 4. The non-linearity ----------------------------------------------
    //
    // The whole reason the band is built from per-position stops. Green
    // (550 nm) is 2.254 eV. On a linear red->violet ramp across 1.65-3.26 eV
    // it would sit at the midpoint, 2.455 eV (505 nm — cyan). So the greenest
    // column must be nearer 2.25 eV than 2.46 eV; if it is not, the gradient
    // was laid out in the wrong variable.
    // Tested by HUE at named energies rather than by hunting for "the greenest
    // column": the fit's most saturated green is ~505 nm (pure green, no red
    // component), not 550 nm, so a 2G-R-B score finds cyan and says nothing
    // about placement.
    //
    // The band is alpha-blended over the plot background, which lowers
    // saturation but preserves hue — so hue is the invariant to compare.
    const double hc = 1239.841984;
    bool placementCorrect = true;
    bool discriminates = false;
    for (const double ev : {1.80, 2.10, 2.254, 2.60, 3.10}) {
        const int column = columnFor(ev, xMin, xMax, with.width());
        const int rendered = with.pixelColor(column, row).hue();
        // What the colour SHOULD be: the colour of this x's own wavelength.
        const int expected = SpectrumPlotWidget::wavelengthColor(hc / ev).hue();
        // What a naive linear red->violet ramp across the band would put here.
        const double t = (ev - hc / 750.0) / (hc / 380.0 - hc / 750.0);
        const int naive = SpectrumPlotWidget::wavelengthColor(
                              750.0 + t * (380.0 - 750.0)).hue();
        const int errCorrect = std::abs(rendered - expected);
        const int errNaive = std::abs(rendered - naive);
        std::printf("    %.3f eV (%3.0f nm): hue %3d, wavelength-correct %3d, "
                    "linear-ramp %3d\n",
                    ev, hc / ev, rendered, expected, naive);
        if (errCorrect > 12)
            placementCorrect = false;
        // At least one probe must separate the two hypotheses, or the test
        // would pass on a plot where they happen to coincide.
        if (std::abs(expected - naive) > 20)
            discriminates = true;
    }
    check(placementCorrect,
          "every sampled column carries the colour of ITS OWN wavelength");
    check(discriminates,
          "and the probes distinguish that from a linear red-to-violet ramp, "
          "which would place the same colours elsewhere");

    // -- 5. The wavelength axis ---------------------------------------------
    std::printf("Band placement on a wavelength axis (300-900 nm):\n");
    const QImage nmWithout = render(
        false, SpectrumPlotWidget::SpectralAxis::WavelengthNm, 300.0, 900.0);
    const QImage nmWith = render(
        true, SpectrumPlotWidget::SpectralAxis::WavelengthNm, 300.0, 900.0);
    const int colNmRed = columnFor(700.0, 300.0, 900.0, nmWith.width());
    const int colNmViolet = columnFor(400.0, 300.0, 900.0, nmWith.width());
    const int colNmIR = columnFor(850.0, 300.0, 900.0, nmWith.width());
    check(nmWith.pixelColor(colNmIR, row) == nmWithout.pixelColor(colNmIR, row),
          "beyond 750 nm is untouched on a wavelength axis");
    const QColor nmRed = nmWith.pixelColor(colNmRed, row);
    const QColor nmViolet = nmWith.pixelColor(colNmViolet, row);
    check(nmRed.red() > nmRed.blue(), "700 nm is red-dominant on that axis");
    check(nmViolet.blue() > nmViolet.red(),
          "and 400 nm violet — the band reverses with the axis, as it must");

    // -- 6. Semi-transparent -----------------------------------------------
    // The curves are the data. A band that hid them would be a downgrade.
    check(lowE != SpectrumPlotWidget::wavelengthColor(1239.841984 / 1.75),
          "the band is blended with the background, not painted opaque");

    if (failures == 0) {
        std::printf("\nAll visible-spectrum checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d visible-spectrum check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
