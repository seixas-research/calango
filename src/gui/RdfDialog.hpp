#pragma once

#include "core/Rdf.hpp"
#include "core/Structure.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QPushButton>
#include <QSpinBox>

#include <memory>

namespace calango::gui {

class LinePlotWidget;

/// Pair radial distribution function g(r) for the active structure or a
/// trajectory frame range: total RDF or element-pair partials, with PBC
/// handling defaulted from the structure (minimum-image / periodic-image
/// evaluation) and a manual override. For trajectories, Start/End/Stride
/// controls restrict which frames enter the frame-averaged g(r).
/// Computation runs on a worker thread; the result is plotted in the
/// interactive chart panel and exportable as .csv/.dat.
class RdfDialog : public QDialog {
    Q_OBJECT

public:
    RdfDialog(std::shared_ptr<const core::Structure> structure,
              std::vector<std::shared_ptr<core::Structure>> frames = {},
              QWidget* parent = nullptr);

private Q_SLOTS:
    void compute();
    void computeFinished();
    void exportData();

private:
    std::shared_ptr<const core::Structure> structure_;
    std::vector<std::shared_ptr<core::Structure>> frames_; ///< trajectory (may be empty)
    core::RdfResult lastResult_;
    std::size_t lastFrameCount_ = 1;

    QComboBox* elementACombo_;
    QComboBox* elementBCombo_;
    QDoubleSpinBox* rMaxSpin_;
    QSpinBox* binsSpin_;
    QCheckBox* pbcCheck_;
    QSpinBox* startFrameSpin_;
    QSpinBox* endFrameSpin_;
    QSpinBox* strideSpin_;
    QPushButton* computeButton_;
    QPushButton* exportButton_;
    LinePlotWidget* plot_;
    QFutureWatcher<core::RdfResult> watcher_;
};

} // namespace calango::gui
