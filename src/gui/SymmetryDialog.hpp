#pragma once

#include "core/Structure.hpp"

#include <QDialog>

#include <memory>
#include <optional>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace calango::gui {

/// Analysis → "Detect Symmetry…": a standalone crystallographic symmetry
/// report and cell-transform tool (extracted from the Structure panel). It
/// shows the space group (symbol + number), point group, Hall number, crystal
/// system, and the symmetry-inequivalent sites with their Wyckoff letters at
/// an adjustable tolerance, and can standardize the cell or reduce it to the
/// primitive cell. A transform exposes the new structure via result().
class SymmetryDialog : public QDialog {
    Q_OBJECT

public:
    explicit SymmetryDialog(std::shared_ptr<const core::Structure> structure,
                            QWidget* parent = nullptr);

    /// A standardized / primitive structure produced by the transform buttons,
    /// or nullopt if the user only inspected the symmetry.
    const std::optional<core::Structure>& result() const { return result_; }
    QString resultName() const { return resultName_; }

private Q_SLOTS:
    void detect();
    void standardize(bool toPrimitive);

private:
    std::shared_ptr<const core::Structure> structure_;
    std::optional<core::Structure> result_;
    QString resultName_;

    QDoubleSpinBox* tolSpin_;
    QLabel* spaceGroupLabel_;
    QLabel* pointGroupLabel_;
    QLabel* crystalLabel_;
    QLabel* hallLabel_;
    QLabel* sitesLabel_;
    QLabel* statusLabel_;
    QTableWidget* table_;
    QPushButton* standardizeButton_;
    QPushButton* primitiveButton_;
};

} // namespace calango::gui
