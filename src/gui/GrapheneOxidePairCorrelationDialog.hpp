#pragma once

#include "core/GrapheneOxidePairCorrelation.hpp"
#include "core/Structure.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QSpinBox;
class QRadioButton;
class QTableWidget;

namespace calango::gui {

class LinePlotWidget;

/// Modules -> Graphene Oxide -> "GO Pair Correlation…": Warren-Cowley
/// short-range-order analysis of a Graphene Oxide Build's (or a GO/MCMD /
/// GO/MC-Opt
/// trajectory's) functional-group DECORATION — core::
/// analyzeGrapheneOxidePairCorrelation() run per frame, reusing core::
/// computeWarrenCowley() (the alloy SRO module) entirely unchanged.
///
/// Same template as RingPercolationDialog / GrapheneOxideGroupAnalysisDialog:
/// scope radios, a shell-resolved matrix ("heatmap" via colored table cells),
/// an evolution plot for one selected species pair across a trajectory, and
/// Compute / Export CSV / Export Plots / Close. No structure-overlay
/// coloring here — unlike the census/geometry module, a per-atom "species"
/// Cast duplicates exactly what GO Functional Group Analysis's overlay
/// already offers, so this dialog stays read-only-and-numeric.
class GrapheneOxidePairCorrelationDialog : public QDialog {
    Q_OBJECT

public:
    GrapheneOxidePairCorrelationDialog(
        std::shared_ptr<core::Structure> structure,
        std::vector<std::shared_ptr<core::Structure>> frames,
        QWidget* parent = nullptr);

private Q_SLOTS:
    void compute();
    void rebuildMatrixView();
    void rebuildEvolutionPlot();
    void exportCsv();
    void exportImage();

private:
    void rebuildPairCombos();

    std::shared_ptr<core::Structure> structure_;
    std::vector<std::shared_ptr<core::Structure>> frames_;

    QRadioButton* scopeCurrentRadio_ = nullptr;
    QRadioButton* scopeTrajectoryRadio_ = nullptr;
    QSpinBox* shellCountSpin_ = nullptr;
    QLabel* summaryLabel_ = nullptr;

    QComboBox* shellCombo_ = nullptr;
    QTableWidget* matrixTable_ = nullptr;

    QComboBox* pairICombo_ = nullptr;
    QComboBox* pairJCombo_ = nullptr;
    QLabel* evolutionStatsLabel_ = nullptr;
    LinePlotWidget* evolutionPlot_ = nullptr;

    /// One core::GrapheneOxidePairCorrelationResult per analyzed frame.
    std::vector<core::GrapheneOxidePairCorrelationResult> results_;
    std::vector<double> shellCutoffsUsed_;
};

} // namespace calango::gui
