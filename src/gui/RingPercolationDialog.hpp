#pragma once

#include "core/GrapheneOxidePercolation.hpp"
#include "core/Structure.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QRadioButton;
class QTableWidget;

namespace calango::gui {

class LinePlotWidget;
class ViewportWidget;

/// Modules -> Graphene Oxide -> "Aromatic Percolation Analysis": classifies every
/// six-membered carbon ring intact/disrupted from GrapheneOxideBuilder's own
/// functional-group labelling (core::GrapheneOxidePercolation — no second
/// chemistry method), groups intact rings into connected sp2 domains, and
/// reports whether any domain percolates the periodic cell along each
/// axis — the graph-theoretic half of the oxidation-vs-conductivity story
/// for graphene oxide. Meaningless on a structure with no carbon framework,
/// which just reports zero rings rather than refusing to open.
///
/// Current structure, or every frame of a loaded trajectory — mirrors
/// PartialChargeDialog's scope radios. "Apply Coloring" always colours the
/// CURRENT structure only (like LocalEntropyDialog); trajectory scope drives
/// the results table and the two time-evolution plots, not per-frame Cast
/// repainting (see FUTURE.md for that gap).
class RingPercolationDialog : public QDialog {
    Q_OBJECT

public:
    RingPercolationDialog(std::shared_ptr<core::Structure> structure,
                          std::vector<std::shared_ptr<core::Structure>> frames,
                          ViewportWidget* viewport, QWidget* parent = nullptr);

Q_SIGNALS:
    /// Cast coloring was (re)applied to the current structure's viewport
    /// style — the owner should sync any panel that mirrors it.
    void castsApplied();

private Q_SLOTS:
    void compute();
    void applyColoring();
    void exportCsv();
    void exportImage();

private:
    void rebuildTable();
    void rebuildPlots();
    /// The frame index compute() should treat as "current" when filling the
    /// summary label and enabling "Apply Coloring": the position of
    /// structure_ within frames_, or -1 if scope is Current / no trajectory.
    int currentFrameIndex() const;

    std::shared_ptr<core::Structure> structure_;
    std::vector<std::shared_ptr<core::Structure>> frames_;
    ViewportWidget* viewport_;

    QRadioButton* scopeCurrentRadio_ = nullptr;
    QRadioButton* scopeTrajectoryRadio_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    LinePlotWidget* intactFractionPlot_ = nullptr;
    LinePlotWidget* largestDomainPlot_ = nullptr;

    std::vector<core::RingPercolationResult> results_; // index-aligned with the analyzed frame set
};

} // namespace calango::gui
