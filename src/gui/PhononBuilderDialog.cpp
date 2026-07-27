#include "gui/PhononBuilderDialog.hpp"

#include "gui/CondaEnvs.hpp"
#include "gui/ScriptStaging.hpp"
#include "gui/PythonHighlighter.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
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

    maceModelPathEdit_ = new QLineEdit(this);
    maceModelPathEdit_->setPlaceholderText(tr("path/to/model.model or .pt"));
    maceBrowseButton_ = new QPushButton(tr("Browse…"), this);
    auto* macePathRow = new QHBoxLayout;
    macePathRow->addWidget(maceModelPathEdit_, 1);
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

    dosGridSpin_ = new QSpinBox(this);
    dosGridSpin_->setRange(2, 64);
    dosGridSpin_->setValue(20);
    dosGridSpin_->setToolTip(tr("Monkhorst-Pack n×n×n grid for the phonon DOS"));
    dosGridSpin_->setEnabled(periodic_);

    auto* form = new QFormLayout;
    form->addRow(infoLabel);
    form->addRow(tr("Mode:"), modeCombo_);
    form->addRow(tr("Supercell (a × b × c):"), supercellRow);
    form->addRow(tr("Displacement δ:"), deltaSpin_);
    form->addRow(tr("Displaced structures:"), countLabel_);
    form->addRow(tr("Calculator:"), calculatorCombo_);
    form->addRow(tr("MACE model:"), maceModelCombo_);
    form->addRow(tr("MACE model size:"), maceSizeCombo_);
    form->addRow(tr("Custom model file:"), macePathRow);
    form->addRow(tr("MACE device:"), maceDeviceCombo_);
    form->addRow(tr("Band-path points:"), bandPointsSpin_);
    form->addRow(tr("DOS k-grid:"), dosGridSpin_);

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
    connect(maceModelPathEdit_, &QLineEdit::textChanged, this, refresh);
    connect(maceDeviceCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(bandPointsSpin_, &QSpinBox::valueChanged, this, refresh);
    connect(dosGridSpin_, &QSpinBox::valueChanged, this, refresh);

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
    c.dosKptGrid = dosGridSpin_->value();

    c.calculator.calculator = kPhononCalculators[calculatorCombo_->currentIndex()];
    c.calculator.maceSource =
        static_cast<core::MaceModelSource>(maceModelCombo_->currentIndex());
    c.calculator.maceSize = maceSizeCombo_->currentText().toStdString();
    c.calculator.maceModelPath = maceModelPathEdit_->text().trimmed().toStdString();
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
        this, tr("Select MACE Model"), QString(),
        tr("MACE models (*.model *.pt);;All files (*)"));
    if (!path.isEmpty())
        maceModelPathEdit_->setText(path); // textChanged refreshes the preview
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
    maceSizeCombo_->setEnabled(!displacedOnly && isMace && !isCustomMace);
    maceModelPathEdit_->setEnabled(!displacedOnly && isCustomMace);
    maceBrowseButton_->setEnabled(!displacedOnly && isCustomMace);
    maceDeviceCombo_->setEnabled(!displacedOnly && isMace);
    bandPointsSpin_->setEnabled(!displacedOnly && periodic_);
    dosGridSpin_->setEnabled(!displacedOnly && periodic_);
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

    // Stages calango_log.py beside the script (it imports CalangoLog).
    QString error;
    if (!writeScriptWithLogger(path, script(), &error))
        QMessageBox::warning(this, tr("Save Script"), error);
}

} // namespace calango::gui
