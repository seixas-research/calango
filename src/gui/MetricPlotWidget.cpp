#include "gui/MetricPlotWidget.hpp"

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
#include <utility>

namespace calango::gui {

namespace {

/// "Nice" tick spacing for a range: the 1/2/5·10ⁿ step closest to (but not
/// finer than) `range / maxTicks`. This is the standard axis heuristic — it
/// keeps labels on round numbers (0, 250, 500 …) regardless of whether the
/// run has 37 steps or 120 000, instead of slicing the range into a fixed
/// count and printing values like 4133.7.
double niceTickStep(double range, int maxTicks)
{
    if (range <= 0.0 || maxTicks < 1)
        return 0.0;
    const double rough = range / maxTicks;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    const double normalized = rough / magnitude; // in [1, 10)
    // Round *up* to the next nice value so the tick count never exceeds
    // maxTicks (which is what guarantees labels cannot overlap).
    const double nice = normalized <= 1.0 ? 1.0
        : normalized <= 2.0              ? 2.0
        : normalized <= 5.0              ? 5.0
                                         : 10.0;
    return nice * magnitude;
}

/// Decimal places needed to render `step` without two adjacent ticks
/// collapsing to the same label (0 for integral steps).
int decimalsForStep(double step)
{
    if (step <= 0.0)
        return 0;
    const int digits = static_cast<int>(std::ceil(-std::log10(step)));
    return std::clamp(digits, 0, 6);
}

} // namespace

MetricPlotWidget::MetricPlotWidget(MetricSpec spec, QWidget* parent)
    : QWidget(parent)
    , spec_(std::move(spec))
{
    // Three label rows below the axis plus the frame need this much to stay
    // legible in the Results dock.
    setMinimumHeight(150);
}

void MetricPlotWidget::clear()
{
    samples_.clear();
    hasTarget_ = false;
    target_ = 0.0;
    update();
}

void MetricPlotWidget::addSample(int step, double value)
{
    samples_.push_back({step, value});
    update();
}

void MetricPlotWidget::setTarget(double value)
{
    target_ = value;
    hasTarget_ = true;
    update();
}

void MetricPlotWidget::setSamples(std::vector<Sample> samples)
{
    samples_ = std::move(samples);
    update();
}

void MetricPlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), PlotPalette::canvas);

    if (samples_.size() < 2) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(rect(), Qt::AlignCenter, spec_.placeholder);
        return;
    }

    const auto [minIt, maxIt] = std::minmax_element(
        samples_.begin(), samples_.end(),
        [](const Sample& a, const Sample& b) { return a.value < b.value; });
    double lo = minIt->value;
    double hi = maxIt->value;
    if (hasTarget_) {
        lo = std::min(lo, target_);
        hi = std::max(hi, target_);
    }
    if (hi - lo < 1e-9) {
        lo -= spec_.flatPadding;
        hi += spec_.flatPadding;
    }
    if (spec_.yAxisFromZero) {
        // Origin-locked axis: T and |F| are read against zero, so the curve's
        // height on screen should mean "how far from zero", not "where in the
        // sampled range". Applied after the flat-series padding so a constant
        // series still gets headroom above the line rather than pinning it to
        // the top edge.
        lo = 0.0;
        hi = std::max(hi, 1e-9); // never a zero-height plot
    }
    const int firstStep = samples_.front().step;
    const int lastStep = std::max(samples_.back().step, firstStep + 1);

    // Left margin holds the y tick labels; the bottom holds three stacked
    // rows: x tick labels, the live-value caption, and the axis title.
    const QRectF plot = rect().adjusted(86, 12, -12, -56);

    const auto toX = [&](int step) {
        return plot.left()
            + plot.width() * (step - firstStep) / double(lastStep - firstStep);
    };
    const auto toY = [&](double value) {
        return plot.bottom() - plot.height() * (value - lo) / (hi - lo);
    };

    // Frame.
    painter.setPen(PlotPalette::spine);
    painter.drawRect(plot);

    // --- Axis ticks --------------------------------------------------------
    // Tick counts are derived from the available pixels, not fixed, so the
    // same code produces a readable axis in the ~200 px Results dock and in a
    // maximized window. The label width estimate below is what actually
    // guarantees non-overlap.
    const QFontMetricsF metrics(painter.font());
    const QColor gridColor = PlotPalette::grid;
    const QColor tickColor = PlotPalette::tickText;

    // X (simulation step): steps are integers, so never emit a fractional
    // tick — a step axis labelled "1250.5" is meaningless.
    {
        const double span = lastStep - firstStep;
        // Widest plausible label ("120000") plus padding sets the tick budget.
        const double labelWidth =
            metrics.horizontalAdvance(QString::number(lastStep)) + 24.0;
        const int maxTicks =
            std::clamp(static_cast<int>(plot.width() / std::max(labelWidth, 1.0)),
                       2, 12);
        double step = niceTickStep(span, maxTicks);
        step = std::max(1.0, std::round(step)); // integral steps only
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

    // Y (the metric): same treatment, budgeted on row height instead.
    {
        const double rowHeight = metrics.height() + 8.0;
        const int maxTicks =
            std::clamp(static_cast<int>(plot.height() / std::max(rowHeight, 1.0)),
                       2, 10);
        const double step = niceTickStep(hi - lo, maxTicks);
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
                // -0.0 is a real double and prints with a leading minus.
                const double shown = std::abs(t) < step * 1e-9 ? 0.0 : t;
                painter.drawText(QRectF(6, y - 7, plot.left() - 12, 14),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(shown, 'f', decimals));
            }
        }
    }

    painter.setPen(PlotPalette::text);
    const QString lastValue =
        QString::number(samples_.back().value, 'f', spec_.decimals);
    painter.drawText(
        QRectF(plot.left(), plot.bottom() + 20, plot.width(), 16), Qt::AlignHCenter,
        hasTarget_ ? tr("Step   (last: %1, %2 = %3 %4, target %5 %4)")
                         .arg(samples_.back().step)
                         .arg(spec_.valueSymbol, lastValue, spec_.unit)
                         .arg(target_, 0, 'f', spec_.decimals)
                   : tr("Step   (last: %1, %2 = %3 %4)")
                         .arg(samples_.back().step)
                         .arg(spec_.valueSymbol, lastValue, spec_.unit));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 36, plot.width(), 16),
                     Qt::AlignHCenter, spec_.xAxisLabel);
    painter.save();
    painter.translate(12, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-90, -8, 180, 16), Qt::AlignCenter, spec_.yAxisLabel);
    painter.restore();

    // Setpoint reference (thermostat / barostat targets only): dashed line
    // plus an annotated value, so the setpoint is readable without hunting
    // for it in the axis caption below.
    if (hasTarget_) {
        const QColor targetColor = PlotPalette::reference;
        painter.setPen(QPen(targetColor, 1.6, Qt::DashLine));
        const double y = toY(target_);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));

        const QString value = QString::number(target_, 'f', spec_.decimals);
        const QString label = spec_.targetLabelFormat.isEmpty()
            ? QStringLiteral("%1 %2").arg(value, spec_.unit)
            : spec_.targetLabelFormat.arg(value);

        const QFontMetricsF metrics(painter.font());
        const QSizeF size(metrics.horizontalAdvance(label) + 8.0,
                          metrics.height() + 2.0);
        // Sit just above the line, flipping below it when the line is close
        // to the top edge so the annotation is never clipped. Anchored LEFT,
        // beside the y-axis: the setpoint is an axis-value annotation, and
        // it reads as one when it sits with the axis labels — on the right
        // it collided with the freshest samples, which is exactly where the
        // eye is during a run.
        const bool below = y - size.height() - 2.0 < plot.top();
        const QRectF box(plot.left() + 4.0,
                         below ? y + 2.0 : y - size.height() - 2.0,
                         size.width(), size.height());
        // Opaque backing: the label often overlaps the data curve.
        painter.setPen(Qt::NoPen);
        painter.setBrush(PlotPalette::readoutFill);
        painter.drawRoundedRect(box, 3.0, 3.0);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(targetColor);
        painter.drawText(box, Qt::AlignCenter, label);
    }

    QPainterPath path;
    path.moveTo(toX(samples_.front().step), toY(samples_.front().value));
    for (const Sample& sample : samples_)
        path.lineTo(toX(sample.step), toY(sample.value));

    painter.setPen(QPen(spec_.lineColor, 2.0));
    painter.drawPath(path);

    painter.setBrush(PlotPalette::highlight);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(toX(samples_.back().step), toY(samples_.back().value)),
                        3.5, 3.5);
}

void MetricPlotWidget::exportData()
{
    const QString title = tr("Export %1 Data").arg(spec_.quantity);
    if (samples_.empty()) {
        QMessageBox::information(this, title,
                                 tr("No samples recorded yet — run a calculation first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, title, spec_.exportBaseName, tr("CSV (*.csv);;Data file (*.dat)"));
    if (path.isEmpty())
        return;
    const bool csv = !path.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, title, tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    const bool withTarget = !spec_.csvTargetColumn.isEmpty() && hasTarget_;
    const QString target =
        withTarget ? QString::number(target_, 'f', spec_.exportDecimals) : QString();

    // A CSV starts with its header row and nothing else — '#' comment lines
    // belong to the .dat convention (gnuplot) and break naive CSV readers.
    if (csv) {
        out << "step," << spec_.csvColumn;
        if (!spec_.csvTargetColumn.isEmpty())
            out << ',' << spec_.csvTargetColumn;
        out << '\n';
        for (const Sample& sample : samples_) {
            out << sample.step << ','
                << QString::number(sample.value, 'f', spec_.exportDecimals);
            if (!spec_.csvTargetColumn.isEmpty())
                out << ',' << target;
            out << '\n';
        }
    } else {
        out << "# Calango job " << spec_.quantity.toLower() << " series ("
            << spec_.marker << " markers)\n";
        if (withTarget)
            out << "# setpoint: "
                << QString::number(target_, 'f', spec_.exportDecimals) << ' '
                << spec_.unit << '\n';
        out << "#     step " << QString::asprintf("%18s", qPrintable(spec_.csvColumn));
        if (!spec_.csvTargetColumn.isEmpty())
            out << QString::asprintf(" %15s", qPrintable(spec_.csvTargetColumn));
        out << '\n';
        for (const Sample& sample : samples_) {
            out << QString::asprintf("%10d %18.*f", sample.step,
                                     spec_.exportDecimals, sample.value);
            if (!spec_.csvTargetColumn.isEmpty())
                out << QString::asprintf(" %15s",
                                         target.isEmpty() ? "nan" : qPrintable(target));
            out << '\n';
        }
    }
}

} // namespace calango::gui
