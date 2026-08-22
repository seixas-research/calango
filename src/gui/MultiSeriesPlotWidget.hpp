#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <map>
#include <vector>

namespace calango::gui {

/// Several named scalar-vs-step series sharing one pair of axes — a legend
/// with one entry per series, drawn in a fixed, stable color per name.
///
/// MetricPlotWidget is deliberately ONE series per instance (Energy,
/// Temperature, Max Force, Pressure each get their own tab), and that shape
/// does not fit "acceptance rate, broken out by move type": GO-MDMC's
/// per-move-class Metropolis acceptance is 1-6 series that only mean
/// anything shown together, on the same step axis, so the reader can compare
/// how fast one move kind's acceptance drifts against another's. This is a
/// separate, purpose-built widget rather than a MetricPlotWidget extension so
/// that widget's four existing, already-tested single-series tabs are
/// untouched.
///
/// A series absent from a given step's sample (GO-MDMC omits a move kind's
/// rate entirely for any cycle before that kind was ever attempted — see
/// `_calango_metric`'s "None fields are skipped" convention) simply has no
/// point there; series are NOT required to share the same set of steps.
class MultiSeriesPlotWidget : public QWidget {
    Q_OBJECT

public:
    struct Sample {
        int step;
        double value;
    };

    /// Static description of the whole plot (axes, title) — analogous to
    /// MetricPlotWidget::MetricSpec but without a per-series value symbol/
    /// unit, since those differ series to series here.
    struct PlotSpec {
        QString quantity;    ///< "Acceptance" — export dialog title
        QString yAxisLabel;  ///< "Acceptance rate"
        QString xAxisLabel;  ///< "MC cycle"
        QString placeholder; ///< empty-state hint
        QString exportBaseName; ///< "acceptance.csv"
        /// y is a fraction in [0, 1] — pin the axis to that range rather than
        /// auto-scaling to whatever the data happens to span, so 0 % and
        /// 100 % always mean the same height across every run.
        bool yAxisIsUnitFraction = true;
        int decimals = 3;
        int exportDecimals = 6;
    };

    explicit MultiSeriesPlotWidget(PlotSpec spec, QWidget* parent = nullptr);

    // Project persistence / bulk refresh: full replace, same convention as
    // MetricPlotWidget::setSamples() — metrics.json is always the complete
    // history, so every poll is a full rebuild, not an incremental append.
    void setSeries(const std::map<QString, std::vector<Sample>>& series);
    /// Stable color for `name`, assigned the first time it is seen (via
    /// setSeries()) and kept for the widget's lifetime — a legend entry must
    /// not change color between polls just because a different move kind
    /// happened to appear first in a later JSON read.
    QColor colorFor(const QString& name) const;

public Q_SLOTS:
    void clear();
    void exportData();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    PlotSpec spec_;
    std::map<QString, std::vector<Sample>> series_;
    /// Insertion-ordered color assignment; a plain map would reorder by name
    /// and reshuffle colors as new series appear mid-run.
    std::vector<QString> seriesOrder_;
    mutable std::map<QString, QColor> colors_;
};

} // namespace calango::gui
