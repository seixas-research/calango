#include "gui/ElectronicBandsWizard.hpp"

#include "core/ElectronicScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/KPathSelector.hpp"

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

ElectronicBandsWizard::ElectronicBandsWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
}

QString ElectronicBandsWizard::wizardTitle() const
{
    return tr("Electronic Structure Setup");
}

QString ElectronicBandsWizard::settingsHeader() const
{
    return tr("k-Path Definition");
}

QWidget* ElectronicBandsWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Define the high-symmetry k-path for the band structure. This run "
           "builds on a single-point (SCF) baseline: the ground-state density "
           "is converged first, then the bands are evaluated along the path "
           "below. The SCF cutoff and k-grid are set in Stage 3."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    layout->addLayout(form);

    kpath_ = new KPathSelector(structure_, page);
    form->addRow(tr("k-path:"), kpath_);

    npointsSpin_ = new QSpinBox(page);
    npointsSpin_->setRange(20, 2000);
    npointsSpin_->setValue(80);
    npointsSpin_->setToolTip(tr("Total k-points sampled along the whole path."));
    form->addRow(tr("k-points along path:"), npointsSpin_);

    valenceSpin_ = new QSpinBox(page);
    valenceSpin_->setRange(1, 200);
    valenceSpin_->setValue(4);
    valenceSpin_->setToolTip(
        tr("Electrons per cell — used only by the free-electron backend."));
    form->addRow(tr("Valence electrons:"), valenceSpin_);

    pdosCheck_ = new QCheckBox(
        tr("Compute element/orbital PDOS (GPAW backend)"), page);
    pdosCheck_->setChecked(true);
    form->addRow(QString(), pdosCheck_);

    layout->addStretch(1);
    return page;
}

bool ElectronicBandsWizard::calculatorAllowed(core::CalculatorKind kind) const
{
    switch (kind) {
    case core::CalculatorKind::Gpaw:
    case core::CalculatorKind::Siesta:
    case core::CalculatorKind::Vasp:
    case core::CalculatorKind::QuantumEspresso:
        return true;
    default:
        return false;
    }
}

QString ElectronicBandsWizard::generateScript() const
{
    core::ElectronicConfig config;
    switch (selectedCalculator()) {
    case core::CalculatorKind::Gpaw:
        config.backend = core::ElectronicBackend::Gpaw;
        break;
    case core::CalculatorKind::QuantumEspresso:
        config.backend = core::ElectronicBackend::Espresso;
        break;
    case core::CalculatorKind::Siesta:
        config.backend = core::ElectronicBackend::Siesta;
        break;
    case core::CalculatorKind::Vasp:
        config.backend = core::ElectronicBackend::Vasp;
        break;
    default:
        // The combo is DFT-only, so this is unreachable; fall back to the
        // free-electron reference model for safety.
        config.backend = core::ElectronicBackend::FreeElectrons;
        break;
    }

    // Keep the ',' section breaks; ASE's cell.bandpath() understands them.
    config.kpath = kpath_->path().toStdString();
    config.npoints = npointsSpin_->value();
    config.nvalence = valenceSpin_->value();
    config.pdos = pdosCheck_->isChecked();

    // SCF baseline from the shared Stage-3 DFT knobs.
    const core::CalculatorConfig base = baseCalculatorConfig();
    config.ecutEv = base.planeWaveCutoffEv;
    config.scfKpts = base.kpts[0];

    return QString::fromStdString(core::generateElectronicScript(config));
}

} // namespace calango::gui
