#pragma once

#include <QLabel>
#include <QWidget>

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Read-only side panel summarizing the current structure: formula, atom
/// and bond counts, and full lattice parameters (a, b, c, α, β, γ, volume,
/// periodicity). Crystallographic symmetry has its own dedicated tool under
/// Analysis → Detect Symmetry…, keeping this dock uncluttered and free of
/// Python calls during trajectory playback.
class StructureInfoWidget : public QWidget {
    Q_OBJECT

public:
    explicit StructureInfoWidget(QWidget* parent = nullptr);

public Q_SLOTS:
    void updateFromStructure(const core::Structure* structure);

private:
    const core::Structure* structure_ = nullptr; ///< observed, not owned

    QLabel* formulaLabel_;
    QLabel* atomCountLabel_;
    QLabel* bondCountLabel_;
    QLabel* lengthsLabel_;
    QLabel* anglesLabel_;
    QLabel* cellLabel_;
    QLabel* pbcLabel_;
};

} // namespace calango::gui
