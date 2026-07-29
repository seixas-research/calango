#pragma once

#include "core/TopologyScriptGenerator.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

/// Settings for a topological-invariant calculation, opened from the MLWF
/// viewer.
class TopologyDialog : public QDialog {
    Q_OBJECT

public:
    explicit TopologyDialog(QWidget* parent = nullptr);

    /// The collected settings; `mlwfDir` is left for the caller to fill.
    core::TopologyConfig config() const;

private:
    /// Say what the current selection is and is not defined for — the two
    /// invariants have mutually exclusive symmetry requirements, and picking
    /// the wrong one yields a confident integer about nothing.
    void refreshApplicabilityNote();

    QComboBox* invariantCombo_ = nullptr;
    QComboBox* directionCombo_ = nullptr;
    QSpinBox* occupiedSpin_ = nullptr;
    QSpinBox* loopSpin_ = nullptr;
    QCheckBox* socCheck_ = nullptr;
    QLabel* note_ = nullptr;
};

} // namespace calango::gui
