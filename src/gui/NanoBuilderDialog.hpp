#pragma once

#include "core/Structure.hpp"

#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QStackedWidget>

#include <memory>

namespace calango::gui {

/// Specialized nanomaterial generators wrapping ase.build:
///   - graphene sheets (periodic supercells)
///   - graphene nanoribbons (zigzag / armchair, optional H termination)
///   - carbon nanotubes with chiral indices (n, m) and length
///   - TMD monolayers via mx2 (formula, 1T/2H phase, lattice parameters,
///     vacuum spacing)
/// On accept, result() holds the built structure and resultName() a label
/// for the new workspace tab.
class NanoBuilderDialog : public QDialog {
    Q_OBJECT

public:
    explicit NanoBuilderDialog(QWidget* parent = nullptr);

    std::shared_ptr<core::Structure> result() const { return result_; }
    QString resultName() const { return resultName_; }

private Q_SLOTS:
    void build();

private:
    QComboBox* typeCombo_;
    QStackedWidget* pages_;

    // Graphene sheet
    QDoubleSpinBox* sheetASpin_;
    QSpinBox* sheetNxSpin_;
    QSpinBox* sheetNySpin_;
    QDoubleSpinBox* sheetVacuumSpin_;

    // Nanoribbon
    QSpinBox* ribbonWidthSpin_;
    QSpinBox* ribbonLengthSpin_;
    QComboBox* ribbonEdgeCombo_;
    QCheckBox* ribbonSaturateCheck_;
    QDoubleSpinBox* ribbonVacuumSpin_;

    // Nanotube
    QSpinBox* tubeNSpin_;
    QSpinBox* tubeMSpin_;
    QSpinBox* tubeLengthSpin_;
    QDoubleSpinBox* tubeBondSpin_;
    QDoubleSpinBox* tubeVacuumSpin_;

    // TMD (mx2)
    QComboBox* tmdFormulaCombo_;
    QComboBox* tmdPhaseCombo_;
    QDoubleSpinBox* tmdASpin_;
    QDoubleSpinBox* tmdThicknessSpin_;
    QSpinBox* tmdNxSpin_;
    QSpinBox* tmdNySpin_;
    QDoubleSpinBox* tmdVacuumSpin_;

    std::shared_ptr<core::Structure> result_;
    QString resultName_;
};

} // namespace calango::gui
