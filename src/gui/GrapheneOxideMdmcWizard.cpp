#include "gui/GrapheneOxideMdmcWizard.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

GrapheneOxideMdmcWizard::GrapheneOxideMdmcWizard(QWidget* parent)
    : SimulationWizardBase(parent)
{
    buildUi();
}

QString GrapheneOxideMdmcWizard::wizardTitle() const
{
    return tr("Graphene Oxide — MDMC Optimization");
}

void GrapheneOxideMdmcWizard::setSubstrate(int functionalGroups,
                                           int basalCarbons, int edgeCarbons,
                                           bool periodic)
{
    groupCount_ = functionalGroups;
    basalCarbons_ = basalCarbons;
    edgeCarbons_ = edgeCarbons;
    periodic_ = periodic;
    setStructurePeriodic(periodic);
    setStructureElements(edgeCarbons > 0
                             ? QStringList{QStringLiteral("C"),
                                           QStringLiteral("H"),
                                           QStringLiteral("O")}
                             : QStringList{QStringLiteral("C"),
                                           QStringLiteral("O")});

    if (!substrateLabel_)
        return;
    if (groupCount_ == 0) {
        // Nothing to move. Said here rather than discovered by the script at
        // run time, after the queue and the engine startup.
        substrateLabel_->setText(
            tr("<b>This structure carries no functional groups.</b> MDMC "
               "rearranges an EXISTING decoration — it does not create one. Go "
               "back and raise the oxidation level."));
    } else {
        substrateLabel_->setText(
            tr("%n functional group(s) on %1 basal and %2 edge carbons. Each "
               "cycle moves one group to a free site.",
               nullptr, groupCount_)
                .arg(basalCarbons_)
                .arg(edgeCarbons_));
    }
    // NPT needs a cell; a flake does not have one.
    if (ensemble_) {
        ensemble_->setEnabled(periodic_);
        if (!periodic_)
            ensemble_->setCurrentIndex(0);
    }
    refreshCost();
}

QWidget* GrapheneOxideMdmcWizard::buildSettingsPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    substrateLabel_ = new QLabel(page);
    substrateLabel_->setWordWrap(true);
    substrateLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(substrateLabel_);

    // -- Sampling ----------------------------------------------------------
    auto* samplingBox = new QGroupBox(tr("Monte Carlo Sampling"), page);
    auto* samplingForm = new QFormLayout(samplingBox);

    temperature_ = new QDoubleSpinBox(samplingBox);
    temperature_->setRange(1.0, 3000.0);
    temperature_->setDecimals(0);
    temperature_->setSingleStep(50.0);
    temperature_->setValue(300.0);
    temperature_->setSuffix(tr(" K"));
    temperature_->setToolTip(
        tr("Temperature of BOTH the dynamics and the acceptance test.\n\n"
           "One temperature used twice, deliberately: a Metropolis test at a "
           "different temperature from the dynamics that generated the state "
           "samples no ensemble at all.\n\n"
           "Higher explores more arrangements and accepts more bad ones. "
           "300–600 K is the useful range for annealing a decoration; well "
           "above that the dynamics starts breaking the chemistry, and the run "
           "will say so."));
    samplingForm->addRow(tr("Temperature:"), temperature_);

    cycles_ = new QSpinBox(samplingBox);
    cycles_->setRange(1, 100000);
    cycles_->setValue(200);
    cycles_->setToolTip(
        tr("Monte Carlo cycles — one attempted group move each. A decoration "
           "with N groups needs on the order of several × N accepted moves "
           "before the arrangement has forgotten where it started."));
    samplingForm->addRow(tr("MC cycles:"), cycles_);

    mdSteps_ = new QSpinBox(samplingBox);
    mdSteps_->setRange(0, 1000);
    mdSteps_->setValue(20);
    mdSteps_->setToolTip(
        tr("Molecular-dynamics steps run after each move, before the energy is "
           "judged.\n\n"
           "This is not a relaxation — it is just enough motion to let the "
           "neighbours accommodate the group that moved, so the move is judged "
           "on a settled geometry rather than on an unrelaxed clash. Few "
           "(5–20) is the intended regime: the run costs cycles × steps energy "
           "evaluations, so this multiplies straight into the wall clock.\n\n"
           "Zero evaluates the energy without any dynamics — the cheapest, "
           "crudest setting."));
    samplingForm->addRow(tr("MD steps per cycle:"), mdSteps_);

    seed_ = new QSpinBox(samplingBox);
    seed_->setRange(0, 999999);
    seed_->setValue(0);
    seed_->setToolTip(
        tr("Seed for the move sequence and the thermostat. The same seed and "
           "settings reproduce the run exactly."));
    samplingForm->addRow(tr("Seed:"), seed_);
    layout->addWidget(samplingBox);

    // -- Dynamics ----------------------------------------------------------
    auto* dynamicsBox = new QGroupBox(tr("Molecular Dynamics"), page);
    auto* dynamicsForm = new QFormLayout(dynamicsBox);

    ensemble_ = new QComboBox(dynamicsBox);
    ensemble_->addItem(tr("NVT — Langevin thermostat"), 0);
    ensemble_->addItem(tr("NPT — Berendsen barostat"), 1);
    ensemble_->setToolTip(
        tr("NVT holds the cell fixed and is what a decoration study wants: the "
           "question is where the groups sit, not what the lattice does.\n\n"
           "NPT lets the cell respond, which matters when heavy oxidation is "
           "expected to buckle or expand the sheet. It needs a cell, so it is "
           "unavailable for a finite nanoflake."));
    dynamicsForm->addRow(tr("Ensemble:"), ensemble_);

    pressure_ = new QDoubleSpinBox(dynamicsBox);
    pressure_->setRange(-10.0, 100.0);
    pressure_->setDecimals(3);
    pressure_->setValue(0.0);
    pressure_->setSuffix(tr(" GPa"));
    dynamicsForm->addRow(tr("Pressure:"), pressure_);

    timestep_ = new QDoubleSpinBox(dynamicsBox);
    timestep_->setRange(0.1, 5.0);
    timestep_->setDecimals(2);
    timestep_->setSingleStep(0.25);
    timestep_->setValue(1.0);
    timestep_->setSuffix(tr(" fs"));
    timestep_->setToolTip(
        tr("Integration step. Graphene oxide carries O–H and C–H stretches, "
           "whose periods are ~10 fs, so a step much above 1 fs integrates "
           "them badly and heats the system artificially — which this run "
           "would then read as broken chemistry."));
    dynamicsForm->addRow(tr("Timestep:"), timestep_);

    friction_ = new QDoubleSpinBox(dynamicsBox);
    friction_->setRange(0.001, 1.0);
    friction_->setDecimals(3);
    friction_->setSingleStep(0.005);
    friction_->setValue(0.02);
    friction_->setSuffix(tr(" fs⁻¹"));
    friction_->setToolTip(
        tr("Langevin friction. Larger than a production-MD value on purpose: "
           "the thermostat has to reach the target temperature within a burst "
           "of only a few tens of steps, and a weak one leaves every move "
           "judged at whatever temperature the previous one left behind."));
    dynamicsForm->addRow(tr("Friction:"), friction_);
    layout->addWidget(dynamicsBox);

    // -- Output ------------------------------------------------------------
    auto* outputBox = new QGroupBox(tr("Output"), page);
    auto* outputForm = new QFormLayout(outputBox);
    bothFaces_ = new QCheckBox(tr("Move basal groups to either face"), outputBox);
    bothFaces_->setChecked(true);
    bothFaces_->setToolTip(
        tr("Let a move flip a basal group from one side of the sheet to the "
           "other. Real graphene oxide is decorated on both faces, and "
           "restricting the sampler to one puts a net dipole across the sheet "
           "that it can never relax away."));
    outputForm->addRow(bothFaces_);

    viewportEvery_ = new QSpinBox(outputBox);
    viewportEvery_->setRange(0, 1000);
    viewportEvery_->setValue(5);
    viewportEvery_->setSpecialValueText(tr("never"));
    viewportEvery_->setToolTip(
        tr("Show the structure in the 3D viewport every this many MC "
           "cycles.\n\n"
           "A throttle, and it needs one: every streamed geometry is written, "
           "parsed, rebuilt and redrawn, and a fast calculator produces them "
           "quicker than a viewport can draw them — at which point the "
           "application spends its time watching the calculation instead of "
           "running it. Every 5 cycles is smooth for a flake; raise it for a "
           "large sheet or a long run.\n\n"
           "\"never\" runs headless, which is what you want on a cluster."));
    outputForm->addRow(tr("Update viewport every:"), viewportEvery_);

    streamMdFrames_ = new QCheckBox(
        tr("Also show the dynamics between moves"), outputBox);
    streamMdFrames_->setToolTip(
        tr("Stream the geometry the molecular dynamics produced, before the "
           "move is accepted or rejected.\n\n"
           "Two different things are worth watching. Without this you see the "
           "groups HOP — the discrete process being sampled, one frame per "
           "accepted move. With it you also see the atoms MOVE, which is how "
           "you catch a structure that is melting or coming apart.\n\n"
           "Costs a frame per shown cycle whether the move is accepted or "
           "not, and a rejected move's geometry is not a state of the "
           "ensemble — useful for diagnosis, misleading as a trajectory."));
    outputForm->addRow(streamMdFrames_);

    snapshotInterval_ = new QSpinBox(outputBox);
    snapshotInterval_->setRange(0, 1000);
    snapshotInterval_->setValue(1);
    snapshotInterval_->setToolTip(
        tr("Write an accepted configuration to the trajectory every this many "
           "ACCEPTED moves. Zero writes no trajectory — only the best "
           "structure found."));
    outputForm->addRow(tr("Snapshot every:"), snapshotInterval_);
    layout->addWidget(outputBox);

    costLabel_ = new QLabel(page);
    costLabel_->setWordWrap(true);
    costLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(costLabel_);
    layout->addStretch(1);

    connect(cycles_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    connect(mdSteps_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    connect(ensemble_, &QComboBox::currentIndexChanged, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    for (QDoubleSpinBox* spin : {temperature_, timestep_, friction_, pressure_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &GrapheneOxideMdmcWizard::refreshCost);
    connect(seed_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    connect(snapshotInterval_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    connect(viewportEvery_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    connect(streamMdFrames_, &QCheckBox::toggled, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    connect(bothFaces_, &QCheckBox::toggled, this,
            &GrapheneOxideMdmcWizard::refreshCost);

    refreshCost();
    return page;
}

void GrapheneOxideMdmcWizard::refreshCost()
{
    if (!costLabel_ || !cycles_ || !mdSteps_)
        return;

    const bool npt = ensemble_ && ensemble_->currentData().toInt() == 1;
    if (pressure_)
        pressure_->setEnabled(npt);

    // The one number that decides whether this run is an afternoon or a month.
    // Stated as evaluations rather than as a time because the per-evaluation
    // cost spans six orders of magnitude across the engines this can use, and
    // quoting a wall clock we cannot know would be worse than quoting none.
    const long long evaluations =
        static_cast<long long>(cycles_->value())
        * std::max(1, mdSteps_->value());
    costLabel_->setText(
        tr("<b>%1 energy evaluations</b> (%2 cycles × %3 MD steps). That is "
           "the cost of the run: seconds each on a machine-learning potential, "
           "minutes each on DFT — with DFT, a few hundred cycles is a large "
           "calculation, not a quick check.")
            .arg(evaluations)
            .arg(cycles_->value())
            .arg(std::max(1, mdSteps_->value())));
}

core::GrapheneOxideMdmcConfig GrapheneOxideMdmcWizard::collectConfig() const
{
    core::GrapheneOxideMdmcConfig config;
    config.calculator = baseCalculatorConfig();
    if (temperature_)
        config.temperatureK = temperature_->value();
    if (cycles_)
        config.cycles = cycles_->value();
    if (mdSteps_)
        config.mdStepsPerCycle = mdSteps_->value();
    if (timestep_)
        config.timestepFs = timestep_->value();
    if (friction_)
        config.frictionPerFs = friction_->value();
    if (ensemble_)
        config.constantPressure =
            periodic_ && ensemble_->currentData().toInt() == 1;
    if (pressure_)
        config.pressureGpa = pressure_->value();
    if (bothFaces_)
        config.bothFaces = bothFaces_->isChecked();
    if (seed_)
        config.seed = static_cast<std::uint32_t>(seed_->value());
    if (snapshotInterval_)
        config.snapshotInterval = snapshotInterval_->value();
    if (viewportEvery_)
        config.viewportEveryCycles = viewportEvery_->value();
    if (streamMdFrames_)
        config.streamMdFrames = streamMdFrames_->isChecked();
    return config;
}

QString GrapheneOxideMdmcWizard::generateScript() const
{
    config_ = collectConfig();
    return QString::fromStdString(
        core::GrapheneOxideMdmcScriptGenerator::generate(config_));
}

} // namespace calango::gui
