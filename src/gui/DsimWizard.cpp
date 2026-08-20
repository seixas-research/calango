#include "gui/DsimWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/Element.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
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

/// EVERY atom relabeled — the "element X occupying element Y's crystal
/// structure" template a multi-phase branch's foreign-element pristine
/// reference needs (positions/cell kept as-is; the subsequent full ion+
/// cell relaxation the wizard always requests is what moves it to that
/// element's own equilibrium volume on this template). See
/// core::solveDsimMultiPhase's doc comment for why this — not a second
/// independent structure — is the right reference.
core::Structure relabelAll(const core::Structure& s, int atomicNumber)
{
    core::Structure out = s;
    for (core::Atom& atom : out.atoms())
        atom.atomicNumber = atomicNumber;
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

    auto* phaseGroup = new QGroupBox(tr("Multi-Phase Alloy"), page);
    auto* phaseForm = new QFormLayout(phaseGroup);
    multiPhaseCheck_ = new QCheckBox(
        tr("Different crystal structures per element (e.g. Fe(bcc)-Co(hcp))"), phaseGroup);
    multiPhaseCheck_->setToolTip(
        tr("Solves two independent DSIM binary branches, one on each element's own "
           "crystal structure (including the OTHER element relabeled onto it and "
           "relaxed), then shifts both onto one common energy reference so the two "
           "curves are directly comparable — the lower one at each composition is the "
           "stable phase. Needs exactly 2 components; each keeps its own structure "
           "from Stage 1 above, nothing else to add."));
    phaseForm->addRow(multiPhaseCheck_);
    phaseALabelEdit_ = new QLineEdit(phaseGroup);
    phaseALabelEdit_->setPlaceholderText(tr("e.g. bcc"));
    phaseBLabelEdit_ = new QLineEdit(phaseGroup);
    phaseBLabelEdit_->setPlaceholderText(tr("e.g. hcp"));
    phaseForm->addRow(tr("Phase label (1st component):"), phaseALabelEdit_);
    phaseForm->addRow(tr("Phase label (2nd component):"), phaseBLabelEdit_);
    connect(multiPhaseCheck_, &QCheckBox::toggled, this, &DsimWizard::updateMultiPhaseVisibility);
    connect(phaseALabelEdit_, &QLineEdit::textChanged, this, &DsimWizard::updateSummary);
    connect(phaseBLabelEdit_, &QLineEdit::textChanged, this, &DsimWizard::updateSummary);
    layout->addWidget(phaseGroup);

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
    updateMultiPhaseVisibility();
    return page;
}

QString DsimWizard::secondSettingsHeader() const
{
    return tr("Geometry Optimization Settings");
}

QWidget* DsimWizard::buildSecondSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* note = new QLabel(
        tr("The paper's protocol: every supercell is relaxed — ions AND "
           "cell, never single-point — to a force criterion of 0.02 eV/Å "
           "by default. These settings apply to all of them alike."),
        page);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* relaxGroup = new QGroupBox(tr("Relaxation"), page);
    auto* relaxForm = new QFormLayout(relaxGroup);

    optimizerCombo_ = new QComboBox(relaxGroup);
    // Order matches core::Optimizer.
    optimizerCombo_->addItems({QStringLiteral("BFGS"), QStringLiteral("LBFGS"),
                               QStringLiteral("FIRE"), QStringLiteral("GPMin"),
                               QStringLiteral("MDMin")});
    optimizerCombo_->setCurrentIndex(2); // FIRE: a robust default for a
                                         // relaxation with no good initial
                                         // Hessian guess (fresh geometry
                                         // every one of the N + N(N-1) times)
    relaxForm->addRow(tr("Optimizer:"), optimizerCombo_);
    connect(optimizerCombo_, &QComboBox::currentIndexChanged, this, &DsimWizard::refreshPreview);

    fmax_.build(relaxForm, relaxGroup, tr("Force convergence (fmax):"));
    fmax_.setValue(0.02); // the paper's own criterion, not
                          // ForceConvergenceControl's shared 0.05 default
    connect(fmax_.spinBox(), &QDoubleSpinBox::valueChanged, this, &DsimWizard::refreshPreview);

    maxStepsSpin_ = new QSpinBox(relaxGroup);
    maxStepsSpin_->setRange(1, 100000);
    maxStepsSpin_->setValue(300);
    maxStepsSpin_->setToolTip(
        tr("Cap per supercell. With N + N(N-1) supercells the worst case is "
           "that many force evaluations times this many steps, so keep it "
           "modest on DFT backends."));
    relaxForm->addRow(tr("Max steps (each):"), maxStepsSpin_);
    connect(maxStepsSpin_, &QSpinBox::valueChanged, this, &DsimWizard::refreshPreview);

    // Variable-cell relaxation, built by the SAME helper Geometry
    // Optimization and Cluster Expansion's batch relax use — identical
    // filters, stress-mask presets and Voigt ticks.
    cell_.build(relaxGroup, relaxForm, [this] { refreshPreview(); });
    // CellRelaxationControls' own checkbox defaults to UNCHECKED (matching
    // Geometry Optimization's own "ion relaxation is the default, cell
    // relaxation is opt-in" convention) — wrong here: the paper's protocol
    // relaxes the cell unconditionally, and a user who never noticed the
    // box would silently get fixed-cell impurity energies, not comparable
    // to the pristine references' own relaxed volumes. No public setter
    // exists on the shared control for this (every other host's default IS
    // unchecked), so the checkbox is found by its own label text instead
    // of widening that shared class for one caller's different default.
    for (QCheckBox* box : relaxGroup->findChildren<QCheckBox*>())
        if (box->text().contains(QStringLiteral("Relax the unit cell")))
            box->setChecked(true);

    layout->addWidget(relaxGroup);
    layout->addStretch(1);
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
    updateMultiPhaseVisibility();
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

void DsimWizard::updateMultiPhaseVisibility()
{
    if (!multiPhaseCheck_)
        return;
    const bool eligible = validEntries().size() == 2;
    multiPhaseCheck_->setEnabled(eligible);
    if (!eligible && multiPhaseCheck_->isChecked())
        multiPhaseCheck_->setChecked(false); // e.g. a 3rd structure just got added — stay
                                              // an ordinary N-component run, not silently
                                              // still "multi-phase" for a mode that needs N=2
    const bool showFields = eligible && multiPhaseCheck_->isChecked();
    if (auto* group = qobject_cast<QGroupBox*>(phaseALabelEdit_ ? phaseALabelEdit_->parentWidget() : nullptr)) {
        setFormRowVisible(group, phaseALabelEdit_, showFields);
        setFormRowVisible(group, phaseBLabelEdit_, showFields);
    }
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

        const bool wantMultiPhase =
            multiPhaseCheck_ && multiPhaseCheck_->isChecked() && valid.size() == 2;
        if (wantMultiPhase) {
            const QString labelA = phaseALabelEdit_ ? phaseALabelEdit_->text().trimmed() : QString();
            const QString labelB = phaseBLabelEdit_ ? phaseBLabelEdit_->text().trimmed() : QString();
            if (labelA.isEmpty() || labelB.isEmpty()) {
                QMessageBox::information(
                    this, tr("DSIM"),
                    tr("Name both phases (e.g. \"bcc\", \"hcp\") before continuing."));
                return;
            }
            const int zA = core::Elements::atomicNumber(valid[0].species.toStdString());
            const int zB = core::Elements::atomicNumber(valid[1].species.toStdString());
            // Each element's OWN supercell, on its OWN input structure —
            // the normal DSIM pristine build — plus the OTHER element
            // relabeled onto that same template (relaxed later, in the
            // generated script, to its own equilibrium volume there).
            const core::Structure pristineA_onA =
                pybridge::AseBridge::makeSupercell(*valid[0].structure, nx, ny, nz);
            const core::Structure pristineB_onB =
                pybridge::AseBridge::makeSupercell(*valid[1].structure, nx, ny, nz);
            // core::solveDsimMultiPhase shares ONE supercellAtomCount (and
            // therefore one dilution) across both branches — needed for
            // the lattice-stability shift's per-atom division to compare
            // like with like (see its own doc comment). Different input
            // cells with different atom counts (e.g. a 1-atom bcc
            // primitive cell against a 2-atom hcp conventional cell) would
            // silently break that, so it is refused here rather than
            // producing a shift computed across mismatched N.
            if (pristineA_onA.size() != pristineB_onB.size()) {
                QMessageBox::information(
                    this, tr("DSIM"),
                    tr("The two input structures build supercells of different atom "
                       "counts here (%1 vs %2 atoms, at this nx/ny/nz repeat) — "
                       "multi-phase mode needs both phase branches on the same atom "
                       "count. Pick input cells with matching atoms-per-cell (e.g. "
                       "both primitive or both conventional), or adjust the repeat.")
                        .arg(pristineA_onA.size())
                        .arg(pristineB_onB.size()));
                return;
            }
            const core::Structure pristineB_onA = relabelAll(pristineA_onA, zB);
            const core::Structure pristineA_onB = relabelAll(pristineB_onB, zA);

            builtSpeciesA_ = valid[0].species.toStdString();
            builtSpeciesB_ = valid[1].species.toStdString();

            builtPhaseA_.phaseLabel = labelA.toStdString();
            builtPhaseA_.pristineA = pristineA_onA;
            builtPhaseA_.pristineB = pristineB_onA;
            builtPhaseA_.impurityBInA = substituteOne(pristineA_onA, zB); // B diluted in A
            builtPhaseA_.impurityAInB = substituteOne(pristineB_onA, zA); // A diluted in (B-on-A's lattice)

            builtPhaseB_.phaseLabel = labelB.toStdString();
            builtPhaseB_.pristineA = pristineA_onB;
            builtPhaseB_.pristineB = pristineB_onB;
            builtPhaseB_.impurityBInA = substituteOne(pristineA_onB, zB); // B diluted in (A-on-B's lattice)
            builtPhaseB_.impurityAInB = substituteOne(pristineB_onB, zA); // A diluted in B

            multiPhaseMode_ = true;
        } else {
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
        }
        structuresBuilt_ = true;
    }
    SimulationWizardBase::goNext();
}

core::CalculatorConfig DsimWizard::builtCalculatorConfig() const
{
    core::CalculatorConfig calc = baseCalculatorConfig();
    calc.task = core::TaskKind::GeometryOptimization;
    // Stage 3's own controls, not silent hardcoding — see
    // buildSecondSettingsPage(). optimizerCombo_/fmax_/maxStepsSpin_/cell_
    // are null only if generateScript() is somehow called before buildUi()
    // finishes building every stage, which does not happen in practice
    // (the review stage that calls it is built last).
    if (optimizerCombo_)
        calc.optimizer = static_cast<core::Optimizer>(optimizerCombo_->currentIndex());
    calc.fmax = fmax_.value();
    if (maxStepsSpin_)
        calc.maxSteps = maxStepsSpin_->value();
    cell_.applyTo(calc);
    return calc;
}

core::DsimConfig DsimWizard::config() const
{
    core::DsimConfig cfg;
    cfg.calculator = builtCalculatorConfig();
    cfg.species = builtSpecies_;
    cfg.pristine = builtPristine_;
    cfg.impurity = builtImpurity_;
    return cfg;
}

QString DsimWizard::generateScript() const
{
    if (multiPhaseMode_) {
        core::DsimMultiPhaseConfig cfg;
        cfg.calculator = builtCalculatorConfig();
        cfg.speciesA = builtSpeciesA_;
        cfg.speciesB = builtSpeciesB_;
        cfg.phaseA = builtPhaseA_;
        cfg.phaseB = builtPhaseB_;
        return QString::fromStdString(core::generateDsimMultiPhaseScript(cfg));
    }
    return QString::fromStdString(core::generateDsimScript(config()));
}

} // namespace calango::gui
