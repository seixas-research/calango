#pragma once

#include "core/StructureFactor.hpp"
#include "core/Structure.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QPushButton>
#include <QSpinBox>

#include <memory>
#include <vector>

namespace calango::gui {

class LinePlotWidget;

/// Static structure factor S(q) via Fourier transform of the pair
/// distribution (Lorch-windowed), for the active structure or a
/// trajectory frame range (Start/End/Stride, frame-averaged g(r)).
/// Worker-thread computation, interactive plot, .csv/.dat export.
class StructureFactorDialog : public QDialog {
    Q_OBJECT

public:
    StructureFactorDialog(std::shared_ptr<const core::Structure> structure,
                          std::vector<std::shared_ptr<core::Structure>> frames = {},
                          QWidget* parent = nullptr);

private Q_SLOTS:
    void compute();
    void computeFinished();
    void exportData();

private:
    std::shared_ptr<const core::Structure> structure_;
    std::vector<std::shared_ptr<core::Structure>> frames_;
    core::StructureFactorResult lastResult_;
    std::size_t lastFrameCount_ = 1;

    QDoubleSpinBox* qMinSpin_;
    QDoubleSpinBox* qMaxSpin_;
    QSpinBox* qPointsSpin_;
    QDoubleSpinBox* rMaxSpin_;
    QSpinBox* binsSpin_;
    QCheckBox* pbcCheck_;
    QSpinBox* startFrameSpin_;
    QSpinBox* endFrameSpin_;
    QSpinBox* strideSpin_;
    QPushButton* computeButton_;
    QPushButton* exportButton_;
    LinePlotWidget* plot_;
    QFutureWatcher<core::StructureFactorResult> watcher_;
};

} // namespace calango::gui
