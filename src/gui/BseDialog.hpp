#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/BseSolver.hpp"

#include <QDialog>
#include <QList>
#include <QPair>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace calango::gui {

class SpectrumPlotWidget;
class WannierModelSource;

/// Wannier-based excitons: the Bethe-Salpeter equation in the basis of
/// Wannier-interpolated valence and conduction states — 3D (model-screened,
/// static epsilon_inf) and 2D (Rytova-Keldysh) in one dialog, since both are
/// the SAME solver (core::BseSolver) with a dimensionality toggle, not two
/// parallel implementations.
///
/// Native throughout — see core::BseSolver's own class doc for the
/// formalism, the level of theory (Tamm-Dancoff, model-screened direct term,
/// dipole-weighted exchange), and precisely which approximations are made.
/// WanTiBEXOS, Yambo and BerkeleyGW are references for the physics and
/// conventions; nothing here invokes, links or requires output from any of
/// them.
class BseDialog : public QDialog {
    Q_OBJECT

public:
    explicit BseDialog(QWidget* parent = nullptr);

    /// Offer the completed Wannier runs in this session as Hamiltonian
    /// sources; forwarded to the shared source row. Call before exec().
    void setWannierRuns(const QList<QPair<QString, QString>>& runs);

private Q_SLOTS:
    void updateSizeEstimate();
    void updateDimensionalityRows();
    void compute();
    void exportData();

private:
    core::BseSolver::Options readOptions() const;

    WannierModelSource* source_ = nullptr;

    QSpinBox* kmesh_[3] = {nullptr, nullptr, nullptr};
    QSpinBox* valenceBandTop_ = nullptr;
    QSpinBox* nValence_ = nullptr;
    QSpinBox* nConduction_ = nullptr;
    QComboBox* spin_ = nullptr;
    QComboBox* dimensionality_ = nullptr;

    QGroupBox* bulkScreeningGroup_ = nullptr;
    QDoubleSpinBox* epsilonInfinity_ = nullptr;

    QGroupBox* slabScreeningGroup_ = nullptr;
    QDoubleSpinBox* keldyshR0_ = nullptr;
    QDoubleSpinBox* environmentEpsilon_ = nullptr;

    QSpinBox* lowestExcitons_ = nullptr;
    QDoubleSpinBox* broadening_ = nullptr;
    QDoubleSpinBox* spectrumWindow_ = nullptr;

    QLabel* sizeEstimateLabel_ = nullptr;
    QPushButton* computeButton_ = nullptr;

    QPlainTextEdit* report_ = nullptr;
    SpectrumPlotWidget* spectrumPlot_ = nullptr;
    SpectrumPlotWidget* rydbergSeriesPlot_ = nullptr;

    core::BseSolver::Result lastResult_;
};

} // namespace calango::gui
