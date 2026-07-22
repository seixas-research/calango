#include "gui/SimulationWizardBase.hpp"

#include "gui/CalculatorDialog.hpp" // resolveEnvironmentPython (shared)
#include "gui/CondaEnvs.hpp"
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
#include <QStringList>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {
const auto kEnvSettingsKey = QStringLiteral("jobs/environmentPath");
} // namespace

SimulationWizardBase::SimulationWizardBase(QWidget* parent) : QDialog(parent) {}

void SimulationWizardBase::buildUi()
{
    setWindowTitle(wizardTitle());
    resize(900, 640);

    auto* root = new QVBoxLayout(this);
    headerLabel_ = new QLabel(this);
    QFont hf = headerLabel_->font();
    hf.setPointSizeF(hf.pointSizeF() * 1.15);
    hf.setBold(true);
    headerLabel_->setFont(hf);
    root->addWidget(headerLabel_);

    hasSettingsStage_ = hasTaskSettingsStage();
    stack_ = new QStackedWidget(this);
    if (hasSettingsStage_)
        stack_->addWidget(buildSettingsPage()); // Stage 1 (subclass)
    stack_->addWidget(buildEnvironmentPage());
    stack_->addWidget(buildCalculatorPage());
    stack_->addWidget(buildReviewPage());
    reviewStage_ = stack_->count() - 1;
    root->addWidget(stack_, 1);

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

    connect(backButton_, &QPushButton::clicked, this, &SimulationWizardBase::goBack);
    connect(nextButton_, &QPushButton::clicked, this, &SimulationWizardBase::goNext);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(exportButton_, &QPushButton::clicked, this,
            &SimulationWizardBase::exportScript);
    connect(runLocalButton_, &QPushButton::clicked, this, [this] {
        action_ = Action::RunLocal;
        accept();
    });
    connect(runRemoteButton_, &QPushButton::clicked, this, [this] {
        action_ = Action::RunRemote;
        accept();
    });

    updateCalculatorEnabled();
    updateStage();
}

// ---------------------------------------------------------------------------
// Stage 2 — Calculator & Execution Environment
// ---------------------------------------------------------------------------
QWidget* SimulationWizardBase::buildEnvironmentPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    calcCombo_ = new QComboBox(page);
    // A subclass may restrict the engine list (e.g. the Electronic Bands
    // wizard offers only DFT-capable calculators). Only allowed kinds appear.
    const auto addCalc = [this](const QString& label, core::CalculatorKind kind) {
        if (calculatorAllowed(kind))
            calcCombo_->addItem(label, static_cast<int>(kind));
    };
    addCalc(tr("MACE (ML potential)"), core::CalculatorKind::Mace);
    addCalc(tr("Quantum ESPRESSO (DFT)"), core::CalculatorKind::QuantumEspresso);
    addCalc(tr("SIESTA (DFT)"), core::CalculatorKind::Siesta);
    addCalc(tr("ORCA (quantum chemistry)"), core::CalculatorKind::Orca);
    addCalc(tr("GPAW (DFT)"), core::CalculatorKind::Gpaw);
    addCalc(tr("VASP (DFT)"), core::CalculatorKind::Vasp);
    addCalc(tr("EMT (fast test potential)"), core::CalculatorKind::EMT);
    addCalc(tr("ASAP (fast EMT / OpenKIM)"), core::CalculatorKind::Asap);
    addCalc(tr("Lennard-Jones"), core::CalculatorKind::LennardJones);
    form->addRow(tr("Calculation engine:"), calcCombo_);
    connect(calcCombo_, &QComboBox::currentIndexChanged, this,
            &SimulationWizardBase::updateCalculatorEnabled);

    auto* envGroup = new QGroupBox(tr("Execution environment"), page);
    auto* envLayout = new QVBoxLayout(envGroup);

    // Conda environments auto-discovered from the Preferences "Conda Directory
    // Path" (or common install locations). Picking one fills the field below.
    const auto condaEnvs = CondaEnvs::discover();
    if (!condaEnvs.isEmpty()) {
        auto* condaRow = new QHBoxLayout;
        condaRow->addWidget(new QLabel(tr("Conda environment:"), envGroup));
        auto* condaCombo = new QComboBox(envGroup);
        condaCombo->addItem(tr("(custom / embedded — use field below)"),
                            QString());
        for (const auto& env : condaEnvs)
            condaCombo->addItem(env.name, env.path);
        condaRow->addWidget(condaCombo, 1);
        envLayout->addLayout(condaRow);
        connect(condaCombo, &QComboBox::currentIndexChanged, this,
                [this, condaCombo](int) {
                    const QString path = condaCombo->currentData().toString();
                    if (!path.isEmpty())
                        envEdit_->setText(path);
                });
    }

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
    connect(localRadio_, &QRadioButton::toggled, this, [this] { updateStage(); });

    layout->addStretch(1);
    envEdit_->textChanged(envEdit_->text()); // seed the status label
    return page;
}

// ---------------------------------------------------------------------------
// Stage 3 — Calculator Settings
// ---------------------------------------------------------------------------
QWidget* SimulationWizardBase::buildCalculatorPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    calcSettingsHint_ = new QLabel(page);
    calcSettingsHint_->setWordWrap(true);
    layout->addWidget(calcSettingsHint_);

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

    // Subclass-supplied extra settings (e.g. Single-point's convergence group,
    // folded in here when it has no separate Stage 1).
    if (QWidget* extras = buildCalculatorExtras())
        layout->addWidget(extras);

    layout->addStretch(1);
    return page;
}

// ---------------------------------------------------------------------------
// Stage 4 — Script Review
// ---------------------------------------------------------------------------
QWidget* SimulationWizardBase::buildReviewPage()
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

void SimulationWizardBase::updateCalculatorEnabled()
{
    const auto kind = selectedCalculator();
    const bool isDft = kind == core::CalculatorKind::QuantumEspresso
        || kind == core::CalculatorKind::Vasp || kind == core::CalculatorKind::Gpaw
        || kind == core::CalculatorKind::Siesta;
    const bool isMace = kind == core::CalculatorKind::Mace;
    const bool isOrca = kind == core::CalculatorKind::Orca;
    dftGroup_->setVisible(isDft);
    maceGroup_->setVisible(isMace);
    orcaGroup_->setVisible(isOrca);
    if (calcSettingsHint_)
        calcSettingsHint_->setText(
            (isDft || isMace || isOrca)
                ? tr("Settings for %1:").arg(calcCombo_->currentText())
                : tr("%1 has no additional settings — continue to the script "
                     "review.").arg(calcCombo_->currentText()));
    updateCalculatorExtras(kind);
}

core::CalculatorKind SimulationWizardBase::selectedCalculator() const
{
    return static_cast<core::CalculatorKind>(calcCombo_->currentData().toInt());
}

core::CalculatorConfig SimulationWizardBase::baseCalculatorConfig() const
{
    core::CalculatorConfig c;
    c.calculator = selectedCalculator();
    c.planeWaveCutoffEv = cutoffSpin_->value();
    for (int i = 0; i < 3; ++i)
        c.kpts[i] = kptSpins_[i]->value();
    c.maceSource =
        static_cast<core::MaceModelSource>(maceModelCombo_->currentIndex());
    c.maceSize = maceSizeCombo_->currentText().toStdString();
    // Device combo carries a friendly suffix; keep only the device token.
    c.maceDevice =
        maceDeviceCombo_->currentText().section(QLatin1Char(' '), 0, 0).toStdString();
    c.orcaMethod = orcaMethodCombo_->currentText().trimmed().toStdString();
    c.orcaBasis = orcaBasisCombo_->currentText().trimmed().toStdString();
    c.charge = chargeSpin_->value();
    c.multiplicity = multiplicitySpin_->value();
    return c;
}

void SimulationWizardBase::refreshPreview()
{
    if (manuallyEdited_)
        return;
    updatingPreview_ = true;
    preview_->setPlainText(generateScript());
    updatingPreview_ = false;
}

void SimulationWizardBase::updateStage()
{
    stack_->setCurrentIndex(stage_);

    QStringList titles;
    if (hasSettingsStage_)
        titles << settingsHeader();
    titles << tr("Calculator & Execution Environment");
    titles << calculatorSettingsHeader();
    titles << tr("ASE Script Review");
    headerLabel_->setText(tr("Stage %1 of %2 — %3")
                              .arg(stage_ + 1)
                              .arg(titles.size())
                              .arg(titles.at(stage_)));

    const bool onReview = stage_ == reviewStage_;
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

void SimulationWizardBase::goNext()
{
    if (stage_ < reviewStage_) {
        ++stage_;
        if (stage_ == reviewStage_)
            refreshPreview(); // (re)generate on arriving at the review stage
        updateStage();
    }
}

void SimulationWizardBase::goBack()
{
    if (stage_ > 0) {
        --stage_;
        updateStage();
    }
}

void SimulationWizardBase::exportScript()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Script"), exportFileName(), tr("Python scripts (*.py)"));
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

QString SimulationWizardBase::script() const
{
    return preview_->toPlainText();
}

QString SimulationWizardBase::pythonExecutable() const
{
    const QString resolved =
        CalculatorDialog::resolveEnvironmentPython(envEdit_->text());
    if (!resolved.isEmpty())
        return resolved;
    return QString::fromStdString(pybridge::PythonEngine::instance().executable());
}

} // namespace calango::gui
