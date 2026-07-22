#include "gui/MetricPlotWidget.hpp"

#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>

#include <algorithm>
#include <utility>

namespace calango::gui {

MetricPlotWidget::MetricPlotWidget(MetricSpec spec, QWidget* parent)
    : QWidget(parent)
    , spec_(std::move(spec))
{
    setMinimumHeight(120);
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
    painter.fillRect(rect(), QColor(28, 30, 34));

    if (samples_.size() < 2) {
        painter.setPen(QColor(150, 150, 150));
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
    const int firstStep = samples_.front().step;
    const int lastStep = std::max(samples_.back().step, firstStep + 1);

    const QRectF plot = rect().adjusted(86, 12, -12, -40);

    const auto toX = [&](int step) {
        return plot.left()
            + plot.width() * (step - firstStep) / double(lastStep - firstStep);
    };
    const auto toY = [&](double value) {
        return plot.bottom() - plot.height() * (value - lo) / (hi - lo);
    };

    // Frame + min/max readouts + descriptive axis labels.
    painter.setPen(QColor(90, 95, 105));
    painter.drawRect(plot);
    painter.setPen(QColor(170, 175, 185));
    painter.drawText(QRectF(16, plot.top() - 7, 66, 14), Qt::AlignRight,
                     QString::number(hi, 'f', spec_.decimals));
    painter.drawText(QRectF(16, plot.bottom() - 7, 66, 14), Qt::AlignRight,
                     QString::number(lo, 'f', spec_.decimals));
    const QString lastValue =
        QString::number(samples_.back().value, 'f', spec_.decimals);
    painter.drawText(
        QRectF(plot.left(), plot.bottom() + 4, plot.width(), 16), Qt::AlignHCenter,
        hasTarget_ ? tr("Step   (last: %1, %2 = %3 %4, target %5 %4)")
                         .arg(samples_.back().step)
                         .arg(spec_.valueSymbol, lastValue, spec_.unit)
                         .arg(target_, 0, 'f', spec_.decimals)
                   : tr("Step   (last: %1, %2 = %3 %4)")
                         .arg(samples_.back().step)
                         .arg(spec_.valueSymbol, lastValue, spec_.unit));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 20, plot.width(), 16),
                     Qt::AlignHCenter, spec_.xAxisLabel);
    painter.save();
    painter.translate(12, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-90, -8, 180, 16), Qt::AlignCenter, spec_.yAxisLabel);
    painter.restore();

    // Setpoint reference (thermostat / barostat targets only).
    if (hasTarget_) {
        QPen dashed(QColor(255, 158, 26), 1.6, Qt::DashLine);
        painter.setPen(dashed);
        const double y = toY(target_);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    QPainterPath path;
    path.moveTo(toX(samples_.front().step), toY(samples_.front().value));
    for (const Sample& sample : samples_)
        path.lineTo(toX(sample.step), toY(sample.value));

    painter.setPen(QPen(spec_.lineColor, 2.0));
    painter.drawPath(path);

    painter.setBrush(QColor(255, 158, 26));
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
    out << "# Calango job " << spec_.quantity.toLower() << " series ("
        << spec_.marker << " markers)\n";
    const bool withTarget = !spec_.csvTargetColumn.isEmpty() && hasTarget_;
    if (withTarget)
        out << "# setpoint: " << QString::number(target_, 'f', spec_.exportDecimals)
            << ' ' << spec_.unit << '\n';
    const QString target =
        withTarget ? QString::number(target_, 'f', spec_.exportDecimals) : QString();

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
