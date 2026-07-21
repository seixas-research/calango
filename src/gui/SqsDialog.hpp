#pragma once

#include "core/Structure.hpp"
#include "python_bridge/SqsBuilder.hpp"

#include <QDialog>

#include <memory>
#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace calango::gui {

/// Build → "Special Quasirandom Structure (SQS)": decorate the sites of
/// one element of the current structure with a target alloy composition
/// so the short-range order of the chosen pair shells approaches the
/// ideal random alloy (icet backend when available, internal annealing
/// otherwise). Accepting the dialog exposes the generated supercell via
/// result().
class SqsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SqsDialog(std::shared_ptr<const core::Structure> structure,
                       QWidget* parent = nullptr);

    const std::optional<pybridge::SqsBuilder::Result>& result() const {
        return result_;
    }

private Q_SLOTS:
    void generate();

private:
    std::shared_ptr<const core::Structure> structure_;
    std::optional<pybridge::SqsBuilder::Result> result_;

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
