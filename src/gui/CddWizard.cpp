#include "gui/CddWizard.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {

/// Role holding an item's 0-based atom index. The list order changes as atoms
/// are moved between the columns, so the row number is not the atom number and
/// must never be used as one.
constexpr int kAtomIndexRole = Qt::UserRole + 1;

/// Sort a column back into atom order after a move, so both sides always read
/// as an ordered list rather than in the order the user happened to click.
void sortByAtomIndex(QListWidget* list)
{
    std::vector<QListWidgetItem*> items;
    items.reserve(static_cast<std::size_t>(list->count()));
    while (list->count() > 0)
        items.push_back(list->takeItem(0));
    std::sort(items.begin(), items.end(),
              [](const QListWidgetItem* a, const QListWidgetItem* b) {
                  return a->data(kAtomIndexRole).toInt()
                      < b->data(kAtomIndexRole).toInt();
              });
    for (QListWidgetItem* item : items)
        list->addItem(item);
}

} // namespace

CddWizard::CddWizard(QWidget* parent) : SimulationWizardBase(parent)
{
    buildUi();
    // Fixes the (hidden) engine for the env resolution fallback; the fragments
    // themselves inherit everything from the baseline's .gpw.
    selectCalculator(core::CalculatorKind::Gpaw);
    onBaselineChanged();
}

void CddWizard::setDensityBaselines(QList<Baseline> baselines)
{
    baselines_ = std::move(baselines);
    if (!baselineCombo_)
        return;
    for (int i = 0; i < baselines_.size(); ++i)
        baselineCombo_->addItem(baselines_.at(i).label, i);
    if (!baselines_.isEmpty())
        baselineCombo_->setCurrentIndex(0);
    onBaselineChanged();
}

QString CddWizard::wizardTitle() const
{
    return tr("Charge Density Difference (CDD) Setup");
}

QString CddWizard::baselineDirectory() const
{
    if (!baselineCombo_)
        return {};
    const int index = baselineCombo_->currentData().toInt(nullptr);
    if (index < 0 || index >= baselines_.size())
        return {};
    return baselines_.at(index).directory;
}

QWidget* CddWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("<b>Δρ = ρ(A+B) − ρ(A) − ρ(B)</b> — where the charge went when two "
           "fragments were brought together. The difference shows the bond."),
        page);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setToolTip(
        tr("Each density on its own is dominated by the atomic cores and shows "
           "nothing.\n\n"
           "The two fragments are recomputed from the selected run's own saved "
           "wavefunctions, in the same cell and with the same parameters, so "
           "the three terms cannot drift apart.\n\n"
           "Nothing is relaxed: Δρ is defined at one geometry, and letting a "
           "fragment move would mix charge transfer with structural "
           "rearrangement."));
    layout->addWidget(intro);

    auto* sourceGroup = new QGroupBox(tr("Single-point calculation"), page);
    auto* sourceForm = new QFormLayout(sourceGroup);

    baselineCombo_ = new QComboBox(sourceGroup);
    baselineCombo_->setToolTip(
        tr("Completed Single-point calculations that saved their "
           "wavefunctions (GPAW .gpw). ρ(A+B) is read from that run and both "
           "fragments are rebuilt from its calculator."));
    sourceForm->addRow(tr("Process:"), baselineCombo_);

    inheritedLabel_ = new QLabel(sourceGroup);
    inheritedLabel_->setWordWrap(true);
    inheritedLabel_->setTextFormat(Qt::RichText);
    sourceForm->addRow(tr("Inherited calculator:"), inheritedLabel_);
    layout->addWidget(sourceGroup);

    auto* densityGroup = new QGroupBox(tr("Density type"), page);
    auto* densityLayout = new QVBoxLayout(densityGroup);
    pseudoRadio_ = new QRadioButton(tr("Pseudodensity"), densityGroup);
    pseudoRadio_->setToolTip(
        tr("The smooth valence density the SCF iterates on. Smaller grid, no "
           "nuclear cusps to swamp the isosurface — for reading off which way "
           "the valence charge moved, this is usually the clearer field."));
    allElectronRadio_ =
        new QRadioButton(tr("All-electron density"), densityGroup);
    allElectronRadio_->setToolTip(
        tr("The full PAW-reconstructed density on a doubled grid. Physically "
           "complete, but the core cusps are orders of magnitude larger than "
           "the bonding features; they cancel exactly here only because the "
           "geometry is identical in all three terms."));
    allElectronRadio_->setChecked(true);
    densityLayout->addWidget(allElectronRadio_);
    densityLayout->addWidget(pseudoRadio_);
    layout->addWidget(densityGroup);

    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            &CddWizard::onBaselineChanged);
    for (QRadioButton* radio : {allElectronRadio_, pseudoRadio_})
        connect(radio, &QRadioButton::toggled, this,
                &CddWizard::refreshPreview);

    layout->addStretch(1);
    return page;
}

QWidget* CddWizard::buildSecondSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Split the structure into the two fragments whose interaction you "
           "want to see. Every atom must end up on exactly one side."),
        page);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setToolTip(
        tr("Select atoms and move them across; double-clicking a row moves it "
           "too.\n\n"
           "The partition is exhaustive because Δρ only sums to zero if A and "
           "B together are the whole system. A fragment left out shows up as a "
           "spurious hole in the difference."));
    layout->addWidget(intro);

    auto* columns = new QHBoxLayout;

    const auto makeColumn = [&](const QString& title, QListWidget*& list) {
        auto* column = new QVBoxLayout;
        column->addWidget(new QLabel(title, page));
        list = new QListWidget(page);
        list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        column->addWidget(list, 1);
        columns->addLayout(column, 1);
    };

    makeColumn(tr("Subsystem A:"), listA_);

    auto* buttonColumn = new QVBoxLayout;
    buttonColumn->addStretch(1);
    toBButton_ = new QPushButton(tr("→"), page);
    toBButton_->setToolTip(tr("Move the selected atoms into subsystem B."));
    toAButton_ = new QPushButton(tr("←"), page);
    toAButton_->setToolTip(tr("Move the selected atoms into subsystem A."));
    for (QPushButton* button : {toBButton_, toAButton_}) {
        button->setFixedWidth(44);
        buttonColumn->addWidget(button);
    }
    buttonColumn->addStretch(1);
    columns->addLayout(buttonColumn);

    makeColumn(tr("Subsystem B:"), listB_);
    layout->addLayout(columns, 1);

    partitionStatus_ = new QLabel(page);
    partitionStatus_->setWordWrap(true);
    partitionStatus_->setTextFormat(Qt::RichText);
    layout->addWidget(partitionStatus_);

    connect(toBButton_, &QPushButton::clicked, this,
            [this] { moveSelection(listA_, listB_); });
    connect(toAButton_, &QPushButton::clicked, this,
            [this] { moveSelection(listB_, listA_); });
    connect(listA_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                item->setSelected(true);
                moveSelection(listA_, listB_);
            });
    connect(listB_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                item->setSelected(true);
                moveSelection(listB_, listA_);
            });
    for (QListWidget* list : {listA_, listB_})
        connect(list, &QListWidget::itemSelectionChanged, this,
                [this] { updatePartitionState(); });

    resetPartition();
    return page;
}

QString CddWizard::atomLabel(int index) const
{
    if (!structure_ || index < 0
        || static_cast<std::size_t>(index) >= structure_->atoms().size())
        return QString::number(index);
    const core::Atom& atom = structure_->atoms()[static_cast<std::size_t>(index)];
    // The index leads, because it is what the generated script names and what
    // the viewport's "Show atomic indices" overlay shows — the two have to be
    // matchable by eye.
    return tr("%1  %2   (%3, %4, %5) Å")
        .arg(index + 1, 3)
        .arg(QString::fromLatin1(atom.symbol()), -2)
        .arg(atom.position.x, 0, 'f', 3)
        .arg(atom.position.y, 0, 'f', 3)
        .arg(atom.position.z, 0, 'f', 3);
}

void CddWizard::resetPartition()
{
    if (!listA_ || !listB_)
        return;
    listA_->clear();
    listB_->clear();
    if (structure_) {
        for (int i = 0; i < static_cast<int>(structure_->atoms().size()); ++i) {
            auto* item = new QListWidgetItem(atomLabel(i), listA_);
            item->setData(kAtomIndexRole, i);
        }
    }
    updatePartitionState();
}

void CddWizard::moveSelection(QListWidget* from, QListWidget* to)
{
    if (!from || !to)
        return;
    const QList<QListWidgetItem*> selected = from->selectedItems();
    for (QListWidgetItem* item : selected)
        to->addItem(from->takeItem(from->row(item)));
    sortByAtomIndex(to);
    updatePartitionState();
    refreshPreview();
}

void CddWizard::updatePartitionState()
{
    if (!listA_ || !listB_ || !partitionStatus_)
        return;
    toBButton_->setEnabled(!listA_->selectedItems().isEmpty());
    toAButton_->setEnabled(!listB_->selectedItems().isEmpty());

    const int a = listA_->count();
    const int b = listB_->count();
    if (!structure_) {
        partitionStatus_->setText(
            tr("<b>No structure loaded.</b> Choose a single-point calculation "
               "on the previous stage."));
        return;
    }
    if (a == 0 || b == 0) {
        partitionStatus_->setText(
            tr("<b>Both subsystems need at least one atom.</b> With one side "
               "empty the difference is identically zero — move some atoms "
               "across."));
        return;
    }
    partitionStatus_->setText(
        tr("<b>A: %1 atom(s) · B: %2 atom(s).</b> Δρ will show what changed "
           "when these two were put together.")
            .arg(a)
            .arg(b));
}

void CddWizard::onBaselineChanged()
{
    const QString dir = baselineDirectory();
    inherited_.reset();
    structure_.reset();

    if (dir.isEmpty()) {
        if (inheritedLabel_)
            inheritedLabel_->setText(
                tr("<i>No completed single-point with saved wavefunctions was "
                   "found. Run one with GPAW first — the difference is built "
                   "from its .gpw.</i>"));
        resetPartition();
        refreshPreview();
        return;
    }

    inherited_ = readCalculatorProvenance(dir);
    if (inheritedLabel_) {
        inheritedLabel_->setText(
            inherited_ ? inherited_->summary()
                       : tr("<i>This run predates calculator provenance; its "
                            "parameters are still read from the .gpw at run "
                            "time.</i>"));
    }

    const int index = baselineCombo_->currentData().toInt(nullptr);
    if (index >= 0 && index < baselines_.size())
        structure_ = baselines_.at(index).structure;
    resetPartition();
    refreshPreview();
}

void CddWizard::goNext()
{
    // The atom columns are only meaningful once a baseline is chosen, and the
    // choice is made on the stage we are leaving.
    if (listA_ && listA_->count() == 0 && listB_->count() == 0)
        resetPartition();
    SimulationWizardBase::goNext();
}

core::CddRunConfig CddWizard::runConfig() const
{
    core::CddRunConfig config;
    config.baselineDir = baselineDirectory().toStdString();
    config.allElectron = !pseudoRadio_ || allElectronRadio_->isChecked();

    // The engine and its settings come from the PARENT run's provenance, not
    // from a control on this page. Δρ is a difference of three densities, and
    // the only way it means anything is if all three were computed the same
    // way — so the fragments inherit whatever produced ρ(A+B) rather than
    // offering the user a second chance to pick something else.
    //
    // GPAW additionally restarts from the .gpw and reads the calculator back
    // out of it, which is stronger still; VASP and Quantum ESPRESSO have no
    // such restart, so these inherited values ARE the specification.
    if (inherited_) {
        if (inherited_->engineKind >= 0)
            config.calculator.calculator =
                static_cast<core::CalculatorKind>(inherited_->engineKind);
        config.calculator.planeWaveCutoffEv = inherited_->cutoffEv > 0.0
            ? inherited_->cutoffEv
            : config.calculator.planeWaveCutoffEv;
        if (!inherited_->xc.isEmpty())
            config.calculator.vaspXc = inherited_->xc.toStdString();
        for (int i = 0; i < 3; ++i)
            if (inherited_->kpts[i] > 0)
                config.calculator.kpts[i] = inherited_->kpts[i];
        // QE reports its cutoff in eV through the shared provenance field;
        // pw.x wants Rydberg.
        if (inherited_->cutoffEv > 0.0)
            config.calculator.qeEcutwfcRy = inherited_->cutoffEv / 13.605693;
    }
    config.calculator.vaspPotcarPath = vaspPotcarDirectory().toStdString();
    config.calculator.espressoPseudoDir =
        espressoPseudoDirectory().toStdString();
    if (listB_) {
        for (int row = 0; row < listB_->count(); ++row)
            config.subsystemB.push_back(
                listB_->item(row)->data(kAtomIndexRole).toInt());
    }
    return config;
}

QString CddWizard::generateScript() const
{
    return QString::fromStdString(
        core::CddScriptGenerator::generate(runConfig()));
}

QString CddWizard::pythonExecutable() const
{
    if (inherited_ && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

} // namespace calango::gui
