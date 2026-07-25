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
    /// "Edit Structure…" pressed. The panel stays read-only: the controller
    /// (MainWindow) owns the document and its undo stack, so it opens the
    /// editor and installs the result.
    void editStructureRequested();

private:
    const core::Structure* structure_ = nullptr; ///< observed, not owned

    QPushButton* editButton_;
    QLabel* formulaLabel_;
    QLabel* atomCountLabel_;
    QLabel* bondCountLabel_;
    QLabel* lengthsLabel_;
    QLabel* anglesLabel_;
    QLabel* cellLabel_;
    QLabel* pbcLabel_;
};

} // namespace calango::gui
