#pragma once

#include "core/CalculatorConfig.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Geometry Optimization → "Geometry constraints…": which degrees of freedom
/// the relaxation is not allowed to move.
///
/// Two tabs, because the two ways of naming the frozen atoms are genuinely
/// different jobs:
///
///   1. "Atoms" — a per-atom table of the structure (index, element, x/y/z)
///      with an x/y/z freeze mask per row. This is the direct answer to "hold
///      THIS atom", and to "let that adatom slide in the surface plane but not
///      along z". Rows can be filtered and mass-assigned so a 400-atom slab
///      does not have to be ticked one row at a time.
///   2. "Regions" — bounds along one Cartesian axis (the classic "freeze
///      everything with z < 5 Å", i.e. the bottom layers of a slab) plus the
///      same x/y/z mask. A region keeps its BOUNDS, not the atoms they
///      currently select: the generated script re-evaluates them against the
///      geometry it reads, so re-running on a thicker slab still freezes the
///      bottom rather than whatever the old indices pointed at.
///
/// The result is a list of core::GeometryConstraint, which
/// AseScriptGenerator turns into ASE FixAtoms / FixCartesian objects.
class GeometryConstraintsDialog : public QDialog {
    Q_OBJECT

public:
    /// `structure` populates the per-atom table; it may be null (or empty), in
    /// which case only region rules can be defined — a wizard opened without a
    /// loaded structure still has something useful to say.
    GeometryConstraintsDialog(
        std::shared_ptr<const core::Structure> structure,
        const std::vector<core::GeometryConstraint>& initial,
        QWidget* parent = nullptr);

    /// The edited rules, with empty and freeze-nothing entries dropped.
    std::vector<core::GeometryConstraint> constraints() const;

private Q_SLOTS:
    void addRegion();
    void removeSelectedRegions();
    /// Apply the freeze mask of the "assign" row to every selected atom row.
    void assignToSelectedAtoms();
    /// Free every atom row (the escape hatch from a mis-click on 400 rows).
    void clearAtomConstraints();
    /// Re-count what is frozen and refresh the summary line.
    void updateSummary();

private:
    QWidget* buildAtomsTab();
    QWidget* buildRegionsTab();
    /// Seed the two tabs from `initial`: index rules become ticked atom rows,
    /// region rules become table rows.
    void applyInitial(const std::vector<core::GeometryConstraint>& initial);
    void appendRegionRow(const core::GeometryConstraint& region);
    /// The x/y/z freeze mask of atom row `row`.
    void atomMask(int row, bool mask[3]) const;
    void setAtomMask(int row, const bool mask[3]);

    std::shared_ptr<const core::Structure> structure_;

    QTableWidget* atomTable_ = nullptr;
    QLineEdit* atomFilterEdit_ = nullptr;
    QCheckBox* assignChecks_[3] = {nullptr, nullptr, nullptr};
    QTableWidget* regionTable_ = nullptr;
    QPushButton* removeRegionButton_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
};

} // namespace calango::gui
