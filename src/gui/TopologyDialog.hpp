#pragma once

#include "core/TopologyScriptGenerator.hpp"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

class MlwfSourceSelector;

/// Settings for a topological-invariant calculation, opened from the MLWF
/// viewer.
class TopologyDialog : public QDialog {
    Q_OBJECT

public:
    /// `mlwfRuns` are the completed MLWF processes to offer as sources —
    /// (label, job directory) pairs; the user may also browse to one.
    explicit TopologyDialog(const QList<QPair<QString, QString>>& mlwfRuns,
                            QWidget* parent = nullptr);

    /// The collected settings, `mlwfDir` included.
    core::TopologyConfig config() const;

    /// Directory of the selected MLWF run.
    QString mlwfDirectory() const;

private:
    /// Say what the current selection is and is not defined for — the two
    /// invariants have mutually exclusive symmetry requirements, and picking
    /// the wrong one yields a confident integer about nothing.
    void refreshApplicabilityNote();

    MlwfSourceSelector* source_ = nullptr;
    QComboBox* invariantCombo_ = nullptr;
    QComboBox* directionCombo_ = nullptr;
    QSpinBox* occupiedSpin_ = nullptr;
    QSpinBox* loopSpin_ = nullptr;
    QCheckBox* socCheck_ = nullptr;
    QLabel* note_ = nullptr;
};

} // namespace calango::gui
