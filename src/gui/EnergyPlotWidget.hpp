#pragma once

#include <QWidget>

#include <vector>

namespace calango::gui {

/// Lightweight energy-vs-step convergence plot fed by CALANGO_ENERGY
/// markers from the running job, with labeled axes and raw-data export.
/// Custom QPainter rendering — no external plotting dependency
/// (QCustomPlot can replace this later if needed).
class EnergyPlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit EnergyPlotWidget(QWidget* parent = nullptr);

public Q_SLOTS:
    void clear();
    void addSample(int step, double energyEv);
    /// Save the recorded (step, energy) series as .csv or .dat.
    void exportData();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Sample {
        int step;
        double energy;
    };
    std::vector<Sample> samples_;
};

} // namespace calango::gui
