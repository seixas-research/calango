#pragma once

#include "core/GrapheneOxideGroupAnalysis.hpp"
#include "core/Structure.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QTabWidget;

namespace calango::gui {

class HistogramPlotWidget;
class LinePlotWidget;
class ViewportWidget;

/// Modules -> Graphene Oxide -> "GO Functional Group Analysis…": a read-only
/// analysis of a Graphene Oxide Build's (or GO-MDMC trajectory's) functional-
/// group census and the geometric distortion each group produces —
/// core::analyzeGrapheneOxideGroups() run per frame, never a second
/// classification of its own.
///
/// Follows the same template RingPercolationDialog established: current
/// structure vs. whole-trajectory scope radios, a results table, plots below
/// it, and Compute / Apply Coloring / Export CSV / Export Plots / Close on
/// one button row. "Apply Coloring" always recolors the CURRENT structure —
/// same convention as RingPercolationDialog and LocalEntropyDialog — by the
/// group kind chosen in the overlay combo, via the existing Cast machinery.
class GrapheneOxideGroupAnalysisDialog : public QDialog {
    Q_OBJECT

public:
    GrapheneOxideGroupAnalysisDialog(
        std::shared_ptr<core::Structure> structure,
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
    void exportImages();

private:
    void rebuildCensusTable();
    void rebuildDistributionPlots();
    void rebuildOverlayCombo();
    /// The frame index compute() should treat as "current" for the census
    /// table/overlay combo: the position of structure_ within frames_, or -1
    /// if scope is Current / no trajectory. Mirrors RingPercolationDialog's
    /// currentFrameIndex().
    int currentFrameIndex() const;

    std::shared_ptr<core::Structure> structure_;
    std::vector<std::shared_ptr<core::Structure>> frames_;
    ViewportWidget* viewport_;

    QRadioButton* scopeCurrentRadio_ = nullptr;
    QRadioButton* scopeTrajectoryRadio_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* skippedLabel_ = nullptr;
    QTableWidget* censusTable_ = nullptr;
    QComboBox* overlayCombo_ = nullptr;
    QTabWidget* geometryTabs_ = nullptr;

    // One histogram per environment/kind, populated from the LAST analyzed
    // frame; one evolution (mean-vs-frame) line plot per same environment,
    // populated across every analyzed frame when the scope is Trajectory.
    HistogramPlotWidget* ccPristineHist_ = nullptr;
    HistogramPlotWidget* ccFunctionalizedHist_ = nullptr;
    LinePlotWidget* ccPristineEvolution_ = nullptr;
    LinePlotWidget* ccFunctionalizedEvolution_ = nullptr;

    HistogramPlotWidget* cccPristineHist_ = nullptr;
    HistogramPlotWidget* cccFunctionalizedHist_ = nullptr;
    LinePlotWidget* cccPristineEvolution_ = nullptr;
    LinePlotWidget* cccFunctionalizedEvolution_ = nullptr;

    HistogramPlotWidget* cocHist_ = nullptr;
    LinePlotWidget* cocEvolution_ = nullptr;

    HistogramPlotWidget* cohHydroxylHist_ = nullptr;
    HistogramPlotWidget* cohCarboxylHist_ = nullptr;
    LinePlotWidget* cohHydroxylEvolution_ = nullptr;
    LinePlotWidget* cohCarboxylEvolution_ = nullptr;

    std::vector<core::GrapheneOxideGroupAnalysis> results_; // index-aligned with the analyzed frame set
};

} // namespace calango::gui
