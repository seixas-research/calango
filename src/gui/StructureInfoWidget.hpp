#pragma once

#include <QLabel>
#include <QWidget>

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Read-only side panel summarizing the current structure: formula, atom
/// and bond counts, full lattice parameters (a, b, c, α, β, γ, volume,
/// periodicity) and crystallographic symmetry (space group, point group,
/// crystal system via spglib through the embedded interpreter). A pure
/// View: no model mutation.
class StructureInfoWidget : public QWidget {
    Q_OBJECT

public:
    explicit StructureInfoWidget(QWidget* parent = nullptr);

public Q_SLOTS:
    void updateFromStructure(const core::Structure* structure);

private:
    QLabel* formulaLabel_;
    QLabel* atomCountLabel_;
    QLabel* bondCountLabel_;
    QLabel* lengthsLabel_;
    QLabel* anglesLabel_;
    QLabel* cellLabel_;
    QLabel* pbcLabel_;
    QLabel* spaceGroupLabel_;
    QLabel* pointGroupLabel_;
    QLabel* crystalSystemLabel_;
};

} // namespace calango::gui
