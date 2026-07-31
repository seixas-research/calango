#include "gui/SolidInterfaceWizard.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

using Builder = core::SolidInterfaceBuilder;

bool isPlanar(Builder::Kind kind)
{
    return kind == Builder::Kind::StackingFault
        || kind == Builder::Kind::TwinBoundary;
}

bool isPolycrystal(Builder::Kind kind)
{
    return kind == Builder::Kind::Polycrystal
        || kind == Builder::Kind::MultiPhasePolycrystal;
}

QString kindExplanation(Builder::Kind kind)
{
    switch (kind) {
    case Builder::Kind::StackingFault:
        return QObject::tr(
            "Everything above the chosen plane is shifted rigidly by a "
            "fraction of an in-plane lattice vector. The cheapest planar "
            "defect there is, and the one whose energy decides how readily a "
            "dislocation splits into partials.\n\n"
            "⚠ A periodic cell cannot hold ONE fault. Shifting the top half "
            "creates a second fault where the cell meets its own image. Halve "
            "the excess energy before quoting a stacking-fault energy.");
    case Builder::Kind::TwinBoundary:
        return QObject::tr(
            "The half above the plane is discarded and replaced by the mirror "
            "image of the half below it. A coherent twin: every atom on the "
            "boundary is shared between the two orientations, so the interface "
            "carries no free volume.\n\n"
            "Anything that existed only in the replaced half is gone — that is "
            "what twinning does. The same doubling caveat applies: a mirror in "
            "a periodic cell is two twin boundaries.");
    case Builder::Kind::Bicrystal:
        return QObject::tr(
            "Two grains with independent orientations, filling the two halves "
            "of a box built from the parent cell. The general grain boundary.\n\n"
            "Only the coincidence-site (CSL) misorientations fit a periodic "
            "box exactly. At any other angle the crystals meet the box "
            "boundary out of register; the mismatch is measured and reported "
            "rather than hidden.");
    case Builder::Kind::Polycrystal:
        return QObject::tr(
            "A Voronoi tessellation: N seeds are scattered in the box, each "
            "gets a uniformly random orientation, and every point of space "
            "joins the grain whose seed is nearest — under the PERIODIC "
            "minimum image, so the grains wrap through the box faces instead "
            "of stopping at them.\n\n"
            "Each atom carries its grain id as a scalar field, so the "
            "tessellation can be seen in the viewport rather than assumed.");
    case Builder::Kind::MultiPhasePolycrystal:
        break;
    }
    return QObject::tr(
        "The same Voronoi tessellation, with each grain drawing its lattice "
        "from a different phase according to the weights below. Every grain is "
        "single-phase; the interfaces between them are phase boundaries.\n\n"
        "The phases are the open workspace tabs. Give a phase a weight of zero "
        "to exclude it.");
}

} // namespace

// ---------------------------------------------------------------------------
// Stage 1
// ---------------------------------------------------------------------------

SolidInterfaceKindPage::SolidInterfaceKindPage(SolidInterfaceWizard* wizard)
    : wizard_(wizard)
{
    setTitle(tr("Interface and Plane"));
    setSubTitle(tr("Which interface to build, and the lattice plane it sits "
                   "on."));

    auto* layout = new QVBoxLayout(this);
    form_ = new QFormLayout;
    layout->addLayout(form_);

    kindCombo_ = new QComboBox(this);
    kindCombo_->setObjectName(QStringLiteral("interfaceKindCombo"));
    kindCombo_->addItem(tr("Stacking fault"),
                        static_cast<int>(Builder::Kind::StackingFault));
    kindCombo_->addItem(tr("Twin boundary"),
                        static_cast<int>(Builder::Kind::TwinBoundary));
    kindCombo_->addItem(tr("Bicrystal"),
                        static_cast<int>(Builder::Kind::Bicrystal));
    kindCombo_->addItem(tr("Polycrystal"),
                        static_cast<int>(Builder::Kind::Polycrystal));
    kindCombo_->addItem(tr("Multi-phase polycrystal"),
                        static_cast<int>(Builder::Kind::MultiPhasePolycrystal));
    form_->addRow(tr("Interface:"), kindCombo_);

    axisCombo_ = new QComboBox(this);
    axisCombo_->addItem(tr("a"), static_cast<int>(Builder::Axis::A));
    axisCombo_->addItem(tr("b"), static_cast<int>(Builder::Axis::B));
    axisCombo_->addItem(tr("c"), static_cast<int>(Builder::Axis::C));
    axisCombo_->setCurrentIndex(2);
    axisCombo_->setToolTip(
        tr("The lattice vector the boundary normal follows. Naming a LATTICE "
           "direction rather than x/y/z is what keeps the boundary plane — "
           "spanned by the other two lattice vectors — commensurate with the "
           "cell."));
    form_->addRow(tr("Boundary normal:"), axisCombo_);

    positionSpin_ = new QDoubleSpinBox(this);
    positionSpin_->setRange(0.01, 0.99);
    positionSpin_->setDecimals(3);
    positionSpin_->setSingleStep(0.05);
    positionSpin_->setValue(0.5);
    positionSpin_->setToolTip(
        tr("Where the boundary sits along that direction, as a cell fraction."));
    form_->addRow(tr("Boundary position:"), positionSpin_);

    auto* faultRow = new QHBoxLayout;
    const double defaults[2] = {1.0 / 3.0, 0.0};
    for (int i = 0; i < 2; ++i) {
        faultSpins_[i] = new QDoubleSpinBox(this);
        faultSpins_[i]->setRange(-2.0, 2.0);
        faultSpins_[i]->setDecimals(4);
        faultSpins_[i]->setSingleStep(1.0 / 6.0);
        faultSpins_[i]->setValue(defaults[i]);
        faultRow->addWidget(faultSpins_[i], 1);
    }
    faultSpins_[0]->setToolTip(
        tr("Displacement of everything above the plane, as fractions of the "
           "two in-plane lattice vectors. ⅓ along one of them is the classic "
           "partial-dislocation fault vector of a close-packed plane."));
    auto* faultWidget = new QWidget(this);
    faultWidget->setLayout(faultRow);
    form_->addRow(tr("Fault vector (in-plane):"), faultWidget);

    gapSpin_ = new QDoubleSpinBox(this);
    gapSpin_->setRange(0.0, 50.0);
    gapSpin_->setDecimals(3);
    gapSpin_->setSingleStep(0.1);
    gapSpin_->setSuffix(tr(" Å"));
    gapSpin_->setToolTip(
        tr("Extra separation opened at the boundary, perpendicular to it. Zero "
           "for a coherent interface; a small positive value gives a "
           "relaxation somewhere to start from instead of two planes on top of "
           "each other."));
    form_->addRow(tr("Boundary gap:"), gapSpin_);

    mergeSpin_ = new QDoubleSpinBox(this);
    mergeSpin_->setRange(0.0, 5.0);
    mergeSpin_->setDecimals(3);
    mergeSpin_->setSingleStep(0.1);
    mergeSpin_->setValue(0.5);
    mergeSpin_->setSuffix(tr(" Å"));
    mergeSpin_->setSpecialValueText(tr("keep every atom"));
    mergeSpin_->setToolTip(
        tr("Atoms landing closer than this to one already placed are deleted. "
           "Two crystals meeting at an arbitrary angle always overlap "
           "somewhere; without this the boundary is a pile-up rather than a "
           "boundary. Too large and it starts eating the grains themselves — "
           "watch the reported atom count."));
    form_->addRow(tr("Merge tolerance:"), mergeSpin_);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_, 1);

    connect(kindCombo_, &QComboBox::currentIndexChanged, this,
            &SolidInterfaceKindPage::refresh);
    connect(axisCombo_, &QComboBox::currentIndexChanged, this,
            &SolidInterfaceKindPage::refresh);
    for (QDoubleSpinBox* spin :
         {positionSpin_, faultSpins_[0], faultSpins_[1], gapSpin_, mergeSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this](double) { refresh(); });
    refresh();
}

void SolidInterfaceKindPage::initializePage() { refresh(); }

bool SolidInterfaceKindPage::isComplete() const { return valid_; }

void SolidInterfaceKindPage::applyKindVisibility()
{
    const Builder::Kind kind = wizard_->params.kind;
    form_->setRowVisible(axisCombo_, !isPolycrystal(kind));
    form_->setRowVisible(positionSpin_, !isPolycrystal(kind));
    form_->setRowVisible(faultSpins_[0]->parentWidget(),
                         kind == Builder::Kind::StackingFault);
    form_->setRowVisible(gapSpin_, isPlanar(kind));
    form_->setRowVisible(mergeSpin_, !isPlanar(kind));
}

void SolidInterfaceKindPage::refresh()
{
    auto& params = wizard_->params;
    params.kind = static_cast<Builder::Kind>(kindCombo_->currentData().toInt());
    params.axis = static_cast<Builder::Axis>(axisCombo_->currentData().toInt());
    params.boundaryPosition = positionSpin_->value();
    params.faultVector = {faultSpins_[0]->value(), faultSpins_[1]->value()};
    params.gap = gapSpin_->value();
    params.mergeTolerance = mergeSpin_->value();
    applyKindVisibility();

    const core::Structure* parent =
        wizard_->phases.empty() ? nullptr : wizard_->phases.front().second.get();
    valid_ = parent != nullptr && !parent->empty() && parent->cell().isDefined();
    if (params.kind == Builder::Kind::MultiPhasePolycrystal
        && wizard_->phases.size() < 2)
        valid_ = false;

    QString text = kindExplanation(params.kind);
    if (!parent || parent->empty())
        text = tr("<b>No structure.</b> Open or build a crystal first.");
    else if (!parent->cell().isDefined())
        text = tr("<b>This structure has no periodic cell.</b> Every "
                  "construction here fills space by repeating one.");
    else if (params.kind == Builder::Kind::MultiPhasePolycrystal
             && wizard_->phases.size() < 2)
        text += tr("<br><br><b>Only one structure is open.</b> A multi-phase "
                   "polycrystal needs a second lattice — open another tab and "
                   "come back.");
    else
        text += tr("<br><br>Parent: %1 atoms, %2.")
                    .arg(static_cast<int>(parent->size()))
                    .arg(QString::fromStdString(parent->chemicalFormula()));
    summaryLabel_->setText(text);
    Q_EMIT completeChanged();
}

// ---------------------------------------------------------------------------
// Stage 2
// ---------------------------------------------------------------------------

SolidInterfaceGrainPage::SolidInterfaceGrainPage(SolidInterfaceWizard* wizard)
    : wizard_(wizard)
{
    setTitle(tr("Grains"));
    setSubTitle(tr("How big the box is, and what fills it."));

    auto* layout = new QVBoxLayout(this);
    form_ = new QFormLayout;
    layout->addLayout(form_);

    auto* repeatRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        repeatSpins_[i] = new QSpinBox(this);
        repeatSpins_[i]->setRange(1, 200);
        repeatSpins_[i]->setValue(4);
        repeatRow->addWidget(repeatSpins_[i], 1);
    }
    repeatSpins_[0]->setToolTip(
        tr("Size of the constructed box, in multiples of the parent cell. A "
           "polycrystal needs a box several grain diameters across before its "
           "grain-boundary fraction stops being dominated by the box."));
    auto* repeatWidget = new QWidget(this);
    repeatWidget->setLayout(repeatRow);
    form_->addRow(tr("Box repeats (a, b, c):"), repeatWidget);

    rotationASpin_ = new QDoubleSpinBox(this);
    rotationASpin_->setRange(-180.0, 180.0);
    rotationASpin_->setDecimals(3);
    rotationASpin_->setSuffix(tr("°"));
    form_->addRow(tr("Grain A rotation:"), rotationASpin_);

    rotationBSpin_ = new QDoubleSpinBox(this);
    rotationBSpin_->setRange(-180.0, 180.0);
    rotationBSpin_->setDecimals(3);
    rotationBSpin_->setValue(36.87);
    rotationBSpin_->setToolTip(
        tr("Both rotations are about the boundary normal, so the pair forms a "
           "TWIST boundary; the misorientation is the difference. 36.87° is "
           "the Σ5 coincidence-site relationship of a cubic lattice."));
    rotationBSpin_->setSuffix(tr("°"));
    form_->addRow(tr("Grain B rotation:"), rotationBSpin_);

    grainSpin_ = new QSpinBox(this);
    grainSpin_->setRange(1, 4096);
    grainSpin_->setValue(8);
    form_->addRow(tr("Grains:"), grainSpin_);

    seedSpin_ = new QSpinBox(this);
    seedSpin_->setRange(0, 1000000);
    seedSpin_->setValue(42);
    seedSpin_->setToolTip(
        tr("Seeds both the grain positions and their orientations. The same "
           "seed rebuilds the same cell exactly, which is what makes a "
           "published polycrystal reproducible."));
    form_->addRow(tr("Random seed:"), seedSpin_);

    phaseTable_ = new QTableWidget(0, 2, this);
    phaseTable_->setHorizontalHeaderLabels({tr("Phase"), tr("Weight")});
    phaseTable_->horizontalHeader()->setStretchLastSection(true);
    phaseTable_->verticalHeader()->setVisible(false);
    phaseTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    phaseTable_->setMaximumHeight(160);
    form_->addRow(tr("Phases:"), phaseTable_);

    estimateLabel_ = new QLabel(this);
    estimateLabel_->setWordWrap(true);
    estimateLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(estimateLabel_, 1);

    for (QSpinBox* spin : {repeatSpins_[0], repeatSpins_[1], repeatSpins_[2],
                           grainSpin_, seedSpin_})
        connect(spin, &QSpinBox::valueChanged, this, [this](int) { refresh(); });
    for (QDoubleSpinBox* spin : {rotationASpin_, rotationBSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this](double) { refresh(); });
}

void SolidInterfaceGrainPage::initializePage()
{
    // The phase rows are seeded here rather than in the constructor: the phase
    // list belongs to the wizard, and building table rows from it before the
    // wizard has finished constructing is the classic way a wizard reads a
    // widget it has not created yet.
    if (phaseTable_->rowCount() != static_cast<int>(wizard_->phases.size())) {
        phaseTable_->setRowCount(static_cast<int>(wizard_->phases.size()));
        wizard_->weights.assign(wizard_->phases.size(), 1.0);
        for (int row = 0; row < phaseTable_->rowCount(); ++row) {
            phaseTable_->setItem(
                row, 0,
                new QTableWidgetItem(
                    wizard_->phases[static_cast<std::size_t>(row)].first));
            auto* weight = new QDoubleSpinBox(phaseTable_);
            weight->setRange(0.0, 100.0);
            weight->setDecimals(2);
            weight->setSingleStep(0.5);
            weight->setValue(1.0);
            connect(weight, &QDoubleSpinBox::valueChanged, this,
                    [this](double) { refresh(); });
            phaseTable_->setCellWidget(row, 1, weight);
        }
    }
    applyKindVisibility();
    refresh();
}

bool SolidInterfaceGrainPage::isComplete() const { return true; }

void SolidInterfaceGrainPage::applyKindVisibility()
{
    const Builder::Kind kind = wizard_->params.kind;
    const bool spaceFilling = !isPlanar(kind);
    form_->setRowVisible(repeatSpins_[0]->parentWidget(), spaceFilling);
    form_->setRowVisible(rotationASpin_, kind == Builder::Kind::Bicrystal);
    form_->setRowVisible(rotationBSpin_, kind == Builder::Kind::Bicrystal);
    form_->setRowVisible(grainSpin_, isPolycrystal(kind));
    form_->setRowVisible(seedSpin_, isPolycrystal(kind));
    form_->setRowVisible(phaseTable_,
                         kind == Builder::Kind::MultiPhasePolycrystal);
}

void SolidInterfaceGrainPage::refresh()
{
    auto& params = wizard_->params;
    params.repeat = {repeatSpins_[0]->value(), repeatSpins_[1]->value(),
                     repeatSpins_[2]->value()};
    params.rotationA = rotationASpin_->value();
    params.rotationB = rotationBSpin_->value();
    params.grainCount = grainSpin_->value();
    params.seed = static_cast<unsigned>(seedSpin_->value());
    for (int row = 0; row < phaseTable_->rowCount(); ++row)
        if (auto* weight =
                qobject_cast<QDoubleSpinBox*>(phaseTable_->cellWidget(row, 1)))
            wizard_->weights[static_cast<std::size_t>(row)] = weight->value();
    applyKindVisibility();

    QString error;
    if (!wizard_->build(&error)) {
        estimateLabel_->setText(
            tr("<span style='color:#c0392b'>%1</span>").arg(error));
        return;
    }
    const auto& result = *wizard_->result();
    QStringList lines;
    lines << tr("<b>%1</b>").arg(QString::fromStdString(result.description));
    lines << tr("%1 atoms across %2 grain(s); %3 interface(s) in the periodic "
                "cell.")
                 .arg(static_cast<int>(result.structure.size()))
                 .arg(static_cast<int>(result.grains.size()))
                 .arg(result.interfaceCount);
    if (result.mergedAtoms > 0)
        lines << tr("%1 overlapping atoms merged away at the seams.")
                     .arg(result.mergedAtoms);
    if (result.density > 0.0)
        lines << tr("Density %1 g/cm³; closest pair %2 Å.")
                     .arg(result.density, 0, 'f', 3)
                     .arg(result.minSeparation, 0, 'f', 3);
    for (const std::string& warning : result.warnings)
        lines << tr("<span style='color:#b9770e'>⚠ %1</span>")
                     .arg(QString::fromStdString(warning));
    lines << tr("<i>Nothing here is relaxed. Grain boundaries have structure — "
                "free volume, reconstruction, segregation — that no geometric "
                "construction produces.</i>");
    estimateLabel_->setText(lines.join(QStringLiteral("<br>")));
}

bool SolidInterfaceGrainPage::validatePage()
{
    QString error;
    if (wizard_->build(&error))
        return true;
    estimateLabel_->setText(
        tr("<span style='color:#c0392b'>%1</span>").arg(error));
    return false;
}

// ---------------------------------------------------------------------------
// Wizard
// ---------------------------------------------------------------------------

SolidInterfaceWizard::SolidInterfaceWizard(std::vector<PhaseSource> sources,
                                           QWidget* parent)
    : QWizard(parent)
    , phases(std::move(sources))
{
    setWindowTitle(tr("Solid Interface Builder"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    weights.assign(phases.size(), 1.0);
    addPage(new SolidInterfaceKindPage(this));
    addPage(new SolidInterfaceGrainPage(this));
    resize(660, 640);
}

bool SolidInterfaceWizard::build(QString* error)
{
    result_.reset();
    std::vector<core::Structure> lattices;
    std::vector<double> phaseWeights;
    for (std::size_t i = 0; i < phases.size(); ++i) {
        if (!phases[i].second)
            continue;
        // A weight of zero excludes a phase from the LIST, not merely from the
        // draw: keeping it would leave a grain able to pick a lattice the user
        // switched off if the weights were later renormalized differently.
        const double weight =
            i < weights.size() ? weights[i] : 1.0;
        if (params.kind == core::SolidInterfaceBuilder::Kind::MultiPhasePolycrystal
            && weight <= 0.0 && i > 0)
            continue;
        lattices.push_back(*phases[i].second);
        phaseWeights.push_back(weight);
        if (params.kind
            != core::SolidInterfaceBuilder::Kind::MultiPhasePolycrystal)
            break; // every other kind reads the parent only
    }
    if (lattices.empty()) {
        if (error)
            *error = tr("No structure to build from.");
        return false;
    }
    params.phaseWeights = phaseWeights;
    try {
        result_ = core::SolidInterfaceBuilder::generate(lattices, params);
    } catch (const std::exception& failure) {
        if (error)
            *error = QString::fromUtf8(failure.what());
        return false;
    }
    return true;
}

} // namespace calango::gui
