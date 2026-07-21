#include "gui/EnergyPlotWidget.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>

#include <algorithm>

namespace calango::gui {

EnergyPlotWidget::EnergyPlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
}

void EnergyPlotWidget::clear()
{
    samples_.clear();
    update();
}

void EnergyPlotWidget::addSample(int step, double energyEv)
{
    samples_.push_back({step, energyEv});
    update();
}

void EnergyPlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(28, 30, 34));

    if (samples_.size() < 2) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Energy vs. step will appear here during a job"));
        return;
    }

    const auto [minIt, maxIt] = std::minmax_element(
        samples_.begin(), samples_.end(),
        [](const Sample& a, const Sample& b) { return a.energy < b.energy; });
    double lo = minIt->energy;
    double hi = maxIt->energy;
    if (hi - lo < 1e-9) {
        lo -= 0.5;
        hi += 0.5;
    }
    const int firstStep = samples_.front().step;
    const int lastStep = std::max(samples_.back().step, firstStep + 1);

    const QRectF plot = rect().adjusted(86, 12, -12, -40);

    const auto toX = [&](int step) {
        return plot.left()
            + plot.width() * (step - firstStep) / double(lastStep - firstStep);
    };
    const auto toY = [&](double energy) {
        return plot.bottom() - plot.height() * (energy - lo) / (hi - lo);
    };

    // Frame + min/max readouts + descriptive axis labels.
    painter.setPen(QColor(90, 95, 105));
    painter.drawRect(plot);
    painter.setPen(QColor(170, 175, 185));
    painter.drawText(QRectF(16, plot.top() - 7, 66, 14), Qt::AlignRight,
                     QString::number(hi, 'f', 3));
    painter.drawText(QRectF(16, plot.bottom() - 7, 66, 14), Qt::AlignRight,
                     QString::number(lo, 'f', 3));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 4, plot.width(), 16),
                     Qt::AlignHCenter,
                     tr("Step   (last: %1, E = %2 eV)")
                         .arg(samples_.back().step)
                         .arg(samples_.back().energy, 0, 'f', 4));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 20, plot.width(), 16),
                     Qt::AlignHCenter, tr("MD/optimization step"));
    painter.save();
    painter.translate(12, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-90, -8, 180, 16), Qt::AlignCenter,
                     tr("Total Energy (eV)"));
    painter.restore();

    QPainterPath path;
    path.moveTo(toX(samples_.front().step), toY(samples_.front().energy));
    for (const Sample& sample : samples_)
        path.lineTo(toX(sample.step), toY(sample.energy));

    painter.setPen(QPen(QColor(102, 153, 255), 2.0));
    painter.drawPath(path);

    painter.setBrush(QColor(255, 158, 26));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(toX(samples_.back().step), toY(samples_.back().energy)),
                        3.5, 3.5);
}

void EnergyPlotWidget::exportData()
{
    if (samples_.empty()) {
        QMessageBox::information(this, tr("Export Energy Data"),
                                 tr("No samples recorded yet — run a job first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Energy Data"), QStringLiteral("energy.csv"),
        tr("CSV (*.csv);;Data file (*.dat)"));
    if (path.isEmpty())
        return;
    const bool csv = !path.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export Energy Data"),
                              tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# Calango job energy series (CALANGO_ENERGY markers)\n";
    if (csv) {
        out << "step,total_energy_eV\n";
        for (const Sample& sample : samples_)
            out << sample.step << ',' << QString::number(sample.energy, 'f', 6)
                << '\n';
    } else {
        out << "#     step   total_energy_eV\n";
        for (const Sample& sample : samples_)
            out << QString::asprintf("%10d %18.6f\n", sample.step, sample.energy);
    }
}

} // namespace calango::gui
