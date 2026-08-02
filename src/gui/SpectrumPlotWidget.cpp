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

bool SpectrumPlotWidget::renderTo(QPainter& p, QSize size) const
{
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(QRect(QPoint(0, 0), size), style_.canvasBackground);

    const double W = size.width();
    const double H = size.height();
    const QRectF plot(70.0, 24.0, W - 70.0 - 20.0, H - 24.0 - 52.0);

    if (x_.empty() || series_.empty() || plot.width() < 20.0
        || plot.height() < 20.0) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(QRect(QPoint(0, 0), size), Qt::AlignCenter,
                   QObject::tr("No data to display"));
        return false;
    }

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

    const auto mapX = [&](double v) {
        return plot.left() + (v - xMin) / (xMax - xMin) * plot.width();
    };
    const auto mapY = [&](double v) {
        return plot.bottom() - (v - yMin) / (yMax - yMin) * plot.height();
    };

    p.fillRect(plot, style_.plotBackground);

    // Grid, ticks and tick labels.
    const int ticks = 5;
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
        p.drawText(QRectF(2.0, py - 8.0, plot.left() - 8.0, 16.0),
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
    p.save();
    p.translate(16.0, plot.center().y());
    p.rotate(-90.0);
    p.drawText(QRectF(-plot.height() / 2.0, -8.0, plot.height(), 16.0),
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
