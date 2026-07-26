#include "gui/GrapheneOxideWizard.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace calango::gui {

namespace {

using Group = core::GrapheneOxideBuilder::Group;

struct GroupSpec {
    Group group;
    const char* label;
    const char* tooltip;
};

// Ordered as the builder applies them, so the UI reads in the order the sites
// are actually consumed.
const std::array<GroupSpec, 4> kGroups{{
    {Group::Epoxide, "Epoxide  (−O−)",
     "A bridging oxygen across a C–C bond. Consumes TWO carbons, both of "
     "which rehybridize to sp3. With hydroxyls, the dominant basal-plane "
     "group in the Lerf–Klinowski picture."},
    {Group::Hydroxyl, "Hydroxyl  (−OH)",
     "One −OH on a single carbon, above or below the plane. The other "
     "dominant basal-plane group."},
    {Group::Carboxyl, "Carboxyl  (−COOH)",
     "A −COOH group, which brings its own carbon. In real graphene oxide "
     "these sit at sheet EDGES and defects rather than on the basal plane; "
     "on a periodic sheet with no edges they are placed on the plane, which "
     "is a modelling compromise rather than the physical situation."},
    {Group::Carbonyl, "Carbonyl  (=O)",
     "A doubly-bonded oxygen on one carbon. Like carboxyls, an edge group in "
     "practice."},
}};

std::size_t slot(Group group)
{
    return static_cast<std::size_t>(group);
}

} // namespace

GrapheneOxideWizard::GrapheneOxideWizard(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Graphene Oxide Builder"));
    resize(620, 560);

    auto* layout = new QVBoxLayout(this);
    stageLabel_ = new QLabel(this);
    QFont stageFont = stageLabel_->font();
    stageFont.setBold(true);
    stageLabel_->setFont(stageFont);
    layout->addWidget(stageLabel_);

    stack_ = new QStackedWidget(this);
    layout->addWidget(stack_, 1);

    // ===== Stage 1 — Base Lattice & Supercell ==============================
    auto* stage1 = new QWidget(stack_);
    auto* stage1Layout = new QVBoxLayout(stage1);
    auto* latticeGroup = new QGroupBox(tr("Base Lattice"), stage1);
    auto* latticeForm = new QFormLayout(latticeGroup);

    latticeCombo_ = new QComboBox(latticeGroup);
    latticeCombo_->addItem(tr("Primitive (a = b = 2.46 Å, 60°, 2 atoms)"),
                           static_cast<int>(
                               core::GrapheneOxideBuilder::Lattice::Primitive));
    latticeCombo_->addItem(tr("Conventional rectangular (2.46 × 4.26 Å, 4 atoms)"),
                           static_cast<int>(
                               core::GrapheneOxideBuilder::Lattice::Rectangular));
    latticeCombo_->setCurrentIndex(1);
    latticeCombo_->setToolTip(
        tr("The primitive cell is the smallest repeat unit. The rectangular "
           "cell has orthogonal axes, which makes supercells, slabs and "
           "interfaces easier to reason about at twice the atom count."));
    latticeForm->addRow(tr("Lattice:"), latticeCombo_);

    auto* superRow = new QHBoxLayout;
    for (int i = 0; i < 2; ++i) {
        supercellSpin_[i] = new QSpinBox(latticeGroup);
        supercellSpin_[i]->setRange(1, 40);
        supercellSpin_[i]->setValue(4);
        superRow->addWidget(supercellSpin_[i]);
    }
    superRow->addStretch(1);
    latticeForm->addRow(tr("Supercell (nx · ny):"), superRow);

    latticeSummary_ = new QLabel(latticeGroup);
    latticeSummary_->setWordWrap(true);
    latticeForm->addRow(latticeSummary_);
    stage1Layout->addWidget(latticeGroup);

    auto* stage1Note = new QLabel(
        tr("The sheet is built in the xy plane with 20 Å of vacuum along z — "
           "enough that the functional groups, which stand ~1.5 Å off the "
           "plane, do not interact with their own periodic image."),
        stage1);
    stage1Note->setWordWrap(true);
    stage1Layout->addWidget(stage1Note);
    stage1Layout->addStretch(1);
    stack_->addWidget(stage1);

    // ===== Stage 2 — Functionalization & Coverages =========================
    auto* stage2 = new QWidget(stack_);
    auto* stage2Layout = new QVBoxLayout(stage2);

    auto* intro = new QLabel(
        tr("Coverage is the fraction of BASAL CARBONS each group consumes, "
           "not the fraction of groups. An epoxide takes two carbons and the "
           "others take one, so the values are additive: 10 % epoxide plus "
           "10 % hydroxyl functionalizes 20 % of the sheet."),
        stage2);
    intro->setWordWrap(true);
    stage2Layout->addWidget(intro);

    auto* groupsGroup = new QGroupBox(tr("Functional Groups"), stage2);
    auto* groupsForm = new QFormLayout(groupsGroup);
    for (const GroupSpec& spec : kGroups) {
        const std::size_t index = slot(spec.group);
        auto* row = new QHBoxLayout;
        groupCheck_[index] = new QCheckBox(tr(spec.label), groupsGroup);
        groupCheck_[index]->setToolTip(tr(spec.tooltip));
        groupCoverage_[index] = new QDoubleSpinBox(groupsGroup);
        groupCoverage_[index]->setRange(0.0, 100.0);
        groupCoverage_[index]->setDecimals(1);
        groupCoverage_[index]->setSingleStep(2.5);
        groupCoverage_[index]->setSuffix(tr(" %"));
        groupCoverage_[index]->setValue(10.0);
        groupCoverage_[index]->setEnabled(false);
        row->addWidget(groupCheck_[index], 1);
        row->addWidget(groupCoverage_[index]);
        groupsForm->addRow(row);
        connect(groupCheck_[index], &QCheckBox::toggled, groupCoverage_[index],
                &QWidget::setEnabled);
        connect(groupCheck_[index], &QCheckBox::toggled, this,
                &GrapheneOxideWizard::refreshSummary);
        connect(groupCoverage_[index], &QDoubleSpinBox::valueChanged, this,
                &GrapheneOxideWizard::refreshSummary);
    }
    stage2Layout->addWidget(groupsGroup);

    auto* optionsGroup = new QGroupBox(tr("Sampling"), stage2);
    auto* optionsForm = new QFormLayout(optionsGroup);
    bothFacesCheck_ = new QCheckBox(tr("Decorate both faces"), optionsGroup);
    bothFacesCheck_->setChecked(true);
    bothFacesCheck_->setToolTip(
        tr("Real graphene oxide is functionalized on both sides. Restricting "
           "to one face puts a net dipole across the sheet, which changes the "
           "electrostatics of anything computed from it."));
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
    refreshSummary();
}

core::GrapheneOxideBuilder::Config GrapheneOxideWizard::config() const
{
    core::GrapheneOxideBuilder::Config config;
    config.lattice = static_cast<core::GrapheneOxideBuilder::Lattice>(
        latticeCombo_->currentData().toInt());
    config.supercell[0] = supercellSpin_[0]->value();
    config.supercell[1] = supercellSpin_[1]->value();
    for (const GroupSpec& spec : kGroups) {
        const std::size_t index = slot(spec.group);
        config.setCoverage(spec.group,
                           groupCheck_[index]->isChecked()
                               ? groupCoverage_[index]->value() / 100.0
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
    const auto cfg = config();
    const int perCell =
        cfg.lattice == core::GrapheneOxideBuilder::Lattice::Primitive ? 2 : 4;
    const int carbons = perCell * cfg.supercell[0] * cfg.supercell[1];
    latticeSummary_->setText(
        tr("%1 carbons in the sheet (%2 per cell × %3 × %4).")
            .arg(carbons)
            .arg(perCell)
            .arg(cfg.supercell[0])
            .arg(cfg.supercell[1]));

    if (!coverageSummary_)
        return;
    double totalFraction = 0.0;
    QStringList parts;
    for (const GroupSpec& spec : kGroups) {
        const double fraction = cfg.coverageFor(spec.group);
        if (fraction <= 0.0)
            continue;
        totalFraction += fraction;
        const int cost = spec.group == Group::Epoxide ? 2 : 1;
        parts << tr("%1 × %2")
                     .arg(static_cast<int>(std::llround(fraction * carbons / cost)))
                     .arg(QString::fromLatin1(
                         core::GrapheneOxideBuilder::name(spec.group)));
    }
    if (parts.isEmpty()) {
        coverageSummary_->setText(
            tr("<i>No groups selected — this builds pristine graphene.</i>"));
        return;
    }
    QString text = tr("Approximately %1, functionalizing %2 % of the carbons.")
                       .arg(parts.join(QStringLiteral(", ")))
                       .arg(totalFraction * 100.0, 0, 'f', 1);
    if (totalFraction > 1.0) {
        // Stated as a fact about the lattice rather than blocked: the builder
        // truncates and reports, so the user can still proceed knowingly.
        text += tr("<br><b>More than 100 %% of the carbons are requested.</b> "
                   "Groups are placed in order — epoxide, carboxyl, carbonyl, "
                   "hydroxyl — until the sheet runs out, and the shortfall is "
                   "reported after building.");
    } else if (totalFraction > 0.6) {
        text += tr("<br>Above roughly 60 %% coverage the epoxides struggle to "
                   "find bonded pairs of free carbons, so the placed count may "
                   "fall short of the request.");
    }
    coverageSummary_->setText(text);
}

void GrapheneOxideWizard::goNext()
{
    if (stack_->currentIndex() == 0) {
        stack_->setCurrentIndex(1);
        stageLabel_->setText(tr("Stage 2 of 2 — Functionalization & Coverages"));
        backButton_->setEnabled(true);
        nextButton_->setText(tr("Build"));
        refreshSummary();
        return;
    }

    core::GrapheneOxideBuilder::Report report;
    result_ = core::GrapheneOxideBuilder::build(config(), &report);
    report_ = report;
    accept();
}

void GrapheneOxideWizard::goBack()
{
    stack_->setCurrentIndex(0);
    stageLabel_->setText(tr("Stage 1 of 2 — Base Lattice & Supercell"));
    backButton_->setEnabled(false);
    nextButton_->setText(tr("Next >"));
}

} // namespace calango::gui
