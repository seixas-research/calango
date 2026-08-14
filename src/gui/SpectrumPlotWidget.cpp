#include "gui/SpectrumPlotWidget.hpp"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace calango::gui {

QColor SpectrumPlotWidget::wavelengthColor(double nm)
{
    // The usual piecewise linear fit to the spectral locus. Six bands, each
    // ramping one channel, which is what produces the red → orange → yellow →
    // green → cyan → blue → violet sequence a reader expects.
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    if (nm >= 380.0 && nm < 440.0) {
        r = -(nm - 440.0) / (440.0 - 380.0);
        b = 1.0;
    } else if (nm < 490.0) {
        g = (nm - 440.0) / (490.0 - 440.0);
        b = 1.0;
    } else if (nm < 510.0) {
        g = 1.0;
        b = -(nm - 510.0) / (510.0 - 490.0);
    } else if (nm < 580.0) {
        r = (nm - 510.0) / (580.0 - 510.0);
        g = 1.0;
    } else if (nm < 645.0) {
        r = 1.0;
        g = -(nm - 645.0) / (645.0 - 580.0);
    } else if (nm <= 750.0) {
        r = 1.0;
    } else {
        return QColor(0, 0, 0);
    }
    // Intensity falls off at both ends of the visible range — the eye's
    // response does, so a band that stayed saturated to the last nanometre
    // would read as a hard edge where the physics has a fade.
    double intensity = 1.0;
    if (nm < 420.0)
        intensity = 0.30 + 0.70 * (nm - 380.0) / (420.0 - 380.0);
    else if (nm > 700.0)
        intensity = 0.30 + 0.70 * (750.0 - nm) / (750.0 - 700.0);
    const auto channel = [intensity](double v) {
        // 0.8 gamma, the conventional companion to this fit.
        return static_cast<int>(std::lround(255.0 * std::pow(
            std::clamp(v * intensity, 0.0, 1.0), 0.8)));
    };
    return QColor(channel(r), channel(g), channel(b));
}

bool SpectrumPlotWidget::renderTo(QPainter& p, QSize size) const
{
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(QRect(QPoint(0, 0), size), style_.canvasBackground);

    const double W = size.width();
    const double H = size.height();

    const auto drawEmpty = [&] {
        p.setPen(QColor(120, 120, 120));
        p.drawText(QRect(QPoint(0, 0), size), Qt::AlignCenter,
                   QObject::tr("No data to display"));
        return false;
    };

    // Ranges are needed BEFORE the plot rect now, because the left margin is
    // derived from the tick labels and those depend on the y range.
    if (x_.empty() || series_.empty())
        return drawEmpty();

    // Data ranges (ignoring non-finite samples such as poles in the loss fn).
    double xMin = x_.front();
    double xMax = x_.front();
    for (double v : x_) {
        xMin = std::min(xMin, v);
        xMax = std::max(xMax, v);
    }
    // A user-set window overrides the data extent.
    const bool clipped = xMaxLimit_ > xMinLimit_;
    if (clipped) {
        xMin = xMinLimit_;
        xMax = xMaxLimit_;
    }
    // The vertical scale follows what is VISIBLE: scanning the whole series
    // while showing a slice of it would leave the curve flattened against the
    // axis by a peak that is off-screen.
    double yMin = std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    for (const auto& s : series_)
        for (std::size_t i = 0; i < s.second.size() && i < x_.size(); ++i) {
            if (clipped && (x_[i] < xMin || x_[i] > xMax))
                continue;
            const double v = s.second[i];
            if (std::isfinite(v)) {
                yMin = std::min(yMin, v);
                yMax = std::max(yMax, v);
            }
        }
    // Reference lines take part in the vertical autoscale: a vacuum level a
    // little above the curve's own maximum must widen the range, not vanish.
    for (const auto& line : referenceLines_) {
        if (std::isfinite(line.second)) {
            yMin = std::min(yMin, line.second);
            yMax = std::max(yMax, line.second);
        }
    }
    if (!(xMax > xMin))
        xMax = xMin + 1.0;
    if (!std::isfinite(yMin) || !std::isfinite(yMax)) {
        yMin = 0.0;
        yMax = 1.0;
    }
    if (!(yMax > yMin))
        yMax = yMin + 1.0;
    const double pad = (yMax - yMin) * 0.06;
    yMin -= pad;
    yMax += pad;

    // Left margin measured rather than assumed.
    //
    // It carries two things side by side: the rotated y-axis title, and the
    // column of tick labels right-aligned against the axis. A fixed 70 px held
    // neither reliably — tick values print in scientific notation, and
    // "2.673e+06" is about 62 px in the default axis font, so the numbers grew
    // leftward into the strip the title is drawn in and the two overlapped,
    // striking the title through the numbers (an absorption spectrum in cm⁻¹
    // reaches 1e6 routinely, so this was the common case, not the corner).
    //
    // Measuring the labels that will actually be drawn fixes it for any
    // combination of range, unit and axis font, including a user-chosen one.
    const int ticks = 5;
    const QFontMetricsF axisMetrics(style_.axisFont());
    double widestTick = 0.0;
    for (int i = 0; i <= ticks; ++i) {
        const double fy = yMin + (yMax - yMin) * i / ticks;
        widestTick = std::max(
            widestTick,
            axisMetrics.horizontalAdvance(QString::number(fy, 'g', 4)));
    }
    // The rotated title is turned on its side, so the width it needs is its
    // font HEIGHT. Zero when there is no title, so an untitled plot pays
    // nothing for one.
    const double titleBand =
        yLabel_.isEmpty() ? 0.0 : axisMetrics.height() + 4.0;
    // 4 px between title band and numbers, 6 px between numbers and the axis.
    // Floored at the old 70 so short labels keep the familiar proportions.
    const double leftMargin =
        std::max(70.0, titleBand + 4.0 + widestTick + 6.0);

    const QRectF plot(leftMargin, 24.0, W - leftMargin - 20.0,
                      H - 24.0 - 52.0);
    if (plot.width() < 20.0 || plot.height() < 20.0)
        return drawEmpty();

    const auto mapX = [&](double v) {
        return plot.left() + (v - xMin) / (xMax - xMin) * plot.width();
    };
    const auto mapY = [&](double v) {
        return plot.bottom() - (v - yMin) / (yMax - yMin) * plot.height();
    };

    p.fillRect(plot, style_.plotBackground);

    // -- Visible-spectrum band ---------------------------------------------
    // Behind the grid and the curves: it is context for the spectrum, not a
    // series, and anything drawn over the data would cost readability for
    // decoration.
    if (showVisibleSpectrum_) {
        // The visible range expressed in whatever the x axis carries.
        const bool energyAxis = spectralAxis_ == SpectralAxis::EnergyEv;
        const double bandLo = energyAxis ? kHcEvNm / kVisibleMaxNm  // 1.65 eV
                                         : kVisibleMinNm;          // 380 nm
        const double bandHi = energyAxis ? kHcEvNm / kVisibleMinNm  // 3.26 eV
                                         : kVisibleMaxNm;          // 750 nm
        // Clipped to what is on screen — the band is usually a slice of a
        // 0–20 eV window, and a run plotted over the UV alone gets no band
        // rather than a misleading edge-to-edge wash.
        const double lo = std::max(bandLo, xMin);
        const double hi = std::min(bandHi, xMax);
        if (hi > lo) {
            const QRectF band(mapX(lo), plot.top(), mapX(hi) - mapX(lo),
                              plot.height());
            QLinearGradient gradient(band.left(), 0.0, band.right(), 0.0);
            // Stops evaluated at their OWN x rather than two end stops: the
            // colour follows wavelength and wavelength is 1/energy, so a
            // straight red→violet ramp across an energy axis puts green at
            // 2.45 eV where it belongs at 2.25 eV. Sampling handles either
            // axis without the mapping being written twice.
            constexpr int kStops = 48;
            for (int i = 0; i <= kStops; ++i) {
                const double t = static_cast<double>(i) / kStops;
                const double xv = lo + t * (hi - lo);
                const double nm = energyAxis ? kHcEvNm / xv : xv;
                QColor color = wavelengthColor(nm);
                // Semi-transparent: the curves are the data and have to stay
                // legible through it.
                color.setAlpha(70);
                gradient.setColorAt(t, color);
            }
            p.fillRect(band, gradient);
        }
    }

    // Grid, ticks and tick labels.
    for (int i = 0; i <= ticks; ++i) {
        const double fx = xMin + (xMax - xMin) * i / ticks;
        const double px = mapX(fx);
        if (style_.showGrid) {
            p.setPen(QPen(style_.effectiveGridColor(), 1.0));
            p.drawLine(QPointF(px, plot.top()), QPointF(px, plot.bottom()));
        }
        p.setFont(style_.axisFont());
        p.setPen(style_.axisLabelColor);
        p.drawText(QRectF(px - 40.0, plot.bottom() + 4.0, 80.0, 16.0),
                   Qt::AlignHCenter | Qt::AlignTop, QString::number(fx, 'g', 4));

        const double fy = yMin + (yMax - yMin) * i / ticks;
        const double py = mapY(fy);
        if (style_.showGrid) {
            p.setPen(QPen(style_.effectiveGridColor(), 1.0));
            p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
        }
        p.setPen(style_.axisLabelColor);
        // Starts clear of the title band and stops 6 px short of the axis, so
        // the widest label measured above lands exactly inside this box.
        p.drawText(QRectF(titleBand + 4.0, py - 8.0,
                          plot.left() - titleBand - 10.0, 16.0),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(fy, 'g', 4));
    }

    // ω = 0 (or y = 0) reference line when the range straddles zero.
    if (yMin < 0.0 && yMax > 0.0) {
        p.setPen(QPen(QColor(150, 150, 150), 1.0, Qt::DashLine));
        const double py = mapY(0.0);
        p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
    }

    // Axis frame on top of the grid.
    p.setPen(QPen(QColor(60, 60, 60), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);

    // The curves, clipped to the plot rectangle.
    p.save();
    p.setClipRect(plot);
    for (std::size_t si = 0; si < series_.size(); ++si) {
        const std::vector<double>& y = series_[si].second;
        QPen pen(style_.overrideCurveColor ? style_.curveColor
                                           : seriesColor(static_cast<int>(si)));
        pen.setWidthF(style_.lineWidth);
        pen.setStyle(style_.lineStyle);
        p.setPen(pen);
        QPolygonF poly;
        const std::size_t n = std::min(x_.size(), y.size());
        poly.reserve(static_cast<int>(n));
        for (std::size_t i = 0; i < n; ++i) {
            if (!std::isfinite(y[i]))
                continue;
            poly << QPointF(mapX(x_[i]), mapY(y[i]));
        }
        p.drawPolyline(poly);
    }

    // Dashed horizontal reference lines, labelled on the line itself: they
    // annotate the y axis (a Fermi level, a vacuum level), so putting them in
    // the legend would present them as one more curve, which they are not.
    for (const auto& line : referenceLines_) {
        if (!std::isfinite(line.second))
            continue;
        const double py = mapY(line.second);
        p.setPen(QPen(QColor(90, 90, 90), 1.2, Qt::DashLine));
        p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
        p.setFont(style_.axisFont());
        p.drawText(QRectF(plot.left() + 6.0, py - 16.0, 200.0, 14.0),
                   Qt::AlignLeft | Qt::AlignVCenter, line.first);
    }
    p.restore();

    // Axis titles, in the configured face and colour.
    p.setFont(style_.axisFont());
    p.setPen(style_.axisLabelColor);
    p.drawText(QRectF(plot.left(), H - 20.0, plot.width(), 18.0),
               Qt::AlignHCenter, xLabel_);
    // Centred in the band reserved for it above, not at a fixed x — that
    // constant is what the tick labels used to be drawn over.
    p.save();
    p.translate(titleBand / 2.0, plot.center().y());
    p.rotate(-90.0);
    p.drawText(QRectF(-plot.height() / 2.0, -axisMetrics.height() / 2.0,
                      plot.height(), axisMetrics.height()),
               Qt::AlignHCenter, yLabel_);
    p.restore();

    // Legend, top-right inside the plot.
    QFontMetrics fm(p.font());
    int labelWidth = 0;
    for (const auto& s : series_)
        labelWidth = std::max(labelWidth, fm.horizontalAdvance(s.first));
    const double boxW = labelWidth + 34.0;
    const double boxH = series_.size() * 16.0 + 8.0;
    const QRectF legend(plot.right() - boxW - 8.0, plot.top() + 8.0, boxW, boxH);
    p.setBrush(QColor(255, 255, 255, 220));
    p.setPen(QColor(185, 185, 185));
    p.drawRect(legend);
    for (std::size_t si = 0; si < series_.size(); ++si) {
        const double ly = legend.top() + 6.0 + si * 16.0;
        p.setPen(QPen(seriesColor(static_cast<int>(si)), 2.4));
        p.drawLine(QPointF(legend.left() + 6.0, ly + 6.0),
                   QPointF(legend.left() + 24.0, ly + 6.0));
        p.setPen(QColor(30, 30, 30));
        p.drawText(QRectF(legend.left() + 28.0, ly, labelWidth + 4.0, 14.0),
                   Qt::AlignLeft | Qt::AlignVCenter, series_[si].first);
    }
    return true;
}

} // namespace calango::gui
