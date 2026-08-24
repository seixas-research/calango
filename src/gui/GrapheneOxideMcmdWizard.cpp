#include "gui/GrapheneOxideMcmdWizard.hpp"

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

GrapheneOxideMcmdWizard::GrapheneOxideMcmdWizard(QWidget* parent)
    : SimulationWizardBase(parent)
{
    buildUi();
}

GrapheneOxideMcmdWizard::GrapheneOxideMcmdWizard(DeferUi, QWidget* parent)
    : SimulationWizardBase(parent)
{
    // Deliberately empty: the subclass calls buildUi(). See DeferUi.
}

QString GrapheneOxideMcmdWizard::wizardTitle() const
{
    return tr("GO/MCMD — Hybrid MD / Monte Carlo Optimization");
}

void GrapheneOxideMcmdWizard::setInputBuild(const core::Structure& structure)
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
            tr("<b>This structure carries no functional groups.</b> MCMD "
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

QString GrapheneOxideMcmdWizard::secondSettingsHeader() const
{
    // Empty under GO/MCMD: the base drops a second stage whose header is
    // empty, so that module's three-stage flow is untouched.
    return relaxationMode() == core::GoMcRelaxation::Optimization
        ? tr("Geometry Optimization")
        : QString();
}

QWidget* GrapheneOxideMcmdWizard::buildSecondSettingsPage()
{
    // The relaxation, on a STAGE OF ITS OWN.
    //
    // It shares the page with nothing because it is a different decision from
    // the Monte Carlo above it: the MC settings say what is proposed and how
    // often, these say how a proposal is relaxed before it is judged. Crowded
    // onto one page they read as one long form; separated, each page is one
    // question. GO/MCMD has no second stage at all — secondSettingsHeader()
    // is empty there — so its flow is unchanged.
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* optBox = new QGroupBox(tr("Geometry Optimization"), page);
    auto* optForm = new QFormLayout(optBox);

    optimizer_ = new QComboBox(optBox);
    optimizer_->setObjectName(QStringLiteral("mcOptOptimizerCombo"));
    // itemData is the enum, not the row: the menu is ordered for the user
    // (default first) and must not have to mirror the enum's order.
    optimizer_->addItem(tr("BFGS — quasi-Newton (default)"),
                        static_cast<int>(core::GoMcOptimizer::Bfgs));
    optimizer_->addItem(tr("LBFGS — limited memory"),
                        static_cast<int>(core::GoMcOptimizer::Lbfgs));
    optimizer_->addItem(tr("FIRE — no Hessian, tolerant of bad starts"),
                        static_cast<int>(core::GoMcOptimizer::Fire));
    optimizer_->addItem(tr("MDMin — cheapest step, slowest convergence"),
                        static_cast<int>(core::GoMcOptimizer::Mdmin));
    optimizer_->setToolTip(
        tr("The local optimizer run after every proposed move.\n\n"
           "BFGS is the default and the right first choice. LBFGS stores "
           "less of the Hessian and is the better bet on a large cell. "
           "FIRE keeps no Hessian at all, which makes it the tolerant "
           "one when a freshly placed group starts far from its minimum. "
           "MDMin is the cheapest per step and needs the most of them."));
    optForm->addRow(tr("Optimizer:"), optimizer_);

    fmax_ = new QDoubleSpinBox(optBox);
    fmax_->setObjectName(QStringLiteral("mcOptFmaxSpin"));
    fmax_->setRange(0.001, 5.0);
    fmax_->setDecimals(3);
    fmax_->setSingleStep(0.01);
    fmax_->setValue(0.05);
    fmax_->setSuffix(tr(" eV/Å"));
    fmax_->setToolTip(
        tr("Force convergence criterion: the relaxation stops when no "
           "atom carries a residual force above this.\n\n"
           "0.05 eV/Å is the value every other relaxation in Calango "
           "defaults to, and the one a reader expects quoted beside a "
           "relaxed energy. Loosening it makes each cycle cheaper and "
           "the energies less comparable — the Metropolis test is only "
           "comparing minima to the extent that both sides ARE minima."));
    optForm->addRow(tr("Force criterion:"), fmax_);

    optimizerMaxSteps_ = new QSpinBox(optBox);
    optimizerMaxSteps_->setObjectName(QStringLiteral("mcOptMaxStepsSpin"));
    optimizerMaxSteps_->setRange(1, 20000);
    optimizerMaxSteps_->setValue(200);
    optimizerMaxSteps_->setToolTip(
        tr("Ceiling on the optimizer steps ONE cycle may spend. A cap, "
           "not a target: convergence to the force criterion is what "
           "ends a relaxation.\n\n"
           "It exists so a single hopeless proposal cannot consume the "
           "whole run. A cycle that hits it is still judged — dropping "
           "the hardest proposals would quietly bias the acceptance "
           "statistics — and the count of such cycles is reported in the "
           "summary, because their energies are not minima."));
    optForm->addRow(tr("Max steps per cycle:"), optimizerMaxSteps_);

    optimizerMaxStep_ = new QDoubleSpinBox(optBox);
    optimizerMaxStep_->setObjectName(QStringLiteral("mcOptMaxStepSpin"));
    optimizerMaxStep_->setRange(0.01, 2.0);
    optimizerMaxStep_->setDecimals(2);
    optimizerMaxStep_->setSingleStep(0.05);
    optimizerMaxStep_->setValue(0.2);
    optimizerMaxStep_->setSuffix(tr(" Å"));
    optimizerMaxStep_->setToolTip(
        tr("Largest distance one optimizer step may move an atom (ase's "
           "own `maxstep`). Raising it gets a badly strained start moving "
           "faster and risks stepping over the minimum. Not used by "
           "MDMin, which has no such parameter."));
    optForm->addRow(tr("Max displacement:"), optimizerMaxStep_);

    // -- Variable cell -------------------------------------------------
    //
    // The SHARED controls Geometry Optimization uses, not a one-checkbox
    // version of them: "relax the cell" is four interacting decisions (the
    // filter, the stress mask, the six Voigt components under a custom mask),
    // and an MC-Opt run relaxed isotropically produces energies that cannot be
    // lined up against an anisotropic relaxation of the same material. A
    // module that offered only an on/off switch would be quietly producing a
    // different quantity from the one next door.
    //
    // In their own group box rather than appended to the optimizer form: they
    // qualify the relaxation as a whole, and the six Voigt ticks are a row of
    // their own that reads badly wedged under "Max displacement".
    layout->addWidget(optBox);

    auto* cellBox = new QGroupBox(tr("Variable Cell"), page);
    cellBox->setObjectName(QStringLiteral("mcOptCellBox"));
    auto* cellForm = new QFormLayout(cellBox);
    cellControls_.build(cellBox, cellForm, [this] {
        refreshCost();
        refreshPreview();
    });
    layout->addWidget(cellBox);

    // refreshPreview() as well as refreshCost(), the way the shared VASP
    // group already wires its own controls: the review page is
    // regenerated on arrival anyway, but a preview that goes stale the
    // moment you change a setting and silently corrects itself on
    // navigation is a worse thing to hand someone than one that tracks.
    for (auto* box : {fmax_, optimizerMaxStep_})
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] {
            refreshCost();
            refreshPreview();
        });
    connect(optimizerMaxSteps_, &QSpinBox::valueChanged, this, [this] {
        refreshCost();
        refreshPreview();
    });
    connect(optimizer_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    layout->addStretch(1);
    return page;
}

QWidget* GrapheneOxideMcmdWizard::buildSettingsPage()
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

    // BUILT ONLY WHERE IT IS SHOWN. This used to be constructed
    // unconditionally with samplingBox as its parent and added to the form
    // only under GO/MCMD — and a child widget that is never laid out keeps its
    // default (0, 0) geometry inside its parent, so under GO/MC-Opt the spin
    // box sat in the corner of the group box painting over its own title. A
    // hidden-but-created control would fix the symptom; not creating one the
    // module has no use for is the actual answer, and collectConfig() already
    // guards every read of it.
    if (relaxationMode() == core::GoMcRelaxation::MolecularDynamics) {
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
    }

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

    // -- Relaxation --------------------------------------------------------
    //
    // The whole difference between the two modules this wizard serves. GO/MCMD
    // gets the ensemble/timestep/friction/equilibration block below; GO/MC-Opt
    // gets none of it — it never runs an integrator, so a timestep, a friction
    // and an ensemble would be controls that change nothing — and its
    // optimizer settings live on a stage of their own
    // (buildSecondSettingsPage()).
    //
    // An early return rather than wrapping two hundred lines in an if: the
    // shared tail is finishSettingsPage(), and both exits go through it.
    if (relaxationMode() == core::GoMcRelaxation::Optimization)
        return finishSettingsPage(page, layout);

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

    return finishSettingsPage(page, layout);
}

QWidget* GrapheneOxideMcmdWizard::finishSettingsPage(QWidget* page,
                                                    QVBoxLayout* layout)
{
    // The half of the page both modules share: the Output group, the cost
    // read-out, and the signal wiring. Extracted so the optimization branch
    // above can skip straight to it after building its own relaxation group,
    // rather than duplicating twenty controls that are identical in both.
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
           "move is accepted, since MCMD's whole point is relocating groups. "
           "Turn this off only to compare against that flat behaviour."));
    outputForm->addRow(castPerFrame_);
    layout->addWidget(outputBox);

    costLabel_ = new QLabel(page);
    costLabel_->setWordWrap(true);
    costLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(costLabel_);
    layout->addStretch(1);

    // Guarded one at a time: the optimization branch never builds the
    // thermostat controls, so half of these are null there. connect() on a
    // null sender is a Qt warning and a silently dead signal, which is
    // exactly the class of thing this file's own construction-order test
    // exists to catch.
    connect(cycles_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMcmdWizard::refreshCost);
    if (mdSteps_)
        connect(mdSteps_, &QSpinBox::valueChanged, this,
                &GrapheneOxideMcmdWizard::refreshCost);
    if (ensemble_)
        connect(ensemble_, &QComboBox::currentIndexChanged, this,
                &GrapheneOxideMcmdWizard::refreshCost);
    for (QDoubleSpinBox* spin :
         {temperature_, timestep_, friction_, pressure_, equilibrationFriction_})
        if (spin)
            connect(spin, &QDoubleSpinBox::valueChanged, this,
                    &GrapheneOxideMcmdWizard::refreshCost);
    if (equilibrationSteps_)
        connect(equilibrationSteps_, &QSpinBox::valueChanged, this,
                &GrapheneOxideMcmdWizard::refreshCost);
    connect(seed_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMcmdWizard::refreshCost);
    connect(snapshotInterval_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMcmdWizard::refreshCost);
    connect(viewportEvery_, &QSpinBox::valueChanged, this,
            &GrapheneOxideMcmdWizard::refreshCost);
    connect(bothFaces_, &QCheckBox::toggled, this,
            &GrapheneOxideMcmdWizard::refreshCost);
    connect(hydroxylAntipositionBox_, &QCheckBox::toggled, this,
            &GrapheneOxideMcmdWizard::refreshCost);

    refreshCost();
    return page;
}


void GrapheneOxideMcmdWizard::refreshCost()
{
    if (!costLabel_ || !cycles_)
        return;

    // GO/MC-Opt cannot quote the run's cost the way GO/MCMD can: a relaxation
    // runs until it converges, so the only honest statement is the CEILING
    // and the fact that a typical cycle is well under it. Quoting
    // cycles x max_steps as "the cost" would overstate it by a factor of
    // several; quoting nothing would hide that this module is the expensive
    // one. So: the bound, named as a bound.
    if (relaxationMode() == core::GoMcRelaxation::Optimization) {
        if (!optimizerMaxSteps_ || !fmax_)
            return;
        const long long ceiling = static_cast<long long>(cycles_->value())
            * optimizerMaxSteps_->value();
        costLabel_->setText(
            tr("<b>Cost:</b> at most %L1 force evaluations — %2 cycles × the "
               "%3-step ceiling. A converged relaxation usually takes a small "
               "fraction of that, so treat this as the worst case and the "
               "reported <i>relaxation_steps_total</i> as what it actually "
               "cost.<br>Each cycle relaxes the proposal to "
               "<b>%4 eV/Å</b>, so the Metropolis test compares two local "
               "minima — which is what this module is for, and what a "
               "GO/MCMD burst cannot promise. The walk is over MINIMA, not "
               "over a canonical ensemble: the temperature is the Metropolis "
               "parameter and nothing more.")
                .arg(ceiling)
                .arg(cycles_->value())
                .arg(optimizerMaxSteps_->value())
                .arg(fmax_->value(), 0, 'f', 3));
        return;
    }
    if (!mdSteps_)
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

core::GrapheneOxideMcmdConfig GrapheneOxideMcmdWizard::collectConfig() const
{
    core::GrapheneOxideMcmdConfig config;
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

    // GO/MC-Opt. Set unconditionally from the hook, then the controls that
    // only exist in that mode. Under GO/MCMD the relaxation stays
    // MolecularDynamics and every optimizer field keeps its struct default,
    // which the generator writes but the script never reads.
    config.relaxation = relaxationMode();
    if (optimizer_) {
        config.optimizer = static_cast<core::GoMcOptimizer>(
            optimizer_->currentData().toInt());
    }
    if (fmax_)
        config.fmax = fmax_->value();
    if (optimizerMaxSteps_)
        config.optimizerMaxSteps = optimizerMaxSteps_->value();
    if (optimizerMaxStep_)
        config.optimizerMaxStep = optimizerMaxStep_->value();
    // The cell group writes into a CalculatorConfig — that is the shape the
    // shared control speaks — and the five fields are copied across rather
    // than the whole config being handed over: config.calculator is the ENGINE
    // configuration, and putting a relaxCell on it would reach
    // vaspDrivesRelaxation() and change the emitted VASP block for a module
    // that drives its own optimizer.
    core::CalculatorConfig cell;
    cellControls_.applyTo(cell);
    config.relaxCell = cell.relaxCell;
    config.cellFilter = cell.cellFilter;
    config.cellHydrostatic = cell.cellHydrostatic;
    config.cellCustomMask = cell.cellCustomMask;
    for (int i = 0; i < 6; ++i)
        config.cellMask[i] = cell.cellMask[i];
    return config;
}

QString GrapheneOxideMcmdWizard::generateScript() const
{
    config_ = collectConfig();
    return QString::fromStdString(
        core::GrapheneOxideMcmdScriptGenerator::generate(config_));
}

} // namespace calango::gui
