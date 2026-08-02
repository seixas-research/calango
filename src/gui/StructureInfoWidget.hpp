#pragma once

#include <QLabel>
#include <QPushButton>
#include <QWidget>

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Read-only side panel summarizing the current structure: formula, atom
/// and bond counts, and full lattice parameters (a, b, c, α, β, γ, volume,
/// periodicity). Crystallographic symmetry has its own dedicated tool under
/// Analysis → Symmetry…, keeping this dock uncluttered and free of
/// Python calls during trajectory playback.
class StructureInfoWidget : public QWidget {
    Q_OBJECT

public:
    explicit StructureInfoWidget(QWidget* parent = nullptr);

public Q_SLOTS:
    void updateFromStructure(const core::Structure* structure);

Q_SIGNALS:
    // The panel stays read-only and REQUESTS: the controller (MainWindow) owns
    // the document and its undo stack, so every one of these is performed
    // there. That is what makes the transforms undoable, which they were not
    // when they lived inside the Edit Structure dialog and were reverted only
    // by cancelling it.
    void editStructureRequested();
    /// Translate every atom so the centroid sits at the cell centre.
    void centerStructureRequested();
    /// Extend the cell along chosen lattice directions (prompts for how much).
    void addVacuumRequested();
    /// Fold atoms back inside the cell by whole lattice vectors.
    void wrapIntoCellRequested();
    /// Open the Supercell builder.
    void supercellRequested();

private:
    /// Enable/disable the action row against what the current structure can
    /// actually support (three of the four need a cell).
    void updateActionsEnabled();

    const core::Structure* structure_ = nullptr; ///< observed, not owned

    // "Edit Structure…" sits alone on a full-width row between the summary
    // and the transforms, icon plus text: it opens the whole editing dialog,
    // and shown as one glyph among the transforms it read as their peer. The
    // four one-click transforms stay one icon-only row — spelled out, that
    // row wrapped to three lines in a narrow dock and pushed the property
    // summary out of view.
    QPushButton* editButton_;
    QPushButton* centerButton_;
    QPushButton* vacuumButton_;
    QPushButton* wrapButton_;
    QPushButton* supercellButton_;
    QLabel* formulaLabel_;
    QLabel* atomCountLabel_;
    QLabel* bondCountLabel_;
    QLabel* lengthsLabel_;
    QLabel* anglesLabel_;
    QLabel* cellLabel_;
    QLabel* pbcLabel_;
};

} // namespace calango::gui
