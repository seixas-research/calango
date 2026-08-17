#include "gui/MaceTrainerDialog.hpp"

#include "core/AseScriptGenerator.hpp"
#include "gui/CondaEnvs.hpp"
#include "gui/PythonPackagePreflight.hpp"
#include "gui/SettingsManager.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
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

    auto* dataGroup = new QGroupBox(tr("Dataset && Architecture"), this);
    auto* dataForm = new QFormLayout(dataGroup);
    auto* trainRow = new QHBoxLayout;
    trainFileEdit_ = new QLineEdit(dataGroup);
    trainFileEdit_->setPlaceholderText(tr("training set (.xyz / .extxyz)"));
    auto* trainBrowse = new QPushButton(tr("Browse…"), dataGroup);
    trainRow->addWidget(trainFileEdit_, 1);
    trainRow->addWidget(trainBrowse);
    dataForm->addRow(tr("Training file:"), trainRow);

    auto* validRow = new QHBoxLayout;
    validFileEdit_ = new QLineEdit(dataGroup);
    validFileEdit_->setPlaceholderText(tr("optional — else 10% is held out"));
    validFileEdit_->setToolTip(
        tr("An explicit validation set, as the Dataset Manager's split "
           "produces. Left empty, MACE holds out a tenth of the training file "
           "at random (valid_fraction), which is fine for a first look and "
           "wrong for comparing runs — the held-out set differs every time."));
    auto* validBrowse = new QPushButton(tr("Browse…"), dataGroup);
    validRow->addWidget(validFileEdit_, 1);
    validRow->addWidget(validBrowse);
    dataForm->addRow(tr("Validation file:"), validRow);

    // The two keys that decide whether the run learns anything at all.
    //
    // MACE defaults to REF_energy / REF_forces. Calango writes its datasets
    // with plain ASE, which stores the energy and forces on a
    // SinglePointCalculator — read back, atoms.info and atoms.arrays carry
    // NEITHER key. MACE does not fail on that: it warns, sets the per-property
    // weight to zero, and trains a model on nothing. Naming the keys "energy"
    // and "forces" takes MACE's ASE-compatibility path, which pulls the values
    // off the calculator and rewrites them as REF_*.
    energyKeyCombo_ = new QComboBox(dataGroup);
    energyKeyCombo_->setEditable(true);
    energyKeyCombo_->addItems({QStringLiteral("energy"),
                               QStringLiteral("REF_energy")});
    energyKeyCombo_->setToolTip(
        tr("Where the reference energy is stored in the training file.\n\n"
           "\"energy\" is what ASE writes — and therefore what a dataset "
           "exported from Calango carries. \"REF_energy\" is MACE's own "
           "default, for sets prepared with MACE's conventions.\n\n"
           "Getting this wrong does not fail the run: MACE drops the energy "
           "term to zero weight and trains anyway."));
    dataForm->addRow(tr("Energy key:"), energyKeyCombo_);

    forcesKeyCombo_ = new QComboBox(dataGroup);
    forcesKeyCombo_->setEditable(true);
    forcesKeyCombo_->addItems({QStringLiteral("forces"),
                               QStringLiteral("REF_forces")});
    forcesKeyCombo_->setToolTip(
        tr("Where the reference forces are stored. Same rule as the energy "
           "key above: \"forces\" for an ASE/Calango dataset, \"REF_forces\" "
           "for a MACE-native one."));
    dataForm->addRow(tr("Forces key:"), forcesKeyCombo_);

    // Isolated-atom energies. Not optional in any sense that matters: with
    // E0s unset and no config_type=IsolatedAtom entries in the training file
    // — which a Calango-exported set never has — MACE raises
    // "E0s not found in training file and not specified in command line"
    // before the first epoch.
    e0sModeCombo_ = new QComboBox(dataGroup);
    e0sModeCombo_->addItem(tr("Average (least-squares fit to the set)"),
                           QStringLiteral("average"));
    e0sModeCombo_->addItem(tr("From a JSON file (Z → energy)"),
                           QStringLiteral("file"));
    e0sModeCombo_->addItem(tr("Isolated atoms in the training file"),
                           QStringLiteral("dataset"));
    e0sModeCombo_->setToolTip(
        tr("The one-atom reference energies MACE subtracts before fitting, so "
           "the model learns interactions rather than the huge constant "
           "offsets of the atomic totals.\n\n"
           "• Average — regress them out of the training set itself. The "
           "right default: it needs nothing extra and is what MACE recommends "
           "when isolated-atom calculations are not to hand.\n"
           "• JSON file — {\"42\": -5.0448, \"16\": -0.9036} from your own "
           "isolated-atom runs. The most accurate option, and the one to use "
           "when several models must share a reference.\n"
           "• Isolated atoms in the training file — only if the set contains "
           "single-atom frames tagged config_type=IsolatedAtom.\n\n"
           "There is no \"leave it out\": with none of these, MACE aborts "
           "before the first epoch."));
    dataForm->addRow(tr("Isolated-atom energies:"), e0sModeCombo_);

    auto* e0sRow = new QHBoxLayout;
    e0sFileEdit_ = new QLineEdit(dataGroup);
    e0sFileEdit_->setPlaceholderText(tr("E0s.json"));
    auto* e0sBrowse = new QPushButton(tr("Browse…"), dataGroup);
    e0sRow->addWidget(e0sFileEdit_, 1);
    e0sRow->addWidget(e0sBrowse);
    dataForm->addRow(tr("E0s file:"), e0sRow);

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

    auto* optGroup = new QGroupBox(tr("Optimization && Loss"), this);
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

    patienceSpin_ = new QSpinBox(optGroup);
    patienceSpin_->setRange(1, 100000);
    patienceSpin_->setValue(50);
    patienceSpin_->setToolTip(
        tr("Stop after this many epochs with no improvement in the validation "
           "loss. MACE's own default is 2048 — i.e. effectively never — which "
           "means an over-long run keeps burning time after it has converged."));
    optForm->addRow(tr("Early-stopping patience:"), patienceSpin_);

    evalIntervalSpin_ = new QSpinBox(optGroup);
    evalIntervalSpin_->setRange(1, 1000);
    evalIntervalSpin_->setValue(5);
    evalIntervalSpin_->setToolTip(
        tr("Evaluate on the validation set every N epochs. Every epoch (MACE's "
           "default) is a real cost on a large set for a curve that barely "
           "moves in one step."));
    optForm->addRow(tr("Validation interval:"), evalIntervalSpin_);

    dtypeCombo_ = new QComboBox(optGroup);
    dtypeCombo_->addItems({QStringLiteral("float64"), QStringLiteral("float32")});
    dtypeCombo_->setCurrentIndex(1);
    dtypeCombo_->setToolTip(
        tr("Training precision. float32 is roughly twice as fast and is what "
           "production MACE models are trained in; float64 matches MACE's own "
           "default and is worth it when the model must reproduce DFT energy "
           "differences at the meV level.\n\n"
           "Whatever is chosen here, the ASE calculator that later loads the "
           "model has to be told the same dtype."));
    optForm->addRow(tr("Precision:"), dtypeCombo_);

    seedSpin_ = new QSpinBox(optGroup);
    seedSpin_->setRange(0, 1000000);
    seedSpin_->setValue(123);
    optForm->addRow(tr("Base random seed:"), seedSpin_);
    settings->addWidget(optGroup);

    // Stage two: MACE's standard two-phase schedule. The second phase raises
    // the energy weight sharply and drops the learning rate, which is what
    // turns a model with good forces into one with good energies as well.
    swaGroup_ = new QGroupBox(tr("Stage two (SWA) && averaging"), this);
    swaGroup_->setCheckable(true);
    swaGroup_->setChecked(true);
    swaGroup_->setToolTip(
        tr("Switch to the stage-two loss after the epoch below. Standard "
           "practice for MACE and off by default in MACE itself, so a config "
           "that does not ask for it trains in stage one only."));
    auto* swaForm = new QFormLayout(swaGroup_);
    swaStartSpin_ = new QSpinBox(swaGroup_);
    swaStartSpin_->setRange(1, 100000);
    swaStartSpin_->setValue(150);
    swaStartSpin_->setToolTip(
        tr("Epoch at which stage two begins. Conventionally around three "
           "quarters of the epoch budget — early enough for the second phase "
           "to converge, late enough that the first has done its work."));
    swaForm->addRow(tr("Start at epoch:"), swaStartSpin_);

    emaCheck_ = new QCheckBox(tr("Exponential moving average of the weights"),
                              swaGroup_);
    emaCheck_->setChecked(true);
    emaCheck_->setToolTip(
        tr("Evaluate and save an exponentially-weighted average of the "
           "weights rather than the last step's. Costs nothing and reliably "
           "smooths the noise a small batch size puts into the final model."));
    swaForm->addRow(QString(), emaCheck_);

    emaDecaySpin_ = new QDoubleSpinBox(swaGroup_);
    emaDecaySpin_->setDecimals(4);
    emaDecaySpin_->setRange(0.5, 0.9999);
    emaDecaySpin_->setSingleStep(0.01);
    emaDecaySpin_->setValue(0.99);
    swaForm->addRow(tr("EMA decay:"), emaDecaySpin_);
    connect(emaCheck_, &QCheckBox::toggled, emaDecaySpin_, &QWidget::setEnabled);
    settings->addWidget(swaGroup_);

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
        QSettings().value(SettingsManager::kEnvironmentPath).toString(),
        envGroup);
    envEdit_->setPlaceholderText(
        tr("conda env folder or python (empty = embedded); needs mace-torch"));
    envLayout->addWidget(envEdit_);
    auto* checkEnvRow = new QHBoxLayout;
    checkEnvButton_ = new QPushButton(tr("Check Environment"), envGroup);
    checkEnvButton_->setToolTip(
        tr("Probes the interpreter above for mace-torch (reporting its "
           "version) and for which PyTorch compute devices it can actually "
           "use — cpu always, cuda/mps only when the installed PyTorch "
           "build and hardware support them. mace-torch is never vendored "
           "or hard-depended-on by Calango itself; this is the same check "
           "either Run button runs automatically before launching."));
    checkEnvRow->addWidget(checkEnvButton_);
    checkEnvRow->addStretch(1);
    envLayout->addLayout(checkEnvRow);
    envStatus_ = new QLabel(
        tr("Not checked yet — press Check Environment, or Run."), envGroup);
    envStatus_->setWordWrap(true);
    envLayout->addWidget(envStatus_);
    connect(checkEnvButton_, &QPushButton::clicked, this,
            &MaceTrainerDialog::checkEnvironment);
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
                                 virialsWeightSpin_, uncertaintySpin_,
                                 emaDecaySpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                &MaceTrainerDialog::refreshPreview);
    for (QSpinBox* spin : {channelsSpin_, maxLSpin_, batchSpin_, epochsSpin_,
                           seedSpin_, committeeSpin_, patienceSpin_,
                           evalIntervalSpin_, swaStartSpin_})
        connect(spin, &QSpinBox::valueChanged, this,
                &MaceTrainerDialog::refreshPreview);
    for (QComboBox* combo : {deviceCombo_, dtypeCombo_, energyKeyCombo_,
                             forcesKeyCombo_, e0sModeCombo_})
        connect(combo, &QComboBox::currentTextChanged, this,
                &MaceTrainerDialog::refreshPreview);
    for (QLineEdit* edit : {trainFileEdit_, validFileEdit_, e0sFileEdit_})
        connect(edit, &QLineEdit::textChanged, this,
                &MaceTrainerDialog::refreshPreview);
    for (QGroupBox* group : {qbcGroup_, swaGroup_})
        connect(group, &QGroupBox::toggled, this,
                &MaceTrainerDialog::refreshPreview);
    connect(emaCheck_, &QCheckBox::toggled, this,
            &MaceTrainerDialog::refreshPreview);
    connect(sizeCombo_, &QComboBox::currentIndexChanged, this,
            &MaceTrainerDialog::applySizePreset);

    // Stage two must have room to run AND to checkpoint. MACE saves a
    // checkpoint on the evaluation cadence, so a start_swa that leaves fewer
    // than eval_interval epochs behind it produces no stage-two checkpoint —
    // and MACE then dies at the very end of an otherwise successful run,
    // trying to load the checkpoint it never wrote ("No SWA checkpoint found",
    // then an UnboundLocalError from its checkpoint handler). Clamping the
    // control is the only way a user finds that out before burning the epochs.
    const auto syncSwaRange = [this] {
        const int room = epochsSpin_->value() - evalIntervalSpin_->value();
        swaStartSpin_->setMaximum(qMax(1, room));
    };
    syncSwaRange();
    connect(epochsSpin_, &QSpinBox::valueChanged, this, syncSwaRange);
    connect(evalIntervalSpin_, &QSpinBox::valueChanged, this, syncSwaRange);

    // The E0s file row is only meaningful for the "from a JSON file" mode.
    const auto syncE0sFileRow = [this, e0sBrowse] {
        const bool fromFile =
            e0sModeCombo_->currentData().toString() == QLatin1String("file");
        e0sFileEdit_->setEnabled(fromFile);
        e0sBrowse->setEnabled(fromFile);
    };
    syncE0sFileRow();
    connect(e0sModeCombo_, &QComboBox::currentIndexChanged, this, syncE0sFileRow);

    connect(trainBrowse, &QPushButton::clicked, this,
            &MaceTrainerDialog::browseTrainFile);
    connect(validBrowse, &QPushButton::clicked, this,
            &MaceTrainerDialog::browseValidFile);
    connect(e0sBrowse, &QPushButton::clicked, this,
            &MaceTrainerDialog::browseE0sFile);
    connect(regenerate, &QPushButton::clicked, this, [this] {
        manuallyEdited_ = false;
        refreshPreview();
    });
    connect(preview_, &QPlainTextEdit::textChanged, this,
            [this] { manuallyEdited_ = true; });
    connect(exportButton, &QPushButton::clicked, this,
            &MaceTrainerDialog::exportYaml);
    connect(runLocalButton, &QPushButton::clicked, this, [this] {
        if (!preflightMaceTorch())
            return;
        action_ = Action::RunLocal;
        accept();
    });
    connect(runRemoteButton, &QPushButton::clicked, this, [this] {
        // Remote: the check still runs against the LOCAL interpreter field
        // above, which is what the user has told Calango to resolve for
        // this node — the actual remote host may differ, and there is no
        // way to probe it from here. Worth doing anyway: the common case is
        // a local conda env name reused verbatim as the remote one, and
        // catching a missing mace-torch before anything is even staged is
        // strictly better than catching it only once a cluster job fails.
        if (!preflightMaceTorch())
            return;
        action_ = Action::RunRemote;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    applySizePreset();
    refreshPreview();
}

void MaceTrainerDialog::checkEnvironment()
{
    envStatus_->setStyleSheet(QString());
    envStatus_->setText(tr("Checking %1…").arg(pythonExecutable()));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();

    const PythonPackagePreflightResult mace =
        checkPythonPackage(pythonExecutable(), QStringLiteral("mace"));
    lastCheckAvailable_ = mace.available;
    QString text;
    if (mace.available) {
        detectedMaceVersion_ = mace.version;
        text = tr("mace-torch %1 found.")
                   .arg(mace.version.isEmpty() ? tr("(version unknown)")
                                               : mace.version);
        const TorchDeviceAvailability devices =
            probeTorchDevices(pythonExecutable());
        if (devices.probeSucceeded) {
            QStringList available{QStringLiteral("cpu")};
            if (devices.cuda)
                available << QStringLiteral("cuda");
            if (devices.mps)
                available << QStringLiteral("mps");
            text += tr(" PyTorch devices available: %1.")
                        .arg(available.join(QStringLiteral(", ")));
            // Suggest the best available device — cuda, then mps, then cpu
            // — as a DEFAULT, not forced: a user who already picked one
            // deliberately (e.g. testing the cpu path) is left alone.
            if (!manuallyEdited_) {
                if (devices.cuda)
                    deviceCombo_->setCurrentText(QStringLiteral("cuda"));
                else if (devices.mps)
                    deviceCombo_->setCurrentText(QStringLiteral("mps"));
            }
            const QString chosen = deviceCombo_->currentText();
            const bool chosenAvailable = chosen == QStringLiteral("cpu")
                || (chosen == QStringLiteral("cuda") && devices.cuda)
                || (chosen == QStringLiteral("mps") && devices.mps);
            if (!chosenAvailable)
                text += tr(" Warning: \"%1\" is selected but was not "
                          "reported as available — the run will likely "
                          "fail or silently fall back.")
                            .arg(chosen);
        } else {
            text += tr(" (could not probe which devices PyTorch itself "
                      "sees — device availability is unknown, not "
                      "necessarily absent.)");
        }
        envStatus_->setStyleSheet(QStringLiteral("color: #2e7d32;"));
    } else {
        detectedMaceVersion_.clear();
        text = tr("mace-torch was not found under %1: %2\n\nInstall it "
                  "with: pip install mace-torch")
                   .arg(pythonExecutable(), mace.errorMessage);
        envStatus_->setStyleSheet(QStringLiteral("color: #c0392b;"));
    }
    envStatus_->setText(text);
    QApplication::restoreOverrideCursor();
    refreshPreview();
}

bool MaceTrainerDialog::preflightMaceTorch()
{
    // Re-checked every time rather than trusting a stale
    // lastCheckAvailable_: the interpreter field is freely editable right
    // up to the moment Run is pressed, and a check against yesterday's
    // choice would be worse than no check at all — it would say "fine"
    // about an environment nobody is about to use.
    checkEnvironment();
    if (lastCheckAvailable_)
        return true;
    QMessageBox::warning(
        this, tr("mace-torch not found"),
        tr("%1\n\nNothing was launched.").arg(envStatus_->text()));
    return false;
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

QString MaceTrainerDialog::e0sValue() const
{
    const QString mode = e0sModeCombo_->currentData().toString();
    if (mode == QLatin1String("dataset"))
        return QString(); // the training file carries IsolatedAtom frames
    if (mode == QLatin1String("file")) {
        const QString path = e0sFileEdit_->text().trimmed();
        // An empty path would emit `E0s: ""`, which MACE parses as neither a
        // file nor a dict and rejects with a confusing message. Fall back to
        // the mode that always works.
        if (path.isEmpty())
            return QStringLiteral("average");
        return QFileInfo(path).absoluteFilePath();
    }
    return QStringLiteral("average");
}

QString MaceTrainerDialog::buildYaml() const
{
    const QString trainFile = trainFileEdit_->text().trimmed().isEmpty()
        ? QStringLiteral("train.xyz")
        : QFileInfo(trainFileEdit_->text().trimmed()).absoluteFilePath();
    const QString validFile = validFileEdit_->text().trimmed();

    QString y;
    y += QStringLiteral("# MACE training configuration — generated by Calango.\n");
    y += QStringLiteral("# Every key below is one mace.tools.arg_parser "
                        "accepts; MACE aborts on any it does not.\n");
    if (!detectedMaceVersion_.isEmpty())
        // Run metadata: which mace-torch this config was generated/verified
        // against, from the last successful "Check Environment"/Run
        // pre-flight — not vendored, not assumed, read off the package
        // actually installed under the interpreter above.
        y += QStringLiteral("# mace-torch %1 (detected under %2)\n")
                 .arg(detectedMaceVersion_, pythonExecutable());
    y += QStringLiteral("model: MACE\n");
    y += QStringLiteral("name: mace_model\n");
    y += QStringLiteral("train_file: \"%1\"\n").arg(trainFile);
    if (validFile.isEmpty()) {
        y += QStringLiteral("valid_fraction: 0.1\n");
    } else {
        y += QStringLiteral("valid_file: \"%1\"\n")
                 .arg(QFileInfo(validFile).absoluteFilePath());
    }

    // Where the reference values live in the training file. Always written,
    // never left to MACE's default: its default (REF_energy / REF_forces) does
    // not match what ASE — and therefore Calango's dataset export — writes,
    // and the mismatch costs a silent zero-weight run rather than an error.
    y += QStringLiteral("energy_key: \"%1\"\n")
             .arg(energyKeyCombo_->currentText().trimmed());
    y += QStringLiteral("forces_key: \"%1\"\n")
             .arg(forcesKeyCombo_->currentText().trimmed());
    // Isolated-atom energies. Without this (and without IsolatedAtom frames in
    // the training file) MACE raises before the first epoch.
    if (const QString e0s = e0sValue(); !e0s.isEmpty())
        y += QStringLiteral("E0s: \"%1\"\n").arg(e0s);

    y += QStringLiteral("r_max: %1\n").arg(rMaxSpin_->value());
    y += QStringLiteral("num_channels: %1\n").arg(channelsSpin_->value());
    y += QStringLiteral("max_L: %1\n").arg(maxLSpin_->value());
    // Plural. `num_interaction` also happens to work, because argparse accepts
    // any unambiguous prefix of a long option — which makes a typo here look
    // deliberate and survive review.
    y += QStringLiteral("num_interactions: 2\n");
    y += QStringLiteral("correlation: 3\n");
    y += QStringLiteral("batch_size: %1\n").arg(batchSpin_->value());
    y += QStringLiteral("max_num_epochs: %1\n").arg(epochsSpin_->value());
    y += QStringLiteral("lr: %1\n").arg(lrSpin_->value());
    y += QStringLiteral("patience: %1\n").arg(patienceSpin_->value());
    y += QStringLiteral("eval_interval: %1\n").arg(evalIntervalSpin_->value());
    y += QStringLiteral("loss: weighted\n");
    y += QStringLiteral("energy_weight: %1\n").arg(energyWeightSpin_->value());
    y += QStringLiteral("forces_weight: %1\n").arg(forcesWeightSpin_->value());
    // Emitted only when they carry weight. `loss: weighted` fits energy and
    // forces alone, so a zero stress/virials weight is a line that says
    // something is being fitted when nothing is — and it reads as a bug in the
    // dataset when the resulting model has no stress term.
    if (stressWeightSpin_->value() > 0.0)
        y += QStringLiteral("stress_weight: %1\n").arg(stressWeightSpin_->value());
    if (virialsWeightSpin_->value() > 0.0)
        y += QStringLiteral("virials_weight: %1\n").arg(virialsWeightSpin_->value());

    if (swaGroup_->isChecked()) {
        // MACE's two-phase schedule. `swa` is the historical spelling of what
        // the docs now call stage two; both map to the same argument.
        y += QStringLiteral("swa: true\n");
        y += QStringLiteral("start_swa: %1\n").arg(swaStartSpin_->value());
        if (emaCheck_->isChecked()) {
            y += QStringLiteral("ema: true\n");
            y += QStringLiteral("ema_decay: %1\n").arg(emaDecaySpin_->value());
        }
    }
    y += QStringLiteral("amsgrad: true\n");
    // Resume from the newest checkpoint instead of restarting from scratch —
    // the difference between a killed job costing an hour and costing the run.
    y += QStringLiteral("restart_latest: true\n");
    // Save a CPU copy of the model as well: a model saved only in CUDA tensors
    // cannot be loaded for inference on a machine without the training GPU,
    // which is exactly where an MLIP is used afterwards.
    y += QStringLiteral("save_cpu: true\n");
    y += QStringLiteral("default_dtype: %1\n").arg(dtypeCombo_->currentText());
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

void MaceTrainerDialog::setInitialYaml(const QString& yaml)
{
    const QSignalBlocker blocker(preview_);
    preview_->setPlainText(yaml);
    // Marks it hand-edited so refreshPreview() (fired by every settings
    // change below, e.g. applySizePreset() at the end of the constructor)
    // never clobbers the restored text with a freshly regenerated config —
    // the individual widgets keep their OWN constructor defaults, not
    // whatever the saved YAML actually contains, since this restores the
    // text only, not the widget state it was generated from.
    manuallyEdited_ = true;
}

void MaceTrainerDialog::prefillFromDatasetManifest(const QString& trainPath,
                                                    const QString& validPath,
                                                    const QString& energyKey,
                                                    const QString& forcesKey)
{
    if (!trainPath.isEmpty())
        trainFileEdit_->setText(trainPath);
    if (!validPath.isEmpty())
        validFileEdit_->setText(validPath);
    if (!energyKey.isEmpty())
        energyKeyCombo_->setCurrentText(energyKey);
    if (!forcesKey.isEmpty())
        forcesKeyCombo_->setCurrentText(forcesKey);
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
    s += QStringLiteral(
        "# MACE training launcher generated by Calango.\n"
        "#\n"
        "# Self-contained: needs mace-torch and nothing from Calango, so it\n"
        "# can be copied to a cluster and run as it stands.\n"
        "import os\n"
        "import subprocess\n"
        "import sys\n"
        "\n"
        "\n"
        "def train_mace(config_file_path):\n"
        "    \"\"\"Run MACE's trainer in-process against one config file.\n"
        "\n"
        "    This is MACE's own documented entry point rather than\n"
        "    `python -m mace.cli.run_train`: run_train.main() reads its\n"
        "    parameters from sys.argv, so handing it the argv it expects is\n"
        "    the invocation MACE actually supports, and it keeps working when\n"
        "    the package ships no runnable __main__.\n"
        "    \"\"\"\n"
        "    import json\n"
        "    import logging\n"
        "    import re\n"
        "    import warnings\n"
        "\n"
        "    warnings.filterwarnings(\"ignore\")\n"
        "    from mace.cli.run_train import main as mace_run_train\n"
        "\n"
        "    # MACE installs its own root handlers. Ours are still attached at\n"
        "    # this point, so without clearing them every line of the training\n"
        "    # log is emitted twice.\n"
        "    logging.getLogger().handlers.clear()\n"
        "\n"
        "    # Live per-epoch metrics: a SELF-CONTAINED handler that only\n"
        "    # regex-parses MACE's own \"Epoch N: ... loss=X, "
        "RMSE_E_per_atom=Y meV, RMSE_F=Z meV / A\" log lines and appends\n"
        "    # them to their own file, one config per process (so a\n"
        "    # committee's several children never write over each other).\n"
        "    # Deliberately NOT wired into the shared _calango_metric()\n"
        "    # progress file below: that one is per-SEED-completed (one\n"
        "    # entry per committee member, written by the PARENT process),\n"
        "    # and a re-entrant child appending its own per-epoch stream\n"
        "    # into the same file would overwrite the parent's own record —\n"
        "    # see the re-entry guard's comment above this function.\n"
        "    class _EpochMetricsHandler(logging.Handler):\n"
        "        _pattern = re.compile(\n"
        "            r\"(?:Epoch (?P<epoch>\\d+)|Initial): .*?\"\n"
        "            r\"loss=(?P<loss>[\\d.eE+-]+), \"\n"
        "            r\"RMSE_E_per_atom=\\s*(?P<rmse_e>[\\d.eE+-]+) meV, \"\n"
        "            r\"RMSE_F=\\s*(?P<rmse_f>[\\d.eE+-]+) meV\")\n"
        "\n"
        "        def __init__(self, path):\n"
        "            super().__init__()\n"
        "            self.path = path\n"
        "            self.entries = []\n"
        "\n"
        "        def emit(self, record):\n"
        "            match = self._pattern.search(record.getMessage())\n"
        "            if not match:\n"
        "                return\n"
        "            self.entries.append({\n"
        "                \"epoch\": int(match.group(\"epoch\")) "
        "if match.group(\"epoch\") else -1,\n"
        "                \"loss\": float(match.group(\"loss\")),\n"
        "                \"rmse_energy_mev_per_atom\": float(match.group(\"rmse_e\")),\n"
        "                \"rmse_forces_mev_per_a\": float(match.group(\"rmse_f\")),\n"
        "            })\n"
        "            try:\n"
        "                with open(self.path, \"w\") as fh:\n"
        "                    json.dump({\"metrics\": self.entries}, fh)\n"
        "            except OSError:\n"
        "                pass  # best-effort only -- never fail a training "
        "run over a metrics file\n"
        "\n"
        "    metrics_path = os.path.splitext(config_file_path)[0] + "
        "\"_metrics.json\"\n"
        "    epoch_handler = _EpochMetricsHandler(metrics_path)\n"
        "    epoch_handler.setLevel(logging.INFO)\n"
        "    logging.getLogger().addHandler(epoch_handler)\n"
        "\n"
        "    sys.argv = [\"program\", \"--config\", config_file_path]\n"
        "    mace_run_train()\n"
        "\n"
        "\n"
        "# Re-entry: `python <this file> <config.yaml>` trains exactly ONE\n"
        "# model and exits. The committee loop below launches the script this\n"
        "# way once per seed, because run_train.main() leaves global torch and\n"
        "# logging state behind that a second call in the same process\n"
        "# inherits — the seeds would stop being independent, which is the one\n"
        "# property a Query-by-Committee ensemble needs.\n"
        "#\n"
        "# The guard precedes the logging block deliberately: a child must not\n"
        "# truncate the metrics.json and warnings.log its parent is writing.\n"
        "if len(sys.argv) > 1:\n"
        "    train_mace(sys.argv[1])\n"
        "    raise SystemExit(0)\n"
        "\n");
    // Shared JSON logger (warnings.log + metrics.json progress) so training
    // progress is written to metrics.json instead of stdout.
    s += QString::fromStdString(core::AseScriptGenerator::jsonLoggerPreamble());
    s += QStringLiteral("CONFIG = r\"\"\"\n");
    s += yaml();
    if (!yaml().endsWith(QLatin1Char('\n')))
        s += QLatin1Char('\n');
    s += QStringLiteral("\"\"\"\n\n");
    s += QStringLiteral(
        "\n"
        "def config_for(seed, name):\n"
        "    \"\"\"CONFIG with `seed` and `name` replaced, as one YAML file.\n"
        "\n"
        "    Per-seed files rather than `--seed`/`--name` overrides on the\n"
        "    command line: MACE reads the config through configargparse, so\n"
        "    the file is the whole parameter set and one reviewable document\n"
        "    per model is what makes a committee reproducible afterwards.\n"
        "    \"\"\"\n"
        "    # Only column-0 keys are top level — the nested entries under\n"
        "    # E0s: are indented and must survive.\n"
        "    kept = [line for line in CONFIG.splitlines()\n"
        "            if not line.startswith((\"seed:\", \"name:\"))]\n"
        "    kept += [f'name: \"{name}\"', f\"seed: {seed}\"]\n"
        "    return \"\\n\".join(kept) + \"\\n\"\n"
        "\n"
        "\n");
    s += QStringLiteral("# Query-by-Committee ensemble: one training run per "
                        "seed.\n");
    s += QStringLiteral("seeds = [%1]\n").arg(seeds.join(QStringLiteral(", ")));
    s += QStringLiteral(
        "for _i, _seed in enumerate(seeds):\n"
        "    _calango_progress(_i, len(seeds))\n"
        "    _name = f\"mace_model_{_seed}\" if len(seeds) > 1 else "
        "\"mace_model\"\n"
        "    _config = f\"mace_train_{_seed}.yaml\"\n"
        "    with open(_config, \"w\") as _fh:\n"
        "        _fh.write(config_for(_seed, _name))\n"
        "    print(f\"CALANGO_INFO training MACE model seed={_seed}\", "
        "flush=True)\n"
        "    _calango_event(\"info\", f\"training {_name} from {_config}\")\n"
        "    subprocess.run([sys.executable, os.path.abspath(__file__), "
        "_config],\n"
        "                   check=True)\n"
        "\n"
        "_calango_progress(len(seeds), len(seeds))\n"
        "_calango_event(\"done\", f\"{len(seeds)} MACE model(s) trained\")\n");
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

void MaceTrainerDialog::browseValidFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select Validation Set"), validFileEdit_->text(),
        tr("Structures (*.xyz *.extxyz);;All files (*)"));
    if (!path.isEmpty())
        validFileEdit_->setText(path);
}

void MaceTrainerDialog::browseE0sFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select Isolated-Atom Energies"), e0sFileEdit_->text(),
        tr("JSON files (*.json);;All files (*)"));
    if (!path.isEmpty())
        e0sFileEdit_->setText(path);
}

} // namespace calango::gui
