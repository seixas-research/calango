#pragma once

#include "core/Structure.hpp"
#include "core/StructureTransforms.hpp"

#include <QDialog>

#include <array>
#include <memory>

class QCheckBox;
class QComboBox;
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
///     either; the other follows. Rows can be sorted by element or by any
///     coordinate, which RENUMBERS the atoms, and a spin-polarization mode
///     adds editable magnetic-moment columns that become the structure's
///     initial moments. Extended per-atom arrays appear as trailing columns:
///     velocities and forces editable (they are inputs as much as results),
///     everything else read-only (see PropertyColumn).
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

    /// The LAST cell transform applied in this session, or None.
    ///
    /// A crystallographic cell transform is not an edit of one structure the
    /// way moving an atom is — it redefines the lattice the coordinates are
    /// expressed in. On a trajectory that has to reach every frame, or the
    /// frames end up in different cells and nothing computed across them means
    /// anything. The caller reads this to know it has to propagate.
    ///
    /// Last rather than a list: each one replaces the working structure
    /// outright, so only the final one describes the lattice the result is in.
    core::CellTransform cellTransform() const { return cellTransform_; }

private Q_SLOTS:
    /// Lattice parameters edited -> rebuild vectors -> refresh vector spins.
    void applyLatticeParameters();
    /// Lattice vectors edited -> refresh the parameter spins.
    void applyLatticeVectors();
    void defineUnitCell();
    /// "Translate atoms…": shift every atom by a vector given in Å or in
    /// fractional cell coordinates.
    void translateAtoms();
    /// Crystallographic cell transforms via Spglib (through AseBridge):
    /// "Standardize Cell" (spglib.standardize_cell) and "Reduce to Primitive
    /// Cell" (spglib.find_primitive). Both replace the working structure and
    /// refresh the dialog; on accept the caller installs it via undo.
    void standardizeCell();
    void reduceToPrimitiveCell();
    /// "Set as origin": put the single selected atom at (0, 0, 0) and shift
    /// every other atom by the same vector. A rigid translation, so distances
    /// and the lattice are untouched.
    void setSelectedAsOrigin();
    /// One cell of the atom table was edited by the user.
    void onAtomCellChanged(int row, int column);
    /// "Sort": permute the atoms by the selected key. This renumbers them, so
    /// it goes through Structure::reorder() rather than sorting the view.
    void applySort();
    /// The spin-polarization mode changed: add or drop the moment columns and
    /// rewrite the structure's initial moments to match.
    void onSpinModeChanged();

private:
    void buildCellSection(QVBoxLayout* parent);
    void buildAtomSection(QVBoxLayout* parent);

    /// How many moment columns the current spin mode shows: 0, 1 or 3.
    int momentColumnCount() const;
    /// Column index of the first moment column (== AtomColumnCount).
    static constexpr int kFirstMomentColumn = 4;

    /// One table column carrying an extended per-atom property that arrived
    /// with the structure — a partial-charge array, a velocity or force
    /// vector, anything an extended-XYZ file put in `atoms.arrays`.
    ///
    /// Read-only by default, on purpose: most of these are RESULTS — a charge
    /// that came out of a Bader partitioning, a magnitude the import bridge
    /// derived. Letting them be typed over in a geometry editor would produce
    /// a frame whose arrays no longer correspond to anything that was
    /// computed, and the file would carry no sign of it.
    ///
    /// Velocities and forces are the two deliberate exceptions, editable like
    /// the coordinates, because both are legitimately INPUT as well as output:
    /// hand-set velocities are the initial conditions of an MD run, and
    /// hand-set forces are how force-field training frames are assembled and
    /// how force-arrow viewers are exercised. Edits write straight into the
    /// structure's "velocities" / "forces" vector fields — the same storage
    /// the ASE bridge exports (velocities through set_velocities(), so the
    /// momenta column follows) and that reorder() permutes, so an edited value
    /// survives both a sort and the round trip to extxyz. Columns exist only
    /// for arrays the structure already carries; editing never invents one.
    struct PropertyColumn {
        QString header;
        std::string field;  ///< key in scalarFields() / vectorFields()
        bool vector = false;
        int component = 0;  ///< 0/1/2 for a vector field, ignored otherwise
        bool editable = false; ///< true only for velocities/forces components
    };
    /// The property columns to show, in a stable order. Excludes the magnetic
    /// moments, which the spin-mode columns already own — showing them twice,
    /// once editable and once not, is worse than not showing them at all.
    std::vector<PropertyColumn> propertyColumns() const;
    /// Column index of the first property column.
    int firstPropertyColumn() const;
    /// Read the moment columns out of the table and store them on the working
    /// structure as `initial_magmoms`.
    void writeMomentsToStructure();
    /// Per-atom moments currently on the structure, as (mx, my, mz). Zero for
    /// atoms that carry none, so the table always has something to show.
    std::vector<core::Vec3> momentsFromStructure() const;

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
    core::CellTransform cellTransform_ = core::CellTransform::None;
    bool updating_ = false; ///< guards the two-way widget<->model sync

    std::array<QDoubleSpinBox*, 3> lengthSpins_{};
    std::array<QDoubleSpinBox*, 3> angleSpins_{};
    std::array<std::array<QDoubleSpinBox*, 3>, 3> vectorSpins_{};
    std::array<QCheckBox*, 3> pbcChecks_{};
    QStackedWidget* cellStack_ = nullptr;
    QPushButton* defineCellButton_ = nullptr;
    QPushButton* standardizeButton_ = nullptr;
    QPushButton* primitiveButton_ = nullptr;
    QPushButton* translateButton_ = nullptr;
    QPushButton* originButton_ = nullptr;
    QComboBox* sortKeyCombo_ = nullptr;
    QCheckBox* sortDescendingCheck_ = nullptr;
    QComboBox* spinModeCombo_ = nullptr;
    /// "Fractional coordinates": which of the two representations the three
    /// coordinate columns show. Unchecked (Cartesian) by default, and disabled
    /// outright without a cell — fractional coordinates are not merely
    /// unavailable there, they are undefined.
    QCheckBox* fractionalCheck_ = nullptr;
    QTableWidget* atomTable_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
};

} // namespace calango::gui
