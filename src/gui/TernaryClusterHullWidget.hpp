#pragma once

#include "core/TernaryConvexHull.hpp"

#include <QColor>
#include <QStringList>
#include <QWidget>

class QPainter;

namespace calango::gui {

/// Cluster Expansion, extended to ternary systems (Task 6): formation energy
/// over the composition triangle — a colour-mapped scatter, one point per
/// inequivalent configuration — with the ground-state facets
/// (core::TernaryConvexHull) drawn over it as a wireframe.
///
/// Native QPainter, not mpltern/matplotlib: Calango draws every 2D plot this
/// way (see PlotPalette.hpp and, for the closest sibling, PhaseDiagramWindow's
/// TernarySectionWidget — the CALPHAD ternary section this shares its
/// barycentric projection and corner-label layout with), and introducing an
/// external Python plotting dependency for one chart type would be the first
/// exception to a convention the project states and defends explicitly
/// elsewhere. What IS taken from the reference notebook
/// (ternary_plots.ipynb) is its VISUAL STYLE, reproduced natively: the
/// matplotlib "Spectral" colormap (hand-reproduced from its ColorBrewer
/// control points — see spectral() in the .cpp), dashed gridlines at 0.20
/// composition intervals on all three axes, and a colorbar on the right.
class TernaryClusterHullWidget : public QWidget {
    Q_OBJECT

public:
    explicit TernaryClusterHullWidget(QWidget* parent = nullptr);

    /// `elements` names the three species, index 0 = A (drawn at the apex),
    /// matching TernarySectionWidget's convention.
    void setData(core::TernaryConvexHullResult result, QStringList elements);
    void clear();
    bool hasData() const { return hasData_; }

    /// CSV of every point: composition, formation energy, ground-state flag,
    /// energy above hull — the data table behind the plot.
    QString toCsv() const;

    bool exportImage(const QString& path, double scale = 3.0);

    /// The widget's paint entry point, exposed (matching TiIntegrandPlot's
    /// own convention) so a test can render into an arbitrary QImage without
    /// a real window — the same call paintEvent() and exportImage() both
    /// make, so what a test measures is exactly what the app draws.
    void render(QPainter& painter, const QRectF& bounds) const;

public Q_SLOTS:
    /// "Export Data…": save-dialog + toCsv(), matching
    /// ConvexHullPlotWidget::exportData()'s pattern for the binary hull.
    void exportData();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Barycentric composition -> screen. Same orientation as
    /// TernarySectionWidget::project(): apex = pure A, bottom-right = pure
    /// B, bottom-left = pure C.
    QPointF project(double xB, double xC) const;

    /// matplotlib's "Spectral" colormap at t in [0, 1] (0 = lowest formation
    /// energy, 1 = highest) — see the .cpp for the ColorBrewer-11 anchors
    /// this linearly interpolates between.
    static QColor spectral(double t);

    core::TernaryConvexHullResult result_;
    QStringList elements_;
    bool hasData_ = false;
    mutable QRectF plotRect_;
    double minEnergy_ = 0.0;
    double maxEnergy_ = 0.0;
};

} // namespace calango::gui
