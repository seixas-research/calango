#pragma once

#include "core/Coordination.hpp"
#include "core/Structure.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

#include <memory>

namespace calango::gui {

class ViewportWidget;

/// Coordination analysis for the active structure: per-atom coordination
/// numbers (CN) and generalized coordination numbers (GCN, Calle-Vallejo)
/// with configurable neighbor cutoffs and bulk reference. Computation runs
/// on a worker thread; results land in a per-atom table plus summary, and
/// one click pushes the CN/GCN mapping onto the viewport's atom colors.
class CoordinationDialog : public QDialog {
    Q_OBJECT

public:
    CoordinationDialog(std::shared_ptr<const core::Structure> structure,
                       ViewportWidget* viewport, QWidget* parent = nullptr);

private Q_SLOTS:
    void compute();
    void computeFinished();

private:
    core::CoordinationOptions currentOptions() const;
    void colorViewport(bool gcn);

    std::shared_ptr<const core::Structure> structure_;
    ViewportWidget* viewport_;

    QComboBox* cutoffModeCombo_;
    QDoubleSpinBox* toleranceSpin_;
    QDoubleSpinBox* cutoffSpin_;
    QDoubleSpinBox* bulkCnSpin_;
    QPushButton* computeButton_;
    QPushButton* colorCnButton_;
    QPushButton* colorGcnButton_;
    QLabel* summaryLabel_;
    QTableWidget* table_;
    QFutureWatcher<core::CoordinationResult> watcher_;
};

} // namespace calango::gui
