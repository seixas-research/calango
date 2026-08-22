#include "gui/MultiSeriesPlotWidget.hpp"
#include "gui/GuiUtils.hpp"

#include "gui/PlotPalette.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace calango::gui {

namespace {

/// The matplotlib "tab10" family, same convention MetricPlotWidget's four
/// tabs already draw with (PlotPalette::series/seriesAlt are its first two
/// entries) — a Calango figure and a matplotlib one from the same exported
/// data read as the same plot. Six is enough for GO-MDMC's "overall" series
/// plus its five move kinds; a widget asked to draw a seventh simply repeats
/// from the top rather than failing.
const std::vector<QColor>& tab10()
{
    static const std::vector<QColor> palette = {
        QColor(0x1f, 0x77, 0xb4), // blue
        QColor(0xd6, 0x27, 0x28), // red
        QColor(0x2c, 0xa0, 0x2c), // green
        QColor(0x94, 0x67, 0xbd), // purple
        QColor(0xff, 0x7f, 0x0e), // orange
        QColor(0x8c, 0x56, 0x4b), // brown
        QColor(0xe3, 0x77, 0xc2), // pink
        QColor(0x7f, 0x7f, 0x7f), // gray
    };
    return palette;
}

int decimalsForStep(double step)
{
    if (step <= 0.0)
        return 0;
    const int digits = static_cast<int>(std::ceil(-std::log10(step)));
    return std::clamp(digits, 0, 6);
}

} // namespace

MultiSeriesPlotWidget::MultiSeriesPlotWidget(PlotSpec spec, QWidget* parent)
    : QWidget(parent)
    , spec_(std::move(spec))
{
    // Three label rows below the axis, plus a legend row above the frame —
    // matches MetricPlotWidget's own budget with headroom for the legend.
    setMinimumHeight(170);
}

void MultiSeriesPlotWidget::clear()
{
    series_.clear();
    seriesOrder_.clear();
    update();
}

QColor MultiSeriesPlotWidget::colorFor(const QString& name) const
{
    auto it = colors_.find(name);
    if (it != colors_.end())
        return it->second;
    const auto& palette = tab10();
    const QColor color = palette[colors_.size() % palette.size()];
    colors_.emplace(name, color);
    return color;
}

void MultiSeriesPlotWidget::setSeries(
    const std::map<QString, std::vector<Sample>>& series)
{
    // New series names get a color (and a legend slot) in the order they are
    // first seen, appended once and never reassigned — see colorFor()'s own
    // doc comment on why stability matters here.
    for (const auto& [name, samples] : series) {
        if (samples.empty())
            continue;
        if (std::find(seriesOrder_.begin(), seriesOrder_.end(), name)
            == seriesOrder_.end()) {
            seriesOrder_.push_back(name);
            colorFor(name); // reserve this name's color slot now
        }
    }
    series_ = series;
    update();
}

void MultiSeriesPlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), PlotPalette::canvas);

    // At least one series with at least two points, or there is nothing to
    // draw a line through.
    int firstStep = std::numeric_limits<int>::max();
    int lastStep = std::numeric_limits<int>::min();
    bool anyDrawable = false;
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (const QString& name : seriesOrder_) {
        const auto it = series_.find(name);
        if (it == series_.end() || it->second.size() < 2)
            continue;
        anyDrawable = true;
        for (const Sample& sample : it->second) {
            firstStep = std::min(firstStep, sample.step);
            lastStep = std::max(lastStep, sample.step);
            lo = std::min(lo, sample.value);
            hi = std::max(hi, sample.value);
        }
    }

    if (!anyDrawable) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(rect(), Qt::AlignCenter, spec_.placeholder);
        return;
    }

    if (spec_.yAxisIsUnitFraction) {
        lo = 0.0;
        hi = 1.0;
    } else if (hi - lo < 1e-9) {
        lo -= 0.5;
        hi += 0.5;
    }
    lastStep = std::max(lastStep, firstStep + 1);

    // Legend row above the plot frame, one swatch + label per series, so the
    // reader can tell "epoxide" from "hydroxyl_pair" without a color key
    // memorized from elsewhere.
    const QRectF legendRect = rect().adjusted(86, 2, -12, 0);
    const QRectF plot = rect().adjusted(86, 24, -12, -56);

    const auto toX = [&](int step) {
        return plot.left()
            + plot.width() * (step - firstStep) / double(lastStep - firstStep);
    };
    const auto toY = [&](double value) {
        return plot.bottom() - plot.height() * (value - lo) / (hi - lo);
    };

    // Legend.
    {
        const QFontMetricsF metrics(painter.font());
        double x = legendRect.left();
        const double y = legendRect.top() + metrics.height() * 0.5;
        for (const QString& name : seriesOrder_) {
            const auto it = series_.find(name);
            if (it == series_.end() || it->second.empty())
                continue;
            const QColor color = colorFor(name);
            painter.setPen(QPen(color, 2.0));
            painter.drawLine(QPointF(x, y), QPointF(x + 14, y));
            painter.setPen(PlotPalette::text);
            painter.drawText(QPointF(x + 18, y + metrics.height() * 0.3), name);
            x += 18 + metrics.horizontalAdvance(name) + 16;
            if (x > legendRect.right() - 60) {
                // Ran out of row width; the remaining series still have
                // their color reserved and still plot, just without a
                // legend entry — better than an unreadably squeezed row.
                break;
            }
        }
    }

    painter.setPen(PlotPalette::spine);
    painter.drawRect(plot);

    const QFontMetricsF metrics(painter.font());
    const QColor gridColor = PlotPalette::grid;
    const QColor tickColor = PlotPalette::tickText;

    // X (cycle/step).
    {
        const double span = lastStep - firstStep;
        const double labelWidth =
            metrics.horizontalAdvance(QString::number(lastStep)) + 24.0;
        const int maxTicks =
            std::clamp(static_cast<int>(plot.width() / std::max(labelWidth, 1.0)),
                       2, 12);
        double step = niceTickStep(span, maxTicks, 0.0);
        step = std::max(1.0, std::round(step));
        const double firstTick = std::ceil(firstStep / step) * step;
        for (double t = firstTick; t <= lastStep + 1e-9; t += step) {
            const double x = toX(static_cast<int>(std::llround(t)));
            if (x < plot.left() - 0.5 || x > plot.right() + 0.5)
                continue;
            painter.setPen(gridColor);
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter.setPen(tickColor);
            painter.drawLine(QPointF(x, plot.bottom()),
                             QPointF(x, plot.bottom() + 4));
            const QString label = QString::number(static_cast<qlonglong>(t));
            painter.drawText(
                QRectF(x - labelWidth / 2.0, plot.bottom() + 5, labelWidth, 13),
                Qt::AlignHCenter | Qt::AlignTop, label);
        }
    }

    // Y (rate, or the raw quantity).
    {
        const double rowHeight = metrics.height() + 8.0;
        const int maxTicks =
            std::clamp(static_cast<int>(plot.height() / std::max(rowHeight, 1.0)),
                       2, 10);
        const double step = niceTickStep(hi - lo, maxTicks, 0.0);
        if (step > 0.0) {
            const int decimals = std::max(decimalsForStep(step), 0);
            const double firstTick = std::ceil(lo / step) * step;
            for (double t = firstTick; t <= hi + step * 1e-9; t += step) {
                const double y = toY(t);
                if (y < plot.top() - 0.5 || y > plot.bottom() + 0.5)
                    continue;
                painter.setPen(gridColor);
                painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
                painter.setPen(tickColor);
                painter.drawLine(QPointF(plot.left() - 4, y), QPointF(plot.left(), y));
                const double shown = std::abs(t) < step * 1e-9 ? 0.0 : t;
                const QString label = spec_.yAxisIsUnitFraction
                    ? QStringLiteral("%1%").arg(shown * 100.0, 0, 'f',
                                                std::max(decimals - 2, 0))
                    : QString::number(shown, 'f', decimals);
                painter.drawText(QRectF(6, y - 7, plot.left() - 12, 14),
                                 Qt::AlignRight | Qt::AlignVCenter, label);
            }
        }
    }

    painter.setPen(PlotPalette::text);
    painter.drawText(QRectF(plot.left(), plot.bottom() + 20, plot.width(), 16),
                     Qt::AlignHCenter,
                     tr("Step: %1").arg(lastStep));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 36, plot.width(), 16),
                     Qt::AlignHCenter, spec_.xAxisLabel);
    painter.save();
    painter.translate(12, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-90, -8, 180, 16), Qt::AlignCenter, spec_.yAxisLabel);
    painter.restore();

    // The curves themselves, each its own polyline plus an end-point marker
    // — the marker is what makes a single-point-so-far series visible at
    // all early in a run, before it has two points to draw a line through.
    for (const QString& name : seriesOrder_) {
        const auto it = series_.find(name);
        if (it == series_.end() || it->second.empty())
            continue;
        const QColor color = colorFor(name);
        const std::vector<Sample>& samples = it->second;
        if (samples.size() >= 2) {
            QPainterPath path;
            path.moveTo(toX(samples.front().step), toY(samples.front().value));
            for (const Sample& sample : samples)
                path.lineTo(toX(sample.step), toY(sample.value));
            painter.setPen(QPen(color, 2.0));
            painter.drawPath(path);
        }
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(
            QPointF(toX(samples.back().step), toY(samples.back().value)), 3.0,
            3.0);
    }
}

void MultiSeriesPlotWidget::exportData()
{
    const QString title = tr("Export %1 Data").arg(spec_.quantity);
    if (series_.empty()) {
        QMessageBox::information(this, title,
                                 tr("No samples recorded yet — run a calculation first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, title, spec_.exportBaseName, tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, title, tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    // Long format (step, series, value) rather than one column per series:
    // different move kinds have different step SETS (a kind's rate is
    // omitted entirely before it is ever attempted — see the widget's own
    // doc comment), so a wide table would need an empty-cell convention a
    // long table simply does not.
    out << "step,series,value\n";
    for (const QString& name : seriesOrder_) {
        const auto it = series_.find(name);
        if (it == series_.end())
            continue;
        for (const Sample& sample : it->second)
            out << sample.step << ',' << name << ','
                << QString::number(sample.value, 'f', spec_.exportDecimals)
                << '\n';
    }
}

} // namespace calango::gui
