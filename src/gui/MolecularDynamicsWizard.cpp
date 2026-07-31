#include "gui/MolecularDynamicsWizard.hpp"

#include "gui/GuiUtils.hpp"

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
#include <QStandardItemModel>
#include <QStringList>
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
    return structureElements(structure_.get());
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
    dynamicsForm_ = form;

    // Mode leads the page because it decides what the rest of it means: in
    // Annealing the single "Temperature" setpoint is replaced by a ramp, and
    // showing both at once invites a run configured at a temperature the
    // schedule immediately overwrites.
    modeCombo_ = new QComboBox(page);
    // Named so the dialog test can drive the mode without matching on
    // translated item text.
    modeCombo_->setObjectName(QStringLiteral("mdModeCombo"));
    modeCombo_->addItem(tr("Constant temperature"), false);
    modeCombo_->addItem(tr("Annealing — scheduled temperature ramp"), true);
    modeCombo_->setToolTip(
        tr("Annealing runs the same integrator with a MOVING setpoint: the "
           "thermostat target is swept from an initial to a final temperature "
           "over the course of the run. Everything else — ensemble, "
           "constraints, sampling, trajectory — is unchanged."));
    form->addRow(tr("Mode:"), modeCombo_);
    connect(modeCombo_, &QComboBox::currentIndexChanged, this,
            &MolecularDynamicsWizard::updateAnnealingMode);

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

    // ----- Annealing schedule ---------------------------------------------
    scheduleCombo_ = new QComboBox(page);
    scheduleCombo_->setObjectName(QStringLiteral("mdScheduleCombo"));
    // Item data is the enum value, so the combo order is free to change
    // without silently re-mapping onto a different law.
    scheduleCombo_->addItem(tr("Linear"),
                            static_cast<int>(core::AnnealingSchedule::Linear));
    scheduleCombo_->addItem(
        tr("Exponential"), static_cast<int>(core::AnnealingSchedule::Exponential));
    scheduleCombo_->addItem(
        tr("Logarithmic"), static_cast<int>(core::AnnealingSchedule::Logarithmic));
    scheduleCombo_->setToolTip(
        tr("How the setpoint travels between the two temperatures, as a "
           "function of the run fraction x = step / total steps:\n\n"
           "  Linear:       T = T₀ + (T₁ − T₀)·x\n"
           "  Exponential:  T = T₁ + (T₀ − T₁)·(e^(−kx) − e^(−k))/(1 − e^(−k))\n"
           "  Logarithmic:  T = T₀ + (T₁ − T₀)·ln(1 + kx)/ln(1 + k)\n\n"
           "All three end exactly on the final temperature. Exponential moves "
           "most of the way early and crawls at the end (quenching a melt); "
           "Logarithmic is the slowest of the three near the target."));
    form->addRow(tr("Schedule:"), scheduleCombo_);
    connect(scheduleCombo_, &QComboBox::currentIndexChanged, this,
            &MolecularDynamicsWizard::updateAnnealingMode);

    annealStartSpin_ = new QDoubleSpinBox(page);
    annealStartSpin_->setRange(0.0, 100000.0);
    annealStartSpin_->setValue(1000.0);
    annealStartSpin_->setSuffix(tr(" K"));
    annealStartSpin_->setToolTip(
        tr("Setpoint at step 0. The initial Maxwell-Boltzmann velocities are "
           "drawn here too, so the run starts where the schedule says it "
           "starts instead of spending its first picosecond catching up."));
    form->addRow(tr("Initial temperature:"), annealStartSpin_);

    annealEndSpin_ = new QDoubleSpinBox(page);
    annealEndSpin_->setRange(0.0, 100000.0);
    annealEndSpin_->setValue(300.0);
    annealEndSpin_->setSuffix(tr(" K"));
    annealEndSpin_->setToolTip(
        tr("Setpoint at the last step. Higher than the initial temperature is "
           "legal — a heating ramp is generated exactly the same way."));
    form->addRow(tr("Final temperature:"), annealEndSpin_);

    annealCoefficientSpin_ = new QDoubleSpinBox(page);
    annealCoefficientSpin_->setRange(0.01, 50.0);
    annealCoefficientSpin_->setDecimals(2);
    annealCoefficientSpin_->setSingleStep(0.25);
    annealCoefficientSpin_->setValue(3.0);
    annealCoefficientSpin_->setToolTip(
        tr("How far the ramp bends away from a straight line. Small k is "
           "nearly linear; large k front-loads almost the whole temperature "
           "change into the first part of the run. Ignored by the Linear "
           "schedule, which has no free coefficient."));
    form->addRow(tr("Ramp curvature k:"), annealCoefficientSpin_);

    annealSummary_ = new QLabel(page);
    annealSummary_->setWordWrap(true);
    annealSummary_->setTextFormat(Qt::RichText);
    form->addRow(annealSummary_);

    for (QDoubleSpinBox* spin :
         {annealStartSpin_, annealEndSpin_, annealCoefficientSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this](double) { refreshAnnealingSummary(); });

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
    // The schedule is written against the run FRACTION, so its shape does not
    // depend on the step count — but the elapsed time it spans does, and that
    // is the number that decides whether a "slow" anneal is slow.
    connect(stepsSpin_, &QSpinBox::valueChanged, this,
            [this](int) { refreshAnnealingSummary(); });
    connect(timestepSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double) { refreshAnnealingSummary(); });

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
    updateAnnealingMode();
    return page;
}

bool MolecularDynamicsWizard::annealingSelected() const
{
    return modeCombo_ && modeCombo_->currentData().toBool();
}

void MolecularDynamicsWizard::updateAnnealingMode()
{
    if (!dynamicsForm_ || !scheduleCombo_)
        return;
    const bool annealing = annealingSelected();

    dynamicsForm_->setRowVisible(temperatureSpin_, !annealing);
    for (QWidget* row : {static_cast<QWidget*>(scheduleCombo_),
                         static_cast<QWidget*>(annealStartSpin_),
                         static_cast<QWidget*>(annealEndSpin_),
                         static_cast<QWidget*>(annealCoefficientSpin_),
                         static_cast<QWidget*>(annealSummary_)})
        dynamicsForm_->setRowVisible(row, annealing);
    // Only the Exponential and Logarithmic laws have a coefficient; Linear is
    // fixed by its two endpoints, so leaving the box live would offer a knob
    // that changes nothing.
    const auto schedule = static_cast<core::AnnealingSchedule>(
        scheduleCombo_->currentData().toInt());
    annealCoefficientSpin_->setEnabled(
        annealing && schedule != core::AnnealingSchedule::Linear);

    // A schedule needs something to retarget. NVE has no thermostat, so it is
    // withdrawn rather than accepted and quietly ignored — and the ensemble is
    // moved to Langevin if it was the one selected.
    if (auto* model = qobject_cast<QStandardItemModel*>(ensembleCombo_->model()))
        if (QStandardItem* nve = model->item(
                static_cast<int>(core::MdEnsemble::VelocityVerletNVE)))
            nve->setEnabled(!annealing);
    if (annealing
        && ensembleCombo_->currentIndex()
            == static_cast<int>(core::MdEnsemble::VelocityVerletNVE))
        ensembleCombo_->setCurrentIndex(
            static_cast<int>(core::MdEnsemble::LangevinNVT));

    updateEnsembleEnabled();
    refreshAnnealingSummary();
    refreshPreview();
}

void MolecularDynamicsWizard::refreshAnnealingSummary()
{
    if (!annealSummary_ || !annealingSelected())
        return;
    const auto schedule = static_cast<core::AnnealingSchedule>(
        scheduleCombo_->currentData().toInt());
    const double start = annealStartSpin_->value();
    const double end = annealEndSpin_->value();
    const double k = annealCoefficientSpin_->value();

    QStringList stops;
    for (const double x : {0.0, 0.25, 0.5, 0.75, 1.0})
        stops << QString::number(
            core::annealingTemperature(schedule, start, end, k, x), 'f', 0);

    const double picoseconds =
        stepsSpin_->value() * timestepSpin_->value() * 1.0e-3;
    QString text =
        tr("Setpoint at 0 / 25 / 50 / 75 / 100 %% of the run: "
           "<b>%1</b> K, over %2 ps.")
            .arg(stops.join(QStringLiteral(" → ")))
            .arg(picoseconds, 0, 'f', picoseconds < 10.0 ? 2 : 1);
    if (start < end)
        text += QLatin1Char(' ') + tr("This is a heating ramp.");
    annealSummary_->setText(text);
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
    // The GPAW electronic settings the shared rows collected.
    electronic_.applyTo(c);
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
    c.annealing = annealingSelected();
    c.annealingSchedule = static_cast<core::AnnealingSchedule>(
        scheduleCombo_->currentData().toInt());
    c.annealStartK = annealStartSpin_->value();
    c.annealEndK = annealEndSpin_->value();
    c.annealCoefficient = annealCoefficientSpin_->value();
    return c;
}

QString MolecularDynamicsWizard::generateScript() const
{
    return QString::fromStdString(
        core::AseScriptGenerator::generate(config(), "structure.extxyz"));
}

} // namespace calango::gui
