#include "gui/LinePlotWidget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace calango::gui {

LinePlotWidget::LinePlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setMouseTracking(true);
}

void LinePlotWidget::setData(std::vector<double> x, std::vector<double> y)
{
    x_ = std::move(x);
    y_ = std::move(y);
    hoverIndex_ = -1;
    if (!x_.empty()) {
        const auto [xlo, xhi] = std::minmax_element(x_.begin(), x_.end());
        const auto [ylo, yhi] = std::minmax_element(y_.begin(), y_.end());
        xMin_ = *xlo;
        xMax_ = std::max(*xhi, xMin_ + 1e-12);
        yMin_ = std::min(0.0, *ylo);
        yMax_ = std::max(*yhi, yMin_ + 1e-12) * 1.05;
    }
    update();
}

void LinePlotWidget::setAxisLabels(const QString& xLabel, const QString& yLabel)
{
    xLabel_ = xLabel;
    yLabel_ = yLabel;
    update();
}

void LinePlotWidget::clear()
{
    x_.clear();
    y_.clear();
    hoverIndex_ = -1;
    update();
}

QRectF LinePlotWidget::plotRect() const
{
    return QRectF(64, 14, width() - 80, height() - 52);
}

void LinePlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(28, 30, 34));

    const QRectF plot = plotRect();
    if (x_.size() < 2) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter, tr("No data — press Compute"));
        return;
    }

    const auto toX = [&](double v) {
        return plot.left() + plot.width() * (v - xMin_) / (xMax_ - xMin_);
    };
    const auto toY = [&](double v) {
        return plot.bottom() - plot.height() * (v - yMin_) / (yMax_ - yMin_);
    };

    // Grid + ticks (5 divisions per axis).
    painter.setPen(QColor(60, 64, 72));
    painter.setFont(QFont(font().family(), font().pointSize() - 1));
    for (int t = 0; t <= 5; ++t) {
        const double fx = xMin_ + (xMax_ - xMin_) * t / 5.0;
        const double fy = yMin_ + (yMax_ - yMin_) * t / 5.0;
        painter.setPen(QColor(52, 56, 63));
        painter.drawLine(QPointF(toX(fx), plot.top()), QPointF(toX(fx), plot.bottom()));
        painter.drawLine(QPointF(plot.left(), toY(fy)), QPointF(plot.right(), toY(fy)));
        painter.setPen(QColor(165, 170, 180));
        painter.drawText(QRectF(toX(fx) - 30, plot.bottom() + 4, 60, 14),
                         Qt::AlignHCenter, QString::number(fx, 'g', 3));
        painter.drawText(QRectF(0, toY(fy) - 7, 58, 14), Qt::AlignRight,
                         QString::number(fy, 'g', 3));
    }
    painter.setPen(QColor(120, 125, 135));
    painter.drawRect(plot);
    painter.drawText(QRectF(plot.left(), height() - 20.0, plot.width(), 16),
                     Qt::AlignHCenter, xLabel_);
    painter.save();
    painter.translate(12, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-60, 0, 120, 14), Qt::AlignHCenter, yLabel_);
    painter.restore();

    // Curve.
    QPainterPath path;
    path.moveTo(toX(x_.front()), toY(y_.front()));
    for (std::size_t i = 1; i < x_.size(); ++i)
        path.lineTo(toX(x_[i]), toY(y_[i]));
    painter.setPen(QPen(QColor(102, 153, 255), 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    // Hover crosshair + readout.
    if (hoverIndex_ >= 0 && hoverIndex_ < static_cast<int>(x_.size())) {
        const auto i = static_cast<std::size_t>(hoverIndex_);
        const QPointF p(toX(x_[i]), toY(y_[i]));
        painter.setPen(QPen(QColor(255, 158, 26), 1.0, Qt::DashLine));
        painter.drawLine(QPointF(p.x(), plot.top()), QPointF(p.x(), plot.bottom()));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 158, 26));
        painter.drawEllipse(p, 3.5, 3.5);
        painter.setPen(QColor(235, 238, 245));
        const QString readout = QStringLiteral("%1 = %2   %3 = %4")
                                    .arg(xLabel_)
                                    .arg(x_[i], 0, 'f', 3)
                                    .arg(yLabel_)
                                    .arg(y_[i], 0, 'f', 3);
        painter.drawText(QRectF(plot.left() + 6, plot.top() + 4, plot.width() - 12, 16),
                         Qt::AlignLeft, readout);
    }
}

void LinePlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (x_.size() < 2)
        return;
    const QRectF plot = plotRect();
    const double frac =
        std::clamp((event->position().x() - plot.left()) / plot.width(), 0.0, 1.0);
    const double xValue = xMin_ + frac * (xMax_ - xMin_);
    // Data is monotone in x — nearest index by value.
    const auto it = std::lower_bound(x_.begin(), x_.end(), xValue);
    int index = static_cast<int>(std::distance(x_.begin(), it));
    if (index > 0
        && (index >= static_cast<int>(x_.size())
            || std::abs(x_[static_cast<std::size_t>(index) - 1] - xValue)
                < std::abs(x_[static_cast<std::size_t>(index)] - xValue)))
        --index;
    if (index != hoverIndex_) {
        hoverIndex_ = index;
        update();
    }
}

void LinePlotWidget::leaveEvent(QEvent*)
{
    hoverIndex_ = -1;
    update();
}

} // namespace calango::gui
