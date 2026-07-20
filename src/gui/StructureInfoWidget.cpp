#include "gui/StructureInfoWidget.hpp"

#include "core/Structure.hpp"

#include <QFormLayout>

namespace calango::gui {

StructureInfoWidget::StructureInfoWidget(QWidget* parent)
    : QWidget(parent)
    , formulaLabel_(new QLabel(this))
    , atomCountLabel_(new QLabel(this))
    , bondCountLabel_(new QLabel(this))
    , cellLabel_(new QLabel(this))
    , pbcLabel_(new QLabel(this))
{
    auto* layout = new QFormLayout(this);
    layout->addRow(tr("Formula:"), formulaLabel_);
    layout->addRow(tr("Atoms:"), atomCountLabel_);
    layout->addRow(tr("Bonds:"), bondCountLabel_);
    layout->addRow(tr("Cell volume:"), cellLabel_);
    layout->addRow(tr("Periodic:"), pbcLabel_);
    updateFromStructure(nullptr);
}

void StructureInfoWidget::updateFromStructure(const core::Structure* structure)
{
    if (!structure || structure->empty()) {
        for (QLabel* label : {formulaLabel_, atomCountLabel_, bondCountLabel_,
                              cellLabel_, pbcLabel_})
            label->setText(QStringLiteral("—"));
        return;
    }

    formulaLabel_->setText(QString::fromStdString(structure->chemicalFormula()));
    atomCountLabel_->setText(QString::number(structure->size()));
    bondCountLabel_->setText(QString::number(structure->detectBonds().size()));

    if (structure->cell().isDefined()) {
        cellLabel_->setText(QStringLiteral("%1 Å³")
                                .arg(structure->cell().volume(), 0, 'f', 2));
        const auto pbc = structure->cell().pbc();
        pbcLabel_->setText(QStringLiteral("%1 %2 %3")
                               .arg(pbc[0] ? "T" : "F", pbc[1] ? "T" : "F",
                                    pbc[2] ? "T" : "F"));
    } else {
        cellLabel_->setText(tr("none"));
        pbcLabel_->setText(QStringLiteral("F F F"));
    }
}

} // namespace calango::gui
