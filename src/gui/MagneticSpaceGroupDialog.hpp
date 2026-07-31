#pragma once

#include "core/Structure.hpp"
#include "python_bridge/MagneticSpaceGroup.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QTableWidget;

namespace calango::gui {

/// Analysis → "Magnetic Space Group…": which of the 1651 magnetic space groups
/// (MSGs) the current structure realizes, given its magnetic moments.
///
/// The classification is the Belov-Neronova-Smirnova one described in
/// Watanabe, Po & Vishwanath, Sci. Adv. 4, eaat8685 (2018) — see
/// pybridge::MagneticSpaceGroup for the four types and what distinguishes
/// them. The dialog reports the BNS label `S.L`, the type, the parent space
/// group, and — always beside it — the CRYSTALLOGRAPHIC space group the same
/// structure would have with the moments ignored. The comparison is the
/// physics: magnetic order can only lower the symmetry, and the operations it
/// removed from the unitary group are exactly the ones it broke.
///
/// The moments are shown in an editable table rather than merely consumed.
/// A structure can carry the moments a calculation CONVERGED to, the ones it
/// was SEEDED with, or neither, and those are different questions — and asking
/// "what would the magnetic space group be if this sublattice flipped?" is the
/// normal way to use this, which needs the moments to be an input the user
/// controls rather than a fact the file dictates.
class MagneticSpaceGroupDialog : public QDialog {
    Q_OBJECT

public:
    explicit MagneticSpaceGroupDialog(
        std::shared_ptr<const core::Structure> structure,
        QWidget* parent = nullptr);

private Q_SLOTS:
    /// Re-read the moments from the table and re-run the determination.
    void detect();

private:
    /// Refill the moment table from the selected source, discarding edits.
    void reloadMoments();
    /// The moment table's contents, one vector per atom.
    std::vector<core::Vec3> tableMoments() const;
    void showResult(const pybridge::MagneticSpaceGroup::Result& result);
    void clearResult(const QString& reason);

    std::shared_ptr<const core::Structure> structure_;

    QComboBox* sourceCombo_ = nullptr;
    QDoubleSpinBox* tolSpin_ = nullptr;
    QDoubleSpinBox* magTolSpin_ = nullptr;

    QLabel* bnsLabel_ = nullptr;
    QLabel* typeLabel_ = nullptr;
    QLabel* ogLabel_ = nullptr;
    QLabel* parentLabel_ = nullptr;
    QLabel* crystalLabel_ = nullptr;
    QLabel* pointGroupLabel_ = nullptr;
    QLabel* orderLabel_ = nullptr;
    QLabel* orderingLabel_ = nullptr;
    QLabel* classificationLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    /// Columns: #, element, x, y, z, mx, my, mz, magnetic class.
    QTableWidget* momentTable_ = nullptr;
    QTableWidget* operationTable_ = nullptr;
};

} // namespace calango::gui
