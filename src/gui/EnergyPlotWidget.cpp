#include "gui/EnergyPlotWidget.hpp"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

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

    const QRectF plot = rect().adjusted(70, 12, -12, -24);

    const auto toX = [&](int step) {
        return plot.left()
            + plot.width() * (step - firstStep) / double(lastStep - firstStep);
    };
    const auto toY = [&](double energy) {
        return plot.bottom() - plot.height() * (energy - lo) / (hi - lo);
    };

    // Frame + min/max/last labels.
    painter.setPen(QColor(90, 95, 105));
    painter.drawRect(plot);
    painter.setPen(QColor(170, 175, 185));
    painter.drawText(QRectF(0, plot.top() - 7, 64, 14), Qt::AlignRight,
                     QString::number(hi, 'f', 3));
    painter.drawText(QRectF(0, plot.bottom() - 7, 64, 14), Qt::AlignRight,
                     QString::number(lo, 'f', 3));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 4, plot.width(), 16),
                     Qt::AlignHCenter,
                     tr("step (last: %1, E = %2 eV)")
                         .arg(samples_.back().step)
                         .arg(samples_.back().energy, 0, 'f', 4));

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

} // namespace calango::gui
