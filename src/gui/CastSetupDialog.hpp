#pragma once

#include "render/StructureRenderer.hpp"

#include <QDialog>

#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Representation → "Cast Setup": which cast each atom belongs to.
///
/// A cast is a group of atoms drawn in its own representation, so one scene can
/// show a metal slab as space-filling CPK spheres (cast 0) and the molecule
/// adsorbed on it as ball-and-stick (cast 1) — the standard way a
/// surface-science figure separates substrate from adsorbate.
///
/// The dialog lists the atoms with their current cast, lets casts be added and
/// removed, and reassigns atoms one at a time or in bulk (filter the table,
/// select rows, assign). Removing a cast folds its atoms back into cast 0
/// rather than leaving them pointing at an index that no longer exists.
///
/// It edits the assignment LIVE: the viewport is a dock the user is looking at
/// while choosing, and a preview that only appears on OK would make the choice
/// blind. Cancel restores the assignment as it was on open.
class CastSetupDialog : public QDialog {
    Q_OBJECT

public:
    /// `viewport` owns the style the casts live in and is repainted on every
    /// edit. `structure` supplies the atom list; a null or empty structure
    /// leaves the table empty but still allows casts to be defined.
    CastSetupDialog(class ViewportWidget* viewport,
                    std::shared_ptr<const core::Structure> structure,
                    QWidget* parent = nullptr);

public Q_SLOTS:
    /// Restores the assignment the dialog opened with. Overridden rather than
    /// wired to the Cancel button alone: Esc and the window close button reach
    /// reject() directly, and those must undo the live edits too.
    void reject() override;

private Q_SLOTS:
    void addCast();
    void removeLastCast();
    /// Put every selected atom row into the cast chosen in the assign row.
    void assignSelected();

private:
    /// Rebuild the atom table from the current assignment.
    void refreshTable();
    /// Re-fill every cast combo (per row and in the assign row) after the cast
    /// count changed.
    void refreshCastChoices();
    /// Push the edited assignment into the viewport and repaint.
    void apply();
    void updateSummary();
    /// The style's cast vector, resized to the atom count if it is stale.
    std::vector<int>& casts();

    ViewportWidget* viewport_;
    std::shared_ptr<const core::Structure> structure_;
    /// Assignment + per-cast modes as they were when the dialog opened, so
    /// Cancel is a real undo rather than "close and hope".
    std::vector<int> initialCasts_;
    std::vector<render::RepresentationMode> initialCastModes_;
    render::RepresentationMode initialMode_;

    QTableWidget* table_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QComboBox* assignCastCombo_ = nullptr;
    QPushButton* removeCastButton_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    /// Guards the per-row combo handler while refreshTable() fills the table.
    bool populating_ = false;
};

} // namespace calango::gui
