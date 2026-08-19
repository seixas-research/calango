#include "gui/EnergyWindowWidget.hpp"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {
constexpr int kBins = 128;
} // namespace

EnergyWindowWidget::EnergyWindowWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(90);
    setMouseTracking(false);
    setToolTip(
        tr("The parent calculation's Kohn-Sham eigenvalue spectrum. Drag "
           "either edge of the shaded band to set the energy window LDOS "
           "integrates over; the dashed line marks the Fermi level."));
}

void EnergyWindowWidget::setLevels(const std::vector<Level>& levels,
                                   double efermiEv)
{
    levels_ = levels;
    efermi_ = efermiEv;
    loading_ = false;

    if (levels_.empty()) {
        dataMin_ = efermi_ - 1.0;
        dataMax_ = efermi_ + 1.0;
    } else {
        dataMin_ = dataMax_ = levels_.front().energyEv;
        for (const Level& lv : levels_) {
            dataMin_ = std::min(dataMin_, lv.energyEv);
            dataMax_ = std::max(dataMax_, lv.energyEv);
        }
        // A little headroom on both ends so the outermost levels are not
        // drawn flush against the widget's edge.
        const double pad = std::max(0.5, (dataMax_ - dataMin_) * 0.05);
        dataMin_ -= pad;
        dataMax_ += pad;
    }
    rebuildHistogram();
    update();
}

void EnergyWindowWidget::setRelativeToFermi(bool relative)
{
    if (relativeToFermi_ == relative)
        return;
    relativeToFermi_ = relative;
    update();
}

void EnergyWindowWidget::setWindow(double minEv, double maxEv)
{
    min_ = std::min(minEv, maxEv);
    max_ = std::max(minEv, maxEv);
    update();
}

void EnergyWindowWidget::setLoading(bool loading, const QString& message)
{
    loading_ = loading;
    loadingMessage_ = message;
    update();
}

void EnergyWindowWidget::rebuildHistogram()
{
    counts_.assign(kBins, 0.0);
    const double span = dataMax_ - dataMin_;
    if (span <= 0.0) {
        maxCount_ = 0.0;
        return;
    }
    for (const Level& lv : levels_) {
        if (!std::isfinite(lv.energyEv))
            continue;
        int index = static_cast<int>((lv.energyEv - dataMin_) / span * kBins);
        index = std::clamp(index, 0, kBins - 1);
        counts_[static_cast<std::size_t>(index)] += std::max(lv.weight, 0.0);
    }
    maxCount_ = counts_.empty() ? 0.0
                                : *std::max_element(counts_.begin(), counts_.end());
}

QRectF EnergyWindowWidget::plotRect() const
{
    return QRectF(6.0, 6.0, std::max(1.0, width() - 12.0),
                  std::max(1.0, height() - 24.0));
}

double EnergyWindowWidget::valueFromX(double x) const
{
    const QRectF plot = plotRect();
    const double t = plot.width() > 0.0
        ? std::clamp((x - plot.left()) / plot.width(), 0.0, 1.0)
        : 0.0;
    return dataMin_ + t * (dataMax_ - dataMin_);
}

double EnergyWindowWidget::xFromValue(double v) const
{
    const QRectF plot = plotRect();
    const double span = dataMax_ - dataMin_;
    const double t = span > 0.0 ? (v - dataMin_) / span : 0.0;
    return plot.left() + std::clamp(t, 0.0, 1.0) * plot.width();
}

EnergyWindowWidget::Handle EnergyWindowWidget::nearestHandle(double x) const
{
    const double dMin = std::abs(x - xFromValue(min_));
    const double dMax = std::abs(x - xFromValue(max_));
    return dMin <= dMax ? Handle::Min : Handle::Max;
}

void EnergyWindowWidget::dragTo(double x)
{
    const double value = valueFromX(x);
    if (activeDrag_ == Handle::Min)
        min_ = std::min(value, max_);
    else if (activeDrag_ == Handle::Max)
        max_ = std::max(value, min_);
    else
        return;
    update();
    Q_EMIT windowChanged(min_, max_);
}

void EnergyWindowWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || loading_)
        return;
    activeDrag_ = nearestHandle(event->position().x());
    dragTo(event->position().x());
}

void EnergyWindowWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton) || activeDrag_ == Handle::None)
        return;
    dragTo(event->position().x());
}

void EnergyWindowWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));

    const QRectF plot = plotRect();
    if (loading_) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter,
                         loadingMessage_.isEmpty()
                             ? tr("Reading eigenvalue spectrum…")
                             : loadingMessage_);
        return;
    }
    if (levels_.empty()) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, tr("No spectrum"));
        return;
    }

    // Shaded window band, drawn first so the histogram bars and handles sit
    // on top of it.
    const double xMin = xFromValue(min_);
    const double xMax = xFromValue(max_);
    QColor band = palette().color(QPalette::Highlight);
    band.setAlpha(60);
    painter.fillRect(QRectF(QPointF(xMin, plot.top()), QPointF(xMax, plot.bottom())),
                     band);

    // Weight-binned histogram, neutral bars like IsovalueHistogramWidget's.
    if (maxCount_ > 0.0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Mid));
        const double binWidth = plot.width() / static_cast<double>(counts_.size());
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            if (counts_[i] <= 0.0)
                continue;
            const double left = plot.left() + static_cast<double>(i) * binWidth;
            const double barHeight = plot.height() * (counts_[i] / maxCount_);
            painter.drawRect(QRectF(
                QPointF(left, plot.bottom() - barHeight),
                QPointF(std::max(left + 0.5, left + binWidth), plot.bottom())));
        }
    }

    // Fermi level: dashed, like BandPdosView's own Fermi line.
    if (efermi_ >= dataMin_ && efermi_ <= dataMax_) {
        const double xf = xFromValue(efermi_);
        QPen fermiPen(palette().color(QPalette::PlaceholderText));
        fermiPen.setStyle(Qt::DashLine);
        painter.setPen(fermiPen);
        painter.drawLine(QPointF(xf, plot.top()), QPointF(xf, plot.bottom()));
    }

    // The two handles.
    QPen handlePen(palette().color(QPalette::Highlight), 2.0);
    painter.setPen(handlePen);
    painter.drawLine(QPointF(xMin, plot.top()), QPointF(xMin, plot.bottom()));
    painter.drawLine(QPointF(xMax, plot.top()), QPointF(xMax, plot.bottom()));

    // Axis labels: the window bounds, and (once) the Fermi reference,
    // in whichever convention setRelativeToFermi() selected.
    painter.setPen(palette().color(QPalette::Text));
    QFont small = painter.font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    painter.setFont(small);
    const auto label = [this](double absoluteEv) {
        const double shown = relativeToFermi_ ? absoluteEv - efermi_ : absoluteEv;
        return QString::number(shown, 'f', 2) + QStringLiteral(" eV");
    };
    painter.drawText(QRectF(plot.left(), plot.bottom() + 2.0, 100.0, 16.0),
                     Qt::AlignLeft, label(min_));
    painter.drawText(QRectF(plot.right() - 100.0, plot.bottom() + 2.0, 100.0, 16.0),
                     Qt::AlignRight, label(max_));
}

} // namespace calango::gui
