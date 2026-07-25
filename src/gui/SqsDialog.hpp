#pragma once

#include "core/SqsGenerator.hpp"
#include "core/Structure.hpp"

#include <QDialog>

#include <memory>
#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace calango::gui {

/// Build → "Special Quasirandom Structure (SQS)": decorate the sites of one
/// element of the current structure with a target alloy composition so the
/// pair correlations of the chosen shells approach those of the ideal random
/// alloy. Runs entirely in-process on core::SqsGenerator — no Python, no icet,
/// no subprocess — so it works in any environment and reports its own
/// convergence. Accepting the dialog exposes the generated supercell via
/// result().
class SqsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SqsDialog(std::shared_ptr<const core::Structure> structure,
                       QWidget* parent = nullptr);

    const std::optional<core::SqsGenerator::Result>& result() const {
        return result_;
    }

    /// One-line description of the last generation (objective, shells, accepted
    /// swaps) for the host's status bar. Empty before a successful run.
    QString resultSummary() const;

private Q_SLOTS:
    void generate();

private:
    std::shared_ptr<const core::Structure> structure_;
    std::optional<core::SqsGenerator::Result> result_;

    QSpinBox* nxSpin_;
    QSpinBox* nySpin_;
    QSpinBox* nzSpin_;
    QComboBox* elementCombo_;
    QLineEdit* compositionEdit_;
    QDoubleSpinBox* shell1Spin_;
    QDoubleSpinBox* shell2Spin_;
    QSpinBox* stepsSpin_;
    QSpinBox* seedSpin_;
    QLabel* statusLabel_;
};

} // namespace calango::gui
