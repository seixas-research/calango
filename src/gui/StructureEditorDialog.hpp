#pragma once

#include "core/Structure.hpp"

#include <QDialog>

#include <array>
#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QVBoxLayout;

namespace calango::gui {

/// "Edit Structure…" (Structure panel, zone 5): a two-section editor over a
/// working copy of the current structure.
///
///  1. Unit Cell Editor — lattice parameters (a, b, c, α, β, γ) or the raw
///     lattice vectors v1/v2/v3, kept in sync; per-axis periodicity; and a
///     "Define Unit Cell…" path that seats an isolated molecule in a
///     bounding box when it has no cell at all.
///  2. Atomic Positions & Elements — an editable table of every site with
///     both Cartesian (x, y, z) and fractional (u, v, w) coordinates. Edit
///     either; the other follows.
///
/// Plus two whole-structure transformations: centering the atoms in the
/// cell, and padding the cell with vacuum along chosen directions.
///
/// The dialog never mutates the caller's structure. On accept the caller
/// takes result() and installs it through its own undo-aware path.
class StructureEditorDialog : public QDialog {
    Q_OBJECT

public:
    StructureEditorDialog(const core::Structure& structure,
                          QWidget* parent = nullptr);

    /// The edited structure (valid after exec() returns Accepted).
    std::shared_ptr<core::Structure> result() const { return working_; }

private Q_SLOTS:
    /// Lattice parameters edited -> rebuild vectors -> refresh vector spins.
    void applyLatticeParameters();
    /// Lattice vectors edited -> refresh the parameter spins.
    void applyLatticeVectors();
    void defineUnitCell();
    void centerInUnitCell();
    void addVacuumLayer();
    /// One cell of the atom table was edited by the user.
    void onAtomCellChanged(int row, int column);

private:
    void buildCellSection(QVBoxLayout* parent);
    void buildAtomSection(QVBoxLayout* parent);

    /// Repopulate every widget from working_ (suppressing edit signals).
    void refreshAll();
    void refreshCellWidgets();
    void refreshAtomTable();
    void refreshSummary();
    /// True when the working structure has a non-degenerate cell.
    bool hasCell() const;

    /// Cell vectors implied by the current a/b/c/α/β/γ spin values, in the
    /// standard crystallographic orientation (a along x, b in the xy plane).
    std::array<core::Vec3, 3> vectorsFromParameters() const;

    std::shared_ptr<core::Structure> working_;
    bool updating_ = false; ///< guards the two-way widget<->model sync

    std::array<QDoubleSpinBox*, 3> lengthSpins_{};
    std::array<QDoubleSpinBox*, 3> angleSpins_{};
    std::array<std::array<QDoubleSpinBox*, 3>, 3> vectorSpins_{};
    std::array<QCheckBox*, 3> pbcChecks_{};
    QStackedWidget* cellStack_ = nullptr;
    QPushButton* defineCellButton_ = nullptr;
    QPushButton* centerButton_ = nullptr;
    QPushButton* vacuumButton_ = nullptr;
    QTableWidget* atomTable_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
};

} // namespace calango::gui
