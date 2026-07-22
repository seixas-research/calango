#include "gui/StructureInfoWidget.hpp"

#include "core/Structure.hpp"

#include <QFormLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

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
    , formulaLabel_(new QLabel(this))
    , atomCountLabel_(new QLabel(this))
    , bondCountLabel_(new QLabel(this))
    , lengthsLabel_(new QLabel(this))
    , anglesLabel_(new QLabel(this))
    , cellLabel_(new QLabel(this))
    , pbcLabel_(new QLabel(this))
{
    // Symmetry detection lives in its own dialog (Analysis → Detect
    // Symmetry…); this panel stays a lean read-only structure summary.
    auto* layout = new QFormLayout(this);
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

void StructureInfoWidget::updateFromStructure(const core::Structure* structure)
{
    structure_ = structure;

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
