#include "gui/HistogramPlotWidget.hpp"

#include "gui/PlotPalette.hpp"

#include <QFont>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

/// Sample standard deviation (ddof = 1). A private copy, deliberately: the
/// original lives in RandomNoiseViewer.cpp too, used there for numbers that
/// never touch this widget (the summary label's headline mean/σ, computed
/// once over the whole ensemble rather than per histogram bin range) — two
/// five-line pure functions are cheaper to keep in step by inspection than a
/// shared header would be to introduce for them alone.
double sampleStdDev(const std::vector<double>& values, double mean)
{
    if (values.size() < 2)
        return 0.0;
    double sum = 0.0;
    for (const double v : values)
        sum += (v - mean) * (v - mean);
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

double meanOf(const std::vector<double>& values)
{
    if (values.empty())
        return 0.0;
    double sum = 0.0;
    for (const double v : values)
        sum += v;
    return sum / static_cast<double>(values.size());
}

} // namespace

HistogramPlotWidget::HistogramPlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(200);
}

void HistogramPlotWidget::setSamples(std::vector<double> samples, int bins)
{
    samples_ = std::move(samples);
    bins_ = std::max(1, bins);
    rebin();
    update();
}

void HistogramPlotWidget::setLabels(const QString& xLabel, const QString& yLabel)
{
    xLabel_ = xLabel;
    yLabel_ = yLabel;
    update();
}

void HistogramPlotWidget::setBarColor(const QColor& color)
{
    barColor_ = color;
    update();
}

void HistogramPlotWidget::setPlaceholder(const QString& text)
{
    placeholder_ = text;
    update();
}

void HistogramPlotWidget::rebin()
{
    counts_.assign(static_cast<std::size_t>(bins_), 0.0);
    maxCount_ = 1.0;
    mean_ = meanOf(samples_);
    sigma_ = sampleStdDev(samples_, mean_);
    if (samples_.size() < 2)
        return;

    const auto [lo, hi] = std::minmax_element(samples_.begin(), samples_.end());
    xMin_ = *lo;
    xMax_ = *hi;
    if (!(xMax_ > xMin_)) {
        // Every sample identical — a zero-width range would divide by zero and
        // is also genuinely degenerate: widen it so the single bar is visible
        // rather than refusing to draw anything.
        const double pad = std::max(1e-12, std::abs(xMin_) * 1e-6);
        xMin_ -= pad;
        xMax_ += pad;
    }
    binWidth_ = (xMax_ - xMin_) / static_cast<double>(bins_);

    for (const double v : samples_) {
        auto index = static_cast<int>((v - xMin_) / binWidth_);
        // The maximum lands exactly on the top edge, which is one past the
        // last bin. It belongs in the last bin, not nowhere.
        index = std::clamp(index, 0, bins_ - 1);
        counts_[static_cast<std::size_t>(index)] += 1.0;
    }
    maxCount_ = *std::max_element(counts_.begin(), counts_.end());
    maxCount_ = std::max(maxCount_, 1.0);

    // A count axis has to be labelled in whole numbers. Cutting the maximum
    // into five equal parts does not: a peak of 4 gives gridlines at 0.8, 1.6,
    // 2.4 … which round to "0 1 2 2 3 4" — two identical labels, and every
    // line drawn where no bar can ever end. So pick a 1/2/5×10^k step instead
    // and round the top of the axis up to a multiple of it.
    yStep_ = 1.0;
    while (maxCount_ / yStep_ > 6.0) {
        // 1 → 2 → 5 → 10 → 20 → …, the standard tick progression.
        const double decade = std::pow(10.0, std::floor(std::log10(yStep_)));
        const double mantissa = std::round(yStep_ / decade);
        yStep_ = mantissa < 2.0 ? 2.0 * decade
                                : (mantissa < 5.0 ? 5.0 * decade : 10.0 * decade);
    }
    yTop_ = yStep_ * std::ceil(maxCount_ / yStep_);
}

void HistogramPlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), PlotPalette::canvas);

    const QRectF plot(64, 14, width() - 80.0, height() - 52.0);
    if (samples_.size() < 2 || plot.width() <= 0 || plot.height() <= 0) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(rect(), Qt::AlignCenter,
                         placeholder_.isEmpty() ? tr("No data") : placeholder_);
        return;
    }

    const auto toX = [&](double v) {
        return plot.left() + plot.width() * (v - xMin_) / (xMax_ - xMin_);
    };
    const auto toY = [&](double count) {
        return plot.bottom() - plot.height() * count / yTop_;
    };

    // Grid + ticks. The x axis takes five equal divisions like every other
    // plot in the application; the y axis steps in whole counts (see rebin()).
    painter.setFont(QFont(font().family(), font().pointSize() - 1));
    for (int t = 0; t <= 5; ++t) {
        const double fx = xMin_ + (xMax_ - xMin_) * t / 5.0;
        painter.setPen(PlotPalette::grid);
        painter.drawLine(QPointF(toX(fx), plot.top()),
                         QPointF(toX(fx), plot.bottom()));
        painter.setPen(PlotPalette::tickText);
        painter.drawText(QRectF(toX(fx) - 34, plot.bottom() + 4, 68, 14),
                         Qt::AlignHCenter, QString::number(fx, 'g', 4));
    }
    for (double fy = 0.0; fy <= yTop_ + 0.5 * yStep_; fy += yStep_) {
        painter.setPen(PlotPalette::grid);
        painter.drawLine(QPointF(plot.left(), toY(fy)),
                         QPointF(plot.right(), toY(fy)));
        painter.setPen(PlotPalette::tickText);
        painter.drawText(QRectF(0, toY(fy) - 7, 58, 14), Qt::AlignRight,
                         QString::number(fy, 'f', 0));
    }

    // ±σ band behind the bars, then the mean line: the two together are the
    // answer to "how wide is the distribution", read straight off the chart.
    if (sigma_ > 0.0) {
        const double low = std::max(xMin_, mean_ - sigma_);
        const double high = std::min(xMax_, mean_ + sigma_);
        painter.setPen(Qt::NoPen);
        // A light grey wash on the white canvas — the old near-transparent
        // white was a highlight against the dark fill and is simply
        // invisible now.
        painter.setBrush(QColor(0, 0, 0, 18));
        painter.drawRect(QRectF(QPointF(toX(low), plot.top()),
                                QPointF(toX(high), plot.bottom())));
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(barColor_);
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        if (counts_[i] <= 0.0)
            continue;
        const double left = toX(xMin_ + static_cast<double>(i) * binWidth_);
        const double right =
            toX(xMin_ + static_cast<double>(i + 1) * binWidth_);
        // A one-pixel gap between bars: adjacent filled rectangles read as one
        // solid block, which hides exactly the shape a histogram is for.
        painter.drawRect(QRectF(QPointF(left + 0.5, toY(counts_[i])),
                                QPointF(std::max(left + 1.0, right - 0.5),
                                        plot.bottom())));
    }

    if (samples_.size() > 1) {
        painter.setPen(QPen(PlotPalette::reference, 1.5, Qt::DashLine));
        painter.drawLine(QPointF(toX(mean_), plot.top()),
                         QPointF(toX(mean_), plot.bottom()));
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(PlotPalette::spine);
    painter.drawRect(plot);
    painter.setPen(PlotPalette::text);
    painter.drawText(QRectF(plot.left(), height() - 20.0, plot.width(), 16),
                     Qt::AlignHCenter, xLabel_);
    painter.save();
    painter.translate(12, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-70, 0, 140, 14), Qt::AlignHCenter, yLabel_);
    painter.restore();

    painter.setPen(PlotPalette::text);
    painter.drawText(
        QRectF(plot.left() + 6, plot.top() + 4, plot.width() - 12, 16),
        Qt::AlignLeft,
        tr("N = %1    mean = %2    σ = %3")
            .arg(samples_.size())
            .arg(mean_, 0, 'g', 4)
            .arg(sigma_, 0, 'g', 4));
}

} // namespace calango::gui
