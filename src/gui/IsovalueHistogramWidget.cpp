#include "gui/IsovalueHistogramWidget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {
// "A few hundred bins" per the spec — fine enough to show real structure
// (a density's core cusp, a bimodal ELF) without costing more than a single
// O(N) pass over the field at load time.
constexpr int kBins = 256;
} // namespace

IsovalueHistogramWidget::IsovalueHistogramWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(40);
    setMaximumHeight(56);
    setToolTip(
        tr("Voxel-count histogram of this field, over the same range the "
           "slider below covers.\n\nClick or drag on it to set the isovalue "
           "directly — the vertical line marks where it is now."));
}

void IsovalueHistogramWidget::setData(const std::vector<double>& values,
                                      double dataMin, double dataMax)
{
    dataMin_ = dataMin;
    dataMax_ = std::max(dataMax, dataMin + 1e-30);
    counts_.assign(kBins, 0.0);
    const double span = dataMax_ - dataMin_;
    for (const double v : values) {
        if (!std::isfinite(v))
            continue;
        int index = span > 0.0
            ? static_cast<int>((v - dataMin_) / span * kBins)
            : 0;
        index = std::clamp(index, 0, kBins - 1);
        counts_[static_cast<std::size_t>(index)] += 1.0;
    }
    maxCount_ =
        counts_.empty() ? 0.0 : *std::max_element(counts_.begin(), counts_.end());

    // A generic peakedness check, not tuned to any one field: when the
    // tallest bin dwarfs the typical occupied one, a linear axis reads as a
    // flat smear with one spike — the common case for a volumetric field,
    // where a vacuum or interstitial plateau dominates the voxel count and
    // buries the occupied tail a user actually wants to see.
    double occupiedSum = 0.0;
    int occupiedBins = 0;
    for (const double c : counts_) {
        if (c > 0.0) {
            occupiedSum += c;
            ++occupiedBins;
        }
    }
    const double meanOccupied = occupiedBins > 0 ? occupiedSum / occupiedBins : 0.0;
    logSuggested_ =
        occupiedBins >= 5 && meanOccupied > 0.0 && maxCount_ > 15.0 * meanOccupied;

    update();
}

void IsovalueHistogramWidget::setReferenceSlider(QSlider* slider)
{
    referenceSlider_ = slider;
    update();
}

void IsovalueHistogramWidget::setCurrentValue(double value)
{
    if (currentValue_ == value)
        return;
    currentValue_ = value;
    update();
}

void IsovalueHistogramWidget::setLogScale(bool on)
{
    if (logScale_ == on)
        return;
    logScale_ = on;
    update();
}

QRectF IsovalueHistogramWidget::plotRect() const
{
    // Pinned to the slider's own groove, queried live rather than assumed as
    // a fixed inset: a style, platform or width change moves both together,
    // and the two stay aligned to the pixel without this widget knowing
    // anything about how QSlider draws its handle.
    double left = 1.0;
    double right = width() - 1.0;
    if (referenceSlider_) {
        QStyleOptionSlider opt;
        opt.initFrom(referenceSlider_);
        opt.minimum = referenceSlider_->minimum();
        opt.maximum = referenceSlider_->maximum();
        opt.sliderPosition = referenceSlider_->sliderPosition();
        opt.sliderValue = referenceSlider_->value();
        opt.orientation = referenceSlider_->orientation();
        const QRect groove = referenceSlider_->style()->subControlRect(
            QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, referenceSlider_);
        left = groove.left();
        right = groove.right();
    }
    return QRectF(left, 2.0, std::max(1.0, right - left), height() - 4.0);
}

double IsovalueHistogramWidget::valueFromX(double x) const
{
    const QRectF plot = plotRect();
    const double t = plot.width() > 0.0
        ? std::clamp((x - plot.left()) / plot.width(), 0.0, 1.0)
        : 0.0;
    return dataMin_ + t * (dataMax_ - dataMin_);
}

void IsovalueHistogramWidget::dragTo(const QPointF& pos)
{
    const double value = valueFromX(pos.x());
    currentValue_ = value;
    update();
    Q_EMIT valueEdited(value);
}

void IsovalueHistogramWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        dragTo(event->position());
}

void IsovalueHistogramWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton)
        dragTo(event->position());
}

void IsovalueHistogramWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));

    const QRectF plot = plotRect();
    if (counts_.empty() || plot.width() <= 0.0 || maxCount_ <= 0.0) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, tr("No data"));
        return;
    }

    const double span = dataMax_ - dataMin_;
    const auto toX = [&](double v) {
        return plot.left() + plot.width() * (v - dataMin_) / span;
    };
    const auto scaleCount = [this](double c) {
        return logScale_ ? std::log1p(c) : c;
    };
    const double topScaled = std::max(1e-12, scaleCount(maxCount_));
    const auto toY = [&](double c) {
        return plot.bottom() - plot.height() * scaleCount(c) / topScaled;
    };

    // Bars in the theme's own Text colour (near-black on Light, near-white
    // on Dark) rather than Mid: Mid is a low-contrast border/disabled tone,
    // and against Base it read as barely-there — the bug this fixes.
    // Text is the correct paired foreground for a Base background (same
    // pairing PlaceholderText/Base uses in the "No data" branch above), and
    // a partial alpha keeps it a soft fill rather than solid blocks while
    // staying far more visible than Mid ever was. The Highlight-coloured
    // marker still reads as distinct by hue, not just by being the only
    // thing with contrast.
    QColor barColor = palette().color(QPalette::Text);
    barColor.setAlpha(200);
    painter.setPen(Qt::NoPen);
    painter.setBrush(barColor);
    const double binWidth = plot.width() / static_cast<double>(counts_.size());
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        if (counts_[i] <= 0.0)
            continue;
        const double left = plot.left() + static_cast<double>(i) * binWidth;
        const double right = left + binWidth;
        painter.drawRect(QRectF(QPointF(left, toY(counts_[i])),
                                QPointF(std::max(left + 0.5, right), plot.bottom())));
    }

    // The current isovalue, clamped onto the plot: the spin box accepts a
    // value typed past the slider's own range, and that should still show at
    // the edge here instead of vanishing off the widget.
    const double markerX = toX(std::clamp(currentValue_, dataMin_, dataMax_));
    painter.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
    painter.drawLine(QPointF(markerX, plot.top()), QPointF(markerX, plot.bottom()));
}

} // namespace calango::gui
