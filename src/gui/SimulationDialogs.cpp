#include "gui/SimulationDialogs.hpp"

#include "core/AseScriptGenerator.hpp"
#include "gui/CalculatorDialog.hpp" // resolveEnvironmentPython (shared)
#include "gui/PythonHighlighter.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

const auto kEnvSettingsKey = QStringLiteral("jobs/environmentPath");

} // namespace

// ===========================================================================
// SimulationDialogBase
// ===========================================================================

SimulationDialogBase::SimulationDialogBase(QWidget* parent) : QDialog(parent) {}

void SimulationDialogBase::updateTaskEnabled(const core::CalculatorConfig&) {}

void SimulationDialogBase::buildUi()
{
    setWindowTitle(titleText());
    resize(880, 580);

    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(introText(), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* content = new QHBoxLayout;

    // ---- Left column: calculator + task + environment --------------------
    auto* left = new QVBoxLayout;

    auto* calcBox = new QGroupBox(tr("Calculator"), this);
    auto* calcForm = new QFormLayout(calcBox);
    buildCalculatorControls(calcForm);
    left->addWidget(calcBox);

    auto* taskBox = new QGroupBox(taskGroupTitle(), this);
    auto* taskForm = new QFormLayout(taskBox);
    buildTaskControls(taskForm);
    left->addWidget(taskBox);

    // Execution environment: a conda env (or any interpreter) the job runner
    // uses instead of the embedded Python. Mirrors CalculatorDialog and shares
    // its persisted setting so the choice follows the user across dialogs.
    auto* envGroup = new QGroupBox(tr("Execution Environment"), this);
    auto* envLayout = new QVBoxLayout(envGroup);
    auto* envRow = new QHBoxLayout;
    envPathEdit_ = new QLineEdit(envGroup);
    envPathEdit_->setPlaceholderText(
        tr("conda env folder or python executable (empty = embedded)"));
    auto* envDirButton = new QPushButton(tr("Env Folder…"), envGroup);
    auto* envFileButton = new QPushButton(tr("Python…"), envGroup);
    envRow->addWidget(envPathEdit_, 1);
    envRow->addWidget(envDirButton);
    envRow->addWidget(envFileButton);
    envLayout->addLayout(envRow);
    envStatusLabel_ = new QLabel(envGroup);
    envStatusLabel_->setWordWrap(true);
    envLayout->addWidget(envStatusLabel_);
    connect(envDirButton, &QPushButton::clicked, this,
            &SimulationDialogBase::browseEnvironmentDir);
    connect(envFileButton, &QPushButton::clicked, this,
            &SimulationDialogBase::browseEnvironmentPython);

    const auto updateEnvStatus = [this] {
        const QString text = envPathEdit_->text().trimmed();
        if (text.isEmpty()) {
            envStatusLabel_->setText(
                tr("Using embedded interpreter: %1")
                    .arg(QString::fromStdString(
                        pybridge::PythonEngine::instance().executable())));
            envStatusLabel_->setStyleSheet(QString());
        } else if (const QString python =
                       CalculatorDialog::resolveEnvironmentPython(text);
                   !python.isEmpty()) {
            envStatusLabel_->setText(tr("Jobs will run with: %1").arg(python));
            envStatusLabel_->setStyleSheet(QString());
        } else {
            envStatusLabel_->setText(
                tr("No python interpreter found at this path."));
            envStatusLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        }
        QSettings().setValue(kEnvSettingsKey, envPathEdit_->text());
    };
    connect(envPathEdit_, &QLineEdit::textChanged, this, updateEnvStatus);
    left->addWidget(envGroup);
    left->addStretch(1);
    content->addLayout(left, 0);

    // ---- Right column: live script preview -------------------------------
    preview_ = new QPlainTextEdit(this);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new PythonHighlighter(preview_->document()); // parented to the document
    connect(preview_, &QPlainTextEdit::textChanged, this, [this] {
        if (updatingPreview_ || manuallyEdited_)
            return;
        manuallyEdited_ = true;
        editedNotice_->setVisible(true);
    });

    auto* previewColumn = new QVBoxLayout;
    auto* previewHeader = new QHBoxLayout;
    previewHeader->addWidget(
        new QLabel(tr("Generated ASE script (editable):"), this));
    previewHeader->addStretch(1);
    editedNotice_ = new QLabel(tr("edited — form sync paused"), this);
    editedNotice_->setStyleSheet(QStringLiteral("color: #b07d2a;"));
    editedNotice_->setVisible(false);
    previewHeader->addWidget(editedNotice_);
    auto* regenerateButton = new QPushButton(tr("Regenerate"), this);
    regenerateButton->setToolTip(
        tr("Discard manual edits and regenerate from the form"));
    connect(regenerateButton, &QPushButton::clicked, this,
            &SimulationDialogBase::regenerateScript);
    previewHeader->addWidget(regenerateButton);
    previewColumn->addLayout(previewHeader);
    previewColumn->addWidget(preview_, 1);
    content->addLayout(previewColumn, 1);

    root->addLayout(content, 1);

    // ---- Buttons ---------------------------------------------------------
    auto* buttons = new QDialogButtonBox(this);
    auto* runButton = buttons->addButton(tr("Run"), QDialogButtonBox::AcceptRole);
    auto* saveButton =
        buttons->addButton(tr("Save Script…"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    runButton->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, this,
            &SimulationDialogBase::saveScript);
    root->addWidget(buttons);

    // Seed the environment field (fires updateEnvStatus) and the preview.
    envPathEdit_->setText(QSettings().value(kEnvSettingsKey).toString());
    updateEnvStatus();
    refreshPreview();
}

void SimulationDialogBase::buildCalculatorControls(QFormLayout* form)
{
    // Order mirrors core::CalculatorKind.
    calculatorCombo_ = new QComboBox(this);
    calculatorCombo_->addItems(
        {tr("EMT (effective medium theory — fast test potential)"),
         tr("Lennard-Jones"),
         tr("Quantum ESPRESSO (DFT, requires pw.x)"),
         tr("VASP (DFT, requires license)"),
         tr("MACE (machine-learning potential, requires mace-torch)"),
         tr("GPAW (DFT, python package)"),
         tr("SIESTA (DFT, requires siesta binary)"),
         tr("ORCA (quantum chemistry, requires orca binary)")});
    form->addRow(tr("Calculator:"), calculatorCombo_);
    watch(calculatorCombo_);

    cutoffSpin_ = new QDoubleSpinBox(this);
    cutoffSpin_->setRange(100.0, 2000.0);
    cutoffSpin_->setValue(550.0);
    cutoffSpin_->setSuffix(tr(" eV"));
    form->addRow(tr("Plane-wave cutoff:"), cutoffSpin_);
    watch(cutoffSpin_);

    auto* kptRow = new QHBoxLayout;
    for (auto*& spin : kptSpins_) {
        spin = new QSpinBox(this);
        spin->setRange(1, 64);
        spin->setValue(4);
        kptRow->addWidget(spin);
        watch(spin);
    }
    form->addRow(tr("k-point grid:"), kptRow);

    maceModelCombo_ = new QComboBox(this);
    maceModelCombo_->addItems({tr("MACE-MP-0 (universal, materials)"),
                               tr("MACE-OFF (universal, organic molecules)"),
                               tr("Custom trained model (file)")});
    form->addRow(tr("MACE model:"), maceModelCombo_);
    watch(maceModelCombo_);

    maceSizeCombo_ = new QComboBox(this);
    maceSizeCombo_->addItems({QStringLiteral("small"), QStringLiteral("medium"),
                              QStringLiteral("large")});
    maceSizeCombo_->setCurrentIndex(1);
    form->addRow(tr("MACE model size:"), maceSizeCombo_);
    watch(maceSizeCombo_);

    maceModelPathEdit_ = new QLineEdit(this);
    maceModelPathEdit_->setPlaceholderText(tr("path/to/model.model or .pt"));
    maceBrowseButton_ = new QPushButton(tr("Browse…"), this);
    auto* macePathRow = new QHBoxLayout;
    macePathRow->addWidget(maceModelPathEdit_, 1);
    macePathRow->addWidget(maceBrowseButton_);
    connect(maceBrowseButton_, &QPushButton::clicked, this,
            &SimulationDialogBase::browseMaceModel);
    form->addRow(tr("Custom model file:"), macePathRow);
    watch(maceModelPathEdit_);

    maceDeviceCombo_ = new QComboBox(this);
    maceDeviceCombo_->addItems({QStringLiteral("cpu"), QStringLiteral("cuda"),
                                QStringLiteral("mps")});
    form->addRow(tr("MACE device:"), maceDeviceCombo_);
    watch(maceDeviceCombo_);

    orcaMethodCombo_ = new QComboBox(this);
    orcaMethodCombo_->setEditable(true);
    orcaMethodCombo_->addItems({QStringLiteral("B3LYP"), QStringLiteral("PBE0"),
                                QStringLiteral("r2SCAN"),
                                QStringLiteral("wB97X-D3"), QStringLiteral("HF"),
                                QStringLiteral("MP2")});
    form->addRow(tr("ORCA method:"), orcaMethodCombo_);
    connect(orcaMethodCombo_, &QComboBox::currentTextChanged, this,
            &SimulationDialogBase::refreshPreview);

    orcaBasisCombo_ = new QComboBox(this);
    orcaBasisCombo_->setEditable(true);
    orcaBasisCombo_->addItems({QStringLiteral("def2-SVP"),
                               QStringLiteral("def2-TZVP"),
                               QStringLiteral("def2-TZVPP"),
                               QStringLiteral("cc-pVDZ"),
                               QStringLiteral("cc-pVTZ")});
    form->addRow(tr("ORCA basis:"), orcaBasisCombo_);
    connect(orcaBasisCombo_, &QComboBox::currentTextChanged, this,
            &SimulationDialogBase::refreshPreview);

    chargeSpin_ = new QSpinBox(this);
    chargeSpin_->setRange(-10, 10);
    form->addRow(tr("Charge:"), chargeSpin_);
    watch(chargeSpin_);

    multiplicitySpin_ = new QSpinBox(this);
    multiplicitySpin_->setRange(1, 11);
    multiplicitySpin_->setValue(1);
    multiplicitySpin_->setToolTip(tr("Spin multiplicity 2S+1"));
    form->addRow(tr("Multiplicity:"), multiplicitySpin_);
    watch(multiplicitySpin_);

    orcaSolvationCombo_ = new QComboBox(this);
    orcaSolvationCombo_->addItems(
        {tr("None (gas phase)"), QStringLiteral("CPCM"), QStringLiteral("SMD")});
    form->addRow(tr("Solvation:"), orcaSolvationCombo_);
    watch(orcaSolvationCombo_);

    orcaSolventEdit_ = new QLineEdit(QStringLiteral("water"), this);
    orcaSolventEdit_->setToolTip(
        tr("Solvent name for CPCM/SMD (water, acetonitrile, toluene, ...)"));
    form->addRow(tr("Solvent:"), orcaSolventEdit_);
    watch(orcaSolventEdit_);
}

void SimulationDialogBase::watch(QWidget* widget)
{
    if (auto* combo = qobject_cast<QComboBox*>(widget))
        connect(combo, &QComboBox::currentIndexChanged, this,
                &SimulationDialogBase::refreshPreview);
    else if (auto* spin = qobject_cast<QSpinBox*>(widget))
        connect(spin, &QSpinBox::valueChanged, this,
                &SimulationDialogBase::refreshPreview);
    else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(widget))
        connect(dspin, &QDoubleSpinBox::valueChanged, this,
                &SimulationDialogBase::refreshPreview);
    else if (auto* edit = qobject_cast<QLineEdit*>(widget))
        connect(edit, &QLineEdit::textChanged, this,
                &SimulationDialogBase::refreshPreview);
}

core::CalculatorConfig SimulationDialogBase::config() const
{
    core::CalculatorConfig c;
    c.calculator =
        static_cast<core::CalculatorKind>(calculatorCombo_->currentIndex());
    c.planeWaveCutoffEv = cutoffSpin_->value();
    for (int i = 0; i < 3; ++i)
        c.kpts[i] = kptSpins_[i]->value();
    c.maceSource =
        static_cast<core::MaceModelSource>(maceModelCombo_->currentIndex());
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

    c.task = taskKind();
    applyTaskConfig(c);
    return c;
}

QString SimulationDialogBase::script() const
{
    return preview_->toPlainText();
}

QString SimulationDialogBase::pythonExecutable() const
{
    const QString resolved =
        CalculatorDialog::resolveEnvironmentPython(envPathEdit_->text());
    if (!resolved.isEmpty())
        return resolved;
    return QString::fromStdString(pybridge::PythonEngine::instance().executable());
}

void SimulationDialogBase::updateCalculatorEnabled(const core::CalculatorConfig& c)
{
    const bool isDft = c.calculator == core::CalculatorKind::QuantumEspresso
        || c.calculator == core::CalculatorKind::Vasp
        || c.calculator == core::CalculatorKind::Gpaw
        || c.calculator == core::CalculatorKind::Siesta;
    const bool isMace = c.calculator == core::CalculatorKind::Mace;
    const bool isCustomMace =
        isMace && c.maceSource == core::MaceModelSource::CustomFile;
    const bool isOrca = c.calculator == core::CalculatorKind::Orca;

    cutoffSpin_->setEnabled(isDft);
    for (auto* spin : kptSpins_)
        spin->setEnabled(isDft);
    maceModelCombo_->setEnabled(isMace);
    maceSizeCombo_->setEnabled(isMace && !isCustomMace);
    maceModelPathEdit_->setEnabled(isCustomMace);
    maceBrowseButton_->setEnabled(isCustomMace);
    maceDeviceCombo_->setEnabled(isMace);
    orcaMethodCombo_->setEnabled(isOrca);
    orcaBasisCombo_->setEnabled(isOrca);
    chargeSpin_->setEnabled(isOrca);
    multiplicitySpin_->setEnabled(isOrca);
    orcaSolvationCombo_->setEnabled(isOrca);
    orcaSolventEdit_->setEnabled(isOrca);
}

void SimulationDialogBase::refreshPreview()
{
    const core::CalculatorConfig c = config();
    updateCalculatorEnabled(c);
    updateTaskEnabled(c);

    // Never clobber the user's manual edits; "Regenerate" re-enables sync.
    if (manuallyEdited_)
        return;
    updatingPreview_ = true;
    // The job runner always stages the structure as structure.extxyz inside
    // the job directory, so the script refers to it relatively.
    preview_->setPlainText(QString::fromStdString(
        core::AseScriptGenerator::generate(c, "structure.extxyz")));
    updatingPreview_ = false;
}

void SimulationDialogBase::regenerateScript()
{
    manuallyEdited_ = false;
    editedNotice_->setVisible(false);
    refreshPreview();
}

void SimulationDialogBase::browseEnvironmentDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Conda Environment Folder"));
    if (!dir.isEmpty())
        envPathEdit_->setText(dir);
}

void SimulationDialogBase::browseEnvironmentPython()
{
    const QString file =
        QFileDialog::getOpenFileName(this, tr("Select Python Interpreter"));
    if (!file.isEmpty())
        envPathEdit_->setText(file);
}

void SimulationDialogBase::browseMaceModel()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select MACE Model"), QString(),
        tr("MACE models (*.model *.pt);;All files (*)"));
    if (!path.isEmpty())
        maceModelPathEdit_->setText(path); // textChanged refreshes the preview
}

void SimulationDialogBase::saveScript()
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

// ===========================================================================
// SinglePointDialog
// ===========================================================================

SinglePointDialog::SinglePointDialog(QWidget* parent) : SimulationDialogBase(parent)
{
    buildUi();
}

QString SinglePointDialog::titleText() const
{
    return tr("Single-point Calculation");
}

QString SinglePointDialog::introText() const
{
    return tr("Static evaluation of energy, forces and electronic properties "
              "at the current geometry. For DFT backends, set the k-point mesh "
              "and plane-wave cutoff in the Calculator group and the electronic "
              "convergence below.");
}

QString SinglePointDialog::taskGroupTitle() const
{
    return tr("Electronic convergence");
}

void SinglePointDialog::buildTaskControls(QFormLayout* form)
{
    scfStepsSpin_ = new QSpinBox(this);
    scfStepsSpin_->setRange(1, 100000);
    scfStepsSpin_->setValue(100);
    scfStepsSpin_->setToolTip(
        tr("Maximum self-consistent-field iterations (DFT backends)."));
    form->addRow(tr("Max electronic (SCF) steps:"), scfStepsSpin_);
    watch(scfStepsSpin_);

    scfTolSpin_ = new QDoubleSpinBox(this);
    scfTolSpin_->setDecimals(8);
    scfTolSpin_->setRange(1e-8, 1.0);
    scfTolSpin_->setSingleStep(1e-5);
    scfTolSpin_->setValue(1e-4);
    scfTolSpin_->setSuffix(tr(" eV"));
    scfTolSpin_->setToolTip(tr("Electronic-energy convergence threshold."));
    form->addRow(tr("SCF energy tolerance:"), scfTolSpin_);
    watch(scfTolSpin_);
}

void SinglePointDialog::applyTaskConfig(core::CalculatorConfig& c) const
{
    c.scfMaxSteps = scfStepsSpin_->value();
    c.scfEnergyTolEv = scfTolSpin_->value();
}

// ===========================================================================
// GeometryOptimizationDialog
// ===========================================================================

GeometryOptimizationDialog::GeometryOptimizationDialog(QWidget* parent)
    : SimulationDialogBase(parent)
{
    buildUi();
}

QString GeometryOptimizationDialog::titleText() const
{
    return tr("Geometry Optimization");
}

QString GeometryOptimizationDialog::introText() const
{
    return tr("Relax the atomic positions to a local energy minimum. The run "
              "streams frames to the viewport and writes optimized.extxyz on "
              "completion.");
}

QString GeometryOptimizationDialog::taskGroupTitle() const
{
    return tr("Relaxation settings");
}

void GeometryOptimizationDialog::buildTaskControls(QFormLayout* form)
{
    optimizerCombo_ = new QComboBox(this);
    // Order mirrors core::Optimizer.
    optimizerCombo_->addItems({tr("BFGS (quasi-Newton, robust default)"),
                               tr("LBFGS (limited-memory, large systems)"),
                               tr("FIRE (inertial, no Hessian)"),
                               tr("GPMin (Gaussian-process, few steps)"),
                               tr("MDMin (velocity-quench MD)")});
    form->addRow(tr("Optimizer:"), optimizerCombo_);
    watch(optimizerCombo_);

    fmaxSpin_ = new QDoubleSpinBox(this);
    fmaxSpin_->setRange(0.001, 1.0);
    fmaxSpin_->setDecimals(3);
    fmaxSpin_->setSingleStep(0.01);
    fmaxSpin_->setValue(0.05);
    fmaxSpin_->setSuffix(tr(" eV/Å"));
    form->addRow(tr("Force convergence (fmax):"), fmaxSpin_);
    watch(fmaxSpin_);

    maxStepsSpin_ = new QSpinBox(this);
    maxStepsSpin_->setRange(1, 100000);
    maxStepsSpin_->setValue(200);
    form->addRow(tr("Max optimization steps:"), maxStepsSpin_);
    watch(maxStepsSpin_);
}

void GeometryOptimizationDialog::applyTaskConfig(core::CalculatorConfig& c) const
{
    c.optimizer = static_cast<core::Optimizer>(optimizerCombo_->currentIndex());
    c.fmax = fmaxSpin_->value();
    c.maxSteps = maxStepsSpin_->value();
}

// ===========================================================================
// MolecularDynamicsDialog
// ===========================================================================

MolecularDynamicsDialog::MolecularDynamicsDialog(QWidget* parent)
    : SimulationDialogBase(parent)
{
    buildUi();
}

QString MolecularDynamicsDialog::titleText() const
{
    return tr("Molecular Dynamics");
}

QString MolecularDynamicsDialog::introText() const
{
    return tr("Integrate the equations of motion in the chosen ensemble. "
              "Frames stream to the viewport; thermostat/barostat controls "
              "enable with the ensemble.");
}

QString MolecularDynamicsDialog::taskGroupTitle() const
{
    return tr("Dynamics settings");
}

void MolecularDynamicsDialog::buildTaskControls(QFormLayout* form)
{
    ensembleCombo_ = new QComboBox(this);
    // Order mirrors core::MdEnsemble.
    ensembleCombo_->addItems(
        {tr("NVE — Velocity Verlet"), tr("NVT — Langevin dynamics"),
         tr("NVT — Andersen thermostat"), tr("NVT — Berendsen thermostat"),
         tr("NVT — Nosé–Hoover chain"), tr("NPT — Berendsen"),
         tr("NPT — Nosé–Hoover / Parrinello–Rahman (Melchionna)")});
    ensembleCombo_->setCurrentIndex(1); // Langevin default
    form->addRow(tr("Ensemble:"), ensembleCombo_);
    watch(ensembleCombo_);

    temperatureSpin_ = new QDoubleSpinBox(this);
    temperatureSpin_->setRange(0.0, 10000.0);
    temperatureSpin_->setValue(300.0);
    temperatureSpin_->setSuffix(tr(" K"));
    form->addRow(tr("Temperature:"), temperatureSpin_);
    watch(temperatureSpin_);

    timestepSpin_ = new QDoubleSpinBox(this);
    timestepSpin_->setRange(0.01, 20.0);
    timestepSpin_->setValue(1.0);
    timestepSpin_->setSuffix(tr(" fs"));
    form->addRow(tr("Timestep:"), timestepSpin_);
    watch(timestepSpin_);

    mdStepsSpin_ = new QSpinBox(this);
    mdStepsSpin_->setRange(1, 10000000);
    mdStepsSpin_->setValue(1000);
    form->addRow(tr("MD steps:"), mdStepsSpin_);
    watch(mdStepsSpin_);

    frictionSpin_ = new QDoubleSpinBox(this);
    frictionSpin_->setDecimals(4);
    frictionSpin_->setRange(0.0001, 10.0);
    frictionSpin_->setSingleStep(0.005);
    frictionSpin_->setValue(0.01);
    frictionSpin_->setSuffix(tr(" fs⁻¹"));
    frictionSpin_->setToolTip(tr("Langevin friction coefficient."));
    form->addRow(tr("Langevin friction:"), frictionSpin_);
    watch(frictionSpin_);

    tautSpin_ = new QDoubleSpinBox(this);
    tautSpin_->setRange(1.0, 100000.0);
    tautSpin_->setValue(100.0);
    tautSpin_->setSuffix(tr(" fs"));
    tautSpin_->setToolTip(tr("Thermostat coupling time (taut / tdamp / ttime)."));
    form->addRow(tr("Thermostat coupling:"), tautSpin_);
    watch(tautSpin_);

    taupSpin_ = new QDoubleSpinBox(this);
    taupSpin_->setRange(1.0, 1000000.0);
    taupSpin_->setValue(1000.0);
    taupSpin_->setSuffix(tr(" fs"));
    taupSpin_->setToolTip(tr("Barostat coupling time (taup / ptime)."));
    form->addRow(tr("Barostat coupling:"), taupSpin_);
    watch(taupSpin_);

    pressureSpin_ = new QDoubleSpinBox(this);
    pressureSpin_->setRange(-10.0, 500.0);
    pressureSpin_->setDecimals(4);
    pressureSpin_->setValue(0.0);
    pressureSpin_->setSuffix(tr(" GPa"));
    pressureSpin_->setToolTip(tr("External pressure for NPT (0 ≈ ambient)."));
    form->addRow(tr("Pressure:"), pressureSpin_);
    watch(pressureSpin_);
}

void MolecularDynamicsDialog::applyTaskConfig(core::CalculatorConfig& c) const
{
    c.ensemble = static_cast<core::MdEnsemble>(ensembleCombo_->currentIndex());
    c.temperatureK = temperatureSpin_->value();
    c.timestepFs = timestepSpin_->value();
    c.mdSteps = mdStepsSpin_->value();
    c.frictionPerFs = frictionSpin_->value();
    c.tautFs = tautSpin_->value();
    c.taupFs = taupSpin_->value();
    c.pressureGPa = pressureSpin_->value();
}

void MolecularDynamicsDialog::updateTaskEnabled(const core::CalculatorConfig& c)
{
    temperatureSpin_->setEnabled(core::isConstantTemperature(c.ensemble));
    frictionSpin_->setEnabled(c.ensemble == core::MdEnsemble::LangevinNVT);
    const bool usesTaut = c.ensemble == core::MdEnsemble::BerendsenNVT
        || c.ensemble == core::MdEnsemble::NoseHooverChainNVT
        || c.ensemble == core::MdEnsemble::BerendsenNPT
        || c.ensemble == core::MdEnsemble::MelchionnaNPT;
    tautSpin_->setEnabled(usesTaut);
    taupSpin_->setEnabled(core::isConstantPressure(c.ensemble));
    pressureSpin_->setEnabled(core::isConstantPressure(c.ensemble));
}

} // namespace calango::gui
