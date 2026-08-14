#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/BerryPhase.hpp"

#include <QDialog>
#include <QList>
#include <QPair>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace calango::gui {

class CurvatureMapWidget;
class SpectrumPlotWidget;
class WannierModelSource;

/// Berry Phase: Wilson loops, Berry curvature, anomalous Hall conductivity,
/// polarization and hybrid Wannier centre flow, all from the Wannier
/// representation.
///
/// Native throughout — Wang/Yates/Souza/Vanderbilt and postw90 are references
/// for the formulas and conventions, not dependencies.
///
/// The hybrid Wannier centre flow is the same object the existing
/// Topological Invariants feature plots. That feature generates a Python
/// script and runs it against a completed DFT baseline; this one evaluates the
/// same quantity in process from H(R), so the two answer the same question
/// from different inputs and are useful as a cross-check rather than as
/// duplicates.
class BerryPhaseDialog : public QDialog {
    Q_OBJECT

public:
    /// Offer the completed Wannier runs in this session as Hamiltonian
    /// sources; forwarded to the shared source row. Call before exec().
    void setWannierRuns(const QList<QPair<QString, QString>>& runs);

    explicit BerryPhaseDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void compute();
    void exportData();

private:
    core::BerryPhase::Options readOptions() const;

    WannierModelSource* source_ = nullptr;
    QSpinBox* kmesh_[3] = {nullptr, nullptr, nullptr};
    QSpinBox* loopPoints_ = nullptr;
    QSpinBox* occupiedCount_ = nullptr;
    QComboBox* plane_ = nullptr;
    QSpinBox* mapSamples_ = nullptr;
    QPushButton* computeButton_ = nullptr;
    QPlainTextEdit* report_ = nullptr;
    CurvatureMapWidget* map_ = nullptr;
    SpectrumPlotWidget* flowPlot_ = nullptr;

    core::BerryPhase::CurvatureMap lastMap_;
    core::BerryPhase::CentreFlow lastFlow_;
};

} // namespace calango::gui
