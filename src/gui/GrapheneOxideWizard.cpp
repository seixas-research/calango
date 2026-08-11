#include "gui/GrapheneOxideWizard.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

using Builder = core::GrapheneOxideBuilder;
using Base = Builder::Base;
using Dosing = Builder::Dosing;
using Group = Builder::Group;
using Lattice = Builder::Lattice;

/// The stoichiometric ceiling on O/C.
///
/// Each oxygen needs at least two carbons to sit on: an epoxide bridges a C–C
/// bond, and a hydroxyl rehybridizes one carbon while the sheet still has to
/// hold together. C2O is the fully-oxidized limit, so O/C = 0.5. Real graphene
/// oxide reaches ~0.5 at its most oxidized and 0.1–0.25 typically; letting the
/// slider past 0.5 would be offering compositions no carbon framework supports.
constexpr double kMaxOxygenToCarbon = 0.5;

/// Width of the caption column in stage 2.
///
/// Each QFormLayout sizes its own label column, so without a shared minimum the
/// four sliders start at four slightly different x positions — separately
/// tidy, collectively ragged. Fixing the column makes the boxes read as one
/// panel.
constexpr int kCaptionWidth = 180;

/// Every stage-2 form is laid out the same way, so the panel reads as one
/// thing rather than four boxes that happen to be stacked.
void styleForm(QFormLayout* form)
{
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
}

} // namespace

double GrapheneOxideWizard::RatioControl::value() const
{
    return static_cast<double>(slider->value()) / kRatioScale;
}

void GrapheneOxideWizard::addRatioRow(QFormLayout* form, RatioControl& control,
                                      const QString& name,
                                      const QString& caption,
                                      const QString& tooltip, double initial,
                                      double maximum)
{
    auto* parent = form->parentWidget();
    const int steps = static_cast<int>(std::lround(maximum * kRatioScale));
    const int start = static_cast<int>(std::lround(initial * kRatioScale));

    control.slider = new QSlider(Qt::Horizontal, parent);
    // Named so the pair can be found unambiguously — two edge controls share a
    // group box, so "the spin box in this box" is not a unique reference, and
    // the test that checks slider and box agree needs to name them exactly.
    control.slider->setObjectName(name + QStringLiteral("Slider"));
    control.slider->setRange(0, steps);
    control.slider->setValue(start);
    control.slider->setToolTip(tooltip);
    // Ticks at tenths of the range: a bare groove gives no sense of where 0.25
    // is, and this is a control people set to a number they read in a paper.
    control.slider->setTickPosition(QSlider::TicksBelow);
    control.slider->setTickInterval(steps / 10);
    // A click in the groove should jump a tenth, not one thousandth — with
    // pageStep left at its default the slider is unusable by keyboard.
    control.slider->setPageStep(steps / 10);
    control.slider->setSingleStep(kRatioScale / 100);

    control.box = new QDoubleSpinBox(parent);
    control.box->setObjectName(name + QStringLiteral("Box"));
    control.box->setRange(0.0, maximum);
    control.box->setDecimals(3);
    control.box->setSingleStep(0.01);
    control.box->setValue(static_cast<double>(start) / kRatioScale);
    control.box->setToolTip(tooltip);
    control.box->setKeyboardTracking(false); // fire on commit, not per digit
    control.box->setMinimumWidth(84);
    control.box->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    control.readout = new QLabel(parent);
    control.readout->setMinimumWidth(190);
    control.readout->setTextFormat(Qt::RichText);
    control.readout->setToolTip(tooltip);

    // --- The two views, kept identical ------------------------------------
    // The slider is authoritative and the box mirrors it. Each handler blocks
    // the other control's signals before writing to it, so an edit travels one
    // way and stops; without that the pair ping-pongs, and because the two
    // quantizations are identical it would ping-pong forever rather than
    // settling.
    connect(control.slider, &QSlider::valueChanged, this,
            [this, &control](int position) {
                const QSignalBlocker block(control.box);
                control.box->setValue(static_cast<double>(position)
                                      / kRatioScale);
                refreshSummary();
            });
    connect(control.box, &QDoubleSpinBox::valueChanged, this,
            [this, &control](double typed) {
                const QSignalBlocker block(control.slider);
                control.slider->setValue(
                    static_cast<int>(std::lround(typed * kRatioScale)));
                refreshSummary();
            });

    auto* row = new QHBoxLayout;
    row->setSpacing(10);
    row->addWidget(control.slider, 1);
    row->addWidget(control.box);
    row->addWidget(control.readout);

    auto* captionLabel = new QLabel(caption, parent);
    captionLabel->setMinimumWidth(kCaptionWidth);
    captionLabel->setBuddy(control.box); // Alt+accelerator lands on the number
    form->addRow(captionLabel, row);
}

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
    baseCombo_->setObjectName(QStringLiteral("baseCombo"));
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
    stage2Layout->setSpacing(10);

    // TWO regions, dosed INDEPENDENTLY, and one group box each so the page
    // says so before any tooltip does. The basal plane and the rim are set by
    // different chemistry — basal coverage tracks oxidant exposure, rim content
    // tracks exfoliation and workup — and a flake can be heavily oxidized on
    // one and clean on the other. Neither slider here moves the other region.
    //
    // There is deliberately no "set by" mode selector. The per-group coverage
    // table this dialog used to carry has been removed, not hidden, so the
    // sliders below are the only thing the build reads.

    // -- The basal plane ----------------------------------------------------
    auto* basalBox =
        new QGroupBox(tr("Basal-Plane Oxidation  —  sp³, off the plane"),
                      stage2);
    auto* basalForm = new QFormLayout(basalBox);
    styleForm(basalForm);
    addRatioRow(
        basalForm, basalOxidation_, QStringLiteral("basalOxidation"),
        tr("Oxygen per basal carbon:"),
        tr("Oxygen delivered to the BASAL PLANE, per basal carbon.\n\n"
           "Deliberately not the structure's overall O/C: the rim's oxygen and "
           "the carbons carboxyls bring with them are excluded, so that this "
           "number does not change when you touch the edge settings. That "
           "independence is the whole point of separating the two.\n\n"
           "0.00 is a pristine basal plane. 0.5 is the stoichiometric ceiling "
           "(C2O): every oxygen needs two carbons to sit on, so no carbon "
           "framework holds more. Heavily oxidized graphene oxide reaches "
           "0.4–0.5; typical material is 0.1–0.25; reduced graphene oxide sits "
           "below 0.1."),
        0.25, kMaxOxygenToCarbon);
    addRatioRow(
        basalForm, basalHydrogen_, QStringLiteral("basalHydrogen"),
        tr("Hydrogen per oxygen  (H/O):"),
        tr("Hydrogen per oxygen on the basal plane, which is the same thing as "
           "the hydroxyl share of the basal groups: an epoxide brings one "
           "oxygen and no hydrogen, a hydroxyl brings one of each.\n\n"
           "0.0 gives only epoxides — a bridging −O− across a C–C bond, "
           "consuming TWO basal carbons, both rehybridized to sp3. 1.0 gives "
           "only hydroxyls — one −OH on a single sp3 carbon (C–O 1.48 Å), "
           "standing above or below the plane. The Lerf–Klinowski picture has "
           "both in comparable amounts, so the middle of the range is the "
           "ordinary case."),
        0.5, 1.0);
    stage2Layout->addWidget(basalBox);

    // -- The rim, if there is one -------------------------------------------
    // Two knobs because they are two different questions: HOW MUCH of the rim
    // is oxidized at all, and WHAT it becomes where it is. Folding them into
    // one slider would make "more edge oxidation" silently also mean "more
    // carboxyl", which is not a relationship the chemistry has.
    edgeChemistryBox_ =
        new QGroupBox(tr("Edge Oxidation  —  sp², in the plane"), stage2);
    auto* edgeForm = new QFormLayout(edgeChemistryBox_);
    styleForm(edgeForm);
    addRatioRow(
        edgeForm, edgeOxidation_, QStringLiteral("edgeOxidation"),
        tr("Rim carbons oxidized:"),
        tr("The fraction of the rim carrying a functional group rather than a "
           "hydrogen.\n\n"
           "0.0 is categorical: the rim is STRICTLY hydrogen-terminated, as in "
           "the parent hydrocarbon — not 'a little edge chemistry' but none, "
           "which is the right starting point for studying basal chemistry on "
           "its own. As this rises, edge hydrogens are replaced by carboxyls "
           "and carbonyls in the proportion set below; the carbons that miss "
           "out keep their hydrogen, so the rim is never left as bare "
           "radicals.\n\n"
           "Independent of the basal setting: a flake can be heavily oxidized "
           "on its rim and clean in its interior, or the reverse."),
        0.0, 1.0);
    addRatioRow(
        edgeForm, edgeCarboxyl_, QStringLiteral("edgeCarboxyl"),
        tr("Carboxyl share  (COOH/O):"),
        tr("What the edge oxygen becomes: the carboxyl share of the edge "
           "oxygen.\n\n"
           "0.0 gives quinone-like carbonyls only (=O, one oxygen each, "
           "collinear with the edge bond it replaces). 1.0 gives carboxyls "
           "only (−COOH, which bring a carbon of their own and TWO oxygens) — "
           "the characteristic edge group of oxidatively exfoliated graphene "
           "oxide. Exfoliation produces both, carboxyls dominating at the most "
           "reactive sites.\n\n"
           "Stated in OXYGEN because that is what a composition is quoted in; "
           "an even split of the oxygen is not an even split of the groups."),
        0.5, 1.0);
    stage2Layout->addWidget(edgeChemistryBox_);

    // Shown in place of the edge box on a periodic sheet. An absent control
    // with no explanation reads as a missing feature; one line saying the
    // question does not arise reads as a fact about the substrate.
    edgeNote_ = new QLabel(
        tr("<i>Edge oxidation does not apply: a periodic sheet is infinite and "
           "has no rim. Choose the nanoflake base in stage 1 to place "
           "carboxyls and carbonyls.</i>"),
        stage2);
    edgeNote_->setWordWrap(true);
    edgeNote_->setTextFormat(Qt::RichText);
    stage2Layout->addWidget(edgeNote_);

    auto* optionsGroup = new QGroupBox(tr("Sampling"), stage2);
    auto* optionsForm = new QFormLayout(optionsGroup);
    styleForm(optionsForm);
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
    seedSpin_->setMaximumWidth(140);
    seedSpin_->setValue(0);
    seedSpin_->setToolTip(
        tr("Random seed for the site assignment. The same seed and settings "
           "reproduce the structure exactly — record it alongside any result."));
    auto* seedCaption = new QLabel(tr("Seed:"), optionsGroup);
    seedCaption->setMinimumWidth(kCaptionWidth);
    optionsForm->addRow(seedCaption, seedSpin_);
    stage2Layout->addWidget(optionsGroup);

    coverageSummary_ = new QLabel(stage2);
    coverageSummary_->setWordWrap(true);
    coverageSummary_->setTextFormat(Qt::RichText);
    stage2Layout->addWidget(coverageSummary_);
    stage2Layout->addStretch(1);
    stack_->addWidget(stage2);

    // ===== Stage 3 — MDMC Optimization (optional) ==========================
    auto* stage3 = new QWidget(stack_);
    auto* stage3Layout = new QVBoxLayout(stage3);
    stage3Layout->setSpacing(10);

    mdmcCheck_ = new QCheckBox(
        tr("Refine the arrangement with hybrid MD / Monte Carlo"), stage3);
    mdmcCheck_->setObjectName(QStringLiteral("mdmcCheck"));
    mdmcCheck_->setToolTip(
        tr("Anneal WHERE the groups sit, at fixed composition.\n\n"
           "The builder places groups at random free sites, which gives the "
           "composition you asked for but not a low-energy arrangement. Real "
           "graphene oxide is strongly correlated — hydroxyls cluster, "
           "epoxides line up along the ridges they create — and no geometry "
           "optimizer can find that, because moving a group from one carbon to "
           "another is a discrete step and an optimizer only moves atoms "
           "continuously."));
    stage3Layout->addWidget(mdmcCheck_);

    auto* mdmcNote = new QLabel(
        tr("<p>Each cycle lifts one functional group, drops it on a randomly "
           "drawn free site — a bonded carbon pair for an epoxide, a single "
           "carbon otherwise, either face of the sheet — runs a short burst of "
           "molecular dynamics, and accepts or rejects by the Metropolis "
           "criterion at the sampling temperature.</p>"
           "<p>A move whose chemistry did not survive the dynamics is rejected "
           "whatever its energy. That check is not optional bookkeeping: "
           "dynamics on a reactive surface will abstract a hydroxyl and a "
           "neighbouring hydrogen and release water, which lowers the energy "
           "and destroys the material.</p>"
           "<p><b>This needs a calculator.</b> Choosing one, reviewing the "
           "script and launching the run happen on the next screen — the "
           "structure is built and opened either way.</p>"),
        stage3);
    mdmcNote->setWordWrap(true);
    mdmcNote->setTextFormat(Qt::RichText);
    stage3Layout->addWidget(mdmcNote);

    mdmcCostNote_ = new QLabel(stage3);
    mdmcCostNote_->setWordWrap(true);
    mdmcCostNote_->setTextFormat(Qt::RichText);
    stage3Layout->addWidget(mdmcCostNote_);
    stage3Layout->addStretch(1);
    stack_->addWidget(stage3);

    connect(mdmcCheck_, &QCheckBox::toggled, this,
            &GrapheneOxideWizard::refreshSummary);

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
    // Neither of these changes the summary TODAY. They are wired anyway so the
    // invariant is "every input refreshes", which is the version of this that
    // does not quietly go stale the first time the summary grows a line.
    connect(bothFacesCheck_, &QCheckBox::toggled, this,
            &GrapheneOxideWizard::refreshSummary);
    connect(seedSpin_, &QSpinBox::valueChanged, this,
            &GrapheneOxideWizard::refreshSummary);
    // The ratio controls wire themselves in addRatioRow(), where the slider
    // and its spin box are kept in step.

    showStage(0); // start on stage 1 with the right button states

    // The summary is driven only now that EVERY widget exists. refreshSummary()
    // calls config(), and config() reads controls from BOTH stages — running it
    // mid-constructor dereferenced widgets that had not been created yet. The
    // built_ flag makes that ordering non-fatal; doing this last keeps it a
    // non-issue.
    built_ = true;
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

core::GrapheneOxideBuilder::Config GrapheneOxideWizard::config() const
{
    Builder::Config config;
    config.base = static_cast<Base>(baseCombo_->currentData().toInt());
    config.lattice = static_cast<Lattice>(latticeCombo_->currentData().toInt());
    config.supercell[0] = supercellSpin_[0]->value();
    config.supercell[1] = supercellSpin_[1]->value();
    config.flakeIndex = generationCombo_->currentData().toInt();
    config.hydrogenTerminateEdges = hydrogenCheck_->isChecked();

    // The one dosing mode this dialog offers: the basal plane and the rim are
    // dosed against their OWN carbon pools, so neither dial can move the other.
    // Config::coverage and Config::weight are left at their defaults and are
    // never read in this mode — the four sliders below are the whole of what
    // the build sees from stage 2.
    config.dosing = Dosing::DecoupledRegions;

    config.basalOxygenToCarbon = basalOxidation_.value();
    config.basalHydroxylShare = basalHydrogen_.value();

    // Edge chemistry on an edgeless substrate is not "requested and unmet":
    // a periodic sheet has no rim, so the dial is not merely hidden, it is
    // reported as zero. The builder warns about edge chemistry asked for where
    // none can go, and a hidden control must not be able to trigger that.
    config.edgeOxidation = flakeSelected() ? edgeOxidation_.value() : 0.0;
    config.carboxylShare = edgeCarboxyl_.value();

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

    sheetBox_->setVisible(!flake);
    flakeBox_->setVisible(flake);
    // Edge chemistry exists only where there is an edge. A periodic sheet is
    // edgeless by construction, so this is not "unavailable pending some
    // setting" — the question does not arise, and the controls go away rather
    // than sitting greyed out inviting the user to look for what enables them.
    // config() zeroes the edge weights to match, so what the build reads and
    // what the page shows cannot disagree.
    edgeChemistryBox_->setVisible(flake);
    edgeNote_->setVisible(!flake);

    // The read-outs say what the number MEANS; the spin box beside each slider
    // already says what it IS, so repeating the value here would be noise.
    const double basalOc = basalOxidation_.value();
    basalOxidation_.readout->setText(
        basalOc <= 0.0 ? tr("pristine basal plane")
                       : tr("1 O per <b>%1</b> basal C").arg(1.0 / basalOc, 0, 'f', 1));
    const double basalH = basalHydrogen_.value();
    basalHydrogen_.readout->setText(tr("%1 % epoxide, %2 % hydroxyl")
                                        .arg((1.0 - basalH) * 100.0, 0, 'f', 0)
                                        .arg(basalH * 100.0, 0, 'f', 0));
    const double edgeOx = edgeOxidation_.value();
    edgeOxidation_.readout->setText(
        edgeOx <= 0.0
            ? tr("<b>all hydrogen</b>")
            : tr("%1 % groups, %2 % H").arg(edgeOx * 100.0, 0, 'f', 0)
                  .arg((1.0 - edgeOx) * 100.0, 0, 'f', 0));
    const double carboxylShare = edgeCarboxyl_.value();
    edgeCarboxyl_.readout->setText(tr("%1 % carbonyl, %2 % carboxyl")
                                       .arg((1.0 - carboxylShare) * 100.0, 0,
                                            'f', 0)
                                       .arg(carboxylShare * 100.0, 0, 'f', 0));

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

    QStringList lines;

    // Basal plane. The count is exact (both basal groups carry one oxygen), so
    // it is stated as a count rather than as a ratio the user has to convert.
    if (basalOc <= 0.0) {
        lines << tr("<i>Basal plane: pristine — no oxygen.</i>");
    } else {
        const int basalOxygens =
            static_cast<int>(std::llround(basal * basalOc));
        lines << tr("Basal plane: about <b>%1 oxygens</b> over %2 basal "
                    "carbons — %3 % epoxide, %4 % hydroxyl.")
                     .arg(basalOxygens)
                     .arg(basal)
                     .arg((1.0 - basalH) * 100.0, 0, 'f', 0)
                     .arg(basalH * 100.0, 0, 'f', 0);
        // One oxygen per basal carbon is the ceiling; an epoxide takes two
        // carbons for one oxygen, so a hydroxyl-poor plane saturates sooner.
        if (basalOxygens > basal) {
            lines << tr("<b>The basal plane cannot hold that much oxygen.</b> "
                        "At most %1 fit, one per basal carbon.")
                         .arg(basal);
        }
    }

    // Rim.
    if (!flake) {
        lines << tr("<i>Edges: none — a periodic sheet has no rim.</i>");
    } else if (edgeOx <= 0.0) {
        lines << tr("Edges: <b>strictly hydrogen-terminated</b> — all %1 rim "
                    "carbons keep their hydrogen.")
                     .arg(edge);
    } else {
        const int edgeGroups = std::max(
            1, static_cast<int>(std::llround(edge * edgeOx)));
        lines << tr("Edges: <b>%1 of %2</b> rim carbons functionalized "
                    "(%3 % carbonyl, %4 % carboxyl); the remaining %5 keep "
                    "their hydrogen.")
                     .arg(edgeGroups)
                     .arg(edge)
                     .arg((1.0 - carboxylShare) * 100.0, 0, 'f', 0)
                     .arg(carboxylShare * 100.0, 0, 'f', 0)
                     .arg(edge - edgeGroups);
    }

    coverageSummary_->setText(lines.join(QStringLiteral("<br>")));

    if (mdmcCostNote_) {
        // The group count is what the sampler moves, so it is what decides
        // whether the refinement has anything to do and how long it takes.
        const int basalGroups = basalOc <= 0.0
            ? 0
            : static_cast<int>(std::llround(basal * basalOc));
        const int edgeGroups = (!flake || edgeOx <= 0.0)
            ? 0
            : std::max(1, static_cast<int>(std::llround(edge * edgeOx)));
        const int total = basalGroups + edgeGroups;
        if (total == 0) {
            mdmcCostNote_->setText(
                tr("<i>This structure will carry no functional groups, so "
                   "there is nothing for MDMC to rearrange. Raise the "
                   "oxidation level on the previous stage.</i>"));
        } else {
            mdmcCostNote_->setText(
                tr("<i>About <b>%n group(s)</b> will be placed. A run needs "
                   "several accepted moves per group before the arrangement "
                   "has forgotten where it started, so plan on a few hundred "
                   "cycles — each one an energy evaluation, or several with "
                   "molecular dynamics enabled.</i>",
                   nullptr, total));
        }
    }
}

void GrapheneOxideWizard::goNext()
{
    if (stack_->currentIndex() < kLastStage) {
        showStage(stack_->currentIndex() + 1);
        return;
    }

    Builder::Report report;
    result_ = Builder::build(config(), &report);
    report_ = report;
    accept();
}

void GrapheneOxideWizard::goBack()
{
    showStage(std::max(0, stack_->currentIndex() - 1));
}

void GrapheneOxideWizard::showStage(int index)
{
    stack_->setCurrentIndex(index);
    static const char* const kHeaders[] = {
        QT_TR_NOOP("Stage 1 of 3 — Base Structure"),
        QT_TR_NOOP("Stage 2 of 3 — Functionalization & Oxidation Level"),
        QT_TR_NOOP("Stage 3 of 3 — MDMC Optimization (optional)"),
    };
    stageLabel_->setText(tr(kHeaders[index]));
    backButton_->setEnabled(index > 0);
    nextButton_->setText(index == kLastStage ? tr("Build") : tr("Next >"));
    refreshSummary();
}

bool GrapheneOxideWizard::mdmcRequested() const
{
    return mdmcCheck_ && mdmcCheck_->isChecked();
}

} // namespace calango::gui
