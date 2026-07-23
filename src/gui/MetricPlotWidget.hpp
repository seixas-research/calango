#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <vector>

namespace calango::gui {

/// One scalar-vs-step job metric plot (Energy, Temperature, Max Force,
/// Pressure, ...), fed by CALANGO_* markers from the running job. A single
/// widget class configured by MetricSpec replaces the former per-metric
/// widgets — all tabs share the same axes/readout/export behavior. Metrics
/// with a setpoint (thermostat target T, barostat target P) draw it as a
/// dashed reference line; setTarget() is simply never called for the
/// others (NVE, non-barostat ensembles). Custom QPainter rendering — no
/// external plotting dependency.
class MetricPlotWidget : public QWidget {
    Q_OBJECT

public:
    /// Static description of one metric tab (labels, units, styling).
    struct MetricSpec {
        QString quantity;      ///< "Energy" — export dialog titles
        QString yAxisLabel;    ///< "Total Energy (eV)"
        QString xAxisLabel;    ///< "MD/optimization step"
        QString valueSymbol;   ///< "E" — last-value readout
        QString unit;          ///< "eV"
        QString placeholder;   ///< empty-state hint
        QString marker;        ///< "CALANGO_ENERGY" — export file comment
        QString csvColumn;     ///< "total_energy_eV"
        QString csvTargetColumn; ///< "target_K"; empty = metric has no setpoint
        QString exportBaseName;  ///< "energy.csv"
        QColor lineColor;
        int decimals = 3;       ///< axis / readout precision
        int exportDecimals = 6; ///< file export precision
        double flatPadding = 0.5; ///< y-range half-width for constant series
        /// Pin the y-axis lower bound to 0 instead of auto-scaling to the
        /// data minimum. Right for quantities that are physically
        /// non-negative and whose distance from zero is the point
        /// (temperature, |force|): an auto-scaled axis makes a 299–301 K
        /// thermostat look like wild oscillation. Wrong for signed
        /// quantities (energy, pressure), where it would flatten the signal.
        bool yAxisFromZero = false;
        /// Short label drawn next to the setpoint line, "%1" replaced by the
        /// formatted value (e.g. "T = %1 K"). Empty = value + unit.
        QString targetLabelFormat;
    };

    explicit MetricPlotWidget(MetricSpec spec, QWidget* parent = nullptr);

    struct Sample {
        int step;
        double value;
    };

    // Project persistence: bulk access to the recorded series.
    const std::vector<Sample>& samples() const { return samples_; }
    void setSamples(std::vector<Sample> samples);
    bool hasTarget() const { return hasTarget_; }
    double targetValue() const { return target_; }

public Q_SLOTS:
    void clear();
    void addSample(int step, double value);
    /// Setpoint reference line (thermostat / barostat target).
    void setTarget(double value);
    /// Save the recorded (step, value[, target]) series as .csv or .dat.
    void exportData();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    MetricSpec spec_;
    std::vector<Sample> samples_;
    double target_ = 0.0;
    bool hasTarget_ = false;
};

} // namespace calango::gui
