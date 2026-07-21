#include "gui/StructureInfoWidget.hpp"

#include "core/Structure.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>

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
    , symprecSpin_(new QDoubleSpinBox(this))
    , detectButton_(new QPushButton(tr("Detect Symmetry"), this))
    , spaceGroupLabel_(new QLabel(this))
    , pointGroupLabel_(new QLabel(this))
    , crystalSystemLabel_(new QLabel(this))
{
    auto* layout = new QFormLayout(this);
    layout->addRow(tr("Formula:"), formulaLabel_);
    layout->addRow(tr("Atoms:"), atomCountLabel_);
    layout->addRow(tr("Bonds:"), bondCountLabel_);
    layout->addRow(tr("a, b, c:"), lengthsLabel_);
    layout->addRow(tr("α, β, γ:"), anglesLabel_);
    layout->addRow(tr("Cell volume:"), cellLabel_);
    layout->addRow(tr("Periodic:"), pbcLabel_);

    symprecSpin_->setRange(1e-6, 1.0);
    symprecSpin_->setDecimals(6);
    symprecSpin_->setSingleStep(0.001);
    symprecSpin_->setValue(0.001);
    symprecSpin_->setSuffix(tr(" Å"));
    symprecSpin_->setToolTip(tr("spglib symmetry tolerance (symprec)"));
    layout->addRow(tr("Tolerance:"), symprecSpin_);
    layout->addRow(detectButton_);
    connect(detectButton_, &QPushButton::clicked,
            this, &StructureInfoWidget::detectSymmetry);

    layout->addRow(tr("Space group:"), spaceGroupLabel_);
    layout->addRow(tr("Point group:"), pointGroupLabel_);
    layout->addRow(tr("Crystal system:"), crystalSystemLabel_);
    for (QLabel* label : {lengthsLabel_, anglesLabel_, spaceGroupLabel_})
        label->setWordWrap(true);
    updateFromStructure(nullptr);
}

void StructureInfoWidget::updateFromStructure(const core::Structure* structure)
{
    structure_ = structure;

    // Symmetry results are on-demand and become stale on any structure
    // change — clear them rather than showing yesterday's space group.
    for (QLabel* label : {spaceGroupLabel_, pointGroupLabel_, crystalSystemLabel_})
        label->setText(QStringLiteral("—"));

    if (!structure || structure->empty()) {
        for (QLabel* label : {formulaLabel_, atomCountLabel_, bondCountLabel_,
                              lengthsLabel_, anglesLabel_, cellLabel_, pbcLabel_})
            label->setText(QStringLiteral("—"));
        detectButton_->setEnabled(false);
        return;
    }

    formulaLabel_->setText(QString::fromStdString(structure->chemicalFormula()));
    atomCountLabel_->setText(QString::number(structure->size()));
    bondCountLabel_->setText(QString::number(structure->detectBonds().size()));

    if (structure->cell().isDefined()) {
        const auto& v = structure->cell().vectors();
        lengthsLabel_->setText(QStringLiteral("%1, %2, %3 Å")
                                   .arg(v[0].norm(), 0, 'f', 4)
                                   .arg(v[1].norm(), 0, 'f', 4)
                                   .arg(v[2].norm(), 0, 'f', 4));
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
        detectButton_->setEnabled(true);
    } else {
        lengthsLabel_->setText(tr("none"));
        anglesLabel_->setText(QStringLiteral("—"));
        cellLabel_->setText(tr("none"));
        pbcLabel_->setText(QStringLiteral("F F F"));
        for (QLabel* label : {spaceGroupLabel_, pointGroupLabel_, crystalSystemLabel_})
            label->setText(tr("aperiodic"));
        detectButton_->setEnabled(false);
    }
}

void StructureInfoWidget::detectSymmetry()
{
    if (!structure_ || structure_->empty() || !structure_->cell().isDefined())
        return;
    if (!pybridge::PythonEngine::instance().aseAvailable()) {
        spaceGroupLabel_->setText(tr("ASE unavailable"));
        return;
    }

    const auto symmetry =
        pybridge::AseBridge::symmetryInfo(*structure_, symprecSpin_->value());
    if (symmetry.error.empty()) {
        spaceGroupLabel_->setText(
            QStringLiteral("%1 (#%2) @ %3 Å")
                .arg(QString::fromStdString(symmetry.spaceGroupSymbol))
                .arg(symmetry.spaceGroupNumber)
                .arg(symprecSpin_->value(), 0, 'g', 3));
        pointGroupLabel_->setText(QString::fromStdString(symmetry.pointGroup));
        crystalSystemLabel_->setText(QString::fromStdString(symmetry.crystalSystem));
    } else {
        spaceGroupLabel_->setText(QString::fromStdString(symmetry.error));
        pointGroupLabel_->setText(QStringLiteral("—"));
        crystalSystemLabel_->setText(QStringLiteral("—"));
    }
}

} // namespace calango::gui
