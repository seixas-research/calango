#include "gui/MolecularDynamicsWizard.hpp"

#include "core/AseScriptGenerator.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QWidget>

namespace calango::gui {

MolecularDynamicsWizard::MolecularDynamicsWizard(QWidget* parent)
    : SimulationWizardBase(parent)
{
    buildUi();
    updateEnsembleEnabled();
}

QString MolecularDynamicsWizard::wizardTitle() const
{
    return tr("Molecular Dynamics Setup");
}

QString MolecularDynamicsWizard::settingsHeader() const
{
    return tr("Dynamics Settings");
}

QWidget* MolecularDynamicsWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    ensembleCombo_ = new QComboBox(page);
    // Order mirrors core::MdEnsemble.
    ensembleCombo_->addItems({tr("NVE — Velocity Verlet (microcanonical)"),
                              tr("NVT — Langevin"), tr("NVT — Andersen"),
                              tr("NVT — Berendsen"), tr("NVT — Nosé–Hoover chain"),
                              tr("NPT — Berendsen"),
                              tr("NPT — Parrinello–Rahman (Nosé–Hoover)")});
    ensembleCombo_->setCurrentIndex(1);
    form->addRow(tr("Ensemble:"), ensembleCombo_);
    connect(ensembleCombo_, &QComboBox::currentIndexChanged, this,
            &MolecularDynamicsWizard::updateEnsembleEnabled);

    temperatureSpin_ = new QDoubleSpinBox(page);
    temperatureSpin_->setRange(0.0, 100000.0);
    temperatureSpin_->setValue(300.0);
    temperatureSpin_->setSuffix(tr(" K"));
    form->addRow(tr("Temperature:"), temperatureSpin_);

    pressureSpin_ = new QDoubleSpinBox(page);
    pressureSpin_->setRange(0.0, 1.0e7);
    pressureSpin_->setDecimals(3);
    pressureSpin_->setValue(1.0);
    pressureSpin_->setSuffix(tr(" bar"));
    pressureSpin_->setToolTip(tr("External pressure for NPT ensembles."));
    form->addRow(tr("Pressure:"), pressureSpin_);

    timestepSpin_ = new QDoubleSpinBox(page);
    timestepSpin_->setRange(0.01, 20.0);
    timestepSpin_->setValue(1.0);
    timestepSpin_->setSuffix(tr(" fs"));
    form->addRow(tr("Time step:"), timestepSpin_);

    frictionSpin_ = new QDoubleSpinBox(page);
    frictionSpin_->setDecimals(4);
    frictionSpin_->setRange(0.0001, 10.0);
    frictionSpin_->setSingleStep(0.005);
    frictionSpin_->setValue(0.01);
    frictionSpin_->setSuffix(tr(" fs⁻¹"));
    frictionSpin_->setToolTip(tr("Langevin friction coefficient."));
    form->addRow(tr("Friction (Langevin):"), frictionSpin_);

    tautSpin_ = new QDoubleSpinBox(page);
    tautSpin_->setRange(1.0, 100000.0);
    tautSpin_->setValue(100.0);
    tautSpin_->setSuffix(tr(" fs"));
    tautSpin_->setToolTip(tr("Thermostat coupling / damping time."));
    form->addRow(tr("Thermostat coupling:"), tautSpin_);

    taupSpin_ = new QDoubleSpinBox(page);
    taupSpin_->setRange(1.0, 1000000.0);
    taupSpin_->setValue(1000.0);
    taupSpin_->setSuffix(tr(" fs"));
    taupSpin_->setToolTip(tr("Barostat coupling time (NPT)."));
    form->addRow(tr("Barostat coupling:"), taupSpin_);

    stepsSpin_ = new QSpinBox(page);
    stepsSpin_->setRange(1, 100000000);
    stepsSpin_->setValue(1000);
    form->addRow(tr("Total steps:"), stepsSpin_);

    sampleSpin_ = new QSpinBox(page);
    sampleSpin_->setRange(0, 1000000);
    sampleSpin_->setValue(10);
    sampleSpin_->setSpecialValueText(tr("auto (~400 frames)"));
    sampleSpin_->setToolTip(tr("Record a trajectory frame + metrics every N "
                               "steps (0 = auto)."));
    form->addRow(tr("Sampling frequency:"), sampleSpin_);
    return page;
}

void MolecularDynamicsWizard::updateEnsembleEnabled()
{
    const auto ensemble =
        static_cast<core::MdEnsemble>(ensembleCombo_->currentIndex());
    temperatureSpin_->setEnabled(core::isConstantTemperature(ensemble));
    frictionSpin_->setEnabled(ensemble == core::MdEnsemble::LangevinNVT);
    const bool usesTaut = ensemble == core::MdEnsemble::BerendsenNVT
        || ensemble == core::MdEnsemble::NoseHooverChainNVT
        || ensemble == core::MdEnsemble::BerendsenNPT
        || ensemble == core::MdEnsemble::MelchionnaNPT;
    tautSpin_->setEnabled(usesTaut);
    taupSpin_->setEnabled(core::isConstantPressure(ensemble));
    pressureSpin_->setEnabled(core::isConstantPressure(ensemble));
}

core::CalculatorConfig MolecularDynamicsWizard::config() const
{
    core::CalculatorConfig c = baseCalculatorConfig();
    c.task = core::TaskKind::MolecularDynamics;
    c.ensemble = static_cast<core::MdEnsemble>(ensembleCombo_->currentIndex());
    c.temperatureK = temperatureSpin_->value();
    c.pressureGPa = pressureSpin_->value() * 1.0e-4; // bar → GPa
    c.timestepFs = timestepSpin_->value();
    c.frictionPerFs = frictionSpin_->value();
    c.tautFs = tautSpin_->value();
    c.taupFs = taupSpin_->value();
    c.mdSteps = stepsSpin_->value();
    c.mdSampleInterval = sampleSpin_->value();
    return c;
}

QString MolecularDynamicsWizard::generateScript() const
{
    return QString::fromStdString(
        core::AseScriptGenerator::generate(config(), "structure.extxyz"));
}

} // namespace calango::gui
