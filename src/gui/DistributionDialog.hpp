#pragma once

#include "core/Distributions.hpp"
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

/// Bond length and bond angle (three-body) distribution histograms for
/// the active structure, within a configurable neighbor cutoff and with
/// exact periodic-image handling. Element filters restrict the pair
/// species (lengths) or the center/neighbor species (angles). Computation
/// runs on a worker thread; results plot interactively and export as
/// .csv/.dat.
class DistributionDialog : public QDialog {
    Q_OBJECT

public:
    DistributionDialog(std::shared_ptr<const core::Structure> structure,
                       QWidget* parent = nullptr);

private Q_SLOTS:
    void compute();
    void computeFinished();
    void exportData();

private:
    std::shared_ptr<const core::Structure> structure_;
    core::HistogramResult lastResult_;
    bool lastWasAngles_ = false;

    QComboBox* kindCombo_;
    QComboBox* elementACombo_;
    QComboBox* elementBCombo_;
    QDoubleSpinBox* cutoffSpin_;
    QSpinBox* binsSpin_;
    QCheckBox* pbcCheck_;
    QPushButton* computeButton_;
    QPushButton* exportButton_;
    LinePlotWidget* plot_;
    QFutureWatcher<core::HistogramResult> watcher_;
};

} // namespace calango::gui
