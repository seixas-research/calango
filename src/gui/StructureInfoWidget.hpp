#pragma once

#include <QLabel>
#include <QWidget>

class QDoubleSpinBox;
class QPushButton;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Read-only side panel summarizing the current structure: formula, atom
/// and bond counts, and full lattice parameters (a, b, c, α, β, γ, volume,
/// periodicity). Crystallographic symmetry (space group, point group,
/// crystal system via spglib through the embedded interpreter) is
/// computed on demand by the "Detect Symmetry" action with a
/// user-adjustable tolerance — never automatically, so trajectory
/// playback stays free of Python calls.
class StructureInfoWidget : public QWidget {
    Q_OBJECT

public:
    explicit StructureInfoWidget(QWidget* parent = nullptr);

public Q_SLOTS:
    void updateFromStructure(const core::Structure* structure);

private Q_SLOTS:
    void detectSymmetry();

private:
    const core::Structure* structure_ = nullptr; ///< observed, not owned

    QLabel* formulaLabel_;
    QLabel* atomCountLabel_;
    QLabel* bondCountLabel_;
    QLabel* lengthsLabel_;
    QLabel* anglesLabel_;
    QLabel* cellLabel_;
    QLabel* pbcLabel_;
    QDoubleSpinBox* symprecSpin_;
    QPushButton* detectButton_;
    QLabel* spaceGroupLabel_;
    QLabel* pointGroupLabel_;
    QLabel* crystalSystemLabel_;
};

} // namespace calango::gui
