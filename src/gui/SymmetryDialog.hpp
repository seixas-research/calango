#pragma once

#include "core/Structure.hpp"

#include <QDialog>

#include <memory>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace calango::gui {

/// Analysis → "Detect Symmetry…": a standalone crystallographic symmetry
/// report (extracted from the Structure panel). It shows the space group
/// (symbol + number), point group, Hall number, crystal system, and the
/// symmetry-inequivalent sites with their Wyckoff letters at an adjustable
/// tolerance. Inspection only — the cell-transform actions ("Standardize
/// Cell" / "Reduce to Primitive Cell") live in the Edit Structure dialog.
class SymmetryDialog : public QDialog {
    Q_OBJECT

public:
    explicit SymmetryDialog(std::shared_ptr<const core::Structure> structure,
                            QWidget* parent = nullptr);

private Q_SLOTS:
    void detect();

private:
    std::shared_ptr<const core::Structure> structure_;

    QDoubleSpinBox* tolSpin_;
    QLabel* spaceGroupLabel_;
    QLabel* pointGroupLabel_;
    QLabel* crystalLabel_;
    QLabel* hallLabel_;
    QLabel* sitesLabel_;
    QLabel* statusLabel_;
    QTableWidget* table_;
};

} // namespace calango::gui
