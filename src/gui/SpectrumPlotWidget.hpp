#pragma once

#include "gui/OpticsPlotStyleDialog.hpp"

#include <QColor>
#include <QPair>
#include <QString>
#include <QPainter>
#include <QWidget>

#include <vector>



namespace calango::gui {

/// A line chart over one shared x axis, styled by OpticsPlotStyle.
///
/// It was file-local to the Optics results window until the XAS module needed
/// exactly the same thing — an energy axis, one or more curves over it, a
/// legend and a PNG/SVG export. Rather than a second copy diverging from the
/// first, it lives here and both windows draw through it.
///
/// Not tied to optics despite the style struct's name: the style is a set of
/// pen widths, fonts and colors, which any spectrum wants.
class SpectrumPlotWidget : public QWidget {
public:
    explicit SpectrumPlotWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(480, 320);
    }

    void setStyle(const OpticsPlotStyle& style)
    {
        style_ = style;
        update();
    }

    /// Explicit x window. An empty (min >= max) range means "fit the data",
    /// which is the default.
    void setXRange(double minimum, double maximum)
    {
        xMinLimit_ = minimum;
        xMaxLimit_ = maximum;
        update();
    }

    void setSeries(const std::vector<double>& x,
                   const std::vector<QPair<QString, std::vector<double>>>& series,
                   const QString& xLabel, const QString& yLabel)
    {
        x_ = x;
        series_ = series;
        xLabel_ = xLabel;
        yLabel_ = yLabel;
        update();
    }

    /// Dashed horizontal reference lines — (label, y) pairs such as a Fermi
    /// level or a vacuum level. Annotations of the y axis rather than curves
    /// over x, so they are drawn dashed and labelled on the line itself, not
    /// in the legend. Included in the vertical autoscale, or a level just
    /// outside the curves' own range would silently be invisible. Empty by
    /// default: existing callers draw exactly what they always did.
    void setReferenceLines(const std::vector<QPair<QString, double>>& lines)
    {
        referenceLines_ = lines;
        update();
    }

    /// What the x axis carries, for the visible-spectrum overlay. The band's
    /// colour is a function of WAVELENGTH, so the widget has to know which
    /// quantity it is plotting against in order to place it.
    enum class SpectralAxis { EnergyEv, WavelengthNm };

    /// Shade the visible range (380–750 nm, i.e. 3.26–1.65 eV) behind the
    /// curves with a spectral gradient.
    ///
    /// `axis` is not cosmetic. Colour follows wavelength, and wavelength is
    /// 1/energy, so a two-stop red→violet gradient drawn across an ENERGY
    /// axis puts every colour in the wrong place — green would land at
    /// 2.45 eV (506 nm) where it belongs at 2.25 eV (550 nm). The band is
    /// therefore built from stops evaluated at their own x, in whichever
    /// quantity the axis is in.
    void setVisibleSpectrum(bool show, SpectralAxis axis)
    {
        showVisibleSpectrum_ = show;
        spectralAxis_ = axis;
        update();
    }

    /// Draw the chart into `painter` filling a logical area of `size`. Returns
    /// false (after drawing a placeholder) when there is nothing to plot.
    bool renderTo(QPainter& painter, QSize size) const;

    /// Photon energy (eV) of a wavelength in nm, and back. hc = 1239.841984
    /// eV·nm, which is why the visible band's bounds are quoted as both
    /// 380–750 nm and 3.26–1.65 eV: they are the same two numbers.
    static constexpr double kHcEvNm = 1239.841984;
    /// The visible range, as the CIE-ish convention has it.
    static constexpr double kVisibleMinNm = 380.0;
    static constexpr double kVisibleMaxNm = 750.0;

    /// Approximate sRGB for a spectral wavelength in nm.
    ///
    /// The standard piecewise fit over 380–750 nm: it is what "the colour of
    /// that wavelength" means to a viewer, not a colorimetrically exact
    /// rendering (no monitor can show a monochromatic line anyway). Outside
    /// the visible range it returns black, which the band never asks for
    /// because it is clipped to that range.
    static QColor wavelengthColor(double nm);


protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        renderTo(painter, size());
    }

private:
    static QColor seriesColor(int index)
    {
        static const QColor palette[] = {
            QColor(0x1f, 0x77, 0xb4), QColor(0xd6, 0x27, 0x28),
            QColor(0x2c, 0xa0, 0x2c), QColor(0xff, 0x7f, 0x0e),
            QColor(0x94, 0x67, 0xbd), QColor(0x8c, 0x56, 0x4b),
        };
        const int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
        return palette[((index % n) + n) % n];
    }

    bool showVisibleSpectrum_ = false;
    SpectralAxis spectralAxis_ = SpectralAxis::EnergyEv;

    std::vector<double> x_;
    std::vector<QPair<QString, std::vector<double>>> series_;
    std::vector<QPair<QString, double>> referenceLines_;
    QString xLabel_;
    QString yLabel_;
    OpticsPlotStyle style_;
    double xMinLimit_ = 0.0;
    double xMaxLimit_ = 0.0;
};

} // namespace calango::gui
