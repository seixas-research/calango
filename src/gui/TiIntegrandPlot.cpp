#include "gui/TiIntegrandPlot.hpp"

#include "gui/PlotPalette.hpp"

#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

/// Ticks at 1, 2 or 5 times a power of ten — the spacings people read without
/// having to decode them.
double niceStep(double span, int target)
{
    if (span <= 0.0 || target <= 0)
        return 1.0;
    const double raw = span / target;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double normalized = raw / magnitude;
    double step = 10.0;
    if (normalized <= 1.0)
        step = 1.0;
    else if (normalized <= 2.0)
        step = 2.0;
    else if (normalized <= 5.0)
        step = 5.0;
    return step * magnitude;
}

} // namespace

TiIntegrandPlot::TiIntegrandPlot(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(360, 240);
}

void TiIntegrandPlot::setWindows(std::vector<core::TiWindowSample> forward,
                                 std::vector<core::TiWindowSample> backward)
{
    forward_ = std::move(forward);
    backward_ = std::move(backward);
    const auto byLambda = [](const core::TiWindowSample& a,
                             const core::TiWindowSample& b) {
        return a.lambda < b.lambda;
    };
    std::sort(forward_.begin(), forward_.end(), byLambda);
    std::sort(backward_.begin(), backward_.end(), byLambda);
    update();
}

void TiIntegrandPlot::setEndpointWarning(bool suspected, const QString& message)
{
    endpointSuspected_ = suspected;
    endpointMessage_ = message;
    update();
}

void TiIntegrandPlot::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    render(painter, QRectF(rect()));
}

void TiIntegrandPlot::render(QPainter& painter, const QRectF& bounds) const
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(bounds, PlotPalette::canvas);

    const double scale = bounds.width() / 640.0;
    const QRectF plot = bounds.adjusted(84.0 * scale, 20.0 * scale,
                                        -18.0 * scale, -50.0 * scale);
    if (plot.width() <= 10.0 || plot.height() <= 10.0)
        return;

    QFont font = painter.font();
    font.setPointSizeF(std::max(6.0, 9.0 * scale));
    painter.setFont(font);

    if (forward_.empty()) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(plot, Qt::AlignCenter,
                         tr("No window averages to plot."));
        return;
    }

    // -- Range. The error bars are part of the data, so they set it too; a
    // window whose uncertainty runs off the top is exactly the one worth
    // seeing.
    double yMin = 0.0;
    double yMax = 0.0;
    bool first = true;
    const auto extend = [&](const std::vector<core::TiWindowSample>& set) {
        for (const auto& w : set) {
            if (!w.ok)
                continue;
            const double lo = w.dudlEv - w.dudlErrorEv;
            const double hi = w.dudlEv + w.dudlErrorEv;
            if (first) {
                yMin = lo;
                yMax = hi;
                first = false;
            }
            yMin = std::min(yMin, lo);
            yMax = std::max(yMax, hi);
        }
    };
    extend(forward_);
    extend(backward_);
    if (first)
        return;
    const double pad = 0.08 * (yMax - yMin > 0.0 ? yMax - yMin : 1.0);
    yMin -= pad;
    yMax += pad;

    const auto toScreen = [&](double x, double y) {
        return QPointF(plot.left() + x * plot.width(),
                       plot.bottom() - (y - yMin) / (yMax - yMin) * plot.height());
    };

    // -- Grid --------------------------------------------------------------
    const double yStep = niceStep(yMax - yMin, 5);
    painter.setPen(QPen(PlotPalette::grid, 1.0));
    for (int i = 0; i <= 10; ++i)
        painter.drawLine(toScreen(i / 10.0, yMin), toScreen(i / 10.0, yMax));
    for (double y = std::ceil(yMin / yStep) * yStep; y <= yMax; y += yStep)
        painter.drawLine(toScreen(0.0, y), toScreen(1.0, y));

    painter.setPen(QPen(PlotPalette::tickText, 1.0));
    for (int i = 0; i <= 10; i += 2) {
        const QPointF at = toScreen(i / 10.0, yMin);
        painter.drawText(QRectF(at.x() - 26.0 * scale, at.y() + 4.0 * scale,
                                52.0 * scale, 17.0 * scale),
                         Qt::AlignHCenter | Qt::AlignTop,
                         QString::number(i / 10.0, 'f', 1));
    }
    for (double y = std::ceil(yMin / yStep) * yStep; y <= yMax; y += yStep) {
        const QPointF at = toScreen(0.0, y);
        painter.drawText(QRectF(plot.left() - 80.0 * scale, at.y() - 9.0 * scale,
                                74.0 * scale, 18.0 * scale),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(y, 'g', 4));
    }

    // -- The integral, shaded ----------------------------------------------
    // Delta_F IS the area under this curve. Shading it is not decoration: it
    // is the one place a reader can see that most of the answer came from one
    // end of the path, which is the signature of an endpoint problem.
    {
        QPainterPath area;
        const double baseline = std::clamp(0.0, yMin, yMax);
        area.moveTo(toScreen(forward_.front().lambda, baseline));
        for (const auto& w : forward_)
            if (w.ok)
                area.lineTo(toScreen(w.lambda, w.dudlEv));
        area.lineTo(toScreen(forward_.back().lambda, baseline));
        area.closeSubpath();
        QColor fill = PlotPalette::series;
        fill.setAlpha(34);
        painter.fillPath(area, fill);
    }

    painter.setPen(QPen(PlotPalette::spine, 1.4));
    painter.drawRect(plot);

    // -- Curves and error bars ---------------------------------------------
    const auto drawSeries = [&](const std::vector<core::TiWindowSample>& set,
                                const QColor& colour, bool dashed) {
        if (set.empty())
            return;
        painter.setPen(QPen(colour, 2.0, dashed ? Qt::DashLine : Qt::SolidLine));
        QPainterPath path;
        bool started = false;
        for (const auto& w : set) {
            if (!w.ok)
                continue;
            const QPointF p = toScreen(w.lambda, w.dudlEv);
            if (!started) {
                path.moveTo(p);
                started = true;
            } else {
                path.lineTo(p);
            }
        }
        painter.drawPath(path);

        for (const auto& w : set) {
            if (!w.ok)
                continue;
            const QPointF p = toScreen(w.lambda, w.dudlEv);
            // 1 sigma, autocorrelation-corrected. The raw variance of a
            // correlated MD series understates this by sqrt(tau_int), which is
            // precisely the factor a TI module exists to get right.
            if (w.dudlErrorEv > 0.0) {
                const QPointF lo = toScreen(w.lambda, w.dudlEv - w.dudlErrorEv);
                const QPointF hi = toScreen(w.lambda, w.dudlEv + w.dudlErrorEv);
                painter.setPen(QPen(colour, 1.2));
                painter.drawLine(lo, hi);
                const double cap = 4.0 * scale;
                painter.drawLine(lo + QPointF(-cap, 0), lo + QPointF(cap, 0));
                painter.drawLine(hi + QPointF(-cap, 0), hi + QPointF(cap, 0));
            }
            painter.setPen(QPen(colour, 1.4));
            painter.setBrush(PlotPalette::canvas);
            painter.drawEllipse(p, 3.2 * scale, 3.2 * scale);
        }
        painter.setBrush(Qt::NoBrush);
    };
    drawSeries(forward_, PlotPalette::series, false);
    drawSeries(backward_, PlotPalette::seriesAlt, true);

    // Windows that FAILED are marked where they should have been, so a gap in
    // the path is visible as a gap rather than as a shorter curve.
    painter.setPen(QPen(PlotPalette::seriesAlt, 1.6));
    for (const auto& w : forward_) {
        if (w.ok)
            continue;
        const double x = plot.left() + w.lambda * plot.width();
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }

    // -- The endpoint diagnosis, on the plot --------------------------------
    if (endpointSuspected_) {
        QColor band = PlotPalette::seriesAlt;
        band.setAlpha(26);
        // The end windows are where it bites; shade the outer tenth of the
        // path rather than pointing at one node.
        painter.fillRect(QRectF(plot.left(), plot.top(), plot.width() * 0.1,
                                plot.height()),
                         band);
        painter.fillRect(QRectF(plot.right() - plot.width() * 0.1, plot.top(),
                                plot.width() * 0.1, plot.height()),
                         band);
        painter.setPen(PlotPalette::seriesAlt);
        painter.drawText(QRectF(plot.left() + 6.0 * scale,
                                plot.top() + 4.0 * scale, plot.width() - 12.0 * scale,
                                18.0 * scale),
                         Qt::AlignLeft | Qt::AlignTop,
                         tr("endpoint singularity suspected"));
    }

    // -- Legend --------------------------------------------------------------
    if (!backward_.empty()) {
        const QFontMetricsF metrics(painter.font());
        const double x = plot.right() - 130.0 * scale;
        double y = plot.top() + 8.0 * scale;
        painter.fillRect(QRectF(x - 6.0 * scale, y - 4.0 * scale,
                                126.0 * scale, 40.0 * scale),
                         PlotPalette::readoutFill);
        painter.setPen(QPen(PlotPalette::grid, 1.0));
        painter.drawRect(QRectF(x - 6.0 * scale, y - 4.0 * scale,
                                126.0 * scale, 40.0 * scale));
        const auto row = [&](const QColor& c, bool dashed, const QString& label) {
            painter.setPen(QPen(c, 2.0, dashed ? Qt::DashLine : Qt::SolidLine));
            painter.drawLine(QPointF(x, y + 8.0 * scale),
                             QPointF(x + 20.0 * scale, y + 8.0 * scale));
            painter.setPen(PlotPalette::text);
            painter.drawText(QPointF(x + 26.0 * scale, y + 12.0 * scale), label);
            y += 17.0 * scale;
        };
        row(PlotPalette::series, false, tr("forward"));
        row(PlotPalette::seriesAlt, true, tr("backward"));
    }

    // -- Axis labels ---------------------------------------------------------
    painter.setPen(PlotPalette::text);
    painter.drawText(QRectF(plot.left(), bounds.bottom() - 26.0 * scale,
                            plot.width(), 20.0 * scale),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     tr("coupling parameter  λ"));
    painter.save();
    painter.translate(bounds.left() + 15.0 * scale, plot.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-plot.height() / 2.0, -10.0 * scale, plot.height(),
                            20.0 * scale),
                     Qt::AlignCenter, tr("⟨∂U/∂λ⟩  (eV)"));
    painter.restore();
}

bool TiIntegrandPlot::exportImage(const QString& path, double scale) const
{
    const QSize size(static_cast<int>(width() * scale),
                     static_cast<int>(height() * scale));
    if (size.isEmpty())
        return false;
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(PlotPalette::canvas);
    QPainter painter(&image);
    // Through the SAME render(), so the file is the figure on screen.
    render(painter, QRectF(QPointF(0, 0), QSizeF(size)));
    painter.end();
    return image.save(path);
}

} // namespace calango::gui
