#pragma once

#include "gui/HistogramPlotWidget.hpp"

#include <QDialog>
#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
class QSpinBox;

namespace calango::gui {

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
