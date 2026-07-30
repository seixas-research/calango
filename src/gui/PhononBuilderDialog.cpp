#include "gui/PhononBuilderDialog.hpp"

#include "gui/SettingsManager.hpp"
#include "gui/CondaEnvs.hpp"
#include "gui/ScriptStaging.hpp"
#include "gui/PythonHighlighter.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

const auto kEnvSettingsKey = QStringLiteral("jobs/environmentPath");

/// Calculators offered for phonon force evaluations — the ones that run
/// out of the box (no external binaries / pseudopotential setup).
constexpr core::CalculatorKind kPhononCalculators[] = {
    core::CalculatorKind::EMT,
    core::CalculatorKind::LennardJones,
    core::CalculatorKind::Mace,
};

} // namespace

PhononBuilderDialog::PhononBuilderDialog(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
{
    setWindowTitle(tr("Phonon Builder — Finite Displacements"));
    resize(900, 600);

    const auto pbc = structure_->cell().pbc();
    periodic_ = structure_->cell().isDefined() && (pbc[0] || pbc[1] || pbc[2]);

    auto* infoLabel = new QLabel(
        periodic_
            ? tr("Periodic structure — supercell finite displacements via "
                 "ase.phonons (dispersion + DOS).")
            : tr("Isolated molecule — Γ-point normal modes via ase.vibrations "
                 "(per-mode animation trajectories)."),
        this);
    infoLabel->setWordWrap(true);

    modeCombo_ = new QComboBox(this);
    modeCombo_->addItems({tr("Run vibrational calculation"),
                          tr("Generate displaced structures only (new tab)")});

    auto* supercellRow = new QHBoxLayout;
    for (auto*& spin : supercellSpins_) {
        spin = new QSpinBox(this);
        spin->setRange(1, 10);
        spin->setValue(2);
        spin->setEnabled(periodic_);
        supercellRow->addWidget(spin);
    }

    deltaSpin_ = new QDoubleSpinBox(this);
    deltaSpin_->setRange(0.001, 0.2);
    deltaSpin_->setDecimals(3);
    deltaSpin_->setSingleStep(0.005);
    deltaSpin_->setValue(0.01);
    deltaSpin_->setSuffix(tr(" Å"));

    countLabel_ = new QLabel(this);

    calculatorCombo_ = new QComboBox(this);
    calculatorCombo_->addItems({tr("EMT (fast test potential)"),
                                tr("Lennard-Jones"),
                                tr("MACE (ML potential, requires mace-torch)")});

    maceModelCombo_ = new QComboBox(this);
    maceModelCombo_->addItems({tr("MACE-MP-0 (universal, materials)"),
                               tr("MACE-OFF (universal, organic molecules)"),
                               tr("Custom trained model (file)")});

    maceSizeCombo_ = new QComboBox(this);
    maceSizeCombo_->addItems({QStringLiteral("small"), QStringLiteral("medium"),
                              QStringLiteral("large")});
    maceSizeCombo_->setCurrentIndex(1);

    maceDispersionCheck_ = new QCheckBox(tr("Dispersion"), this);
    maceDispersionCheck_->setChecked(false);
    maceDispersionCheck_->setToolTip(
        tr("mace_mp(dispersion=True): add the D3(BJ) van der Waals correction "
           "the MACE-MP-0 foundation model ships. Needs the torch-dftd "
           "package in the job environment."));

    // Custom checkpoints: a dropdown over the ML potentials directory
    // (Preferences), editable for hand-typed paths, Browse… for elsewhere.
    maceModelFileCombo_ = new QComboBox(this);
    maceModelFileCombo_->setEditable(true);
    maceModelFileCombo_->lineEdit()->setPlaceholderText(
        tr("path/to/model.model or .pt"));
    for (const QString& path : SettingsManager::mlModelFiles())
        maceModelFileCombo_->addItem(QFileInfo(path).fileName(), path);
    maceModelFileCombo_->setCurrentIndex(-1);
    maceBrowseButton_ = new QPushButton(tr("Browse…"), this);
    maceModelFileRow_ = new QWidget(this);
    auto* macePathRow = new QHBoxLayout(maceModelFileRow_);
    macePathRow->setContentsMargins(0, 0, 0, 0);
    macePathRow->addWidget(maceModelFileCombo_, 1);
    macePathRow->addWidget(maceBrowseButton_);
    connect(maceBrowseButton_, &QPushButton::clicked,
            this, &PhononBuilderDialog::browseMaceModel);

    maceDeviceCombo_ = new QComboBox(this);
    maceDeviceCombo_->addItems({QStringLiteral("cpu"), QStringLiteral("cuda"),
                                QStringLiteral("mps")});

    bandPointsSpin_ = new QSpinBox(this);
    bandPointsSpin_->setRange(20, 1000);
    bandPointsSpin_->setValue(100);
    bandPointsSpin_->setEnabled(periodic_);

    // One spin box per reciprocal axis — anisotropic cells want anisotropic
    // q-meshes (a slab samples q_z once, not twenty times).
    auto* dosGridRow = new QHBoxLayout;
    for (QSpinBox*& spin : dosGridSpins_) {
        spin = new QSpinBox(this);
        spin->setRange(1, 64);
        spin->setValue(20);
        spin->setToolTip(
            tr("Monkhorst-Pack q-grid for the phonon DOS, per axis"));
        spin->setEnabled(periodic_);
        dosGridRow->addWidget(spin);
    }

    auto* form = new QFormLayout;
    form_ = form;
    form->addRow(infoLabel);
    form->addRow(tr("Mode:"), modeCombo_);
    form->addRow(tr("Supercell (a × b × c):"), supercellRow);
    form->addRow(tr("Displacement δ:"), deltaSpin_);
    form->addRow(tr("Displaced structures:"), countLabel_);
    form->addRow(tr("Calculator:"), calculatorCombo_);
    form->addRow(tr("MACE model:"), maceModelCombo_);
    form->addRow(tr("MACE model size:"), maceSizeCombo_);
    form->addRow(maceDispersionCheck_);
    form->addRow(tr("Custom model file:"), maceModelFileRow_);
    form->addRow(tr("MACE device:"), maceDeviceCombo_);
    form->addRow(tr("Band-path points:"), bandPointsSpin_);
    form->addRow(tr("DOS q-grid (qx·qy·qz):"), dosGridRow);

    // Execution environment (shared setting with the calculator dialog).
    auto* envGroup = new QGroupBox(tr("Execution Environment"), this);
    auto* envLayout = new QVBoxLayout(envGroup);
    envPathEdit_ = new QLineEdit(envGroup);
    envPathEdit_->setPlaceholderText(
        tr("conda env folder or python executable (empty = embedded)"));
    envLayout->addWidget(envPathEdit_);
    envStatusLabel_ = new QLabel(envGroup);
    envStatusLabel_->setWordWrap(true);
    envLayout->addWidget(envStatusLabel_);
    const auto updateEnvStatus = [this] {
        const QString text = envPathEdit_->text().trimmed();
        if (text.isEmpty()) {
            envStatusLabel_->setText(
                tr("Using embedded interpreter: %1")
                    .arg(QString::fromStdString(
                        pybridge::PythonEngine::instance().executable())));
            envStatusLabel_->setStyleSheet(QString());
        } else if (const QString python =
                       CondaEnvs::resolvePython(text);
                   !python.isEmpty()) {
            envStatusLabel_->setText(tr("Jobs will run with: %1").arg(python));
            envStatusLabel_->setStyleSheet(QString());
        } else {
            envStatusLabel_->setText(tr("No python interpreter found at this path."));
            envStatusLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        }
        QSettings().setValue(kEnvSettingsKey, envPathEdit_->text());
    };
    connect(envPathEdit_, &QLineEdit::textChanged, this, updateEnvStatus);
    envPathEdit_->setText(QSettings().value(kEnvSettingsKey).toString());
    updateEnvStatus();

    // Editable script preview, same contract as the calculator dialog.
    preview_ = new QPlainTextEdit(this);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new PythonHighlighter(preview_->document()); // parented to the document
    connect(preview_, &QPlainTextEdit::textChanged, this, [this] {
        if (updatingPreview_ || manuallyEdited_)
            return;
        manuallyEdited_ = true;
        editedNotice_->setVisible(true);
    });

    auto* buttons = new QDialogButtonBox(this);
    runButton_ = buttons->addButton(tr("Run"), QDialogButtonBox::AcceptRole);
    auto* saveButton = buttons->addButton(tr("Save Script…"),
                                          QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    runButton_->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, this, &PhononBuilderDialog::saveScript);

    auto* left = new QVBoxLayout;
    left->addLayout(form);
    left->addWidget(envGroup);
    left->addStretch(1);

    auto* previewColumn = new QVBoxLayout;
    auto* previewHeader = new QHBoxLayout;
    previewHeader->addWidget(new QLabel(tr("Generated ASE script (editable):"), this));
    previewHeader->addStretch(1);
    editedNotice_ = new QLabel(tr("edited — form sync paused"), this);
    editedNotice_->setStyleSheet(QStringLiteral("color: #b07d2a;"));
    editedNotice_->setVisible(false);
    previewHeader->addWidget(editedNotice_);
    auto* regenerateButton = new QPushButton(tr("Regenerate"), this);
    regenerateButton->setToolTip(tr("Discard manual edits and regenerate from the form"));
    previewHeader->addWidget(regenerateButton);
    connect(regenerateButton, &QPushButton::clicked,
            this, &PhononBuilderDialog::regenerateScript);
    previewColumn->addLayout(previewHeader);
    previewColumn->addWidget(preview_, 1);

    auto* content = new QHBoxLayout;
    content->addLayout(left, 0);
    content->addLayout(previewColumn, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(content, 1);
    layout->addWidget(buttons);

    const auto refresh = [this] { refreshPreview(); };
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, refresh);
    for (auto* spin : supercellSpins_)
        connect(spin, &QSpinBox::valueChanged, this, refresh);
    connect(deltaSpin_, &QDoubleSpinBox::valueChanged, this, refresh);
    connect(calculatorCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(maceModelCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(maceSizeCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(maceModelFileCombo_, &QComboBox::editTextChanged, this, refresh);
    connect(maceDispersionCheck_, &QCheckBox::toggled, this, refresh);
    connect(maceDeviceCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(bandPointsSpin_, &QSpinBox::valueChanged, this, refresh);
    for (QSpinBox* spin : dosGridSpins_)
        connect(spin, &QSpinBox::valueChanged, this, refresh);

    refreshPreview();
}

bool PhononBuilderDialog::generateDisplacementsOnly() const
{
    return modeCombo_->currentIndex() == 1;
}

core::PhononConfig PhononBuilderDialog::config() const
{
    core::PhononConfig c;
    c.periodic = periodic_;
    for (int i = 0; i < 3; ++i)
        c.supercell[i] = supercellSpins_[i]->value();
    c.deltaAngstrom = deltaSpin_->value();
    c.bandPathPoints = bandPointsSpin_->value();
    for (int i = 0; i < 3; ++i)
        c.dosKptGrid[i] = dosGridSpins_[i]->value();

    c.calculator.calculator = kPhononCalculators[calculatorCombo_->currentIndex()];
    c.calculator.maceSource =
        static_cast<core::MaceModelSource>(maceModelCombo_->currentIndex());
    c.calculator.maceSize = maceSizeCombo_->currentText().toStdString();
    // A picked list entry resolves to its stored absolute path; typed or
    // browsed text is taken verbatim. Foundation models carry no file.
    QString modelFile;
    if (c.calculator.maceSource == core::MaceModelSource::CustomFile) {
        const int index = maceModelFileCombo_->currentIndex();
        modelFile = index >= 0
                && maceModelFileCombo_->currentText()
                    == maceModelFileCombo_->itemText(index)
            ? maceModelFileCombo_->itemData(index).toString()
            : maceModelFileCombo_->currentText().trimmed();
    }
    c.calculator.maceModelPath = modelFile.toStdString();
    c.calculator.maceDispersion =
        c.calculator.maceSource == core::MaceModelSource::FoundationMP
        && maceDispersionCheck_->isChecked();
    c.calculator.maceDevice = maceDeviceCombo_->currentText().toStdString();
    return c;
}

QString PhononBuilderDialog::script() const
{
    return preview_->toPlainText();
}

QString PhononBuilderDialog::pythonExecutable() const
{
    const QString resolved =
        CondaEnvs::resolvePython(envPathEdit_->text());
    if (!resolved.isEmpty())
        return resolved;
    return QString::fromStdString(pybridge::PythonEngine::instance().executable());
}

void PhononBuilderDialog::regenerateScript()
{
    manuallyEdited_ = false;
    editedNotice_->setVisible(false);
    refreshPreview();
}

void PhononBuilderDialog::browseMaceModel()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select MACE Model"),
        SettingsManager::mlPotentialsStartPath(
            maceModelFileCombo_->currentText()),
        tr("MACE models (*.model *.pt);;All files (*)"));
    if (!path.isEmpty())
        // editTextChanged refreshes the preview
        maceModelFileCombo_->setCurrentText(path);
}

void PhononBuilderDialog::refreshPreview()
{
    const core::PhononConfig c = config();
    const bool displacedOnly = generateDisplacementsOnly();
    const bool isMace = c.calculator.calculator == core::CalculatorKind::Mace;
    const bool isCustomMace =
        isMace && c.calculator.maceSource == core::MaceModelSource::CustomFile;

    // Displacements act on the expanded supercell, so the frame count
    // scales with the supercell multiplicity for periodic systems.
    const std::size_t multiplicity = periodic_
        ? static_cast<std::size_t>(c.supercell[0]) * c.supercell[1] * c.supercell[2]
        : 1;
    const std::size_t frameCount = 6 * structure_->size() * multiplicity + 1;
    countLabel_->setText(
        tr("%1 (reference + 6 × %2 supercell atoms)")
            .arg(frameCount)
            .arg(structure_->size() * multiplicity));

    runButton_->setText(displacedOnly ? tr("Generate") : tr("Run"));
    calculatorCombo_->setEnabled(!displacedOnly);
    maceModelCombo_->setEnabled(!displacedOnly && isMace);
    // The foundation families take a size keyword and no file; a custom
    // checkpoint is the reverse. The rows are hidden, not merely disabled —
    // a disabled field still claims space and invites reading.
    const auto setRowVisible = [this](QWidget* field, bool visible) {
        int row = -1;
        QFormLayout::ItemRole role{};
        form_->getWidgetPosition(field, &row, &role);
        if (row >= 0)
            form_->setRowVisible(row, visible);
    };
    setRowVisible(maceSizeCombo_, isMace && !isCustomMace);
    setRowVisible(maceModelFileRow_, isCustomMace);
    setRowVisible(maceDispersionCheck_,
                  isMace
                      && c.calculator.maceSource
                          == core::MaceModelSource::FoundationMP);
    maceSizeCombo_->setEnabled(!displacedOnly && isMace && !isCustomMace);
    maceModelFileCombo_->setEnabled(!displacedOnly && isCustomMace);
    maceBrowseButton_->setEnabled(!displacedOnly && isCustomMace);
    maceDispersionCheck_->setEnabled(!displacedOnly && isMace);
    maceDeviceCombo_->setEnabled(!displacedOnly && isMace);
    bandPointsSpin_->setEnabled(!displacedOnly && periodic_);
    for (QSpinBox* spin : dosGridSpins_)
        spin->setEnabled(!displacedOnly && periodic_);
    preview_->setEnabled(!displacedOnly);

    // Never clobber the user's manual edits; "Regenerate" re-enables sync.
    if (manuallyEdited_)
        return;
    updatingPreview_ = true;
    // The job runner stages the structure as structure.extxyz in the job
    // directory, so the script refers to it relatively.
    preview_->setPlainText(QString::fromStdString(
        core::PhononScriptGenerator::generate(c, "structure.extxyz")));
    updatingPreview_ = false;
}

void PhononBuilderDialog::saveScript()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Phonon Script"), QStringLiteral("phonons.py"),
        tr("Python scripts (*.py)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!writeScript(path, script(), &error))
        QMessageBox::warning(this, tr("Save Script"), error);
}

} // namespace calango::gui
