#include "gui/MolecularDynamicsWizard.hpp"

#include "core/AseScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/GeometryConstraintsDialog.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

#include <set>

namespace calango::gui {

MolecularDynamicsWizard::MolecularDynamicsWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
    updateEnsembleEnabled();
}

QStringList MolecularDynamicsWizard::calculatorElements() const
{
    if (!structure_)
        return {};
    // Sorted and unique: this feeds a completer, and repeating "Fe" once per
    // Fe atom makes it useless.
    std::set<QString> symbols;
    for (const core::Atom& atom : structure_->atoms())
        symbols.insert(QString::fromLatin1(atom.symbol()));
    QStringList result;
    for (const QString& symbol : symbols)
        result << symbol;
    return result;
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

    // Frozen degrees of freedom, exactly as Geometry Optimization offers them:
    // ASE applies FixAtoms / FixCartesian to the integrator the same way it
    // applies them to an optimizer, so holding a substrate rigid while an
    // adsorbate moves — or freezing the bottom layers of a slab — works
    // identically here. Without it the whole cell thermalizes, which for a
    // surface simulation is rarely what is wanted.
    auto* constraintRow = new QHBoxLayout;
    auto* constraintButton = new QPushButton(tr("Geometry constraints…"), page);
    constraintButton->setToolTip(
        tr("Hold atoms — or single Cartesian directions of them — fixed for "
           "the whole trajectory. Select them individually, or by a region "
           "such as z < 5 Å (the bottom layers of a slab).\n\n"
           "Frozen atoms are also excluded from the initial Maxwell-Boltzmann "
           "velocities, so they start and stay at rest."));
    connect(constraintButton, &QPushButton::clicked, this,
            &MolecularDynamicsWizard::editConstraints);
    constraintRow->addWidget(constraintButton);
    constraintSummary_ = new QLabel(page);
    constraintSummary_->setWordWrap(true);
    constraintRow->addWidget(constraintSummary_, 1);
    form->addRow(constraintRow);
    refreshConstraintSummary();
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

void MolecularDynamicsWizard::editConstraints()
{
    GeometryConstraintsDialog dialog(structure_, constraints_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    constraints_ = dialog.constraints();
    refreshConstraintSummary();
    refreshPreview();
}

void MolecularDynamicsWizard::refreshConstraintSummary()
{
    if (!constraintSummary_)
        return;
    if (constraints_.empty()) {
        constraintSummary_->setText(tr("None — every atom moves freely."));
        return;
    }
    int fixedAtoms = 0;
    int regions = 0;
    for (const core::GeometryConstraint& rule : constraints_) {
        if (rule.selection == core::GeometryConstraint::Selection::Region)
            ++regions;
        else
            fixedAtoms += static_cast<int>(rule.indices.size());
    }
    QStringList parts;
    if (fixedAtoms > 0)
        parts << tr("%n atom(s)", nullptr, fixedAtoms);
    if (regions > 0)
        parts << tr("%n region(s)", nullptr, regions);
    constraintSummary_->setText(
        tr("Constrained: %1.").arg(parts.join(tr(", "))));
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
    c.constraints = constraints_;
    return c;
}

QString MolecularDynamicsWizard::generateScript() const
{
    return QString::fromStdString(
        core::AseScriptGenerator::generate(config(), "structure.extxyz"));
}

} // namespace calango::gui
