#pragma once

#include <QDialog>
#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
class QSpinBox;

namespace calango::gui {

/// A histogram drawn as bars, with the mean and a ±σ band marked on it.
///
/// Not LinePlotWidget with the bin centres joined up: a polyline through bin
/// counts reads as a continuous signal that was sampled, when a histogram is
/// the opposite — a count of things that fell in a box, with nothing between
/// the boxes. The distinction matters at the small ensemble sizes this window
/// is usually looking at (20–100 members), where a polyline invents structure
/// between bins that the data does not contain.
///
/// The mean/σ overlay is drawn HERE rather than left to a caption because the
/// whole question the window answers is "how wide is this?", and a number in a
/// label beside a chart makes the reader do the mapping themselves.
class HistogramPlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit HistogramPlotWidget(QWidget* parent = nullptr);

    /// Bin `samples` into `bins` equal-width bars and redraw. Fewer than two
    /// distinct values draws the empty-state message instead.
    void setSamples(std::vector<double> samples, int bins);
    void setLabels(const QString& xLabel, const QString& yLabel);
    /// Colour of the bars. The energy and force panels differ so the two are
    /// distinguishable at a glance in an exported figure.
    void setBarColor(const QColor& color);
    /// Message shown when there is nothing to draw ("this run recorded no
    /// forces" reads better than an empty frame).
    void setPlaceholder(const QString& text);

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

/// Results window for a Random Noise run: what the ensemble's spread actually
/// came out to be.
///
/// A perturbed ensemble is not read one member at a time — a single displaced
/// energy means nothing. What it produces is two distributions, and the window
/// is built around them:
///
///   • the ENERGY spread, σ(E), which measures the curvature of the
///     potential-energy surface around the reference geometry and says whether
///     the displacement amplitude is still inside the harmonic well;
///   • the FORCE spread, σ(F) over the Cartesian components, which is the
///     force scale an ML potential trained on this ensemble has to reproduce.
///
/// Both come from random_noise.json, which the generated script writes with
/// the per-member records and the pooled per-atom force magnitudes. Export
/// hands over the evaluated trajectory itself — every frame with its energy
/// and forces attached — as the `.extxyz` a trainer consumes.
class RandomNoiseViewer : public QDialog {
    Q_OBJECT

public:
    /// `directory` is the finished job's folder (the one holding
    /// random_noise.json). Construct, check hasData(), then show.
    explicit RandomNoiseViewer(const QString& directory,
                               QWidget* parent = nullptr);

    /// False when the directory holds no readable random_noise.json — the
    /// caller deletes the window rather than showing an empty one.
    bool hasData() const { return hasData_; }

private Q_SLOTS:
    /// Save the evaluated trajectory (geometry + energy + forces per frame)
    /// somewhere the user chooses.
    void exportTrajectory();

private:
    bool load();
    void rebinPlots();
    /// Path of the evaluated trajectory inside the job directory, or an empty
    /// string when the run wrote none (every member failed).
    QString trajectoryPath() const;

    QString directory_;
    bool hasData_ = false;

    std::vector<double> energies_;
    std::vector<double> forceMagnitudes_;

    HistogramPlotWidget* energyPlot_ = nullptr;
    HistogramPlotWidget* forcePlot_ = nullptr;
    QSpinBox* binsSpin_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
};

} // namespace calango::gui
