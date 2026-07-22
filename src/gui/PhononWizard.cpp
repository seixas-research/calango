#include "gui/PhononWizard.hpp"

#include "core/PhononScriptGenerator.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QWidget>

namespace calango::gui {

PhononWizard::PhononWizard(bool periodic, QWidget* parent)
    : SimulationWizardBase(parent), periodic_(periodic)
{
    buildUi();
}

QString PhononWizard::wizardTitle() const
{
    return tr("Phonon Calculator Setup");
}

QString PhononWizard::settingsHeader() const
{
    return tr("Phonon Settings");
}

QWidget* PhononWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    if (!periodic_)
        form->addRow(new QLabel(
            tr("Molecular system: Γ-point normal modes (no supercell / mesh)."),
            page));

    deltaSpin_ = new QDoubleSpinBox(page);
    deltaSpin_->setRange(0.001, 0.2);
    deltaSpin_->setDecimals(3);
    deltaSpin_->setSingleStep(0.005);
    deltaSpin_->setValue(0.01);
    deltaSpin_->setSuffix(tr(" Å"));
    deltaSpin_->setToolTip(tr("Finite-displacement step amplitude (± per atom)."));
    form->addRow(tr("Displacement δ:"), deltaSpin_);

    auto* superRow = new QHBoxLayout;
    for (auto*& spin : supercellSpins_) {
        spin = new QSpinBox(page);
        spin->setRange(1, 10);
        spin->setValue(2);
        spin->setEnabled(periodic_);
        superRow->addWidget(spin);
    }
    form->addRow(tr("Supercell (nx·ny·nz):"), superRow);

    acousticCheck_ = new QCheckBox(tr("Enforce acoustic sum rule"), page);
    acousticCheck_->setChecked(true);
    acousticCheck_->setToolTip(
        tr("Make the 3 acoustic branches vanish at Γ (recommended)."));
    acousticCheck_->setEnabled(periodic_);
    form->addRow(acousticCheck_);

    meshSpin_ = new QSpinBox(page);
    meshSpin_->setRange(2, 64);
    meshSpin_->setValue(20);
    meshSpin_->setToolTip(tr("Monkhorst-Pack n×n×n mesh density for the phonon "
                             "DOS / band interpolation."));
    meshSpin_->setEnabled(periodic_);
    form->addRow(tr("Mesh density (DOS):"), meshSpin_);

    bandPointsSpin_ = new QSpinBox(page);
    bandPointsSpin_->setRange(20, 1000);
    bandPointsSpin_->setValue(100);
    bandPointsSpin_->setToolTip(tr("k-points along the band-structure path."));
    bandPointsSpin_->setEnabled(periodic_);
    form->addRow(tr("Band-path points:"), bandPointsSpin_);
    return page;
}

QString PhononWizard::generateScript() const
{
    core::PhononConfig pc;
    pc.calculator = baseCalculatorConfig();
    pc.periodic = periodic_;
    for (int i = 0; i < 3; ++i)
        pc.supercell[i] = supercellSpins_[i]->value();
    pc.deltaAngstrom = deltaSpin_->value();
    pc.bandPathPoints = bandPointsSpin_->value();
    pc.dosKptGrid = meshSpin_->value();
    pc.acousticSumRule = acousticCheck_->isChecked();
    return QString::fromStdString(
        core::PhononScriptGenerator::generate(pc, "structure.extxyz"));
}

} // namespace calango::gui
