#include "gui/MaceTrainerDialog.hpp"

#include "core/AseScriptGenerator.hpp"
#include "gui/CondaEnvs.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

namespace calango::gui {

MaceTrainerDialog::MaceTrainerDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("MACE Trainer"));
    resize(920, 660);

    auto* root = new QHBoxLayout(this);

    // -- Left: settings -----------------------------------------------------
    auto* settings = new QVBoxLayout;
    root->addLayout(settings, 0);

    auto* dataGroup = new QGroupBox(tr("Dataset & Architecture"), this);
    auto* dataForm = new QFormLayout(dataGroup);
    auto* trainRow = new QHBoxLayout;
    trainFileEdit_ = new QLineEdit(dataGroup);
    trainFileEdit_->setPlaceholderText(tr("training set (.xyz / .extxyz)"));
    auto* trainBrowse = new QPushButton(tr("Browse…"), dataGroup);
    trainRow->addWidget(trainFileEdit_, 1);
    trainRow->addWidget(trainBrowse);
    dataForm->addRow(tr("Training file:"), trainRow);

    sizeCombo_ = new QComboBox(dataGroup);
    sizeCombo_->addItems({tr("small"), tr("medium"), tr("large")});
    sizeCombo_->setCurrentIndex(1);
    dataForm->addRow(tr("Model architecture:"), sizeCombo_);

    rMaxSpin_ = new QDoubleSpinBox(dataGroup);
    rMaxSpin_->setRange(2.0, 12.0);
    rMaxSpin_->setDecimals(2);
    rMaxSpin_->setSingleStep(0.5);
    rMaxSpin_->setValue(5.0);
    rMaxSpin_->setSuffix(tr(" Å"));
    dataForm->addRow(tr("Cutoff radius (r_max):"), rMaxSpin_);

    channelsSpin_ = new QSpinBox(dataGroup);
    channelsSpin_->setRange(8, 1024);
    channelsSpin_->setValue(128);
    dataForm->addRow(tr("Number of channels:"), channelsSpin_);

    maxLSpin_ = new QSpinBox(dataGroup);
    maxLSpin_->setRange(0, 4);
    maxLSpin_->setValue(1);
    dataForm->addRow(tr("Max L (equivariance):"), maxLSpin_);

    deviceCombo_ = new QComboBox(dataGroup);
    deviceCombo_->addItems({QStringLiteral("cpu"), QStringLiteral("cuda"),
                            QStringLiteral("mps")});
    dataForm->addRow(tr("Device:"), deviceCombo_);
    settings->addWidget(dataGroup);

    auto* optGroup = new QGroupBox(tr("Optimization & Loss"), this);
    auto* optForm = new QFormLayout(optGroup);
    lrSpin_ = new QDoubleSpinBox(optGroup);
    lrSpin_->setDecimals(5);
    lrSpin_->setRange(1e-5, 1.0);
    lrSpin_->setSingleStep(0.001);
    lrSpin_->setValue(0.01);
    optForm->addRow(tr("Learning rate:"), lrSpin_);

    batchSpin_ = new QSpinBox(optGroup);
    batchSpin_->setRange(1, 4096);
    batchSpin_->setValue(10);
    optForm->addRow(tr("Batch size:"), batchSpin_);

    epochsSpin_ = new QSpinBox(optGroup);
    epochsSpin_->setRange(1, 100000);
    epochsSpin_->setValue(200);
    optForm->addRow(tr("Max epochs:"), epochsSpin_);

    const auto weightSpin = [&](double value) {
        auto* spin = new QDoubleSpinBox(optGroup);
        spin->setRange(0.0, 100000.0);
        spin->setDecimals(2);
        spin->setValue(value);
        return spin;
    };
    energyWeightSpin_ = weightSpin(1.0);
    forcesWeightSpin_ = weightSpin(100.0);
    stressWeightSpin_ = weightSpin(0.0);
    virialsWeightSpin_ = weightSpin(0.0);
    optForm->addRow(tr("Energy weight:"), energyWeightSpin_);
    optForm->addRow(tr("Forces weight:"), forcesWeightSpin_);
    optForm->addRow(tr("Stress weight:"), stressWeightSpin_);
    optForm->addRow(tr("Virials weight:"), virialsWeightSpin_);

    seedSpin_ = new QSpinBox(optGroup);
    seedSpin_->setRange(0, 1000000);
    seedSpin_->setValue(123);
    optForm->addRow(tr("Base random seed:"), seedSpin_);
    settings->addWidget(optGroup);

    // Active learning / Query by Committee.
    qbcGroup_ = new QGroupBox(tr("Active Learning (Query by Committee)"), this);
    qbcGroup_->setCheckable(true);
    qbcGroup_->setChecked(false);
    auto* qbcForm = new QFormLayout(qbcGroup_);
    committeeSpin_ = new QSpinBox(qbcGroup_);
    committeeSpin_->setRange(2, 16);
    committeeSpin_->setValue(3);
    committeeSpin_->setToolTip(
        tr("Number of independently-seeded models in the committee."));
    qbcForm->addRow(tr("Committee size (N):"), committeeSpin_);
    uncertaintySpin_ = new QDoubleSpinBox(qbcGroup_);
    uncertaintySpin_->setDecimals(4);
    uncertaintySpin_->setRange(0.0, 100.0);
    uncertaintySpin_->setSingleStep(0.01);
    uncertaintySpin_->setValue(0.05);
    uncertaintySpin_->setSuffix(tr(" eV/Å"));
    uncertaintySpin_->setToolTip(
        tr("Force-disagreement threshold that flags a configuration for "
           "labelling in the active-learning loop."));
    qbcForm->addRow(tr("Uncertainty threshold:"), uncertaintySpin_);
    settings->addWidget(qbcGroup_);

    auto* envGroup = new QGroupBox(tr("Execution environment"), this);
    auto* envLayout = new QVBoxLayout(envGroup);
    const auto condaEnvs = CondaEnvs::discover();
    if (!condaEnvs.isEmpty()) {
        auto* condaCombo = new QComboBox(envGroup);
        condaCombo->addItem(tr("(custom / embedded — use field below)"),
                            QString());
        for (const auto& env : condaEnvs)
            condaCombo->addItem(env.name, env.path);
        envLayout->addWidget(condaCombo);
        connect(condaCombo, &QComboBox::currentIndexChanged, this,
                [this, condaCombo](int) {
                    const QString path = condaCombo->currentData().toString();
                    if (!path.isEmpty())
                        envEdit_->setText(path);
                });
    }
    envEdit_ = new QLineEdit(
        QSettings().value(QStringLiteral("jobs/environmentPath")).toString(),
        envGroup);
    envEdit_->setPlaceholderText(
        tr("conda env folder or python (empty = embedded); needs mace-torch"));
    envLayout->addWidget(envEdit_);
    settings->addWidget(envGroup);
    settings->addStretch(1);

    // -- Right: YAML preview + actions --------------------------------------
    auto* right = new QVBoxLayout;
    root->addLayout(right, 1);
    auto* previewHeader = new QHBoxLayout;
    previewHeader->addWidget(new QLabel(tr("mace_train.yaml (editable):"), this));
    previewHeader->addStretch(1);
    auto* regenerate = new QPushButton(tr("Regenerate"), this);
    previewHeader->addWidget(regenerate);
    right->addLayout(previewHeader);
    preview_ = new QPlainTextEdit(this);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    right->addWidget(preview_, 1);

    auto* buttons = new QDialogButtonBox(this);
    auto* exportButton =
        buttons->addButton(tr("Export YAML…"), QDialogButtonBox::ActionRole);
    auto* runRemoteButton =
        buttons->addButton(tr("Run (Remote)"), QDialogButtonBox::AcceptRole);
    auto* runLocalButton =
        buttons->addButton(tr("Run (Local)"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    runLocalButton->setDefault(true);
    right->addWidget(buttons);

    // Wiring: any setting change regenerates the preview (unless hand-edited).
    for (QDoubleSpinBox* spin : {rMaxSpin_, lrSpin_, energyWeightSpin_,
                                 forcesWeightSpin_, stressWeightSpin_,
                                 virialsWeightSpin_, uncertaintySpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &MaceTrainerDialog::refreshPreview);
    for (QSpinBox* spin : {channelsSpin_, maxLSpin_, batchSpin_, epochsSpin_,
                           seedSpin_, committeeSpin_})
        connect(spin, &QSpinBox::valueChanged, this,
                &MaceTrainerDialog::refreshPreview);
    connect(deviceCombo_, &QComboBox::currentIndexChanged, this,
            &MaceTrainerDialog::refreshPreview);
    connect(trainFileEdit_, &QLineEdit::textChanged, this,
            &MaceTrainerDialog::refreshPreview);
    connect(qbcGroup_, &QGroupBox::toggled, this,
            &MaceTrainerDialog::refreshPreview);
    connect(sizeCombo_, &QComboBox::currentIndexChanged, this,
            &MaceTrainerDialog::applySizePreset);

    connect(trainBrowse, &QPushButton::clicked, this,
            &MaceTrainerDialog::browseTrainFile);
    connect(regenerate, &QPushButton::clicked, this, [this] {
        manuallyEdited_ = false;
        refreshPreview();
    });
    connect(preview_, &QPlainTextEdit::textChanged, this,
            [this] { manuallyEdited_ = true; });
    connect(exportButton, &QPushButton::clicked, this,
            &MaceTrainerDialog::exportYaml);
    connect(runLocalButton, &QPushButton::clicked, this, [this] {
        action_ = Action::RunLocal;
        accept();
    });
    connect(runRemoteButton, &QPushButton::clicked, this, [this] {
        action_ = Action::RunRemote;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    applySizePreset();
    refreshPreview();
}

void MaceTrainerDialog::applySizePreset()
{
    // Preset the channels / max L for the chosen MACE size, then regenerate.
    switch (sizeCombo_->currentIndex()) {
    case 0: channelsSpin_->setValue(64);  maxLSpin_->setValue(0); break; // small
    case 2: channelsSpin_->setValue(192); maxLSpin_->setValue(2); break; // large
    default: channelsSpin_->setValue(128); maxLSpin_->setValue(1); break; // medium
    }
    refreshPreview();
}

QString MaceTrainerDialog::buildYaml() const
{
    const QString trainFile = trainFileEdit_->text().trimmed().isEmpty()
        ? QStringLiteral("train.xyz")
        : QFileInfo(trainFileEdit_->text().trimmed()).absoluteFilePath();

    QString y;
    y += QStringLiteral("# MACE training configuration — generated by Calango\n");
    y += QStringLiteral("model: MACE\n");
    y += QStringLiteral("name: mace_model\n");
    y += QStringLiteral("train_file: \"%1\"\n").arg(trainFile);
    y += QStringLiteral("valid_fraction: 0.1\n");
    y += QStringLiteral("r_max: %1\n").arg(rMaxSpin_->value());
    y += QStringLiteral("num_channels: %1\n").arg(channelsSpin_->value());
    y += QStringLiteral("max_L: %1\n").arg(maxLSpin_->value());
    y += QStringLiteral("num_interactions: 2\n");
    y += QStringLiteral("correlation: 3\n");
    y += QStringLiteral("batch_size: %1\n").arg(batchSpin_->value());
    y += QStringLiteral("max_num_epochs: %1\n").arg(epochsSpin_->value());
    y += QStringLiteral("lr: %1\n").arg(lrSpin_->value());
    y += QStringLiteral("loss: weighted\n");
    y += QStringLiteral("energy_weight: %1\n").arg(energyWeightSpin_->value());
    y += QStringLiteral("forces_weight: %1\n").arg(forcesWeightSpin_->value());
    y += QStringLiteral("stress_weight: %1\n").arg(stressWeightSpin_->value());
    y += QStringLiteral("virials_weight: %1\n").arg(virialsWeightSpin_->value());
    y += QStringLiteral("device: %1\n").arg(deviceCombo_->currentText());
    y += QStringLiteral("seed: %1\n").arg(seedSpin_->value());

    if (qbcGroup_->isChecked()) {
        y += QStringLiteral("\n# --- Active learning (Query by Committee) ---\n");
        y += QStringLiteral("# committee_size: %1\n").arg(committeeSpin_->value());
        y += QStringLiteral("# uncertainty_threshold_eV_per_A: %1\n")
                 .arg(uncertaintySpin_->value());
        y += QStringLiteral("# The launcher trains %1 models with seeds "
                            "%2..%3; the force spread across the committee is "
                            "the per-configuration uncertainty used to select "
                            "new structures for labelling.\n")
                 .arg(committeeSpin_->value())
                 .arg(seedSpin_->value())
                 .arg(seedSpin_->value() + committeeSpin_->value() - 1);
    }
    return y;
}

void MaceTrainerDialog::refreshPreview()
{
    if (manuallyEdited_)
        return;
    const QSignalBlocker blocker(preview_);
    preview_->setPlainText(buildYaml());
}

QString MaceTrainerDialog::yaml() const
{
    return preview_->toPlainText();
}

QString MaceTrainerDialog::runnerScript() const
{
    const int base = seedSpin_->value();
    const int count = qbcGroup_->isChecked() ? committeeSpin_->value() : 1;
    QStringList seeds;
    for (int i = 0; i < count; ++i)
        seeds << QString::number(base + i);

    QString s;
    s += QStringLiteral("#!/usr/bin/env python3\n");
    s += QStringLiteral("# MACE training launcher generated by Calango.\n");
    s += QStringLiteral("import subprocess\n");
    s += QStringLiteral("import sys\n\n");
    // Shared JSON logger (warnings.log + metrics.json progress) so training
    // progress is written to metrics.json instead of stdout.
    s += QString::fromStdString(core::AseScriptGenerator::jsonLoggerPreamble());
    s += QStringLiteral("CONFIG = r\"\"\"\n");
    s += yaml();
    if (!yaml().endsWith(QLatin1Char('\n')))
        s += QLatin1Char('\n');
    s += QStringLiteral("\"\"\"\n\n");
    s += QStringLiteral("with open(\"mace_train.yaml\", \"w\") as _fh:\n");
    s += QStringLiteral("    _fh.write(CONFIG)\n\n");
    s += QStringLiteral("# Query-by-Committee ensemble: one training run per "
                        "seed.\n");
    s += QStringLiteral("seeds = [%1]\n").arg(seeds.join(QStringLiteral(", ")));
    s += QStringLiteral("for _i, _seed in enumerate(seeds):\n");
    s += QStringLiteral("    _calango_log.progress(_i, len(seeds))\n");
    s += QStringLiteral("    print(f\"CALANGO_INFO training MACE model "
                        "seed={_seed}\", flush=True)\n");
    s += QStringLiteral("    subprocess.run([sys.executable, \"-m\", "
                        "\"mace.cli.run_train\",\n");
    s += QStringLiteral("                    \"--config\", \"mace_train.yaml\",\n");
    s += QStringLiteral("                    \"--seed\", str(_seed),\n");
    s += QStringLiteral("                    \"--name\", f\"mace_model_{_seed}\"],\n");
    s += QStringLiteral("                   check=True)\n\n");
    s += QStringLiteral("print(\"CALANGO_RESULT models_trained=\" + "
                        "str(len(seeds)), flush=True)\n");
    s += QStringLiteral("print(\"CALANGO_DONE\", flush=True)\n");
    return s;
}

QString MaceTrainerDialog::pythonExecutable() const
{
    const QString resolved =
        CondaEnvs::resolvePython(envEdit_->text());
    if (!resolved.isEmpty())
        return resolved;
    return QString::fromStdString(pybridge::PythonEngine::instance().executable());
}

void MaceTrainerDialog::exportYaml()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export MACE Config"), QStringLiteral("mace_train.yaml"),
        tr("YAML files (*.yaml *.yml);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export MACE Config"),
                             tr("Could not write %1").arg(path));
        return;
    }
    file.write(yaml().toUtf8());
}

void MaceTrainerDialog::browseTrainFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select Training Set"), trainFileEdit_->text(),
        tr("Structures (*.xyz *.extxyz);;All files (*)"));
    if (!path.isEmpty())
        trainFileEdit_->setText(path);
}

} // namespace calango::gui
