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

/// Modules -> Graphene Oxide -> "π Percolation Analysis": the conjugated
/// carbon network, and whether it crosses the cell.
///
/// The sibling of RingPercolationDialog, asking the same physical question
/// under a WEAKER and more directly conduction-relevant criterion. That one
/// requires an intact BENZENE RING — all six carbons free of oxygen — before
/// anything counts, and joins rings that share an edge. This one asks only
/// which carbons still carry a p_z orbital (unoxidized and three-coordinate,
/// see core::analyzePiPercolation()) and joins any two of them that are
/// bonded. No ring is required.
///
/// The difference is not academic. One epoxide breaks three hexagons at once,
/// so a lightly oxidized sheet loses its intact-ring network well before it
/// loses the conjugated path that actually carries current — measured on a
/// 6x6 sheet at 16 % epoxide coverage, the rings percolate one in-plane axis
/// and the π network percolates both. Run both when the question is "does
/// this still conduct"; they disagree exactly where the answer is interesting.
///
/// Same shape as its sibling in every other respect: current structure or
/// every frame of a loaded trajectory, a results table, two time-evolution
/// plots, and "Apply Coloring" that always colours the CURRENT structure.
class PiPercolationDialog : public QDialog {
    Q_OBJECT

public:
    PiPercolationDialog(std::shared_ptr<core::Structure> structure,
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
    /// The frame index compute() should treat as "current": the position of
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
    LinePlotWidget* piFractionPlot_ = nullptr;
    LinePlotWidget* largestDomainPlot_ = nullptr;

    /// Index-aligned with the analyzed frame set.
    std::vector<core::PiPercolationResult> results_;
};

} // namespace calango::gui
