#include "gui/StructureInfoWidget.hpp"

#include "core/Structure.hpp"
#include "ui/IconManager.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

/// Horizontal inset for the property rows (Formula … Periodic), in logical
/// pixels. Generous on purpose: the summary is a block of short label/value
/// pairs, and letting it breathe away from both dock edges is what separates
/// it visually from the full-width controls above and below rather than
/// leaving it looking crammed against the window frame.
constexpr int kFormSideMargin = 24;

double vectorAngleDeg(const core::Vec3& a, const core::Vec3& b)
{
    const double lengths = a.norm() * b.norm();
    if (lengths < 1e-12)
        return 0.0;
    return std::acos(std::clamp(a.dot(b) / lengths, -1.0, 1.0)) * 180.0 / M_PI;
}

} // namespace

StructureInfoWidget::StructureInfoWidget(QWidget* parent)
    : QWidget(parent)
    , editButton_(new QPushButton(this))
    , centerButton_(new QPushButton(this))
    , vacuumButton_(new QPushButton(this))
    , wrapButton_(new QPushButton(this))
    , supercellButton_(new QPushButton(this))
    , formulaLabel_(new QLabel(this))
    , atomCountLabel_(new QLabel(this))
    , bondCountLabel_(new QLabel(this))
    , lengthsLabel_(new QLabel(this))
    , anglesLabel_(new QLabel(this))
    , cellLabel_(new QLabel(this))
    , pbcLabel_(new QLabel(this))
{
    // Symmetry detection lives in its own dialog (Analysis → Detect
    // Symmetry…); this panel stays a lean read-only *summary* — the one
    // action it offers hands editing off to a dedicated dialog rather than
    // making the labels editable in place.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* form = new QFormLayout;
    // Inset the property block (Formula … Periodic) from the dock edge. With
    // zero margins the label column sits flush against the window frame, which
    // reads as clipped text rather than as a deliberate flush-left layout.
    // Applied to the form alone so the action row below spans the panel's
    // full width.
    form->setContentsMargins(kFormSideMargin, 0, kFormSideMargin, 0);
    outer->addLayout(form);

    // The structure-modification row. Centring, vacuum padding and wrapping
    // used to be buttons INSIDE the Edit Structure dialog, acting on its
    // working copy; the supercell builder was a Build-menu entry. All four are
    // one-click whole-structure transforms, so they belong together, on the
    // panel that already shows what they change.
    auto* actionRow = new QHBoxLayout;
    actionRow->setSpacing(4);
    const auto makeAction = [actionRow](QPushButton* button,
                                        const QString& icon,
                                        const QString& tip) {
        ui::IconManager::bind(button, icon);
        button->setIconSize(QSize(20, 20));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        button->setEnabled(false);
        actionRow->addWidget(button);
    };
    makeAction(editButton_, QStringLiteral("edit-box-fill"),
               tr("Edit Structure… — lattice parameters, lattice vectors and "
                  "atomic positions."));
    makeAction(centerButton_, QStringLiteral("align-item-vertical-center-line"),
               tr("Center Structure — translate every atom so the centroid "
                  "sits at the center of the cell."));
    makeAction(vacuumButton_, QStringLiteral("expand-height-line"),
               tr("Add vacuum… — extend the cell along chosen lattice "
                  "directions and re-center the atoms in the enlarged cell."));
    makeAction(wrapButton_, QStringLiteral("contract-left-right-line"),
               tr("Wrap within the unit cell — translate atoms by whole "
                  "lattice vectors until they lie inside the cell. Acts on the "
                  "viewport selection, or on every atom when nothing is "
                  "selected."));
    makeAction(supercellButton_, QStringLiteral("grid-line"),
               tr("Supercell… — build a supercell from a repetition or "
                  "transformation matrix."));
    actionRow->addStretch(1);
    outer->addLayout(actionRow);
    outer->addStretch(1);

    connect(editButton_, &QPushButton::clicked,
            this, &StructureInfoWidget::editStructureRequested);
    connect(centerButton_, &QPushButton::clicked,
            this, &StructureInfoWidget::centerStructureRequested);
    connect(vacuumButton_, &QPushButton::clicked,
            this, &StructureInfoWidget::addVacuumRequested);
    connect(wrapButton_, &QPushButton::clicked,
            this, &StructureInfoWidget::wrapIntoCellRequested);
    connect(supercellButton_, &QPushButton::clicked,
            this, &StructureInfoWidget::supercellRequested);

    QFormLayout* layout = form;
    layout->addRow(tr("Formula:"), formulaLabel_);
    layout->addRow(tr("Atoms:"), atomCountLabel_);
    layout->addRow(tr("Bonds:"), bondCountLabel_);
    layout->addRow(tr("a, b, c:"), lengthsLabel_);
    layout->addRow(tr("α, β, γ:"), anglesLabel_);
    layout->addRow(tr("Cell volume:"), cellLabel_);
    layout->addRow(tr("Periodic:"), pbcLabel_);
    for (QLabel* label : {lengthsLabel_, anglesLabel_})
        label->setWordWrap(true);
    updateFromStructure(nullptr);
}

void StructureInfoWidget::updateActionsEnabled()
{
    const bool hasAtoms = structure_ != nullptr && !structure_->empty();
    // Three of the four are defined against a lattice: centring needs a cell
    // centre to move to, vacuum needs a cell to extend, wrapping needs
    // boundaries to wrap into, and a supercell needs vectors to repeat.
    // Offering them on an isolated molecule would only produce a message box.
    const bool hasCell = hasAtoms && structure_->cell().isDefined();
    editButton_->setEnabled(hasAtoms);
    centerButton_->setEnabled(hasCell);
    vacuumButton_->setEnabled(hasCell);
    wrapButton_->setEnabled(hasCell);
    supercellButton_->setEnabled(hasCell);
}

void StructureInfoWidget::updateFromStructure(const core::Structure* structure)
{
    structure_ = structure;
    updateActionsEnabled();

    if (!structure || structure->empty()) {
        for (QLabel* label : {formulaLabel_, atomCountLabel_, bondCountLabel_,
                              lengthsLabel_, anglesLabel_, cellLabel_, pbcLabel_})
            label->setText(QStringLiteral("—"));
        return;
    }

    formulaLabel_->setText(QString::fromStdString(structure->chemicalFormula()));
    atomCountLabel_->setText(QString::number(structure->size()));
    bondCountLabel_->setText(QString::number(structure->detectBonds().size()));

    if (structure->cell().isDefined()) {
        const auto& v = structure->cell().vectors();
        lengthsLabel_->setText(QStringLiteral("%1, %2, %3 Å")
                                   .arg(v[0].norm(), 0, 'f', 2)
                                   .arg(v[1].norm(), 0, 'f', 2)
                                   .arg(v[2].norm(), 0, 'f', 2));
        // Crystallographic convention: α = ∠(b, c), β = ∠(a, c), γ = ∠(a, b).
        anglesLabel_->setText(QStringLiteral("%1°, %2°, %3°")
                                  .arg(vectorAngleDeg(v[1], v[2]), 0, 'f', 2)
                                  .arg(vectorAngleDeg(v[0], v[2]), 0, 'f', 2)
                                  .arg(vectorAngleDeg(v[0], v[1]), 0, 'f', 2));
        cellLabel_->setText(QStringLiteral("%1 Å³")
                                .arg(structure->cell().volume(), 0, 'f', 2));
        const auto pbc = structure->cell().pbc();
        pbcLabel_->setText(QStringLiteral("%1 %2 %3")
                               .arg(pbc[0] ? "T" : "F", pbc[1] ? "T" : "F",
                                    pbc[2] ? "T" : "F"));
    } else {
        lengthsLabel_->setText(tr("none"));
        anglesLabel_->setText(QStringLiteral("—"));
        cellLabel_->setText(tr("none"));
        pbcLabel_->setText(QStringLiteral("F F F"));
    }
}

} // namespace calango::gui
