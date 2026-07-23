#pragma once

#include "core/ConvexHull.hpp"

#include <QString>
#include <QWidget>

#include <vector>

namespace calango::gui {

/// Formation-energy convex hull for a cluster-expansion ensemble: E_form
/// (eV/atom) against concentration x, drawn with the same hand-painted
/// QPainter approach as the other Calango plots (no external plotting
/// dependency — see LinePlotWidget / MetricPlotWidget).
///
/// Stable configurations (on the lower hull) are filled and joined by the
/// tie-lines; metastable ones are hollow, and hovering any point reports its
/// formula, composition and energy above the hull.
class ConvexHullPlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit ConvexHullPlotWidget(QWidget* parent = nullptr);

    /// Load from a finished job's cluster_expansion.json. Returns false (and
    /// clears) when the file is missing or carries no usable configuration.
    bool loadFromJson(const QString& path);

    void setResult(core::ConvexHullResult result, const QString& axisSpecies);
    void clear();

    bool hasData() const { return !result_.points.empty(); }
    const core::ConvexHullResult& result() const { return result_; }

    /// Save the tabulated hull data (x, E_form, E_above_hull, on-hull flag).
    void exportData();

Q_SIGNALS:
    /// The user double-clicked a point — the controller shows that frame of
    /// the optimized trajectory.
    void frameActivated(int frameIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /// Index of the plotted point nearest `pos` within the pick radius, or -1.
    int pointAt(const QPointF& pos) const;

    core::ConvexHullResult result_;
    QString axisSpecies_;
    /// Screen positions of result_.points, refreshed every paint so hit
    /// testing always matches what is on screen.
    mutable std::vector<QPointF> screenPositions_;
    int hoverIndex_ = -1;
};

} // namespace calango::gui
