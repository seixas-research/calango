#pragma once

#include "core/Egqca.hpp"

#include <QDialog>
#include <QString>

class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

class EgqcaPlotWidget;

/// Task 1 — EGQCA (Extended Generalized Quasichemical Approximation): loads
/// the cluster ensemble a finished Cluster Expansion Calculation left behind
/// (cluster_expansion.json — same file the binary Convex Hull window and the
/// ternary ground-state map read; EGQCA is a third analysis layer over the
/// identical batch results, per Task 1's own "wire EGQCA as an analysis
/// layer over those results" instruction), lets the user set the reference
/// enthalpies and the (x, T) grid, and solves core::solveEgqca() in process.
///
/// Binary only, matching the working paper's own stated scope ("for
/// simplicity, we will focus on the binary and pseudobinary descriptions" —
/// Sec. 2). An ensemble with more than two species opens with a note saying
/// so rather than silently projecting onto the first two.
class EgqcaWindow : public QDialog {
    Q_OBJECT

public:
    explicit EgqcaWindow(const QString& directory, QWidget* parent = nullptr);

    /// True when the ensemble had every ingredient EGQCA needs (exactly two
    /// species, a degeneracy on every configuration, both pure end-members).
    bool hasData() const { return hasClusters_; }

private Q_SLOTS:
    void solve();

private:
    void refreshPlots();

    core::EgqcaInput input_;
    core::EgqcaResult result_;
    bool hasClusters_ = false;

    QDoubleSpinBox* referenceASpin_ = nullptr;
    QDoubleSpinBox* referenceBSpin_ = nullptr;
    QDoubleSpinBox* minCompositionSpin_ = nullptr;
    QDoubleSpinBox* maxCompositionSpin_ = nullptr;
    QSpinBox* compositionStepsSpin_ = nullptr;
    QDoubleSpinBox* minTemperatureSpin_ = nullptr;
    QDoubleSpinBox* maxTemperatureSpin_ = nullptr;
    QSpinBox* temperatureStepsSpin_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    /// Delta G / M vs composition, one curve per temperature (Fig. 2c style).
    EgqcaPlotWidget* freeEnergyPlot_ = nullptr;
    /// p_j vs T at the composition grid point nearest x = 0.5 (Fig. 3 style).
    EgqcaPlotWidget* probabilityPlot_ = nullptr;
};

} // namespace calango::gui
