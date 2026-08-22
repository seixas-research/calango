#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <vector>

namespace calango::gui {

/// A histogram drawn as bars, with the mean and a ±σ band marked on it.
///
/// Not LinePlotWidget with the bin centres joined up: a polyline through bin
/// counts reads as a continuous signal that was sampled, when a histogram is
/// the opposite — a count of things that fell in a box, with nothing between
/// the boxes. The distinction matters at small ensemble sizes, where a
/// polyline invents structure between bins that the data does not contain.
///
/// The mean/σ overlay is drawn HERE rather than left to a caption because the
/// question a distribution plot answers is "how wide is this, and where is
/// it centered?", and a number in a label beside a chart makes the reader do
/// the mapping themselves.
///
/// Originally private to RandomNoiseViewer (its energy/force spread plots);
/// extracted here once a second consumer needed the same "histogram with
/// mean/σ" template — GO Functional Group Analysis's bond-length and angle
/// distributions — so there is one implementation rather than two drifting
/// copies. Check here before writing a third.
class HistogramPlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit HistogramPlotWidget(QWidget* parent = nullptr);

    /// Bin `samples` into `bins` equal-width bars and redraw. Fewer than two
    /// distinct values draws the empty-state message instead.
    void setSamples(std::vector<double> samples, int bins);
    void setLabels(const QString& xLabel, const QString& yLabel);
    /// Colour of the bars. Callers with more than one histogram on screen at
    /// once (e.g. pristine vs. functionalized bond lengths) give each a
    /// distinct color so the two are told apart at a glance.
    void setBarColor(const QColor& color);
    /// Message shown when there is nothing to draw ("this run recorded no
    /// forces" reads better than an empty frame).
    void setPlaceholder(const QString& text);

    // Read-back for a caller that wants the summary numbers without
    // re-deriving them (e.g. a results table row beside the plot).
    double mean() const { return mean_; }
    double sigma() const { return sigma_; }
    int sampleCount() const { return static_cast<int>(samples_.size()); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void rebin();

    std::vector<double> samples_;
    std::vector<double> counts_; ///< one per bin
    int bins_ = 24;
    double binWidth_ = 0.0;
    double xMin_ = 0.0;
    double xMax_ = 1.0;
    double maxCount_ = 1.0;
    /// Top of the count axis and its tick spacing, both whole numbers — see
    /// rebin(). A count axis cut into five equal parts labels itself in
    /// fractions of a sample, which no bar can ever reach.
    double yTop_ = 1.0;
    double yStep_ = 1.0;
    double mean_ = 0.0;
    double sigma_ = 0.0;
    QString xLabel_;
    QString yLabel_;
    QString placeholder_;
    QColor barColor_{102, 153, 255};
};

} // namespace calango::gui
