#include "gui/MolecularDynamicsWizard.hpp"

#include "core/AseScriptGenerator.hpp"
#include "gui/CalculatorDialog.hpp" // resolveEnvironmentPython (shared)
#include "gui/PythonHighlighter.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QComboBox>
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
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {
const auto kEnvSettingsKey = QStringLiteral("jobs/environmentPath");
} // namespace

MolecularDynamicsWizard::MolecularDynamicsWizard(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Molecular Dynamics Setup"));
    resize(900, 640);

    auto* root = new QVBoxLayout(this);
    headerLabel_ = new QLabel(this);
    QFont hf = headerLabel_->font();
    hf.setPointSizeF(hf.pointSizeF() * 1.15);
    hf.setBold(true);
    headerLabel_->setFont(hf);
    root->addWidget(headerLabel_);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(buildDynamicsPage());
    stack_->addWidget(buildEnvironmentPage());
    stack_->addWidget(buildCalculatorPage());
    stack_->addWidget(buildReviewPage());
    root->addWidget(stack_, 1);

    // --- Action bar --------------------------------------------------------
    auto* bar = new QHBoxLayout;
    backButton_ = new QPushButton(tr("‹ Back"), this);
    auto* cancelButton = new QPushButton(tr("Cancel"), this);
    nextButton_ = new QPushButton(tr("Next ›"), this);
    exportButton_ = new QPushButton(tr("Export Script…"), this);
    runRemoteButton_ = new QPushButton(tr("Run (Remote)"), this);
    runLocalButton_ = new QPushButton(tr("Run (Local)"), this);
    runLocalButton_->setDefault(true);
    bar->addWidget(backButton_);
    bar->addStretch(1);
    bar->addWidget(cancelButton);
    bar->addWidget(exportButton_);
    bar->addWidget(runRemoteButton_);
    bar->addWidget(nextButton_);
    bar->addWidget(runLocalButton_);
    root->addLayout(bar);

    connect(backButton_, &QPushButton::clicked, this, &MolecularDynamicsWizard::goBack);
    connect(nextButton_, &QPushButton::clicked, this, &MolecularDynamicsWizard::goNext);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(exportButton_, &QPushButton::clicked, this,
            &MolecularDynamicsWizard::exportScript);
    connect(runLocalButton_, &QPushButton::clicked, this, [this] {
        action_ = Action::RunLocal;
        accept();
    });
    connect(runRemoteButton_, &QPushButton::clicked, this, [this] {
        action_ = Action::RunRemote;
        accept();
    });

    updateEnsembleEnabled();
    updateCalculatorEnabled();
    updateStage();
}

// ---------------------------------------------------------------------------
// Stage 1 — Dynamics Settings
// ---------------------------------------------------------------------------
QWidget* MolecularDynamicsWizard::buildDynamicsPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    ensembleCombo_ = new QComboBox(page);
    // Order mirrors core::MdEnsemble.
    ensembleCombo_->addItems({tr("NVE — Velocity Verlet (microcanonical)"),
                              tr("NVT — Langevin"),
                              tr("NVT — Andersen"),
                              tr("NVT — Berendsen"),
                              tr("NVT — Nosé–Hoover chain"),
                              tr("NPT — Berendsen"),
                              tr("NPT — Parrinello–Rahman (Nosé–Hoover)")});
    ensembleCombo_->setCurrentIndex(1);
    form->addRow(tr("Ensemble:"), ensembleCombo_);
    connect(ensembleCombo_, &QComboBox::currentIndexChanged, this,
            &MolecularDynamicsWizard::updateEnsembleEnabled);

    temperatureSpin_ = new QDoubleSpinBox(page);
    temperatureSpin_->setRange(0.0, 100000.0);
    temperatureSpin_->setValue(300.0);
    temperatureSpin_->setSuffix(tr(" K"));
    form->addRow(tr("Temperature:"), temperatureSpin_);

    pressureSpin_ = new QDoubleSpinBox(page);
    pressureSpin_->setRange(0.0, 1.0e7);
    pressureSpin_->setDecimals(3);
    pressureSpin_->setValue(1.0);
    pressureSpin_->setSuffix(tr(" bar"));
    pressureSpin_->setToolTip(tr("External pressure for NPT ensembles."));
    form->addRow(tr("Pressure:"), pressureSpin_);

    timestepSpin_ = new QDoubleSpinBox(page);
    timestepSpin_->setRange(0.01, 20.0);
    timestepSpin_->setValue(1.0);
    timestepSpin_->setSuffix(tr(" fs"));
    form->addRow(tr("Time step:"), timestepSpin_);

    frictionSpin_ = new QDoubleSpinBox(page);
    frictionSpin_->setDecimals(4);
    frictionSpin_->setRange(0.0001, 10.0);
    frictionSpin_->setSingleStep(0.005);
    frictionSpin_->setValue(0.01);
    frictionSpin_->setSuffix(tr(" fs⁻¹"));
    frictionSpin_->setToolTip(tr("Langevin friction coefficient."));
    form->addRow(tr("Friction (Langevin):"), frictionSpin_);

    tautSpin_ = new QDoubleSpinBox(page);
    tautSpin_->setRange(1.0, 100000.0);
    tautSpin_->setValue(100.0);
    tautSpin_->setSuffix(tr(" fs"));
    tautSpin_->setToolTip(tr("Thermostat coupling / damping time."));
    form->addRow(tr("Thermostat coupling:"), tautSpin_);

    taupSpin_ = new QDoubleSpinBox(page);
    taupSpin_->setRange(1.0, 1000000.0);
    taupSpin_->setValue(1000.0);
    taupSpin_->setSuffix(tr(" fs"));
    taupSpin_->setToolTip(tr("Barostat coupling time (NPT)."));
    form->addRow(tr("Barostat coupling:"), taupSpin_);

    stepsSpin_ = new QSpinBox(page);
    stepsSpin_->setRange(1, 100000000);
    stepsSpin_->setValue(1000);
    form->addRow(tr("Total steps:"), stepsSpin_);

    sampleSpin_ = new QSpinBox(page);
    sampleSpin_->setRange(0, 1000000);
    sampleSpin_->setValue(0);
    sampleSpin_->setSpecialValueText(tr("auto (~400 frames)"));
    sampleSpin_->setToolTip(tr("Record a trajectory frame + metrics every N "
                               "steps (0 = auto)."));
    form->addRow(tr("Sampling frequency:"), sampleSpin_);

    return page;
}

// ---------------------------------------------------------------------------
// Stage 2 — Calculator & Execution Environment
// ---------------------------------------------------------------------------
QWidget* MolecularDynamicsWizard::buildEnvironmentPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    calcCombo_ = new QComboBox(page);
    calcCombo_->addItem(tr("MACE (ML potential)"),
                        static_cast<int>(core::CalculatorKind::Mace));
    calcCombo_->addItem(tr("Quantum ESPRESSO (DFT)"),
                        static_cast<int>(core::CalculatorKind::QuantumEspresso));
    calcCombo_->addItem(tr("SIESTA (DFT)"),
                        static_cast<int>(core::CalculatorKind::Siesta));
    calcCombo_->addItem(tr("ORCA (quantum chemistry)"),
                        static_cast<int>(core::CalculatorKind::Orca));
    calcCombo_->addItem(tr("GPAW (DFT)"),
                        static_cast<int>(core::CalculatorKind::Gpaw));
    calcCombo_->addItem(tr("VASP (DFT)"),
                        static_cast<int>(core::CalculatorKind::Vasp));
    calcCombo_->addItem(tr("EMT (fast test potential)"),
                        static_cast<int>(core::CalculatorKind::EMT));
    calcCombo_->addItem(tr("ASAP (fast EMT / OpenKIM)"),
                        static_cast<int>(core::CalculatorKind::Asap));
    calcCombo_->addItem(tr("Lennard-Jones"),
                        static_cast<int>(core::CalculatorKind::LennardJones));
    form->addRow(tr("Calculation engine:"), calcCombo_);
    connect(calcCombo_, &QComboBox::currentIndexChanged, this,
            &MolecularDynamicsWizard::updateCalculatorEnabled);

    auto* envGroup = new QGroupBox(tr("Execution environment"), page);
    auto* envLayout = new QVBoxLayout(envGroup);
    auto* envRow = new QHBoxLayout;
    envEdit_ = new QLineEdit(envGroup);
    envEdit_->setPlaceholderText(
        tr("Conda env folder or python executable (empty = embedded)"));
    envEdit_->setText(QSettings().value(kEnvSettingsKey).toString());
    auto* envButton = new QPushButton(tr("Browse…"), envGroup);
    envRow->addWidget(envEdit_, 1);
    envRow->addWidget(envButton);
    envLayout->addLayout(envRow);
    envStatus_ = new QLabel(envGroup);
    envStatus_->setWordWrap(true);
    envLayout->addWidget(envStatus_);
    layout->addWidget(envGroup);
    connect(envButton, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select Conda Environment Folder"));
        if (!dir.isEmpty())
            envEdit_->setText(dir);
    });
    connect(envEdit_, &QLineEdit::textChanged, this, [this] {
        const QString python =
            CalculatorDialog::resolveEnvironmentPython(envEdit_->text());
        if (envEdit_->text().trimmed().isEmpty())
            envStatus_->setText(tr("Using embedded interpreter: %1")
                                    .arg(QString::fromStdString(
                                        pybridge::PythonEngine::instance().executable())));
        else if (!python.isEmpty())
            envStatus_->setText(tr("Runs will use: %1").arg(python));
        else
            envStatus_->setText(tr("No python interpreter found at this path."));
        QSettings().setValue(kEnvSettingsKey, envEdit_->text());
    });

    auto* modeGroup = new QGroupBox(tr("Execution mode"), page);
    auto* modeLayout = new QVBoxLayout(modeGroup);
    localRadio_ = new QRadioButton(tr("Local (background process on this machine)"),
                                   modeGroup);
    remoteRadio_ = new QRadioButton(
        tr("Remote (submit to the Remote Access manager / HPC queue)"), modeGroup);
    localRadio_->setChecked(true);
    modeLayout->addWidget(localRadio_);
    modeLayout->addWidget(remoteRadio_);
    layout->addWidget(modeGroup);
    // Highlight the matching Run button as the mode changes.
    connect(localRadio_, &QRadioButton::toggled, this, [this] { updateStage(); });

    layout->addStretch(1);
    envEdit_->textChanged(envEdit_->text()); // seed the status label
    return page;
}

// ---------------------------------------------------------------------------
// Stage 3 — Calculator Settings
// ---------------------------------------------------------------------------
QWidget* MolecularDynamicsWizard::buildCalculatorPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    calcSettingsHint_ = new QLabel(page);
    calcSettingsHint_->setWordWrap(true);
    layout->addWidget(calcSettingsHint_);

    // DFT knobs.
    dftGroup_ = new QGroupBox(tr("DFT settings"), page);
    auto* dftForm = new QFormLayout(dftGroup_);
    cutoffSpin_ = new QDoubleSpinBox(dftGroup_);
    cutoffSpin_->setRange(100.0, 2000.0);
    cutoffSpin_->setValue(550.0);
    cutoffSpin_->setSuffix(tr(" eV"));
    dftForm->addRow(tr("Plane-wave cutoff:"), cutoffSpin_);
    auto* kptRow = new QHBoxLayout;
    for (auto*& spin : kptSpins_) {
        spin = new QSpinBox(dftGroup_);
        spin->setRange(1, 64);
        spin->setValue(4);
        kptRow->addWidget(spin);
    }
    dftForm->addRow(tr("k-point grid:"), kptRow);
    dftForm->addRow(new QLabel(
        tr("XC functional defaults to PBE in the script (editable in Stage 4)."),
        dftGroup_));
    layout->addWidget(dftGroup_);

    // MACE knobs.
    maceGroup_ = new QGroupBox(tr("MACE settings"), page);
    auto* maceForm = new QFormLayout(maceGroup_);
    maceModelCombo_ = new QComboBox(maceGroup_);
    maceModelCombo_->addItems({tr("MACE-MP-0 (materials)"),
                               tr("MACE-OFF (organic molecules)"),
                               tr("Custom trained model")});
    maceForm->addRow(tr("Model:"), maceModelCombo_);
    maceSizeCombo_ = new QComboBox(maceGroup_);
    maceSizeCombo_->addItems({QStringLiteral("small"), QStringLiteral("medium"),
                              QStringLiteral("large")});
    maceSizeCombo_->setCurrentIndex(1);
    maceForm->addRow(tr("Model size:"), maceSizeCombo_);
    maceDeviceCombo_ = new QComboBox(maceGroup_);
    maceDeviceCombo_->addItems({QStringLiteral("cpu"), QStringLiteral("cuda (GPU)"),
                                QStringLiteral("mps (Apple GPU)")});
    maceForm->addRow(tr("Device / GPU:"), maceDeviceCombo_);
    layout->addWidget(maceGroup_);

    // ORCA knobs.
    orcaGroup_ = new QGroupBox(tr("ORCA settings"), page);
    auto* orcaForm = new QFormLayout(orcaGroup_);
    orcaMethodCombo_ = new QComboBox(orcaGroup_);
    orcaMethodCombo_->setEditable(true);
    orcaMethodCombo_->addItems({QStringLiteral("B3LYP"), QStringLiteral("PBE0"),
                                QStringLiteral("r2SCAN"), QStringLiteral("HF")});
    orcaForm->addRow(tr("Method / functional:"), orcaMethodCombo_);
    orcaBasisCombo_ = new QComboBox(orcaGroup_);
    orcaBasisCombo_->setEditable(true);
    orcaBasisCombo_->addItems({QStringLiteral("def2-SVP"),
                               QStringLiteral("def2-TZVP"),
                               QStringLiteral("cc-pVDZ")});
    orcaForm->addRow(tr("Basis set:"), orcaBasisCombo_);
    chargeSpin_ = new QSpinBox(orcaGroup_);
    chargeSpin_->setRange(-10, 10);
    orcaForm->addRow(tr("Charge:"), chargeSpin_);
    multiplicitySpin_ = new QSpinBox(orcaGroup_);
    multiplicitySpin_->setRange(1, 11);
    multiplicitySpin_->setValue(1);
    orcaForm->addRow(tr("Multiplicity:"), multiplicitySpin_);
    layout->addWidget(orcaGroup_);

    layout->addStretch(1);
    return page;
}

// ---------------------------------------------------------------------------
// Stage 4 — Script Review
// ---------------------------------------------------------------------------
QWidget* MolecularDynamicsWizard::buildReviewPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* header = new QHBoxLayout;
    header->addWidget(new QLabel(tr("Generated ASE script (editable):"), page));
    header->addStretch(1);
    auto* regenerate = new QPushButton(tr("Regenerate"), page);
    header->addWidget(regenerate);
    layout->addLayout(header);
    preview_ = new QPlainTextEdit(page);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new PythonHighlighter(preview_->document());
    layout->addWidget(preview_, 1);
    connect(preview_, &QPlainTextEdit::textChanged, this, [this] {
        if (!updatingPreview_)
            manuallyEdited_ = true;
    });
    connect(regenerate, &QPushButton::clicked, this, [this] {
        manuallyEdited_ = false;
        refreshPreview();
    });
    return page;
}

void MolecularDynamicsWizard::updateEnsembleEnabled()
{
    const auto ensemble =
        static_cast<core::MdEnsemble>(ensembleCombo_->currentIndex());
    temperatureSpin_->setEnabled(core::isConstantTemperature(ensemble));
    frictionSpin_->setEnabled(ensemble == core::MdEnsemble::LangevinNVT);
    const bool usesTaut = ensemble == core::MdEnsemble::BerendsenNVT
        || ensemble == core::MdEnsemble::NoseHooverChainNVT
        || ensemble == core::MdEnsemble::BerendsenNPT
        || ensemble == core::MdEnsemble::MelchionnaNPT;
    tautSpin_->setEnabled(usesTaut);
    taupSpin_->setEnabled(core::isConstantPressure(ensemble));
    pressureSpin_->setEnabled(core::isConstantPressure(ensemble));
}

void MolecularDynamicsWizard::updateCalculatorEnabled()
{
    const auto kind =
        static_cast<core::CalculatorKind>(calcCombo_->currentData().toInt());
    const bool isDft = kind == core::CalculatorKind::QuantumEspresso
        || kind == core::CalculatorKind::Vasp || kind == core::CalculatorKind::Gpaw
        || kind == core::CalculatorKind::Siesta;
    const bool isMace = kind == core::CalculatorKind::Mace;
    const bool isOrca = kind == core::CalculatorKind::Orca;
    dftGroup_->setVisible(isDft);
    maceGroup_->setVisible(isMace);
    orcaGroup_->setVisible(isOrca);
    if (calcSettingsHint_) {
        calcSettingsHint_->setText(
            (isDft || isMace || isOrca)
                ? tr("Settings for %1:").arg(calcCombo_->currentText())
                : tr("%1 has no additional settings — continue to the script "
                     "review.").arg(calcCombo_->currentText()));
    }
}

core::CalculatorConfig MolecularDynamicsWizard::config() const
{
    core::CalculatorConfig c;
    c.task = core::TaskKind::MolecularDynamics;
    c.calculator =
        static_cast<core::CalculatorKind>(calcCombo_->currentData().toInt());

    c.ensemble = static_cast<core::MdEnsemble>(ensembleCombo_->currentIndex());
    c.temperatureK = temperatureSpin_->value();
    c.pressureGPa = pressureSpin_->value() * 1.0e-4; // bar → GPa
    c.timestepFs = timestepSpin_->value();
    c.frictionPerFs = frictionSpin_->value();
    c.tautFs = tautSpin_->value();
    c.taupFs = taupSpin_->value();
    c.mdSteps = stepsSpin_->value();
    c.mdSampleInterval = sampleSpin_->value();

    c.planeWaveCutoffEv = cutoffSpin_->value();
    for (int i = 0; i < 3; ++i)
        c.kpts[i] = kptSpins_[i]->value();
    c.maceSource =
        static_cast<core::MaceModelSource>(maceModelCombo_->currentIndex());
    c.maceSize = maceSizeCombo_->currentText().toStdString();
    // Combo text carries a friendly suffix; keep only the device token.
    c.maceDevice =
        maceDeviceCombo_->currentText().section(QLatin1Char(' '), 0, 0).toStdString();
    c.orcaMethod = orcaMethodCombo_->currentText().trimmed().toStdString();
    c.orcaBasis = orcaBasisCombo_->currentText().trimmed().toStdString();
    c.charge = chargeSpin_->value();
    c.multiplicity = multiplicitySpin_->value();
    return c;
}

void MolecularDynamicsWizard::refreshPreview()
{
    if (manuallyEdited_)
        return;
    updatingPreview_ = true;
    preview_->setPlainText(QString::fromStdString(
        core::AseScriptGenerator::generate(config(), "structure.extxyz")));
    updatingPreview_ = false;
}

void MolecularDynamicsWizard::updateStage()
{
    stack_->setCurrentIndex(stage_);
    static const char* titles[] = {
        QT_TR_NOOP("Stage 1 of 4 — Dynamics Settings"),
        QT_TR_NOOP("Stage 2 of 4 — Calculator & Execution Environment"),
        QT_TR_NOOP("Stage 3 of 4 — Calculator Settings"),
        QT_TR_NOOP("Stage 4 of 4 — ASE Script Review")};
    headerLabel_->setText(tr(titles[stage_]));

    const bool onReview = stage_ == 3;
    backButton_->setEnabled(stage_ > 0);
    nextButton_->setVisible(!onReview);
    exportButton_->setVisible(onReview);
    runLocalButton_->setVisible(onReview);
    runRemoteButton_->setVisible(onReview);
    if (onReview) {
        const bool local = localRadio_->isChecked();
        runLocalButton_->setDefault(local);
        runRemoteButton_->setDefault(!local);
    }
}

void MolecularDynamicsWizard::goNext()
{
    if (stage_ < 3) {
        ++stage_;
        if (stage_ == 3)
            refreshPreview(); // (re)generate when arriving at the review stage
        updateStage();
    }
}

void MolecularDynamicsWizard::goBack()
{
    if (stage_ > 0) {
        --stage_;
        updateStage();
    }
}

void MolecularDynamicsWizard::exportScript()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export MD Script"), QStringLiteral("md.py"),
        tr("Python scripts (*.py)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Script"),
                             tr("Could not write %1").arg(path));
        return;
    }
    QTextStream(&file) << script();
}

QString MolecularDynamicsWizard::script() const
{
    return preview_->toPlainText();
}

QString MolecularDynamicsWizard::pythonExecutable() const
{
    const QString resolved =
        CalculatorDialog::resolveEnvironmentPython(envEdit_->text());
    if (!resolved.isEmpty())
        return resolved;
    return QString::fromStdString(pybridge::PythonEngine::instance().executable());
}

} // namespace calango::gui
