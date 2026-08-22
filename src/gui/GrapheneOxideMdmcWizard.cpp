#include "gui/GrapheneOxideMdmcWizard.hpp"

#include "core/Structure.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cstdint>
#include <cstdlib>
#include <set>

namespace calango::gui {

GrapheneOxideMdmcWizard::GrapheneOxideMdmcWizard(QWidget* parent)
    : SimulationWizardBase(parent)
{
    buildUi();
}

QString GrapheneOxideMdmcWizard::wizardTitle() const
{
    return tr("GO-MDMC — Hybrid MD / Monte Carlo Optimization");
}

void GrapheneOxideMdmcWizard::setInputBuild(const core::Structure& structure)
{
    // Every quantity below is read straight off the persisted classification
    // ("edge" / "go_group_id" / "go_pair_id") rather than passed in by a
    // caller that "just built" this structure — see the header doc comment.
    const auto& fields = structure.scalarFields();
    const auto edgeIt = fields.find("edge");
    const auto groupIdIt = fields.find("go_group_id");
    const auto pairIdIt = fields.find("go_pair_id");

    int basalCarbons = 0;
    int edgeCarbons = 0;
    if (edgeIt != fields.end() && edgeIt->second.size() == structure.size()) {
        for (std::size_t i = 0; i < structure.size(); ++i) {
            if (structure.atoms()[i].atomicNumber != 6) // carbon only
                continue;
            (edgeIt->second[i] > 0.5 ? edgeCarbons : basalCarbons)++;
        }
    }

    // One functional-group INSTANCE per distinct id — an antiposition
    // hydroxyl pair is two instances, matching how the builder's own Report
    // counts a pair as two hydroxyls placed.
    std::set<int> instanceIds;
    if (groupIdIt != fields.end()
        && groupIdIt->second.size() == structure.size()) {
        for (const double id : groupIdIt->second)
            if (id >= 0.0)
                instanceIds.insert(static_cast<int>(std::lround(id)));
    }

    bool hydroxylAntiposition = false;
    if (pairIdIt != fields.end()
        && pairIdIt->second.size() == structure.size()) {
        for (const double p : pairIdIt->second) {
            if (p >= 0.0) {
                hydroxylAntiposition = true;
                break;
            }
        }
    }

    // A GO Build is aperiodic (a nanoflake) iff it has edge carbons at all —
    // a periodic sheet is edgeless by construction, so this agrees with
    // reading the cell's own pbc flags without needing them.
    const bool periodic = edgeCarbons == 0;

    groupCount_ = static_cast<int>(instanceIds.size());
    basalCarbons_ = basalCarbons;
    edgeCarbons_ = edgeCarbons;
    periodic_ = periodic;
    hydroxylAntiposition_ = hydroxylAntiposition;
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
               "rearranges an existing decoration; it does not create one."));
        substrateLabel_->setToolTip(
            tr("Go back and raise the oxidation level."));
    } else {
        QString text =
            tr("%n functional group(s) on %1 basal and %2 edge carbons. Each "
               "cycle moves one group to a free site.",
               nullptr, groupCount_)
                .arg(basalCarbons_)
                .arg(edgeCarbons_);
        // Informational, and it stays: the checkbox below says how the
        // hydroxyls will be MOVED, this says how they were actually BUILT.
        // Keeping both visible is what stops the control from silently
        // disagreeing with the geometry — the hazard that kept this
        // read-only in the first place.
        if (hydroxylAntiposition_) {
            text += tr(
                " <b>This build placed its hydroxyls in antiposition</b> — "
                "bonded, opposite-face pairs. With the setting below on, "
                "each pair moves as one unit and is never split.");
        }
        substrateLabel_->setText(text);
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
    mdSteps_->setRange(0, 10000);
    mdSteps_->setValue(5);
    mdSteps_->setToolTip(
        tr("Molecular-dynamics steps run after each move, before the energy is "
           "judged.\n\n"
           "The burst is the only relaxation in the run. Many cheap cycles "
           "with a short burst each is the protocol's own regime. A longer "
           "burst judges a move more fairly — measured under MACE-MP-0 at 300 K with "
           "a 0.5 fs step, the leftover placement strain is within the "
           "thermal noise by 80–120 steps and the burst carries the sheet's "
           "own relaxation by 200. The per-cycle ΔE is recorded as "
           "trial_delta in metrics.json; if it sits eV above zero cycle "
           "after cycle, lengthen the burst.\n\n"
           "The run costs cycles × steps energy evaluations. This is NOT "
           "what brings the as-built structure to temperature — that is the "
           "separate equilibration stage below, run once.\n\n"
           "Zero evaluates the energy without any dynamics — the cheapest, "
           "crudest setting."));
    samplingForm->addRow(tr("MD steps per cycle:"), mdSteps_);

    hydroxylAntipositionBox_ = new QCheckBox(
        tr("Hydroxyls antiposition — move each pair as one unit"),
        samplingBox);
    hydroxylAntipositionBox_->setChecked(true);
    hydroxylAntipositionBox_->setToolTip(
        tr("Recover every bonded, opposite-face hydroxyl PAIR from the "
           "starting geometry once, and move it from then on as a single "
           "compound unit — drawing a bonded pair site, the same pool an "
           "epoxide draws from — so no move can ever separate a pair onto "
           "two independently sited carbons.\n\n"
           "On by default. Pairing is recovered FROM THE GEOMETRY, not "
           "declared: on a build whose hydroxyls were not placed in "
           "antiposition there are no pairs to find, and every hydroxyl "
           "stays an ordinary single — so leaving this on costs nothing "
           "there. The summary above says what THIS build actually "
           "contains.\n\n"
           "Off moves every hydroxyl individually. On an antiposition build "
           "that breaks the arrangement the first time either half of a pair "
           "hops, and nothing re-forms it."));
    samplingForm->addRow(hydroxylAntipositionBox_);

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
    timestep_->setValue(0.5);
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
        tr("Thermostat coupling for the per-cycle burst: the Langevin "
           "friction under NVT, and 1/(Berendsen temperature time) under NPT, "
           "so it means the same thing in both ensembles.\n\n"
           "Larger than a production-MD value on purpose — a burst is only a "
           "few tens of femtoseconds long — though at this length the burst "
           "is mostly a thermal perturbation of an already-equilibrated "
           "structure whatever the coupling."));
    dynamicsForm->addRow(tr("Friction:"), friction_);

    equilibrationSteps_ = new QSpinBox(dynamicsBox);
    equilibrationSteps_->setRange(0, 100000);
    equilibrationSteps_->setValue(10);
    equilibrationSteps_->setSpecialValueText(tr("none"));
    equilibrationSteps_->setToolTip(
        tr("Molecular-dynamics steps run ONCE, before the first cycle, to "
           "bring the as-built structure to the target temperature.\n\n"
           "The builder places every group on a flat sheet: each host carbon "
           "is still planar where the chemistry wants it pyramidal, so the "
           "as-built structure carries ~10 eV/Å on its carbons and tens of eV "
           "of strain. Released in a single short burst that is a thermal "
           "shock, and the group placed closest to a neighbour comes apart. "
           "This stage drains it gradually instead: the chemistry is checked "
           "every few steps, and a group that still opens is relocated to a "
           "fresh free site — the sampler's own move, inventory preserved — "
           "with the dynamics resuming from the last intact state. The run "
           "refuses only when relocating stops helping.\n\n"
           "The default is deliberately short: this stage costs one energy "
           "evaluation per step before any sampling has started, and the "
           "protocol is many cheap cycles. A few hundred steps (100–300 fs) "
           "is what a sheet needs to pucker and thermalize fully — raise it "
           "for an as-built structure carrying more strain than a burst can "
           "drain. \"none\" starts the walk from the as-built geometry's own "
           "energy."));
    dynamicsForm->addRow(tr("Equilibration steps:"), equilibrationSteps_);

    equilibrationFriction_ = new QDoubleSpinBox(dynamicsBox);
    equilibrationFriction_->setRange(0.001, 1.0);
    equilibrationFriction_->setDecimals(3);
    equilibrationFriction_->setSingleStep(0.01);
    equilibrationFriction_->setValue(0.1);
    equilibrationFriction_->setSuffix(tr(" fs⁻¹"));
    equilibrationFriction_->setToolTip(
        tr("Thermostat coupling during the equilibration. Stronger than the "
           "per-cycle value because this stage has the as-built strain to "
           "drain as it is released: with a 50 fs time constant that energy "
           "becomes a thermal shock before the thermostat sees it; with 10 fs "
           "it is carried away as it appears."));
    dynamicsForm->addRow(tr("Equilibration friction:"), equilibrationFriction_);
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
    viewportEvery_->setValue(1);
    viewportEvery_->setSpecialValueText(tr("never"));
    viewportEvery_->setToolTip(
        tr("Show the run in the 3D viewport every this many MC cycles — both "
           "the geometry the dynamics produced (the atoms MOVING, so a "
           "structure that is coming apart is visible while it happens) and "
           "the configuration an accepted move leaves behind (the groups "
           "HOPPING, the discrete process being sampled). Both are always "
           "shown; this interval is the only knob.\n\n"
           "It is a throttle, and it needs one: every streamed geometry is "
           "written, parsed, rebuilt and redrawn, and a fast calculator "
           "produces them quicker than a viewport can draw them — at which "
           "point the application spends its time watching the calculation "
           "instead of running it. Every cycle is smooth for a flake; raise "
           "it for a large sheet or a long run.\n\n"
           "\"never\" runs headless, which is what you want on a cluster."));
    outputForm->addRow(tr("Update viewport every:"), viewportEvery_);

    snapshotInterval_ = new QSpinBox(outputBox);
    snapshotInterval_->setRange(0, 1000);
    snapshotInterval_->setValue(1);
    snapshotInterval_->setToolTip(
        tr("Write an accepted configuration to the trajectory every this many "
           "ACCEPTED moves. Zero writes no trajectory — only the best "
           "structure found."));
    outputForm->addRow(tr("Snapshot every:"), snapshotInterval_);

    castPerFrame_ = new QCheckBox(
        tr("Redefine Cast on every accepted move"), outputBox);
    castPerFrame_->setChecked(true);
    castPerFrame_->setToolTip(
        tr("Recompute which functional group (or none) each carbon is bonded "
           "to for every accepted frame, and recolour by Cast accordingly — "
           "so a carbon whose epoxide hops away over the run stops reading "
           "as \"epoxide-carbon\" the moment it does.\n\n"
           "On by default: a Cast fixed at frame 0, the way every other "
           "trajectory in Calango behaves, goes stale the moment the first "
           "move is accepted, since MDMC's whole point is relocating groups. "
           "Turn this off only to compare against that flat behaviour."));
    outputForm->addRow(castPerFrame_);
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
    for (QDoubleSpinBox* spin :
         {temperature_, timestep_, friction_, pressure_, equilibrationFriction_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &GrapheneOxideMdmcWizard::refreshCost);
    connect(equilibrationSteps_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    connect(seed_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    connect(snapshotInterval_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    connect(viewportEvery_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    connect(bothFaces_, &QCheckBox::toggled, this,
            &GrapheneOxideMdmcWizard::refreshCost);
    connect(hydroxylAntipositionBox_, &QCheckBox::toggled, this,
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
    const long long equilibration =
        equilibrationSteps_ ? equilibrationSteps_->value() : 0;
    const long long evaluations =
        equilibration
        + static_cast<long long>(cycles_->value())
              * std::max(1, mdSteps_->value());
    costLabel_->setText(
        tr("<b>%1 energy evaluations</b> (%2 equilibration + %3 cycles × %4 "
           "MD steps).")
            .arg(evaluations)
            .arg(equilibration)
            .arg(cycles_->value())
            .arg(std::max(1, mdSteps_->value())));
    costLabel_->setToolTip(
        tr("Seconds each on a machine-learning potential, minutes each on DFT "
           "— with DFT, a few hundred cycles is a large calculation, not a "
           "quick check."));
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
    if (equilibrationSteps_)
        config.equilibrationSteps = equilibrationSteps_->value();
    if (equilibrationFriction_)
        config.equilibrationFrictionPerFs = equilibrationFriction_->value();
    if (ensemble_)
        config.constantPressure =
            periodic_ && ensemble_->currentData().toInt() == 1;
    if (pressure_)
        config.pressureGpa = pressure_->value();
    if (bothFaces_)
        config.bothFaces = bothFaces_->isChecked();
    if (hydroxylAntipositionBox_)
        config.hydroxylAntiposition = hydroxylAntipositionBox_->isChecked();
    if (seed_)
        config.seed = static_cast<std::uint32_t>(seed_->value());
    if (snapshotInterval_)
        config.snapshotInterval = snapshotInterval_->value();
    if (viewportEvery_)
        config.viewportEveryCycles = viewportEvery_->value();
    // config.streamMdFrames is left at its struct default (true) on purpose:
    // showing the dynamics between MC steps is always-on behavior now, and
    // `viewportEveryCycles` above is its configurable interval. There is no
    // control here to read it from any more.
    if (castPerFrame_)
        config.castPerFrame = castPerFrame_->isChecked();
    return config;
}

QString GrapheneOxideMdmcWizard::generateScript() const
{
    config_ = collectConfig();
    return QString::fromStdString(
        core::GrapheneOxideMdmcScriptGenerator::generate(config_));
}

} // namespace calango::gui
