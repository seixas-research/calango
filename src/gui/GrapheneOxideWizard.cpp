#include "gui/GrapheneOxideWizard.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace calango::gui {

namespace {

using Builder = core::GrapheneOxideBuilder;
using Base = Builder::Base;
using Dosing = Builder::Dosing;
using Group = Builder::Group;
using Lattice = Builder::Lattice;
using Region = Builder::Region;

struct GroupSpec {
    Group group;
    const char* label;
    const char* tooltip;
};

// Basal-plane chemistry: the carbon rehybridizes to sp3 and the group stands
// off the plane. Ordered as the builder applies them.
const std::array<GroupSpec, 2> kBasalGroups{{
    {Group::Epoxide, "Epoxide  (−O−)",
     "A bridging oxygen across a C–C bond. Consumes TWO basal carbons, both of "
     "which rehybridize to sp3. With hydroxyls, the dominant basal-plane group "
     "in the Lerf–Klinowski picture."},
    {Group::Hydroxyl, "Hydroxyl  (−OH, sp3)",
     "One −OH on a single basal carbon, standing above or below the plane on "
     "an sp3 carbon (C–O 1.48 Å). The other dominant basal-plane group."},
}};

// Edge chemistry: the carbon stays sp2 and the group replaces its hydrogen,
// lying in the plane of the flake. Carboxyl and carbonyl only — see
// GrapheneOxideBuilder::Group.
const std::array<GroupSpec, 2> kEdgeGroups{{
    {Group::Carboxyl, "Carboxyl  (−COOH)",
     "A −COOH group replacing an edge hydrogen, in the plane. It brings a "
     "carbon of its own and delivers TWO oxygens, so it moves both sides of "
     "the C/O ratio. The characteristic edge group of oxidatively exfoliated "
     "graphene oxide."},
    {Group::Carbonyl, "Carbonyl  (=O)",
     "A doubly-bonded oxygen replacing an edge hydrogen, collinear with the "
     "bond it replaces — the quinone-like edge carbonyl."},
}};

std::size_t slot(Group group)
{
    return static_cast<std::size_t>(group);
}

/// Every ratio slider is an integer 0..kSliderSteps. A QSlider is integer-only
/// and these are all continuous ratios, so one scale is fixed here rather than
/// each control inventing its own.
constexpr int kSliderSteps = 1000;

/// The stoichiometric ceiling on O/C.
///
/// Each oxygen needs at least two carbons to sit on: an epoxide bridges a C–C
/// bond, and a hydroxyl rehybridizes one carbon while the sheet still has to
/// hold together. C2O is the fully-oxidized limit, so O/C = 0.5. Real graphene
/// oxide reaches ~0.5 at its most oxidized and 0.1–0.25 typically; letting the
/// slider past 0.5 would be offering compositions no carbon framework supports.
constexpr double kMaxOxygenToCarbon = 0.5;

/// A labelled ratio slider: the row every stage-2 control is built from.
QSlider* makeRatioSlider(QWidget* parent, QFormLayout* form,
                         const QString& caption, const QString& tooltip,
                         double initial, double maximum, QLabel** readout)
{
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(0, kSliderSteps);
    slider->setValue(static_cast<int>(
        std::lround(initial / maximum * kSliderSteps)));
    slider->setToolTip(tooltip);
    // Ticks at tenths: a bare groove gives no sense of where 0.25 is, and this
    // is a control people set to a number they read in a paper.
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(kSliderSteps / 10);

    *readout = new QLabel(parent);
    (*readout)->setMinimumWidth(210);
    (*readout)->setTextFormat(Qt::RichText);
    (*readout)->setToolTip(tooltip);

    auto* row = new QHBoxLayout;
    row->addWidget(slider, 1);
    row->addWidget(*readout);
    form->addRow(caption, row);
    return slider;
}

} // namespace

GrapheneOxideWizard::GrapheneOxideWizard(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Graphene Oxide Builder"));
    resize(680, 660);

    auto* layout = new QVBoxLayout(this);
    stageLabel_ = new QLabel(this);
    QFont stageFont = stageLabel_->font();
    stageFont.setBold(true);
    stageLabel_->setFont(stageFont);
    layout->addWidget(stageLabel_);

    stack_ = new QStackedWidget(this);
    layout->addWidget(stack_, 1);

    // ===== Stage 1 — Base Structure ========================================
    auto* stage1 = new QWidget(stack_);
    auto* stage1Layout = new QVBoxLayout(stage1);

    auto* baseBox = new QGroupBox(tr("Base Structure"), stage1);
    auto* baseForm = new QFormLayout(baseBox);
    baseCombo_ = new QComboBox(baseBox);
    baseCombo_->addItem(tr("Periodic sheet — infinite, no edges"),
                        static_cast<int>(Base::PeriodicSheet));
    baseCombo_->addItem(tr("Nanoflake — finite, with edges"),
                        static_cast<int>(Base::Nanoflake));
    baseCombo_->setToolTip(
        tr("A periodic sheet models the interior of a graphene oxide flake and "
           "has no edges at all, so only basal-plane chemistry applies to it. "
           "A finite nanoflake carries both: basal carbons in its "
           "interior and a hydrogen-terminated rim that carboxyls and "
           "carbonyls can substitute into."));
    baseForm->addRow(tr("Base:"), baseCombo_);
    stage1Layout->addWidget(baseBox);

    // -- Periodic sheet ----------------------------------------------------
    sheetBox_ = new QGroupBox(tr("Periodic Sheet"), stage1);
    auto* latticeForm = new QFormLayout(sheetBox_);
    latticeCombo_ = new QComboBox(sheetBox_);
    latticeCombo_->addItem(tr("Primitive (a = b = 2.46 Å, 60°, 2 atoms)"),
                           static_cast<int>(Lattice::Primitive));
    latticeCombo_->addItem(tr("Conventional rectangular (2.46 × 4.26 Å, 4 atoms)"),
                           static_cast<int>(Lattice::Rectangular));
    latticeCombo_->setCurrentIndex(1);
    latticeCombo_->setToolTip(
        tr("The primitive cell is the smallest repeat unit. The rectangular "
           "cell has orthogonal axes, which makes supercells, slabs and "
           "interfaces easier to reason about at twice the atom count."));
    latticeForm->addRow(tr("Lattice:"), latticeCombo_);

    auto* superRow = new QHBoxLayout;
    for (int i = 0; i < 2; ++i) {
        supercellSpin_[i] = new QSpinBox(sheetBox_);
        supercellSpin_[i]->setRange(1, 40);
        supercellSpin_[i]->setValue(4);
        superRow->addWidget(supercellSpin_[i]);
    }
    superRow->addStretch(1);
    latticeForm->addRow(tr("Supercell (nx · ny):"), superRow);
    stage1Layout->addWidget(sheetBox_);

    // -- Nanoflake ---------------------------------------------------------
    flakeBox_ = new QGroupBox(tr("Nanoflake"), stage1);
    auto* flakeForm = new QFormLayout(flakeBox_);
    generationCombo_ = new QComboBox(flakeBox_);
    for (int m = 1; m <= Builder::kMaxFlakeIndex; ++m) {
        generationCombo_->addItem(
            tr("m = %1 — %2 (%3)")
                .arg(m)
                .arg(QString::fromLatin1(Builder::flakeName(m)))
                .arg(QString::fromStdString(Builder::flakeFormula(m))),
            m);
    }
    generationCombo_->setCurrentIndex(2); // m = 3, a well-studied size
    generationCombo_->setToolTip(
        tr("Nanoflake index m — the size knob. C(6m²)H(6m): a "
           "hexagonal, all-armchair, D6h flake built from 3m(m−1)+1 fused "
           "rings. It has 6m(m−1) basal carbons in the interior and exactly 6m "
           "edge carbons on the rim — the two pools the basal and edge "
           "chemistry draw from."));
    flakeForm->addRow(tr("Index m:"), generationCombo_);

    hydrogenCheck_ = new QCheckBox(tr("Hydrogen-terminate the unreacted edge"),
                                   flakeBox_);
    hydrogenCheck_->setChecked(true);
    hydrogenCheck_->setToolTip(
        tr("Cap every edge carbon that did not receive a functional group with "
           "a hydrogen, as in the parent hydrocarbon. Turning this off leaves "
           "radical dangling bonds — a deliberate choice for edge-state "
           "studies and a mistake otherwise."));
    flakeForm->addRow(hydrogenCheck_);
    stage1Layout->addWidget(flakeBox_);

    baseSummary_ = new QLabel(stage1);
    baseSummary_->setWordWrap(true);
    baseSummary_->setTextFormat(Qt::RichText);
    stage1Layout->addWidget(baseSummary_);

    auto* stage1Note = new QLabel(
        tr("The sheet is built in the xy plane with 20 Å of vacuum along z — "
           "enough that the functional groups, which stand ~1.5 Å off the "
           "plane, do not interact with their own periodic image. A flake is "
           "not periodic at all: it is fitted with a box carrying 10 Å of "
           "vacuum on every side."),
        stage1);
    stage1Note->setWordWrap(true);
    stage1Layout->addWidget(stage1Note);
    stage1Layout->addStretch(1);
    stack_->addWidget(stage1);

    // ===== Stage 2 — Functionalization & Oxidation Level ===================
    auto* stage2 = new QWidget(stack_);
    auto* stage2Layout = new QVBoxLayout(stage2);

    auto* amountBox = new QGroupBox(tr("Oxidation Level"), stage2);
    auto* amountForm = new QFormLayout(amountBox);
    dosingCombo_ = new QComboBox(amountBox);
    // O/C first, and therefore the default. It is the metric the composition
    // of graphene oxide is quoted in, it is linear in oxygen content, and — the
    // reason the explicit-coverage mode cannot be the default — it has a
    // meaningful zero and a meaningful ceiling, so a slider over it means
    // something end to end.
    dosingCombo_->addItem(tr("Target O/C ratio"),
                          static_cast<int>(Dosing::TargetRatio));
    dosingCombo_->addItem(tr("Explicit coverages per group"),
                          static_cast<int>(Dosing::ExplicitCoverage));
    dosingCombo_->setToolTip(
        tr("A target O/C ratio says how oxidized the finished structure should "
           "be, and the builder places groups — in the proportions set by the "
           "sliders — until it gets there. Explicit coverages instead say how "
           "much of each individual group to attach."));
    amountForm->addRow(tr("Set by:"), dosingCombo_);
    stage2Layout->addWidget(amountBox);

    // -- The ratio sliders -------------------------------------------------
    ratioBox_ = new QGroupBox(tr("Composition"), stage2);
    auto* ratioForm = new QFormLayout(ratioBox_);

    oxygenToCarbonSlider_ = makeRatioSlider(
        ratioBox_, ratioForm, tr("Oxidation  (O/C):"),
        tr("Oxygen per carbon in the FINISHED structure — every oxygen over "
           "every carbon, including the ones carboxyls bring with them. That "
           "is the composition XPS reports.\n\n"
           "0.00 is pristine graphene. 0.5 is the stoichiometric ceiling "
           "(C2O): every oxygen needs two carbons to sit on, so no carbon "
           "framework holds more. Heavily oxidized graphene oxide reaches "
           "0.4–0.5; typical material is 0.1–0.25; reduced graphene oxide "
           "sits below 0.1."),
        0.25, kMaxOxygenToCarbon, &oxygenToCarbonLabel_);

    basalHydrogenSlider_ = makeRatioSlider(
        ratioBox_, ratioForm, tr("Basal chemistry  (H/O):"),
        tr("Hydrogen per oxygen on the BASAL PLANE, which is the same thing as "
           "the hydroxyl share of the basal groups: an epoxide brings one "
           "oxygen and no hydrogen, a hydroxyl brings one of each.\n\n"
           "0.0 gives a sheet with only epoxides. 1.0 gives one with only "
           "hydroxyls. The Lerf–Klinowski picture has both in comparable "
           "amounts, so the middle of the range is the ordinary case."),
        0.5, 1.0, &basalHydrogenLabel_);
    stage2Layout->addWidget(ratioBox_);

    // Outside the form layout on purpose: a word-wrapped label only gets the
    // height its text actually needs when its parent layout honours
    // heightForWidth, and a spanning QFormLayout row does not — the last line
    // of this hint was being clipped on first show.
    amountHint_ = new QLabel(stage2);
    amountHint_->setWordWrap(true);
    amountHint_->setTextFormat(Qt::RichText);
    stage2Layout->addWidget(amountHint_);

    // -- Edge oxidation, its own control ------------------------------------
    // Two knobs because they are two different questions: HOW MUCH oxygen ends
    // up at the rim rather than on the basal plane, and WHAT it is once it is
    // there. Folding them into one slider would make "more edge oxidation"
    // silently also mean "more carboxyl", which is not a relationship the
    // chemistry has.
    edgeChemistryBox_ = new QGroupBox(tr("Edge Oxidation"), stage2);
    auto* edgeChemistryForm = new QFormLayout(edgeChemistryBox_);
    edgeShareSlider_ = makeRatioSlider(
        edgeChemistryBox_, edgeChemistryForm, tr("Oxygen at the edges:"),
        tr("The share of the oxygen budget delivered at the rim rather than on "
           "the basal plane — the edge oxidation density.\n\n"
           "0.0 puts every oxygen on the basal plane and leaves the rim fully "
           "hydrogen-terminated; 1.0 oxidizes only the rim. A large flake has "
           "far more basal than edge carbons, so a small edge share is the "
           "physically ordinary case; small flakes are edge-dominated."),
        0.3, 1.0, &edgeShareLabel_);
    edgeCarboxylSlider_ = makeRatioSlider(
        edgeChemistryBox_, edgeChemistryForm, tr("Edge chemistry  (COOH/O):"),
        tr("What the edge oxygen becomes: the carboxyl share of the edge "
           "groups.\n\n"
           "0.0 gives quinone-like carbonyls only (=O, one oxygen each). 1.0 "
           "gives carboxyls only (−COOH, which bring a carbon of their own and "
           "TWO oxygens, so they move both sides of the O/C ratio). Oxidative "
           "exfoliation produces both, carboxyls dominating at the most "
           "reactive sites."),
        0.5, 1.0, &edgeCarboxylLabel_);
    stage2Layout->addWidget(edgeChemistryBox_);

    // -- The two chemistries, kept apart -----------------------------------
    const auto addGroupRow = [this](QGroupBox* box, QFormLayout* form,
                                    const GroupSpec& spec) {
        const std::size_t index = slot(spec.group);
        auto* row = new QHBoxLayout;
        groupCheck_[index] = new QCheckBox(tr(spec.label), box);
        groupCheck_[index]->setToolTip(tr(spec.tooltip));
        groupAmount_[index] = new QDoubleSpinBox(box);
        groupAmount_[index]->setRange(0.0, 100.0);
        groupAmount_[index]->setDecimals(1);
        groupAmount_[index]->setSingleStep(2.5);
        groupAmount_[index]->setValue(10.0);
        groupAmount_[index]->setEnabled(false);
        row->addWidget(groupCheck_[index], 1);
        row->addWidget(groupAmount_[index]);
        form->addRow(row);
        connect(groupCheck_[index], &QCheckBox::toggled, groupAmount_[index],
                &QWidget::setEnabled);
        connect(groupCheck_[index], &QCheckBox::toggled, this,
                &GrapheneOxideWizard::refreshSummary);
        connect(groupAmount_[index], &QDoubleSpinBox::valueChanged, this,
                &GrapheneOxideWizard::refreshSummary);
    };

    auto* basalBox = new QGroupBox(tr("Basal Plane  —  sp3, out of the plane"),
                                   stage2);
    auto* basalForm = new QFormLayout(basalBox);
    for (const GroupSpec& spec : kBasalGroups)
        addGroupRow(basalBox, basalForm, spec);
    stage2Layout->addWidget(basalBox);

    auto* edgeBox = new QGroupBox(tr("Edges  —  sp2, in the plane"), stage2);
    auto* edgeForm = new QFormLayout(edgeBox);
    for (const GroupSpec& spec : kEdgeGroups)
        addGroupRow(edgeBox, edgeForm, spec);
    stage2Layout->addWidget(edgeBox);
    edgeNote_ = new QLabel(stage2); // outside the form, for the same reason
    edgeNote_->setWordWrap(true);
    edgeNote_->setTextFormat(Qt::RichText);
    stage2Layout->addWidget(edgeNote_);

    auto* optionsGroup = new QGroupBox(tr("Sampling"), stage2);
    auto* optionsForm = new QFormLayout(optionsGroup);
    bothFacesCheck_ = new QCheckBox(tr("Decorate both faces"), optionsGroup);
    bothFacesCheck_->setChecked(true);
    bothFacesCheck_->setToolTip(
        tr("Real graphene oxide is functionalized on both sides. Restricting "
           "to one face puts a net dipole across the sheet, which changes the "
           "electrostatics of anything computed from it. Applies to the basal "
           "groups; edge groups lie in the plane."));
    optionsForm->addRow(bothFacesCheck_);
    seedSpin_ = new QSpinBox(optionsGroup);
    seedSpin_->setRange(0, 999999);
    seedSpin_->setValue(0);
    seedSpin_->setToolTip(
        tr("Random seed for the site assignment. The same seed and settings "
           "reproduce the structure exactly — record it alongside any result."));
    optionsForm->addRow(tr("Seed:"), seedSpin_);
    stage2Layout->addWidget(optionsGroup);

    coverageSummary_ = new QLabel(stage2);
    coverageSummary_->setWordWrap(true);
    coverageSummary_->setTextFormat(Qt::RichText);
    stage2Layout->addWidget(coverageSummary_);
    stage2Layout->addStretch(1);
    stack_->addWidget(stage2);

    // ===== Navigation ======================================================
    auto* buttonRow = new QHBoxLayout;
    backButton_ = new QPushButton(tr("< Back"), this);
    nextButton_ = new QPushButton(tr("Next >"), this);
    auto* cancelButton = new QPushButton(tr("Cancel"), this);
    buttonRow->addWidget(cancelButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(backButton_);
    buttonRow->addWidget(nextButton_);
    layout->addLayout(buttonRow);

    connect(backButton_, &QPushButton::clicked, this,
            &GrapheneOxideWizard::goBack);
    connect(nextButton_, &QPushButton::clicked, this,
            &GrapheneOxideWizard::goNext);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    for (QSpinBox* spin : supercellSpin_)
        connect(spin, &QSpinBox::valueChanged, this,
                &GrapheneOxideWizard::refreshSummary);
    connect(latticeCombo_, &QComboBox::currentIndexChanged, this,
            &GrapheneOxideWizard::refreshSummary);
    connect(baseCombo_, &QComboBox::currentIndexChanged, this,
            &GrapheneOxideWizard::refreshSummary);
    connect(generationCombo_, &QComboBox::currentIndexChanged, this,
            &GrapheneOxideWizard::refreshSummary);
    connect(hydrogenCheck_, &QCheckBox::toggled, this,
            &GrapheneOxideWizard::refreshSummary);
    connect(dosingCombo_, &QComboBox::currentIndexChanged, this,
            &GrapheneOxideWizard::refreshSummary);
    for (QSlider* slider : {oxygenToCarbonSlider_, basalHydrogenSlider_,
                            edgeShareSlider_, edgeCarboxylSlider_})
        connect(slider, &QSlider::valueChanged, this,
                &GrapheneOxideWizard::refreshSummary);

    goBack(); // start on stage 1 with the right button states

    // Initial selection is applied only now that EVERY widget exists.
    // setChecked() emits toggled(), which reaches refreshSummary() ->
    // config(), and config() reads controls from both stages — doing this
    // mid-constructor dereferenced widgets that had not been created yet.
    // built_ below makes that ordering non-fatal; this keeps it non-issue.
    //
    // Epoxide + hydroxyl: the basal-plane pair that defines the accepted
    // structural picture of graphene oxide.
    built_ = true;
    groupCheck_[slot(Group::Epoxide)]->setChecked(true);
    groupCheck_[slot(Group::Hydroxyl)]->setChecked(true);
    // Stage 2 opens on the O/C sliders: the composition metric graphene oxide
    // is quoted in, rather than a per-group coverage table.
    dosingCombo_->setCurrentIndex(0);
    refreshSummary();
}

bool GrapheneOxideWizard::flakeSelected() const
{
    return static_cast<Base>(baseCombo_->currentData().toInt())
        == Base::Nanoflake;
}

void GrapheneOxideWizard::substrateCounts(int& total, int& basal,
                                          int& edge) const
{
    if (flakeSelected()) {
        const int m = generationCombo_->currentData().toInt();
        total = Builder::flakeCarbonCount(m);
        edge = Builder::flakeEdgeCarbonCount(m);
        basal = total - edge;
        return;
    }
    const auto lattice = static_cast<Lattice>(latticeCombo_->currentData().toInt());
    const int perCell = lattice == Lattice::Primitive ? 2 : 4;
    total = perCell * supercellSpin_[0]->value() * supercellSpin_[1]->value();
    basal = total;
    edge = 0; // a periodic sheet is edgeless by construction
}

double GrapheneOxideWizard::sliderValue(const QSlider* slider, double maximum)
{
    return slider->value() * maximum / kSliderSteps;
}

core::GrapheneOxideBuilder::Config GrapheneOxideWizard::config() const
{
    Builder::Config config;
    config.base = static_cast<Base>(baseCombo_->currentData().toInt());
    config.lattice = static_cast<Lattice>(latticeCombo_->currentData().toInt());
    config.supercell[0] = supercellSpin_[0]->value();
    config.supercell[1] = supercellSpin_[1]->value();
    config.flakeIndex = generationCombo_->currentData().toInt();
    config.hydrogenTerminateEdges = hydrogenCheck_->isChecked();
    config.dosing = static_cast<Dosing>(dosingCombo_->currentData().toInt());

    const bool hasEdges = flakeSelected();
    const bool targeting = config.dosing == Dosing::TargetRatio;

    if (targeting) {
        // --- The sliders drive the weights ---------------------------------
        // Both basal groups deliver exactly one oxygen, so weighting them by
        // GROUP count and weighting them by OXYGEN count are the same thing —
        // which is what makes the hydroxyl share of the basal groups equal to
        // H/O on the basal plane exactly, rather than approximately.
        const double basalH = sliderValue(basalHydrogenSlider_, 1.0);
        config.setWeight(Group::Epoxide, 1.0 - basalH);
        config.setWeight(Group::Hydroxyl, basalH);

        // The edges are not so tidy: a carboxyl brings TWO oxygens and a
        // carbonyl one, so a carboxyl share stated in oxygen has to be
        // converted to the propensity-per-GROUP the builder draws with.
        // f oxygens from carboxyls means f/2 carboxyl groups against (1-f)
        // carbonyl groups.
        const double carboxylOxygenShare =
            sliderValue(edgeCarboxylSlider_, 1.0);
        config.setWeight(Group::Carboxyl, carboxylOxygenShare / 2.0);
        config.setWeight(Group::Carbonyl, 1.0 - carboxylOxygenShare);

        // Edge chemistry on an edgeless substrate is not "requested and then
        // unmet": a periodic sheet has no rim, so the whole budget is basal.
        const double edgeShare =
            hasEdges ? sliderValue(edgeShareSlider_, 1.0) : 0.0;
        config.basalOxygenShare = 1.0 - edgeShare;

        // O/C -> C/O, which is what the builder works in. Zero oxidation has
        // no C/O to express at all, so it is handled where it belongs: every
        // weight goes to zero and the builder returns the pristine substrate.
        const double oxygenToCarbon =
            sliderValue(oxygenToCarbonSlider_, kMaxOxygenToCarbon);
        if (oxygenToCarbon <= 0.0) {
            for (std::size_t index = 0; index < kGroups; ++index)
                config.weight[index] = 0.0;
            config.targetCarbonToOxygen = 1e9;
        } else {
            config.targetCarbonToOxygen = 1.0 / oxygenToCarbon;
        }
        config.bothFaces = bothFacesCheck_->isChecked();
        config.seed = static_cast<std::uint32_t>(seedSpin_->value());
        return config;
    }

    for (std::size_t index = 0; index < kGroups; ++index) {
        const auto group = static_cast<Group>(index);
        const bool usable =
            groupCheck_[index]->isChecked()
            && (hasEdges || Builder::region(group) == Region::Basal);
        config.setCoverage(group, usable ? groupAmount_[index]->value() / 100.0
                                         : 0.0);
    }
    config.bothFaces = bothFacesCheck_->isChecked();
    config.seed = static_cast<std::uint32_t>(seedSpin_->value());
    return config;
}

void GrapheneOxideWizard::refreshSummary()
{
    // Signals emitted while the constructor is still wiring widgets reach here
    // before the widgets config() reads exist. Rather than null-check each one
    // at every use, the whole slot is inert until construction completes.
    if (!built_)
        return;

    const bool flake = flakeSelected();
    const bool targeting =
        static_cast<Dosing>(dosingCombo_->currentData().toInt())
        == Dosing::TargetRatio;

    sheetBox_->setVisible(!flake);
    flakeBox_->setVisible(flake);
    // One mode's controls at a time: the sliders and the per-group coverages
    // answer the same question two ways, and showing both greyed halves makes
    // a busy panel out of a simple choice.
    ratioBox_->setVisible(targeting);
    // Edge oxidation exists only where there is an edge. A periodic sheet is
    // edgeless by construction, so this is not "unavailable", it is not a
    // question.
    edgeChemistryBox_->setVisible(targeting && flake);

    const double oxygenToCarbon =
        sliderValue(oxygenToCarbonSlider_, kMaxOxygenToCarbon);
    oxygenToCarbonLabel_->setText(
        oxygenToCarbon <= 0.0
            ? tr("<b>0.000</b> — pristine")
            : tr("<b>%1</b> &nbsp; (C/O = %2)")
                  .arg(oxygenToCarbon, 0, 'f', 3)
                  .arg(1.0 / oxygenToCarbon, 0, 'f', 2));
    const double basalH = sliderValue(basalHydrogenSlider_, 1.0);
    basalHydrogenLabel_->setText(tr("<b>%1</b> &nbsp; %2 % epoxide, %3 % hydroxyl")
                                     .arg(basalH, 0, 'f', 2)
                                     .arg((1.0 - basalH) * 100.0, 0, 'f', 0)
                                     .arg(basalH * 100.0, 0, 'f', 0));
    const double edgeShare = sliderValue(edgeShareSlider_, 1.0);
    edgeShareLabel_->setText(tr("<b>%1</b> &nbsp; %2 % basal, %3 % edge")
                                 .arg(edgeShare, 0, 'f', 2)
                                 .arg((1.0 - edgeShare) * 100.0, 0, 'f', 0)
                                 .arg(edgeShare * 100.0, 0, 'f', 0));
    const double carboxylShare = sliderValue(edgeCarboxylSlider_, 1.0);
    edgeCarboxylLabel_->setText(
        tr("<b>%1</b> &nbsp; %2 % carbonyl, %3 % carboxyl")
            .arg(carboxylShare, 0, 'f', 2)
            .arg((1.0 - carboxylShare) * 100.0, 0, 'f', 0)
            .arg(carboxylShare * 100.0, 0, 'f', 0));

    // Every edge group is unavailable on a sheet, and saying why beats an
    // unexplained grey box.
    for (const GroupSpec& spec : kEdgeGroups) {
        const std::size_t index = slot(spec.group);
        groupCheck_[index]->setEnabled(flake);
        groupAmount_[index]->setEnabled(flake && groupCheck_[index]->isChecked());
    }
    edgeNote_->setVisible(!flake);
    edgeNote_->setText(
        tr("<i>A periodic sheet has no edges. Choose the nanoflake base in "
           "stage 1 to place edge chemistry — carboxyls and carbonyls.</i>"));

    int carbons = 0;
    int basal = 0;
    int edge = 0;
    substrateCounts(carbons, basal, edge);

    if (flake) {
        const int m = generationCombo_->currentData().toInt();
        baseSummary_->setText(
            tr("<b>%1</b>, %2 — %3 carbons: %4 basal (interior, three carbon "
               "neighbours) and %5 edge (rim, one substitutable hydrogen). "
               "Built from %6 fused rings.")
                .arg(QString::fromLatin1(Builder::flakeName(m)))
                .arg(QString::fromStdString(Builder::flakeFormula(m)))
                .arg(carbons)
                .arg(basal)
                .arg(edge)
                .arg(3 * m * (m - 1) + 1));
    } else {
        const auto lattice =
            static_cast<Lattice>(latticeCombo_->currentData().toInt());
        baseSummary_->setText(
            tr("%1 carbons in the sheet (%2 per cell × %3 × %4), all basal — "
               "an infinite sheet has no edge carbons.")
                .arg(carbons)
                .arg(lattice == Lattice::Primitive ? 2 : 4)
                .arg(supercellSpin_[0]->value())
                .arg(supercellSpin_[1]->value()));
    }

    if (!coverageSummary_)
        return;

    const auto cfg = config();

    if (targeting) {
        amountHint_->setText(
            tr("<i>The sliders set the composition; the builder places groups "
               "in those proportions until the structure reaches the requested "
               "O/C. Because a carboxyl brings a carbon of its own, the ratio "
               "is recomputed after every placement rather than worked to a "
               "fixed count.</i>"));

        if (oxygenToCarbon <= 0.0) {
            coverageSummary_->setText(
                tr("<i>O/C = 0 — this builds the pristine substrate, with no "
                   "oxygen at all.</i>"));
            return;
        }
        const int oxygens =
            static_cast<int>(std::llround(carbons * oxygenToCarbon));
        // Upper bound on what the substrate can carry: one oxygen per basal
        // carbon, and up to two per edge carbon (a carboxyl).
        const int capacity = basal + 2 * edge;
        QString text =
            tr("O/C = %1 (C/O = %2) → roughly <b>%3 oxygen atoms</b> on this "
               "substrate.")
                .arg(oxygenToCarbon, 0, 'f', 3)
                .arg(1.0 / oxygenToCarbon, 0, 'f', 2)
                .arg(oxygens);
        text += tr("<br>Basal plane: %1 % epoxide, %2 % hydroxyl (H/O = %3).")
                    .arg((1.0 - basalH) * 100.0, 0, 'f', 0)
                    .arg(basalH * 100.0, 0, 'f', 0)
                    .arg(basalH, 0, 'f', 2);
        if (flake) {
            text += tr("<br>Oxygen split: %1 % basal, %2 % edge; the edge "
                       "oxygen is %3 % carbonyl, %4 % carboxyl.")
                        .arg((1.0 - edgeShare) * 100.0, 0, 'f', 0)
                        .arg(edgeShare * 100.0, 0, 'f', 0)
                        .arg((1.0 - carboxylShare) * 100.0, 0, 'f', 0)
                        .arg(carboxylShare * 100.0, 0, 'f', 0);
        }
        if (oxygens > capacity) {
            text += tr("<br><b>This substrate cannot hold that much oxygen.</b> "
                       "At most %1 oxygens fit (one per basal carbon, two per "
                       "edge carbon), so the build will stop short and report "
                       "the ratio it reached.")
                        .arg(capacity);
        }
        coverageSummary_->setText(text);
        return;
    }

    amountHint_->setText(
        tr("<i>Coverage is the fraction of the carbons IN THAT REGION which "
           "the group consumes, not the fraction of groups. An epoxide takes "
           "two basal carbons and the others take one, so the values are "
           "additive within a region: 10 % epoxide plus 10 % hydroxyl "
           "functionalizes 20 % of the basal plane.</i>"));
    for (std::size_t index = 0; index < kGroups; ++index)
        groupAmount_[index]->setSuffix(tr(" %"));

    QStringList basalParts;
    QStringList edgeParts;
    double basalFraction = 0.0;
    double edgeFraction = 0.0;
    for (std::size_t index = 0; index < kGroups; ++index) {
        const auto group = static_cast<Group>(index);
        const double fraction = cfg.coverageFor(group);
        if (fraction <= 0.0)
            continue;
        const bool isBasal = Builder::region(group) == Region::Basal;
        const int pool = isBasal ? basal : edge;
        const QString part =
            tr("%1 × %2")
                .arg(static_cast<int>(
                    std::llround(fraction * pool / Builder::carbonCost(group))))
                .arg(QString::fromLatin1(Builder::name(group)));
        (isBasal ? basalParts : edgeParts) << part;
        (isBasal ? basalFraction : edgeFraction) += fraction;
    }

    if (basalParts.isEmpty() && edgeParts.isEmpty()) {
        coverageSummary_->setText(
            tr("<i>No groups selected — this builds the pristine substrate.</i>"));
        return;
    }

    QStringList lines;
    if (!basalParts.isEmpty()) {
        lines << tr("Basal plane: %1 — %2 % of the %3 basal carbons.")
                     .arg(basalParts.join(QStringLiteral(", ")))
                     .arg(basalFraction * 100.0, 0, 'f', 1)
                     .arg(basal);
    }
    if (!edgeParts.isEmpty()) {
        lines << tr("Edges: %1 — %2 % of the %3 edge carbons.")
                     .arg(edgeParts.join(QStringLiteral(", ")))
                     .arg(edgeFraction * 100.0, 0, 'f', 1)
                     .arg(edge);
    }
    QString text = lines.join(QStringLiteral("<br>"));
    if (basalFraction > 1.0 || edgeFraction > 1.0) {
        // Stated as a fact about the substrate rather than blocked: the builder
        // truncates and reports, so the user can still proceed knowingly.
        text += tr("<br><b>More than 100 % of a region's carbons are "
                   "requested.</b> Groups are placed in order — epoxide, "
                   "carboxyl, carbonyl, hydroxyl — until the "
                   "sites run out, and the shortfall is reported after "
                   "building.");
    } else if (basalFraction > 0.6) {
        text += tr("<br>Above roughly 60 % basal coverage the epoxides "
                   "struggle to find bonded pairs of free carbons, so the "
                   "placed count may fall short of the request.");
    }
    coverageSummary_->setText(text);
}

void GrapheneOxideWizard::goNext()
{
    if (stack_->currentIndex() == 0) {
        stack_->setCurrentIndex(1);
        stageLabel_->setText(
            tr("Stage 2 of 2 — Functionalization & Oxidation Level"));
        backButton_->setEnabled(true);
        nextButton_->setText(tr("Build"));
        refreshSummary();
        return;
    }

    Builder::Report report;
    result_ = Builder::build(config(), &report);
    report_ = report;
    accept();
}

void GrapheneOxideWizard::goBack()
{
    stack_->setCurrentIndex(0);
    stageLabel_->setText(tr("Stage 1 of 2 — Base Structure"));
    backButton_->setEnabled(false);
    nextButton_->setText(tr("Next >"));
}

} // namespace calango::gui
