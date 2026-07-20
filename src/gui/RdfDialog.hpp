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

/// Pair radial distribution function g(r) for the active structure:
/// total RDF or element-pair partials, with PBC handling defaulted from
/// the structure (minimum-image / periodic-image evaluation) and a manual
/// override. Computation runs on a worker thread; the result is plotted
/// in the interactive chart panel.
class RdfDialog : public QDialog {
    Q_OBJECT

public:
    RdfDialog(std::shared_ptr<const core::Structure> structure, QWidget* parent = nullptr);

private Q_SLOTS:
    void compute();
    void computeFinished();

private:
    std::shared_ptr<const core::Structure> structure_;

    QComboBox* elementACombo_;
    QComboBox* elementBCombo_;
    QDoubleSpinBox* rMaxSpin_;
    QSpinBox* binsSpin_;
    QCheckBox* pbcCheck_;
    QPushButton* computeButton_;
    LinePlotWidget* plot_;
    QFutureWatcher<core::RdfResult> watcher_;
};

} // namespace calango::gui
