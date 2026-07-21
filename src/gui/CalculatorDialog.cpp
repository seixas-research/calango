#include "gui/CalculatorDialog.hpp"

#include "core/AseScriptGenerator.hpp"
#include "gui/PythonHighlighter.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

const auto kEnvSettingsKey = QStringLiteral("jobs/environmentPath");

} // namespace

QString CalculatorDialog::resolveEnvironmentPython(const QString& input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return {};
    const QFileInfo info(trimmed);
    if (info.isFile())
        return info.absoluteFilePath();
    if (info.isDir()) {
        const QDir dir(trimmed);
        const QStringList candidates = {
#ifdef Q_OS_WIN
            QStringLiteral("python.exe"), QStringLiteral("Scripts/python.exe"),
#endif
            QStringLiteral("bin/python"), QStringLiteral("bin/python3"),
            QStringLiteral("python"), QStringLiteral("python3")};
        for (const QString& candidate : candidates) {
            if (QFileInfo::exists(dir.filePath(candidate)))
                return QFileInfo(dir.filePath(candidate)).absoluteFilePath();
        }
    }
    return {};
}

CalculatorDialog::CalculatorDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New Calculation"));
    resize(860, 560);

    calculatorCombo_ = new QComboBox(this);
    calculatorCombo_->addItems({tr("EMT (effective medium theory — fast test potential)"),
                                tr("Lennard-Jones"),
                                tr("Quantum ESPRESSO (DFT, requires pw.x)"),
                                tr("VASP (DFT, requires license)"),
                                tr("MACE (machine-learning potential, requires mace-torch)"),
                                tr("GPAW (DFT, python package)"),
                                tr("SIESTA (DFT, requires siesta binary)"),
                                tr("ORCA (quantum chemistry, requires orca binary)")});

    taskCombo_ = new QComboBox(this);
    taskCombo_->addItems({tr("Single-point energy"),
                          tr("Geometry optimization (BFGS)"),
                          tr("Molecular dynamics")});

    // Order mirrors core::MdEnsemble.
    ensembleCombo_ = new QComboBox(this);
    ensembleCombo_->addItems({tr("NVE — Velocity Verlet"),
                              tr("NVT — Langevin dynamics"),
                              tr("NVT — Andersen thermostat"),
                              tr("NVT — Berendsen thermostat"),
                              tr("NVT — Nosé–Hoover chain"),
                              tr("NPT — Berendsen"),
                              tr("NPT — Nosé–Hoover / Parrinello–Rahman "
                                 "(Melchionna)")});
    ensembleCombo_->setCurrentIndex(1); // Langevin stays the default

    fmaxSpin_ = new QDoubleSpinBox(this);
    fmaxSpin_->setRange(0.001, 1.0);
    fmaxSpin_->setDecimals(3);
    fmaxSpin_->setSingleStep(0.01);
    fmaxSpin_->setValue(0.05);
    fmaxSpin_->setSuffix(tr(" eV/Å"));

    maxStepsSpin_ = new QSpinBox(this);
    maxStepsSpin_->setRange(1, 100000);
    maxStepsSpin_->setValue(200);

    temperatureSpin_ = new QDoubleSpinBox(this);
    temperatureSpin_->setRange(0.0, 10000.0);
    temperatureSpin_->setValue(300.0);
    temperatureSpin_->setSuffix(tr(" K"));

    timestepSpin_ = new QDoubleSpinBox(this);
    timestepSpin_->setRange(0.01, 20.0);
    timestepSpin_->setValue(1.0);
    timestepSpin_->setSuffix(tr(" fs"));

    mdStepsSpin_ = new QSpinBox(this);
    mdStepsSpin_->setRange(1, 10000000);
    mdStepsSpin_->setValue(1000);

    tautSpin_ = new QDoubleSpinBox(this);
    tautSpin_->setRange(1.0, 100000.0);
    tautSpin_->setValue(100.0);
    tautSpin_->setSuffix(tr(" fs"));
    tautSpin_->setToolTip(tr("Thermostat coupling time (taut / tdamp / ttime)"));

    taupSpin_ = new QDoubleSpinBox(this);
    taupSpin_->setRange(1.0, 1000000.0);
    taupSpin_->setValue(1000.0);
    taupSpin_->setSuffix(tr(" fs"));
    taupSpin_->setToolTip(tr("Barostat coupling time (taup / ptime)"));

    pressureSpin_ = new QDoubleSpinBox(this);
    pressureSpin_->setRange(-10.0, 500.0);
    pressureSpin_->setDecimals(4);
    pressureSpin_->setValue(0.0);
    pressureSpin_->setSuffix(tr(" GPa"));
    pressureSpin_->setToolTip(tr("External pressure for NPT (0 ≈ ambient)"));

    cutoffSpin_ = new QDoubleSpinBox(this);
    cutoffSpin_->setRange(100.0, 2000.0);
    cutoffSpin_->setValue(550.0);
    cutoffSpin_->setSuffix(tr(" eV"));

    auto* kptRow = new QHBoxLayout;
    for (auto*& spin : kptSpins_) {
        spin = new QSpinBox(this);
        spin->setRange(1, 64);
        spin->setValue(4);
        kptRow->addWidget(spin);
    }

    // MACE: universal foundation models (downloaded & cached automatically
    // by mace-torch on first use) or a user-trained checkpoint file.
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
            this, &CalculatorDialog::browseMaceModel);

    maceDeviceCombo_ = new QComboBox(this);
    maceDeviceCombo_->addItems({QStringLiteral("cpu"), QStringLiteral("cuda"),
                                QStringLiteral("mps")});

    // ORCA: method / basis are editable combos (any ORCA keyword works).
    orcaMethodCombo_ = new QComboBox(this);
    orcaMethodCombo_->setEditable(true);
    orcaMethodCombo_->addItems({QStringLiteral("B3LYP"), QStringLiteral("PBE0"),
                                QStringLiteral("r2SCAN"),
                                QStringLiteral("wB97X-D3"), QStringLiteral("HF"),
                                QStringLiteral("MP2")});
    orcaBasisCombo_ = new QComboBox(this);
    orcaBasisCombo_->setEditable(true);
    orcaBasisCombo_->addItems({QStringLiteral("def2-SVP"),
                               QStringLiteral("def2-TZVP"),
                               QStringLiteral("def2-TZVPP"),
                               QStringLiteral("cc-pVDZ"),
                               QStringLiteral("cc-pVTZ")});
    chargeSpin_ = new QSpinBox(this);
    chargeSpin_->setRange(-10, 10);
    chargeSpin_->setValue(0);
    multiplicitySpin_ = new QSpinBox(this);
    multiplicitySpin_->setRange(1, 11);
    multiplicitySpin_->setValue(1);
    multiplicitySpin_->setToolTip(tr("Spin multiplicity 2S+1"));
    orcaSolvationCombo_ = new QComboBox(this);
    orcaSolvationCombo_->addItems({tr("None (gas phase)"),
                                   QStringLiteral("CPCM"),
                                   QStringLiteral("SMD")});
    orcaSolventEdit_ = new QLineEdit(QStringLiteral("water"), this);
    orcaSolventEdit_->setToolTip(tr("Solvent name for CPCM/SMD "
                                    "(water, acetonitrile, toluene, ...)"));

    auto* form = new QFormLayout;
    form->addRow(tr("Calculator:"), calculatorCombo_);
    form->addRow(tr("Task:"), taskCombo_);
    form->addRow(tr("MD ensemble:"), ensembleCombo_);
    form->addRow(tr("Force convergence (fmax):"), fmaxSpin_);
    form->addRow(tr("Max optimization steps:"), maxStepsSpin_);
    form->addRow(tr("Temperature:"), temperatureSpin_);
    form->addRow(tr("Timestep:"), timestepSpin_);
    form->addRow(tr("MD steps:"), mdStepsSpin_);
    form->addRow(tr("Thermostat coupling:"), tautSpin_);
    form->addRow(tr("Barostat coupling:"), taupSpin_);
    form->addRow(tr("Pressure:"), pressureSpin_);
    form->addRow(tr("Plane-wave cutoff:"), cutoffSpin_);
    form->addRow(tr("k-point grid:"), kptRow);
    form->addRow(tr("MACE model:"), maceModelCombo_);
    form->addRow(tr("MACE model size:"), maceSizeCombo_);
    form->addRow(tr("Custom model file:"), macePathRow);
    form->addRow(tr("MACE device:"), maceDeviceCombo_);
    form->addRow(tr("ORCA method:"), orcaMethodCombo_);
    form->addRow(tr("ORCA basis:"), orcaBasisCombo_);
    form->addRow(tr("Charge:"), chargeSpin_);
    form->addRow(tr("Multiplicity:"), multiplicitySpin_);
    form->addRow(tr("Solvation:"), orcaSolvationCombo_);
    form->addRow(tr("Solvent:"), orcaSolventEdit_);

    // The script pane is a real editor: syntax-highlighted and editable.
    preview_ = new QPlainTextEdit(this);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new PythonHighlighter(preview_->document()); // parented to the document
    connect(preview_, &QPlainTextEdit::textChanged, this, [this] {
        if (updatingPreview_ || manuallyEdited_)
            return;
        manuallyEdited_ = true;
        editedNotice_->setVisible(true);
    });

    // Execution environment: a conda env (or any interpreter) the job
    // runner uses instead of the embedded Python.
    auto* envGroup = new QGroupBox(tr("Execution Environment"), this);
    auto* envLayout = new QVBoxLayout(envGroup);
    auto* envRow = new QHBoxLayout;
    envPathEdit_ = new QLineEdit(envGroup);
    envPathEdit_->setPlaceholderText(tr("conda env folder or python executable (empty = embedded)"));
    auto* envDirButton = new QPushButton(tr("Env Folder…"), envGroup);
    auto* envFileButton = new QPushButton(tr("Python…"), envGroup);
    envRow->addWidget(envPathEdit_, 1);
    envRow->addWidget(envDirButton);
    envRow->addWidget(envFileButton);
    envLayout->addLayout(envRow);
    envStatusLabel_ = new QLabel(envGroup);
    envStatusLabel_->setWordWrap(true);
    envLayout->addWidget(envStatusLabel_);
    connect(envDirButton, &QPushButton::clicked,
            this, &CalculatorDialog::browseEnvironmentDir);
    connect(envFileButton, &QPushButton::clicked,
            this, &CalculatorDialog::browseEnvironmentPython);

    const auto updateEnvStatus = [this] {
        const QString text = envPathEdit_->text().trimmed();
        if (text.isEmpty()) {
            envStatusLabel_->setText(
                tr("Using embedded interpreter: %1")
                    .arg(QString::fromStdString(
                        pybridge::PythonEngine::instance().executable())));
            envStatusLabel_->setStyleSheet(QString());
        } else if (const QString python = resolveEnvironmentPython(text);
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

    auto* buttons = new QDialogButtonBox(this);
    auto* runButton = buttons->addButton(tr("Run"), QDialogButtonBox::AcceptRole);
    auto* saveButton = buttons->addButton(tr("Save Script…"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    runButton->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, this, &CalculatorDialog::saveScript);

    auto* left = new QVBoxLayout;
    left->addLayout(form);
    left->addWidget(envGroup);
    left->addStretch(1);

    auto* content = new QHBoxLayout;
    content->addLayout(left, 0);
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
            this, &CalculatorDialog::regenerateScript);
    previewColumn->addLayout(previewHeader);
    previewColumn->addWidget(preview_, 1);
    content->addLayout(previewColumn, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(content, 1);
    layout->addWidget(buttons);

    const auto refresh = [this] { refreshPreview(); };
    connect(orcaMethodCombo_, &QComboBox::currentTextChanged, this, refresh);
    connect(orcaBasisCombo_, &QComboBox::currentTextChanged, this, refresh);
    connect(chargeSpin_, &QSpinBox::valueChanged, this, refresh);
    connect(multiplicitySpin_, &QSpinBox::valueChanged, this, refresh);
    connect(orcaSolvationCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(orcaSolventEdit_, &QLineEdit::textChanged, this, refresh);
    connect(calculatorCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(taskCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(ensembleCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(fmaxSpin_, &QDoubleSpinBox::valueChanged, this, refresh);
    connect(maxStepsSpin_, &QSpinBox::valueChanged, this, refresh);
    connect(temperatureSpin_, &QDoubleSpinBox::valueChanged, this, refresh);
    connect(timestepSpin_, &QDoubleSpinBox::valueChanged, this, refresh);
    connect(mdStepsSpin_, &QSpinBox::valueChanged, this, refresh);
    connect(tautSpin_, &QDoubleSpinBox::valueChanged, this, refresh);
    connect(taupSpin_, &QDoubleSpinBox::valueChanged, this, refresh);
    connect(pressureSpin_, &QDoubleSpinBox::valueChanged, this, refresh);
    connect(cutoffSpin_, &QDoubleSpinBox::valueChanged, this, refresh);
    for (auto* spin : kptSpins_)
        connect(spin, &QSpinBox::valueChanged, this, refresh);
    connect(maceModelCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(maceSizeCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(maceModelPathEdit_, &QLineEdit::textChanged, this, refresh);
    connect(maceDeviceCombo_, &QComboBox::currentIndexChanged, this, refresh);

    refreshPreview();
}

core::CalculatorConfig CalculatorDialog::config() const
{
    core::CalculatorConfig c;
    c.calculator = static_cast<core::CalculatorKind>(calculatorCombo_->currentIndex());
    c.task = static_cast<core::TaskKind>(taskCombo_->currentIndex());
    c.ensemble = static_cast<core::MdEnsemble>(ensembleCombo_->currentIndex());
    c.fmax = fmaxSpin_->value();
    c.maxSteps = maxStepsSpin_->value();
    c.temperatureK = temperatureSpin_->value();
    c.timestepFs = timestepSpin_->value();
    c.mdSteps = mdStepsSpin_->value();
    c.tautFs = tautSpin_->value();
    c.taupFs = taupSpin_->value();
    c.pressureGPa = pressureSpin_->value();
    c.planeWaveCutoffEv = cutoffSpin_->value();
    for (int i = 0; i < 3; ++i)
        c.kpts[i] = kptSpins_[i]->value();
    c.maceSource = static_cast<core::MaceModelSource>(maceModelCombo_->currentIndex());
    c.maceSize = maceSizeCombo_->currentText().toStdString();
    c.maceModelPath = maceModelPathEdit_->text().trimmed().toStdString();
    c.maceDevice = maceDeviceCombo_->currentText().toStdString();
    c.orcaMethod = orcaMethodCombo_->currentText().trimmed().toStdString();
    c.orcaBasis = orcaBasisCombo_->currentText().trimmed().toStdString();
    c.charge = chargeSpin_->value();
    c.multiplicity = multiplicitySpin_->value();
    c.orcaSolvationModel = orcaSolvationCombo_->currentIndex() == 0
        ? std::string()
        : orcaSolvationCombo_->currentText().toStdString();
    c.orcaSolvent = orcaSolventEdit_->text().trimmed().toStdString();
    return c;
}

QString CalculatorDialog::script() const
{
    return preview_->toPlainText();
}

QString CalculatorDialog::pythonExecutable() const
{
    const QString resolved = resolveEnvironmentPython(envPathEdit_->text());
    if (!resolved.isEmpty())
        return resolved;
    return QString::fromStdString(pybridge::PythonEngine::instance().executable());
}

void CalculatorDialog::regenerateScript()
{
    manuallyEdited_ = false;
    editedNotice_->setVisible(false);
    refreshPreview();
}

void CalculatorDialog::browseEnvironmentDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Conda Environment Folder"));
    if (!dir.isEmpty())
        envPathEdit_->setText(dir);
}

void CalculatorDialog::browseEnvironmentPython()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Select Python Interpreter"));
    if (!file.isEmpty())
        envPathEdit_->setText(file);
}

void CalculatorDialog::refreshPreview()
{
    const core::CalculatorConfig c = config();
    const bool isOpt = c.task == core::TaskKind::GeometryOptimization;
    const bool isMd = c.task == core::TaskKind::MolecularDynamics;
    const bool isDft = c.calculator == core::CalculatorKind::QuantumEspresso
        || c.calculator == core::CalculatorKind::Vasp
        || c.calculator == core::CalculatorKind::Gpaw
        || c.calculator == core::CalculatorKind::Siesta;
    const bool isMace = c.calculator == core::CalculatorKind::Mace;
    const bool isCustomMace =
        isMace && c.maceSource == core::MaceModelSource::CustomFile;

    fmaxSpin_->setEnabled(isOpt);
    maxStepsSpin_->setEnabled(isOpt);
    ensembleCombo_->setEnabled(isMd);
    temperatureSpin_->setEnabled(
        isMd && core::isConstantTemperature(c.ensemble));
    timestepSpin_->setEnabled(isMd);
    mdStepsSpin_->setEnabled(isMd);
    const bool usesTaut = c.ensemble == core::MdEnsemble::BerendsenNVT
        || c.ensemble == core::MdEnsemble::NoseHooverChainNVT
        || c.ensemble == core::MdEnsemble::BerendsenNPT
        || c.ensemble == core::MdEnsemble::MelchionnaNPT;
    const bool isNpt = c.ensemble == core::MdEnsemble::BerendsenNPT
        || c.ensemble == core::MdEnsemble::MelchionnaNPT;
    tautSpin_->setEnabled(isMd && usesTaut);
    taupSpin_->setEnabled(isMd && isNpt);
    pressureSpin_->setEnabled(isMd && isNpt);
    cutoffSpin_->setEnabled(isDft);
    for (auto* spin : kptSpins_)
        spin->setEnabled(isDft);
    maceModelCombo_->setEnabled(isMace);
    maceSizeCombo_->setEnabled(isMace && !isCustomMace);
    maceModelPathEdit_->setEnabled(isCustomMace);
    maceBrowseButton_->setEnabled(isCustomMace);
    maceDeviceCombo_->setEnabled(isMace);

    // Never clobber the user's manual edits; "Regenerate" re-enables sync.
    if (manuallyEdited_)
        return;
    updatingPreview_ = true;
    // The job runner always stages the structure as structure.extxyz
    // inside the job directory, so the script refers to it relatively.
    preview_->setPlainText(QString::fromStdString(
        core::AseScriptGenerator::generate(config(), "structure.extxyz")));
    updatingPreview_ = false;
}

void CalculatorDialog::browseMaceModel()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select MACE Model"), QString(),
        tr("MACE models (*.model *.pt);;All files (*)"));
    if (!path.isEmpty())
        maceModelPathEdit_->setText(path); // textChanged refreshes the preview
}

void CalculatorDialog::saveScript()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save ASE Script"), QStringLiteral("run.py"),
        tr("Python scripts (*.py)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save Script"),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream(&file) << script();
}

} // namespace calango::gui
