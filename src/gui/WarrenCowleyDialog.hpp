#pragma once

#include "core/ChemicalOrder.hpp"
#include "core/Structure.hpp"

#include <QDialog>

#include <memory>

class QDoubleSpinBox;
class QLabel;
class QTableWidget;

namespace calango::gui {

/// Analysis → "Warren-Cowley analysis": short-range order parameters
/// α_ij = 1 − p_ij / c_j for every ordered species pair of the current
/// structure, evaluated on one or two coordination shells. α = 0 is the
/// ideal random alloy, α < 0 unlike-pair ordering, α > 0 clustering.
class WarrenCowleyDialog : public QDialog {
    Q_OBJECT

public:
    explicit WarrenCowleyDialog(std::shared_ptr<const core::Structure> structure,
                                QWidget* parent = nullptr);

private Q_SLOTS:
    void compute();
    void exportData();

private:
    std::shared_ptr<const core::Structure> structure_;
    core::WarrenCowleyResult result_;

    QDoubleSpinBox* shell1Spin_;
    QDoubleSpinBox* shell2Spin_;
    QLabel* infoLabel_;
    QTableWidget* table_;
};

} // namespace calango::gui
