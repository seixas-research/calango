#include "gui/TemperaturePlotWidget.hpp"

#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>

#include <algorithm>

namespace calango::gui {

TemperaturePlotWidget::TemperaturePlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
}

void TemperaturePlotWidget::clear()
{
    samples_.clear();
    hasTarget_ = false;
    targetK_ = 0.0;
    update();
}

void TemperaturePlotWidget::addSample(int step, double temperatureK)
{
    samples_.push_back({step, temperatureK});
    update();
}

void TemperaturePlotWidget::setTargetTemperature(double temperatureK)
{
    targetK_ = temperatureK;
    hasTarget_ = true;
    update();
}

void TemperaturePlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(28, 30, 34));

    if (samples_.size() < 2) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Temperature vs. step will appear here during an "
                            "MD run"));
        return;
    }

    const auto [minIt, maxIt] = std::minmax_element(
        samples_.begin(), samples_.end(), [](const Sample& a, const Sample& b) {
            return a.temperature < b.temperature;
        });
    double lo = minIt->temperature;
    double hi = maxIt->temperature;
    if (hasTarget_) {
        lo = std::min(lo, targetK_);
        hi = std::max(hi, targetK_);
    }
    if (hi - lo < 1e-9) {
        lo -= 5.0;
        hi += 5.0;
    }
    const int firstStep = samples_.front().step;
    const int lastStep = std::max(samples_.back().step, firstStep + 1);

    const QRectF plot = rect().adjusted(86, 12, -12, -40);

    const auto toX = [&](int step) {
        return plot.left()
            + plot.width() * (step - firstStep) / double(lastStep - firstStep);
    };
    const auto toY = [&](double temperature) {
        return plot.bottom() - plot.height() * (temperature - lo) / (hi - lo);
    };

    painter.setPen(QColor(90, 95, 105));
    painter.drawRect(plot);
    painter.setPen(QColor(170, 175, 185));
    painter.drawText(QRectF(16, plot.top() - 7, 66, 14), Qt::AlignRight,
                     QString::number(hi, 'f', 1));
    painter.drawText(QRectF(16, plot.bottom() - 7, 66, 14), Qt::AlignRight,
                     QString::number(lo, 'f', 1));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 4, plot.width(), 16),
                     Qt::AlignHCenter,
                     hasTarget_
                         ? tr("Step   (last: %1, T = %2 K, target %3 K)")
                               .arg(samples_.back().step)
                               .arg(samples_.back().temperature, 0, 'f', 1)
                               .arg(targetK_, 0, 'f', 1)
                         : tr("Step   (last: %1, T = %2 K)")
                               .arg(samples_.back().step)
                               .arg(samples_.back().temperature, 0, 'f', 1));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 20, plot.width(), 16),
                     Qt::AlignHCenter, tr("MD step"));
    painter.save();
    painter.translate(12, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-90, -8, 180, 16), Qt::AlignCenter,
                     tr("Temperature (K)"));
    painter.restore();

    // Thermostat setpoint reference (constant-T ensembles only).
    if (hasTarget_) {
        QPen dashed(QColor(255, 158, 26), 1.6, Qt::DashLine);
        painter.setPen(dashed);
        const double y = toY(targetK_);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    QPainterPath path;
    path.moveTo(toX(samples_.front().step), toY(samples_.front().temperature));
    for (const Sample& sample : samples_)
        path.lineTo(toX(sample.step), toY(sample.temperature));

    painter.setPen(QPen(QColor(235, 110, 80), 2.0));
    painter.drawPath(path);

    painter.setBrush(QColor(255, 158, 26));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(
        QPointF(toX(samples_.back().step), toY(samples_.back().temperature)), 3.5,
        3.5);
}

void TemperaturePlotWidget::exportData()
{
    if (samples_.empty()) {
        QMessageBox::information(this, tr("Export Temperature Data"),
                                 tr("No samples recorded yet — run an MD job "
                                    "first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Temperature Data"), QStringLiteral("temperature.csv"),
        tr("CSV (*.csv);;Data file (*.dat)"));
    if (path.isEmpty())
        return;
    const bool csv = !path.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export Temperature Data"),
                              tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# Calango job temperature series (CALANGO_TEMP markers)\n";
    if (hasTarget_)
        out << "# thermostat target: " << QString::number(targetK_, 'f', 2)
            << " K\n";
    const QString target = hasTarget_ ? QString::number(targetK_, 'f', 2)
                                      : QString();
    if (csv) {
        out << "step,temperature_K,target_K\n";
        for (const Sample& sample : samples_)
            out << sample.step << ','
                << QString::number(sample.temperature, 'f', 2) << ',' << target
                << '\n';
    } else {
        out << "#     step   temperature_K        target_K\n";
        for (const Sample& sample : samples_)
            out << QString::asprintf("%10d %15.2f %15s\n", sample.step,
                                     sample.temperature,
                                     target.isEmpty() ? "nan"
                                                      : qPrintable(target));
    }
}

} // namespace calango::gui
