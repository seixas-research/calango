#pragma once

#include <QColor>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

class QPainter;

namespace calango::gui {

/// DeltaH_mix(x) over a ternary composition triangle — Task 5's "similar to
/// mpltern" request. Unlike TernaryClusterHullWidget (a discrete scatter +
/// ground-state wireframe over a finite, scattered configuration set — the
/// right shape for Cluster Expansion, wrong shape here), DSIM's
/// DeltaH_mix(x) is a CLOSED FORM (Eq. 4+6) already evaluated on a regular
/// simplex grid (core::simplexGrid()/DsimScriptGenerator's Python mirror),
/// so this widget rasterizes a genuinely filled, continuous-looking contour
/// by reconstructing the grid's own small-triangle mesh directly — no
/// Delaunay triangulation or interpolation-from-scattered-points needed,
/// since the mesh connectivity is exactly the grid's own combinatorial
/// structure (integer barycentric counts summing to the resolution).
///
/// Reuses TernaryClusterHullWidget's chrome (projection, gridlines, frame,
/// ticks, corner labels, colourbar, the Spectral colormap) — the render
/// LOOP is new, the surrounding scaffolding is not.
class DsimTernaryPlotWidget : public QWidget {
    Q_OBJECT

public:
    struct GridPoint {
        double xB = 0.0;
        double xC = 0.0;
        double enthalpy = 0.0;
    };

    explicit DsimTernaryPlotWidget(QWidget* parent = nullptr);

    /// `resolution` must be the SAME value the grid was built with
    /// (core::simplexGrid()'s own parameter — stored in dsim.json's
    /// "ternary.resolution") so xB/xC can be rounded back to the exact
    /// integer barycentric counts the mesh connectivity needs.
    void setData(std::vector<GridPoint> grid, int resolution, QStringList species,
                QString unitLabel);
    void clear();
    bool hasData() const { return hasData_; }

    QString toCsv() const;
    bool exportImage(const QString& path, double scale = 3.0);
    void render(QPainter& painter, const QRectF& bounds) const;

public Q_SLOTS:
    void exportData();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPointF project(double xB, double xC) const;
    static QColor spectral(double t);

    std::vector<GridPoint> grid_;
    int resolution_ = 0;
    QStringList species_;
    QString unitLabel_;
    bool hasData_ = false;
    mutable QRectF plotRect_;
    double minEnthalpy_ = 0.0;
    double maxEnthalpy_ = 0.0;
};

} // namespace calango::gui
