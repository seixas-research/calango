#include "gui/CalculatorDialog.hpp"

#include "core/AseScriptGenerator.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

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
                                tr("MACE (machine-learning potential, requires mace-torch)")});

    taskCombo_ = new QComboBox(this);
    taskCombo_->addItems({tr("Single-point energy"),
                          tr("Geometry optimization (BFGS)"),
                          tr("Molecular dynamics")});

    ensembleCombo_ = new QComboBox(this);
    ensembleCombo_->addItems({tr("NVT — Langevin thermostat"),
                              tr("NVE — Velocity Verlet")});

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

    auto* form = new QFormLayout;
    form->addRow(tr("Calculator:"), calculatorCombo_);
    form->addRow(tr("Task:"), taskCombo_);
    form->addRow(tr("MD ensemble:"), ensembleCombo_);
    form->addRow(tr("Force convergence (fmax):"), fmaxSpin_);
    form->addRow(tr("Max optimization steps:"), maxStepsSpin_);
    form->addRow(tr("Temperature:"), temperatureSpin_);
    form->addRow(tr("Timestep:"), timestepSpin_);
    form->addRow(tr("MD steps:"), mdStepsSpin_);
    form->addRow(tr("Plane-wave cutoff:"), cutoffSpin_);
    form->addRow(tr("k-point grid:"), kptRow);
    form->addRow(tr("MACE model:"), maceModelCombo_);
    form->addRow(tr("MACE model size:"), maceSizeCombo_);
    form->addRow(tr("Custom model file:"), macePathRow);
    form->addRow(tr("MACE device:"), maceDeviceCombo_);

    preview_ = new QPlainTextEdit(this);
    preview_->setReadOnly(true);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

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
    left->addStretch(1);

    auto* content = new QHBoxLayout;
    content->addLayout(left, 0);
    auto* previewColumn = new QVBoxLayout;
    previewColumn->addWidget(new QLabel(tr("Generated ASE script:"), this));
    previewColumn->addWidget(preview_, 1);
    content->addLayout(previewColumn, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(content, 1);
    layout->addWidget(buttons);

    const auto refresh = [this] { refreshPreview(); };
    connect(calculatorCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(taskCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(ensembleCombo_, &QComboBox::currentIndexChanged, this, refresh);
    connect(fmaxSpin_, &QDoubleSpinBox::valueChanged, this, refresh);
    connect(maxStepsSpin_, &QSpinBox::valueChanged, this, refresh);
    connect(temperatureSpin_, &QDoubleSpinBox::valueChanged, this, refresh);
    connect(timestepSpin_, &QDoubleSpinBox::valueChanged, this, refresh);
    connect(mdStepsSpin_, &QSpinBox::valueChanged, this, refresh);
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
    c.planeWaveCutoffEv = cutoffSpin_->value();
    for (int i = 0; i < 3; ++i)
        c.kpts[i] = kptSpins_[i]->value();
    c.maceSource = static_cast<core::MaceModelSource>(maceModelCombo_->currentIndex());
    c.maceSize = maceSizeCombo_->currentText().toStdString();
    c.maceModelPath = maceModelPathEdit_->text().trimmed().toStdString();
    c.maceDevice = maceDeviceCombo_->currentText().toStdString();
    return c;
}

QString CalculatorDialog::script() const
{
    // The job runner always stages the structure as structure.extxyz
    // inside the job directory, so the script refers to it relatively.
    return QString::fromStdString(
        core::AseScriptGenerator::generate(config(), "structure.extxyz"));
}

void CalculatorDialog::refreshPreview()
{
    const core::CalculatorConfig c = config();
    const bool isOpt = c.task == core::TaskKind::GeometryOptimization;
    const bool isMd = c.task == core::TaskKind::MolecularDynamics;
    const bool isDft = c.calculator == core::CalculatorKind::QuantumEspresso
        || c.calculator == core::CalculatorKind::Vasp;
    const bool isMace = c.calculator == core::CalculatorKind::Mace;
    const bool isCustomMace =
        isMace && c.maceSource == core::MaceModelSource::CustomFile;

    fmaxSpin_->setEnabled(isOpt);
    maxStepsSpin_->setEnabled(isOpt);
    ensembleCombo_->setEnabled(isMd);
    temperatureSpin_->setEnabled(isMd);
    timestepSpin_->setEnabled(isMd);
    mdStepsSpin_->setEnabled(isMd);
    cutoffSpin_->setEnabled(isDft);
    for (auto* spin : kptSpins_)
        spin->setEnabled(isDft);
    maceModelCombo_->setEnabled(isMace);
    maceSizeCombo_->setEnabled(isMace && !isCustomMace);
    maceModelPathEdit_->setEnabled(isCustomMace);
    maceBrowseButton_->setEnabled(isCustomMace);
    maceDeviceCombo_->setEnabled(isMace);

    preview_->setPlainText(script());
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
