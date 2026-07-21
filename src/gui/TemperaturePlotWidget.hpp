#pragma once

#include <QWidget>

#include <vector>

namespace calango::gui {

/// Ionic temperature vs. MD step, fed by CALANGO_TEMP markers from the
/// running job. Constant-temperature ensembles additionally announce
/// their thermostat setpoint (CALANGO_TARGET_TEMP), drawn as a dashed
/// reference line; microcanonical (NVE) runs never set one. Custom
/// QPainter rendering, matching the Energy tab.
class TemperaturePlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit TemperaturePlotWidget(QWidget* parent = nullptr);

public Q_SLOTS:
    void clear();
    void addSample(int step, double temperatureK);
    void setTargetTemperature(double temperatureK);
    /// Save the recorded (step, temperature, target) series as .csv/.dat.
    void exportData();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Sample {
        int step;
        double temperature;
    };
    std::vector<Sample> samples_;
    double targetK_ = 0.0;
    bool hasTarget_ = false;
};

} // namespace calango::gui
