#include "gui/DsimWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/Element.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <set>

namespace calango::gui {

namespace {

/// One atom (index 0) relabeled — the single substitutional impurity
/// Eq. 9-10 needs. Index 0 is as good as any other site: every site in a
/// pristine supercell is symmetry-equivalent before the substitution
/// breaks that symmetry, which is exactly why a SINGLE substitution
/// suffices (unlike the multi-site SQS/cluster-expansion machinery).
core::Structure substituteOne(const core::Structure& s, int atomicNumber)
{
    core::Structure out = s;
    if (!out.atoms().empty())
        out.atoms()[0].atomicNumber = atomicNumber;
    return out;
}

/// One (name, structure) pair per frame in `path` — the same shape and
/// behaviour as OrchestrationWindow::readStructuresFromFile() (a
/// multi-frame file contributes one entry per frame, named "stem #n"),
/// duplicated in miniature here rather than linking the whole
/// Orchestration canvas translation unit into this wizard for one helper.
DsimWizard::MaterialList readStructuresFromFile(const QString& path, QString* error)
{
    DsimWizard::MaterialList items;
    const QString stem = QFileInfo(path).completeBaseName();
    try {
        std::vector<core::Structure> frames = pybridge::AseBridge::readTrajectory(path.toStdString());
        if (frames.empty()) {
            *error = QObject::tr("%1 holds no structures.").arg(stem);
            return items;
        }
        const bool many = frames.size() > 1;
        for (std::size_t i = 0; i < frames.size(); ++i)
            items.append({many ? QStringLiteral("%1 #%2").arg(stem).arg(i + 1) : stem,
                          std::make_shared<const core::Structure>(std::move(frames[i]))});
    } catch (const std::exception& e) {
        *error = QObject::tr("%1 could not be read: %2").arg(stem, QString::fromUtf8(e.what()));
    }
    return items;
}

} // namespace

DsimWizard::DsimWizard(MaterialList openDocuments, QWidget* parent)
    : SimulationWizardBase(parent), openDocuments_(std::move(openDocuments))
{
    buildUi();
}

QString DsimWizard::wizardTitle() const
{
    return tr("Dilute Solution Interpolation (DSIM)");
}

QString DsimWizard::settingsHeader() const
{
    return tr("Alloy Composition and Supercell");
}

QStringList DsimWizard::calculatorElements() const
{
    QStringList out;
    for (const Entry& entry : validEntries())
        out << entry.species;
    return out;
}

QWidget* DsimWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* group = new QGroupBox(tr("Pristine Reference Structures"), page);
    auto* groupLayout = new QVBoxLayout(group);

    auto* note = new QLabel(
        tr("Add two or more single-species structures — one per alloy "
           "component, each its own element's own geometry (not a shared "
           "template) — from an open document or an imported file. DSIM "
           "builds every pristine supercell plus one single-substitution "
           "impurity supercell per ordered pair of components (N(N-1) of "
           "them for N species), full ion + cell relaxation, then "
           "interpolates the mixing enthalpy from those dilute limits."),
        group);
    note->setWordWrap(true);
    groupLayout->addWidget(note);

    listWidget_ = new QListWidget(group);
    listWidget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    disableTypeToEdit(listWidget_);
    groupLayout->addWidget(listWidget_);

    auto* buttonRow = new QHBoxLayout;
    addDocumentButton_ = new QPushButton(tr("Add from Open Document…"), group);
    addFileButton_ = new QPushButton(tr("Import from File…"), group);
    removeButton_ = new QPushButton(tr("Remove Selected"), group);
    buttonRow->addWidget(addDocumentButton_);
    buttonRow->addWidget(addFileButton_);
    buttonRow->addWidget(removeButton_);
    buttonRow->addStretch(1);
    groupLayout->addLayout(buttonRow);
    connect(addDocumentButton_, &QPushButton::clicked, this, &DsimWizard::addFromOpenDocument);
    connect(addFileButton_, &QPushButton::clicked, this, &DsimWizard::addFromFile);
    connect(removeButton_, &QPushButton::clicked, this, &DsimWizard::removeSelected);

    layout->addWidget(group);

    auto* superGroup = new QGroupBox(tr("Supercell"), page);
    auto* superForm = new QFormLayout(superGroup);
    auto* superRow = new QWidget(superGroup);
    auto* superRowLayout = new QHBoxLayout(superRow);
    superRowLayout->setContentsMargins(0, 0, 0, 0);
    nxSpin_ = new QSpinBox(superRow);
    nySpin_ = new QSpinBox(superRow);
    nzSpin_ = new QSpinBox(superRow);
    for (QSpinBox* spin : {nxSpin_, nySpin_, nzSpin_}) {
        spin->setRange(1, 6);
        spin->setValue(3);
        superRowLayout->addWidget(spin);
        connect(spin, &QSpinBox::valueChanged, this, &DsimWizard::updateSummary);
    }
    superForm->addRow(tr("Repeat (nx, ny, nz):"), superRow);
    summaryLabel_ = new QLabel(superGroup);
    summaryLabel_->setWordWrap(true);
    superForm->addRow(summaryLabel_);
    layout->addWidget(superGroup);
    layout->addStretch(1);

    refillList();
    updateSummary();
    return page;
}

void DsimWizard::addStructures(const MaterialList& structures)
{
    for (const auto& [name, structure] : structures)
        entries_.push_back({name, structure, QString()});
    validateEntries();
    if (listWidget_)
        refillList();
}

DsimWizard::MaterialList DsimWizard::validStructures() const
{
    MaterialList out;
    for (const Entry& entry : validEntries())
        out.append({entry.label, entry.structure});
    return out;
}

void DsimWizard::addFromOpenDocument()
{
    if (openDocuments_.isEmpty()) {
        QMessageBox::information(this, tr("Add from Open Document"),
                                 tr("No open documents with a structure."));
        return;
    }
    QStringList names;
    for (const auto& [name, structure] : openDocuments_)
        names << name;
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("Add from Open Document"),
                                                 tr("Structure:"), names, 0, false, &ok);
    if (!ok)
        return;
    const int index = names.indexOf(chosen);
    if (index < 0)
        return;
    entries_.push_back({openDocuments_[index].first, openDocuments_[index].second, QString()});
    validateEntries();
    refillList();
}

void DsimWizard::addFromFile()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Import Structures"), QString(),
        tr("Structure files (*.extxyz *.xyz *.cif *.traj *.json *.pdb *.poscar "
           "POSCAR CONTCAR *.vasp *.gen *.cube);;All files (*)"));
    if (paths.isEmpty())
        return;
    QStringList problems;
    for (const QString& path : paths) {
        QString error;
        const auto imported = readStructuresFromFile(path, &error);
        if (!error.isEmpty())
            problems << error;
        for (const auto& item : imported)
            entries_.push_back({item.first, item.second, QString()});
    }
    validateEntries();
    refillList();
    if (!problems.isEmpty())
        QMessageBox::warning(this, tr("Import Structures"), problems.join(QStringLiteral("\n")));
}

void DsimWizard::removeSelected()
{
    std::vector<int> rows;
    for (QListWidgetItem* item : listWidget_->selectedItems())
        rows.push_back(listWidget_->row(item));
    std::sort(rows.rbegin(), rows.rend());
    for (int row : rows)
        if (row >= 0 && static_cast<std::size_t>(row) < entries_.size())
            entries_.erase(entries_.begin() + row);
    validateEntries();
    refillList();
}

void DsimWizard::validateEntries()
{
    std::set<QString> seen;
    for (Entry& entry : entries_) {
        const QStringList symbols = structureElements(entry.structure.get());
        entry.species = symbols.size() == 1 ? symbols.front() : QString();
    }
    // Second pass: the FIRST occurrence of a species wins, later ones are
    // duplicates — flagged (species cleared) rather than silently merged,
    // so the list shows exactly which entry needs removing.
    for (Entry& entry : entries_) {
        if (entry.species.isEmpty())
            continue;
        if (seen.count(entry.species)) {
            entry.species.clear();
        } else {
            seen.insert(entry.species);
        }
    }
}

std::vector<DsimWizard::Entry> DsimWizard::validEntries() const
{
    std::vector<Entry> out;
    for (const Entry& entry : entries_)
        if (!entry.species.isEmpty())
            out.push_back(entry);
    return out;
}

void DsimWizard::refillList()
{
    listWidget_->clear();
    for (const Entry& entry : entries_) {
        QString text;
        if (!entry.species.isEmpty())
            text = tr("%1 — %2").arg(entry.label, entry.species);
        else {
            const QStringList symbols = structureElements(entry.structure.get());
            text = symbols.size() != 1
                ? tr("%1 — refused: not single-species (%2)")
                      .arg(entry.label, symbols.join(QStringLiteral(", ")))
                : tr("%1 — refused: %2 already added from another entry")
                      .arg(entry.label, symbols.front());
        }
        listWidget_->addItem(text);
    }
    updateSummary();
}

void DsimWizard::updateSummary()
{
    if (!summaryLabel_)
        return;
    const std::vector<Entry> valid = validEntries();
    if (valid.size() < 2) {
        summaryLabel_->setText(
            tr("Add at least 2 valid single-species structures (have %1).").arg(valid.size()));
        return;
    }
    const int nx = nxSpin_ ? nxSpin_->value() : 3;
    const int ny = nySpin_ ? nySpin_->value() : 3;
    const int nz = nzSpin_ ? nzSpin_->value() : 3;
    QStringList species;
    for (const Entry& entry : valid)
        species << entry.species;
    // Every valid entry's own supercell atom count, since each keeps its
    // own native geometry now rather than sharing one template — the atom
    // COUNT is the same for all of them (nx*ny*nz repeats of a 1-atom-
    // basis-per-entry structure is the common case, but the dilution
    // shown here is per entry, not a single shared number, whenever they
    // differ).
    QStringList counts;
    for (const Entry& entry : valid) {
        const int n = static_cast<int>(entry.structure ? entry.structure->size() : 0) * nx * ny * nz;
        counts << tr("%1: %2 atoms (x0=1/%2)").arg(entry.species).arg(n);
    }
    summaryLabel_->setText(tr("%1 components: %2 — %3")
                                .arg(species.size())
                                .arg(species.join(QStringLiteral(", ")))
                                .arg(counts.join(QStringLiteral("; "))));
}

void DsimWizard::goNext()
{
    if (!structuresBuilt_) {
        const std::vector<Entry> valid = validEntries();
        if (valid.size() < 2) {
            QMessageBox::information(
                this, tr("DSIM"),
                tr("Add at least 2 valid single-species structures before continuing."));
            return;
        }
        const int nx = nxSpin_->value();
        const int ny = nySpin_->value();
        const int nz = nzSpin_->value();
        const std::size_t n = valid.size();
        builtSpecies_.resize(n);
        builtPristine_.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            builtSpecies_[i] = valid[i].species.toStdString();
            builtPristine_[i] =
                pybridge::AseBridge::makeSupercell(*valid[i].structure, nx, ny, nz);
        }
        builtImpurity_.assign(n, std::vector<core::Structure>(n));
        for (std::size_t i = 0; i < n; ++i) {
            const int soluteZ = core::Elements::atomicNumber(builtSpecies_[i]);
            for (std::size_t j = 0; j < n; ++j) {
                if (i == j)
                    continue;
                builtImpurity_[i][j] = substituteOne(builtPristine_[j], soluteZ);
            }
        }
        structuresBuilt_ = true;
    }
    SimulationWizardBase::goNext();
}

core::DsimConfig DsimWizard::config() const
{
    core::DsimConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    // The paper's protocol: full ion + cell relaxation for every one of
    // the N + N(N-1) supercells, never single-point — see the module doc
    // page's "Methods" note.
    cfg.calculator.task = core::TaskKind::GeometryOptimization;
    cfg.calculator.relaxCell = true;
    cfg.species = builtSpecies_;
    cfg.pristine = builtPristine_;
    cfg.impurity = builtImpurity_;
    return cfg;
}

QString DsimWizard::generateScript() const
{
    return QString::fromStdString(core::generateDsimScript(config()));
}

} // namespace calango::gui
