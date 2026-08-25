#include "gui/MaceTrainerBackend.hpp"

#include "core/AseScriptGenerator.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>
#include <QWizardPage>

namespace calango::gui {

namespace {

/// A wizard page whose content scrolls vertically.
///
/// The same wrapper SimulationWizardBase::wrapInScrollArea() applies, and
/// for the same reason: a settings page grows one reasonable addition at a
/// time until it runs off the bottom of a laptop screen, and no single
/// addition can see the total. Vertical only — a horizontally scrolled
/// settings row is a worse outcome than the overflow it would fix.
QWizardPage* makeScrollingPage(QWidget* parent, const QString& title,
                               const QString& subTitle, QWidget* content)
{
    auto* page = new QWizardPage(parent);
    page->setTitle(title);
    page->setSubTitle(subTitle);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content->setParent(scroll);
    scroll->setWidget(content);
    layout->addWidget(scroll);
    return page;
}

/// A muted, word-wrapped explanatory line — the "Advanced" groups' own
/// preamble, so a collapsed-by-habit reader still knows what is inside.
QLabel* note(QWidget* parent, const QString& text)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}

} // namespace

MaceTrainerBackend::MaceTrainerBackend() = default;

core::CalculatorKind MaceTrainerBackend::kind() const
{
    return core::CalculatorKind::Mace;
}

QList<QWizardPage*> MaceTrainerBackend::createParameterPages(QWidget* parent)
{
    QList<QWizardPage*> pages;

    // ================= Page: Dataset ==================================
    //
    // First because it is first in the work: the trainer consumes a dataset,
    // and the commonest way a MACE run fails is a dataset whose energy /
    // forces keys or E0s were never checked. Both of those are on this page,
    // prominently, rather than filed under an architecture heading.
    {
        auto* content = new QWidget(parent);
        auto* form = new QFormLayout(content);

        auto* trainRow = new QHBoxLayout;
        trainFileEdit_ = new QLineEdit(content);
        trainFileEdit_->setObjectName(QStringLiteral("maceTrainFile"));
        trainFileEdit_->setPlaceholderText(
            QObject::tr("training set (.xyz / .extxyz)"));
        auto* trainBrowse = new QPushButton(QObject::tr("Browse…"), content);
        trainRow->addWidget(trainFileEdit_, 1);
        trainRow->addWidget(trainBrowse);
        form->addRow(QObject::tr("Training file:"), trainRow);
        QObject::connect(trainBrowse, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(
                nullptr, QObject::tr("Select Training Set"),
                trainFileEdit_->text(),
                QObject::tr("Structures (*.xyz *.extxyz);;All files (*)"));
            if (!path.isEmpty())
                trainFileEdit_->setText(path);
        });

        auto* validRow = new QHBoxLayout;
        validFileEdit_ = new QLineEdit(content);
        validFileEdit_->setObjectName(QStringLiteral("maceValidFile"));
        validFileEdit_->setPlaceholderText(
            QObject::tr("optional — else 10% is held out"));
        validFileEdit_->setToolTip(QObject::tr(
            "An explicit validation set, as the Dataset Manager's split "
            "produces. Left empty, MACE holds out a tenth of the training "
            "file at random (valid_fraction), which is fine for a first look "
            "and wrong for comparing runs — the held-out set differs every "
            "time."));
        auto* validBrowse = new QPushButton(QObject::tr("Browse…"), content);
        validRow->addWidget(validFileEdit_, 1);
        validRow->addWidget(validBrowse);
        form->addRow(QObject::tr("Validation file:"), validRow);
        QObject::connect(validBrowse, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(
                nullptr, QObject::tr("Select Validation Set"),
                validFileEdit_->text(),
                QObject::tr("Structures (*.xyz *.extxyz);;All files (*)"));
            if (!path.isEmpty())
                validFileEdit_->setText(path);
        });

        // The two keys that decide whether the run learns anything at all.
        //
        // MACE defaults to REF_energy / REF_forces. Calango writes its
        // datasets with plain ASE, which stores the energy and forces on a
        // SinglePointCalculator; read back, atoms.info and atoms.arrays carry
        // NEITHER key. MACE does not fail on that: it warns, sets the
        // per-property weight to zero, and trains a model on nothing. Naming
        // the keys "energy" and "forces" takes MACE's ASE-compatibility path,
        // which pulls the values off the calculator and rewrites them as
        // REF_*.
        energyKeyCombo_ = new QComboBox(content);
        energyKeyCombo_->setObjectName(QStringLiteral("maceEnergyKey"));
        energyKeyCombo_->setEditable(true);
        energyKeyCombo_->addItems(
            {QStringLiteral("energy"), QStringLiteral("REF_energy")});
        energyKeyCombo_->setToolTip(QObject::tr(
            "Where the reference energy is stored in the training file.\n\n"
            "\"energy\" is what ASE writes — and therefore what a dataset "
            "exported from Calango carries. \"REF_energy\" is MACE's own "
            "default, for sets prepared with MACE's conventions.\n\n"
            "Getting this wrong does not fail the run: MACE drops the energy "
            "term to zero weight and trains anyway."));
        form->addRow(QObject::tr("Energy key:"), energyKeyCombo_);

        forcesKeyCombo_ = new QComboBox(content);
        forcesKeyCombo_->setObjectName(QStringLiteral("maceForcesKey"));
        forcesKeyCombo_->setEditable(true);
        forcesKeyCombo_->addItems(
            {QStringLiteral("forces"), QStringLiteral("REF_forces")});
        forcesKeyCombo_->setToolTip(QObject::tr(
            "Where the reference forces are stored. Same rule as the energy "
            "key above: \"forces\" for an ASE/Calango dataset, "
            "\"REF_forces\" for a MACE-native one."));
        form->addRow(QObject::tr("Forces key:"), forcesKeyCombo_);

        // Isolated-atom energies. Not optional in any sense that matters:
        // with E0s unset and no config_type=IsolatedAtom entries in the
        // training file — which a Calango-exported set never has — MACE
        // raises "E0s not found in training file and not specified in command
        // line" before the first epoch.
        e0sModeCombo_ = new QComboBox(content);
        e0sModeCombo_->setObjectName(QStringLiteral("maceE0sMode"));
        e0sModeCombo_->addItem(
            QObject::tr("Average (least-squares fit to the set)"),
            QStringLiteral("average"));
        e0sModeCombo_->addItem(QObject::tr("From a JSON file (Z → energy)"),
                               QStringLiteral("file"));
        e0sModeCombo_->addItem(
            QObject::tr("Isolated atoms in the training file"),
            QStringLiteral("dataset"));
        e0sModeCombo_->setToolTip(QObject::tr(
            "The one-atom reference energies MACE subtracts before fitting, "
            "so the model learns interactions rather than the huge constant "
            "offsets of the atomic totals.\n\n"
            "• Average — regress them out of the training set itself. The "
            "right default: it needs nothing extra and is what MACE "
            "recommends when isolated-atom calculations are not to hand.\n"
            "• JSON file — {\"42\": -5.0448, \"16\": -0.9036} from your own "
            "isolated-atom runs. The most accurate option, and the one to "
            "use when several models must share a reference.\n"
            "• Isolated atoms in the training file — only if the set "
            "contains single-atom frames tagged config_type=IsolatedAtom.\n\n"
            "There is no \"leave it out\": with none of these, MACE aborts "
            "before the first epoch."));
        form->addRow(QObject::tr("Isolated-atom energies:"), e0sModeCombo_);

        auto* e0sRow = new QHBoxLayout;
        e0sFileEdit_ = new QLineEdit(content);
        e0sFileEdit_->setObjectName(QStringLiteral("maceE0sFile"));
        e0sFileEdit_->setPlaceholderText(QStringLiteral("E0s.json"));
        auto* e0sBrowse = new QPushButton(QObject::tr("Browse…"), content);
        e0sRow->addWidget(e0sFileEdit_, 1);
        e0sRow->addWidget(e0sBrowse);
        form->addRow(QObject::tr("E0s file:"), e0sRow);
        QObject::connect(e0sBrowse, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(
                nullptr, QObject::tr("Select Isolated-Atom Energies"),
                e0sFileEdit_->text(),
                QObject::tr("JSON files (*.json);;All files (*)"));
            if (!path.isEmpty())
                e0sFileEdit_->setText(path);
        });

        // The E0s file row is only meaningful for the "from a JSON file" mode.
        const auto syncE0sFileRow = [this, e0sBrowse] {
            const bool fromFile = e0sModeCombo_->currentData().toString()
                == QLatin1String("file");
            e0sFileEdit_->setEnabled(fromFile);
            e0sBrowse->setEnabled(fromFile);
        };
        syncE0sFileRow();
        QObject::connect(e0sModeCombo_, &QComboBox::currentIndexChanged, this,
                         syncE0sFileRow);

        form->addRow(note(content,
                          QObject::tr(
                              "<i>The Dataset Manager writes all four of "
                              "these into its manifest, and an Orchestration "
                              "Dataset Manager node wired into this one "
                              "fills them in for you.</i>")));
        pages << makeScrollingPage(
            parent, QObject::tr("Dataset"),
            QObject::tr("Where the reference data is, and how it is named. "
                        "Both of the settings a MACE run most often dies on "
                        "are here."),
            content);
    }

    // ================= Page: Model ====================================
    {
        auto* content = new QWidget(parent);
        auto* outer = new QVBoxLayout(content);
        auto* basic = new QGroupBox(QObject::tr("Architecture"), content);
        auto* form = new QFormLayout(basic);

        sizeCombo_ = new QComboBox(basic);
        sizeCombo_->setObjectName(QStringLiteral("maceSizePreset"));
        sizeCombo_->addItems({QObject::tr("small"), QObject::tr("medium"),
                              QObject::tr("large")});
        sizeCombo_->setCurrentIndex(1);
        sizeCombo_->setToolTip(QObject::tr(
            "MACE's own three sizes, as channel count and equivariance "
            "order. Picking one SETS the two controls below, which stay "
            "editable — the preset is a starting point, not a lock."));
        form->addRow(QObject::tr("Model architecture:"), sizeCombo_);

        rMaxSpin_ = new QDoubleSpinBox(basic);
        rMaxSpin_->setObjectName(QStringLiteral("maceRMax"));
        rMaxSpin_->setRange(2.0, 12.0);
        rMaxSpin_->setDecimals(2);
        rMaxSpin_->setSingleStep(0.5);
        rMaxSpin_->setValue(5.0);
        rMaxSpin_->setSuffix(QObject::tr(" Å"));
        rMaxSpin_->setToolTip(QObject::tr(
            "Cut-off radius of the local environment each message-passing "
            "layer sees. The EFFECTIVE range of the model is this times the "
            "number of interactions below, which is why 5 Å with two layers "
            "already reaches 10 Å."));
        form->addRow(QObject::tr("Cutoff radius (r_max):"), rMaxSpin_);

        channelsSpin_ = new QSpinBox(basic);
        channelsSpin_->setObjectName(QStringLiteral("maceChannels"));
        channelsSpin_->setRange(8, 1024);
        channelsSpin_->setValue(128);
        form->addRow(QObject::tr("Number of channels:"), channelsSpin_);

        maxLSpin_ = new QSpinBox(basic);
        maxLSpin_->setObjectName(QStringLiteral("maceMaxL"));
        maxLSpin_->setRange(0, 4);
        maxLSpin_->setValue(1);
        maxLSpin_->setToolTip(QObject::tr(
            "Highest spherical-harmonic order the messages carry. 0 is "
            "invariant (fast, blind to angular detail); each step up costs "
            "roughly a factor in time and buys accuracy on directional "
            "bonding."));
        form->addRow(QObject::tr("Max L (equivariance):"), maxLSpin_);
        outer->addWidget(basic);

        // -- Advanced ------------------------------------------------------
        //
        // The two constants this dialog used to WRITE INTO EVERY CONFIG with
        // no control at all. They are here rather than on the page above
        // because the defaults are right for almost every run and getting
        // them wrong is expensive; they are here rather than hard-coded
        // because "I need three message-passing layers" is a real request
        // that used to mean hand-editing the YAML.
        auto* advanced = new QGroupBox(QObject::tr("Advanced"), content);
        auto* advancedForm = new QFormLayout(advanced);
        advancedForm->addRow(note(
            advanced,
            QObject::tr("<i>Defaults are MACE's own published architecture. "
                        "Change them only against a paper or a benchmark — "
                        "both cost time superlinearly.</i>")));

        interactionsSpin_ = new QSpinBox(advanced);
        interactionsSpin_->setObjectName(QStringLiteral("maceInteractions"));
        interactionsSpin_->setRange(1, 5);
        interactionsSpin_->setValue(2);
        interactionsSpin_->setToolTip(QObject::tr(
            "Message-passing layers. Two is MACE's default and what every "
            "published MACE model uses; the receptive field is this times "
            "r_max, so three layers at 5 Å reaches 15 Å and costs "
            "accordingly."));
        advancedForm->addRow(QObject::tr("Interaction layers:"),
                             interactionsSpin_);

        correlationSpin_ = new QSpinBox(advanced);
        correlationSpin_->setObjectName(QStringLiteral("maceCorrelation"));
        correlationSpin_->setRange(1, 5);
        correlationSpin_->setValue(3);
        correlationSpin_->setToolTip(QObject::tr(
            "Body order of the equivariant product basis — 3 gives MACE its "
            "characteristic 4-body messages. This is the parameter the MACE "
            "architecture is named for; lowering it makes a cheaper, "
            "less expressive model."));
        advancedForm->addRow(QObject::tr("Correlation order:"),
                             correlationSpin_);
        outer->addWidget(advanced);
        outer->addStretch(1);

        QObject::connect(sizeCombo_, &QComboBox::currentIndexChanged, this,
                         [this] { applySizePreset(); });

        pages << makeScrollingPage(
            parent, QObject::tr("Model"),
            QObject::tr("What is being fitted. The preset sets the two "
                        "architecture numbers below it; both stay editable."),
            content);
    }

    // ================= Page: Training =================================
    {
        auto* content = new QWidget(parent);
        auto* outer = new QVBoxLayout(content);

        auto* optGroup = new QGroupBox(QObject::tr("Optimization"), content);
        auto* optForm = new QFormLayout(optGroup);
        lrSpin_ = new QDoubleSpinBox(optGroup);
        lrSpin_->setObjectName(QStringLiteral("maceLearningRate"));
        lrSpin_->setDecimals(5);
        lrSpin_->setRange(1e-5, 1.0);
        lrSpin_->setSingleStep(0.001);
        lrSpin_->setValue(0.01);
        optForm->addRow(QObject::tr("Learning rate:"), lrSpin_);

        batchSpin_ = new QSpinBox(optGroup);
        batchSpin_->setObjectName(QStringLiteral("maceBatchSize"));
        batchSpin_->setRange(1, 4096);
        batchSpin_->setValue(10);
        optForm->addRow(QObject::tr("Batch size:"), batchSpin_);

        epochsSpin_ = new QSpinBox(optGroup);
        epochsSpin_->setObjectName(QStringLiteral("maceEpochs"));
        epochsSpin_->setRange(1, 100000);
        epochsSpin_->setValue(200);
        optForm->addRow(QObject::tr("Max epochs:"), epochsSpin_);

        deviceCombo_ = new QComboBox(optGroup);
        deviceCombo_->setObjectName(QStringLiteral("maceDevice"));
        deviceCombo_->addItems({QStringLiteral("cpu"), QStringLiteral("cuda"),
                                QStringLiteral("mps")});
        deviceCombo_->setToolTip(QObject::tr(
            "Compute device. {guilabel}Check Environment on the final page "
            "reports which of these the installed PyTorch can actually use, "
            "and defaults this to the best one it finds — until you pick one "
            "yourself, after which it leaves your choice alone."));
        optForm->addRow(QObject::tr("Device:"), deviceCombo_);

        seedSpin_ = new QSpinBox(optGroup);
        seedSpin_->setObjectName(QStringLiteral("maceSeed"));
        seedSpin_->setRange(0, 1000000);
        seedSpin_->setValue(123);
        optForm->addRow(QObject::tr("Base random seed:"), seedSpin_);
        outer->addWidget(optGroup);

        auto* lossGroup = new QGroupBox(QObject::tr("Loss weights"), content);
        auto* lossForm = new QFormLayout(lossGroup);
        const auto weightSpin = [lossGroup](double value, const char* name) {
            auto* spin = new QDoubleSpinBox(lossGroup);
            spin->setObjectName(QString::fromLatin1(name));
            spin->setRange(0.0, 100000.0);
            spin->setDecimals(2);
            spin->setValue(value);
            return spin;
        };
        energyWeightSpin_ = weightSpin(1.0, "maceEnergyWeight");
        forcesWeightSpin_ = weightSpin(100.0, "maceForcesWeight");
        stressWeightSpin_ = weightSpin(0.0, "maceStressWeight");
        virialsWeightSpin_ = weightSpin(0.0, "maceVirialsWeight");
        lossForm->addRow(QObject::tr("Energy weight:"), energyWeightSpin_);
        lossForm->addRow(QObject::tr("Forces weight:"), forcesWeightSpin_);
        lossForm->addRow(QObject::tr("Stress weight:"), stressWeightSpin_);
        lossForm->addRow(QObject::tr("Virials weight:"), virialsWeightSpin_);
        lossForm->addRow(note(
            lossGroup,
            QObject::tr("<i>A weight left at zero is not written into the "
                        "config at all: under <tt>loss: weighted</tt> it "
                        "fits nothing, and a line saying otherwise reads as "
                        "a stress term that exists when none does.</i>")));
        outer->addWidget(lossGroup);

        // -- Advanced ------------------------------------------------------
        auto* advanced = new QGroupBox(QObject::tr("Advanced"), content);
        auto* advancedLayout = new QVBoxLayout(advanced);
        auto* scheduleForm = new QFormLayout;

        patienceSpin_ = new QSpinBox(advanced);
        patienceSpin_->setObjectName(QStringLiteral("macePatience"));
        patienceSpin_->setRange(1, 100000);
        patienceSpin_->setValue(50);
        patienceSpin_->setToolTip(QObject::tr(
            "Stop after this many epochs with no improvement in the "
            "validation loss. MACE's own default is 2048 — i.e. effectively "
            "never — which means an over-long run keeps burning time after "
            "it has converged."));
        scheduleForm->addRow(QObject::tr("Early-stopping patience:"),
                             patienceSpin_);

        evalIntervalSpin_ = new QSpinBox(advanced);
        evalIntervalSpin_->setObjectName(QStringLiteral("maceEvalInterval"));
        evalIntervalSpin_->setRange(1, 1000);
        evalIntervalSpin_->setValue(5);
        evalIntervalSpin_->setToolTip(QObject::tr(
            "Evaluate on the validation set every N epochs. Every epoch "
            "(MACE's default) is a real cost on a large set for a curve "
            "that barely moves in one step."));
        scheduleForm->addRow(QObject::tr("Validation interval:"),
                             evalIntervalSpin_);

        dtypeCombo_ = new QComboBox(advanced);
        dtypeCombo_->setObjectName(QStringLiteral("maceDtype"));
        dtypeCombo_->addItems(
            {QStringLiteral("float64"), QStringLiteral("float32")});
        dtypeCombo_->setCurrentIndex(1);
        dtypeCombo_->setToolTip(QObject::tr(
            "Training precision. float32 is roughly twice as fast and is "
            "what production MACE models are trained in; float64 matches "
            "MACE's own default and is worth it when the model must "
            "reproduce DFT energy differences at the meV level.\n\n"
            "Whatever is chosen here, the ASE calculator that later loads "
            "the model has to be told the same dtype."));
        scheduleForm->addRow(QObject::tr("Precision:"), dtypeCombo_);
        advancedLayout->addLayout(scheduleForm);

        // Stage two: MACE's standard two-phase schedule. The second phase
        // raises the energy weight sharply and drops the learning rate,
        // which is what turns a model with good forces into one with good
        // energies as well.
        swaGroup_ = new QGroupBox(QObject::tr("Stage two (SWA) && averaging"),
                                  advanced);
        swaGroup_->setObjectName(QStringLiteral("maceStageTwo"));
        swaGroup_->setCheckable(true);
        swaGroup_->setChecked(true);
        swaGroup_->setToolTip(QObject::tr(
            "Switch to the stage-two loss after the epoch below. Standard "
            "practice for MACE and off by default in MACE itself, so a "
            "config that does not ask for it trains in stage one only."));
        auto* swaForm = new QFormLayout(swaGroup_);
        swaStartSpin_ = new QSpinBox(swaGroup_);
        swaStartSpin_->setObjectName(QStringLiteral("maceStageTwoStart"));
        swaStartSpin_->setRange(1, 100000);
        swaStartSpin_->setValue(150);
        swaStartSpin_->setToolTip(QObject::tr(
            "Epoch at which stage two begins. Conventionally around three "
            "quarters of the epoch budget — early enough for the second "
            "phase to converge, late enough that the first has done its "
            "work."));
        swaForm->addRow(QObject::tr("Start at epoch:"), swaStartSpin_);

        emaCheck_ = new QCheckBox(
            QObject::tr("Exponential moving average of the weights"),
            swaGroup_);
        emaCheck_->setChecked(true);
        emaCheck_->setToolTip(QObject::tr(
            "Evaluate and save an exponentially-weighted average of the "
            "weights rather than the last step's. Costs nothing and "
            "reliably smooths the noise a small batch size puts into the "
            "final model."));
        swaForm->addRow(QString(), emaCheck_);

        emaDecaySpin_ = new QDoubleSpinBox(swaGroup_);
        emaDecaySpin_->setObjectName(QStringLiteral("maceEmaDecay"));
        emaDecaySpin_->setDecimals(4);
        emaDecaySpin_->setRange(0.5, 0.9999);
        emaDecaySpin_->setSingleStep(0.01);
        emaDecaySpin_->setValue(0.99);
        swaForm->addRow(QObject::tr("EMA decay:"), emaDecaySpin_);
        QObject::connect(emaCheck_, &QCheckBox::toggled, emaDecaySpin_,
                         &QWidget::setEnabled);
        advancedLayout->addWidget(swaGroup_);

        // Active learning / Query by Committee.
        qbcGroup_ = new QGroupBox(
            QObject::tr("Active Learning (Query by Committee)"), advanced);
        qbcGroup_->setObjectName(QStringLiteral("maceQbc"));
        qbcGroup_->setCheckable(true);
        qbcGroup_->setChecked(false);
        auto* qbcForm = new QFormLayout(qbcGroup_);
        committeeSpin_ = new QSpinBox(qbcGroup_);
        committeeSpin_->setObjectName(QStringLiteral("maceCommitteeSize"));
        committeeSpin_->setRange(2, 16);
        committeeSpin_->setValue(3);
        committeeSpin_->setToolTip(QObject::tr(
            "Number of independently-seeded models in the committee."));
        qbcForm->addRow(QObject::tr("Committee size (N):"), committeeSpin_);
        uncertaintySpin_ = new QDoubleSpinBox(qbcGroup_);
        uncertaintySpin_->setObjectName(QStringLiteral("maceUncertainty"));
        uncertaintySpin_->setDecimals(4);
        uncertaintySpin_->setRange(0.0, 100.0);
        uncertaintySpin_->setSingleStep(0.01);
        uncertaintySpin_->setValue(0.05);
        uncertaintySpin_->setSuffix(QObject::tr(" eV/Å"));
        uncertaintySpin_->setToolTip(QObject::tr(
            "Force-disagreement threshold that flags a configuration for "
            "labelling in the active-learning loop."));
        qbcForm->addRow(QObject::tr("Uncertainty threshold:"),
                        uncertaintySpin_);
        advancedLayout->addWidget(qbcGroup_);
        outer->addWidget(advanced);
        outer->addStretch(1);

        // Stage two must have room to run AND to checkpoint. MACE saves a
        // checkpoint on the evaluation cadence, so a start_swa that leaves
        // fewer than eval_interval epochs behind it produces no stage-two
        // checkpoint — and MACE then dies at the very end of an otherwise
        // successful run, trying to load the checkpoint it never wrote ("No
        // SWA checkpoint found", then an UnboundLocalError from its
        // checkpoint handler). Clamping the control is the only way a user
        // finds that out before burning the epochs.
        const auto syncSwaRange = [this] {
            const int room = epochsSpin_->value() - evalIntervalSpin_->value();
            swaStartSpin_->setMaximum(qMax(1, room));
        };
        syncSwaRange();
        QObject::connect(epochsSpin_, &QSpinBox::valueChanged, this,
                         syncSwaRange);
        QObject::connect(evalIntervalSpin_, &QSpinBox::valueChanged, this,
                         syncSwaRange);
        QObject::connect(deviceCombo_, &QComboBox::activated, this,
                         [this](int) { deviceChosenByHand_ = true; });

        pages << makeScrollingPage(
            parent, QObject::tr("Training"),
            QObject::tr("How it is fitted. The common controls first; the "
                        "schedule, the two-phase loss and the "
                        "active-learning committee under Advanced."),
            content);
    }

    applySizePreset();
    connectSettingSignals();
    return pages;
}

void MaceTrainerBackend::connectSettingSignals()
{
    for (QDoubleSpinBox* spin : {rMaxSpin_, lrSpin_, energyWeightSpin_,
                                 forcesWeightSpin_, stressWeightSpin_,
                                 virialsWeightSpin_, uncertaintySpin_,
                                 emaDecaySpin_})
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, this,
                         [this] { Q_EMIT settingsChanged(); });
    for (QSpinBox* spin : {channelsSpin_, maxLSpin_, batchSpin_, epochsSpin_,
                           seedSpin_, committeeSpin_, patienceSpin_,
                           evalIntervalSpin_, swaStartSpin_,
                           interactionsSpin_, correlationSpin_})
        QObject::connect(spin, &QSpinBox::valueChanged, this,
                         [this] { Q_EMIT settingsChanged(); });
    for (QComboBox* combo : {deviceCombo_, dtypeCombo_, energyKeyCombo_,
                             forcesKeyCombo_, e0sModeCombo_, sizeCombo_})
        QObject::connect(combo, &QComboBox::currentTextChanged, this,
                         [this] { Q_EMIT settingsChanged(); });
    for (QLineEdit* edit : {trainFileEdit_, validFileEdit_, e0sFileEdit_})
        QObject::connect(edit, &QLineEdit::textChanged, this,
                         [this] { Q_EMIT settingsChanged(); });
    for (QGroupBox* group : {qbcGroup_, swaGroup_})
        QObject::connect(group, &QGroupBox::toggled, this,
                         [this] { Q_EMIT settingsChanged(); });
    QObject::connect(emaCheck_, &QCheckBox::toggled, this,
                     [this] { Q_EMIT settingsChanged(); });
}

void MaceTrainerBackend::applySizePreset()
{
    if (!sizeCombo_ || !channelsSpin_ || !maxLSpin_)
        return;
    switch (sizeCombo_->currentIndex()) {
    case 0: channelsSpin_->setValue(64);  maxLSpin_->setValue(0); break;
    case 2: channelsSpin_->setValue(192); maxLSpin_->setValue(2); break;
    default: channelsSpin_->setValue(128); maxLSpin_->setValue(1); break;
    }
}

QString MaceTrainerBackend::e0sValue() const
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

QString MaceTrainerBackend::buildConfig(const QString& interpreterNote) const
{
    const QString trainFile = trainFileEdit_->text().trimmed().isEmpty()
        ? QStringLiteral("train.xyz")
        : QFileInfo(trainFileEdit_->text().trimmed()).absoluteFilePath();
    const QString validFile = validFileEdit_->text().trimmed();

    QString y;
    y += QStringLiteral(
        "# MACE training configuration — generated by Calango.\n");
    y += QStringLiteral("# Every key below is one mace.tools.arg_parser "
                        "accepts; MACE aborts on any it does not.\n");
    if (!interpreterNote.isEmpty())
        // Run metadata: which mace-torch this config was generated/verified
        // against, from the last successful "Check Environment"/Run
        // pre-flight — not vendored, not assumed, read off the package
        // actually installed under the chosen interpreter.
        y += QStringLiteral("# %1\n").arg(interpreterNote);
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
    // never left to MACE's default: its default (REF_energy / REF_forces)
    // does not match what ASE — and therefore Calango's dataset export —
    // writes, and the mismatch costs a silent zero-weight run rather than an
    // error.
    y += QStringLiteral("energy_key: \"%1\"\n")
             .arg(energyKeyCombo_->currentText().trimmed());
    y += QStringLiteral("forces_key: \"%1\"\n")
             .arg(forcesKeyCombo_->currentText().trimmed());
    // Isolated-atom energies. Without this (and without IsolatedAtom frames
    // in the training file) MACE raises before the first epoch.
    if (const QString e0s = e0sValue(); !e0s.isEmpty())
        y += QStringLiteral("E0s: \"%1\"\n").arg(e0s);

    y += QStringLiteral("r_max: %1\n").arg(rMaxSpin_->value());
    y += QStringLiteral("num_channels: %1\n").arg(channelsSpin_->value());
    y += QStringLiteral("max_L: %1\n").arg(maxLSpin_->value());
    // Plural. `num_interaction` also happens to work, because argparse
    // accepts any unambiguous prefix of a long option — which makes a typo
    // here look deliberate and survive review.
    y += QStringLiteral("num_interactions: %1\n")
             .arg(interactionsSpin_->value());
    y += QStringLiteral("correlation: %1\n").arg(correlationSpin_->value());
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
    // something is being fitted when nothing is — and it reads as a bug in
    // the dataset when the resulting model has no stress term.
    if (stressWeightSpin_->value() > 0.0)
        y += QStringLiteral("stress_weight: %1\n")
                 .arg(stressWeightSpin_->value());
    if (virialsWeightSpin_->value() > 0.0)
        y += QStringLiteral("virials_weight: %1\n")
                 .arg(virialsWeightSpin_->value());

    if (swaGroup_->isChecked()) {
        // MACE's two-phase schedule. `swa` is the historical spelling of
        // what the docs now call stage two; both map to the same argument.
        y += QStringLiteral("swa: true\n");
        y += QStringLiteral("start_swa: %1\n").arg(swaStartSpin_->value());
        if (emaCheck_->isChecked()) {
            y += QStringLiteral("ema: true\n");
            y += QStringLiteral("ema_decay: %1\n").arg(emaDecaySpin_->value());
        }
    }
    y += QStringLiteral("amsgrad: true\n");
    // Resume from the newest checkpoint instead of restarting from scratch —
    // the difference between a killed job costing an hour and costing the
    // run.
    y += QStringLiteral("restart_latest: true\n");
    // Save a CPU copy of the model as well: a model saved only in CUDA
    // tensors cannot be loaded for inference on a machine without the
    // training GPU, which is exactly where an MLIP is used afterwards.
    y += QStringLiteral("save_cpu: true\n");
    y += QStringLiteral("default_dtype: %1\n").arg(dtypeCombo_->currentText());
    y += QStringLiteral("device: %1\n").arg(deviceCombo_->currentText());
    y += QStringLiteral("seed: %1\n").arg(seedSpin_->value());

    if (qbcGroup_->isChecked()) {
        y += QStringLiteral(
            "\n# --- Active learning (Query by Committee) ---\n");
        y += QStringLiteral("# committee_size: %1\n")
                 .arg(committeeSpin_->value());
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

void MaceTrainerBackend::prefillFromDatasetManifest(const QString& trainPath,
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

void MaceTrainerBackend::applyTorchDevices(bool cuda, bool mps,
                                           bool probeSucceeded)
{
    if (!probeSucceeded || !deviceCombo_ || deviceChosenByHand_)
        return;
    // Suggest the best available device — cuda, then mps, then cpu — as a
    // DEFAULT, not forced: a user who already picked one deliberately (e.g.
    // testing the cpu path) is left alone, which is what
    // deviceChosenByHand_ guards.
    if (cuda)
        deviceCombo_->setCurrentText(QStringLiteral("cuda"));
    else if (mps)
        deviceCombo_->setCurrentText(QStringLiteral("mps"));
}

QString MaceTrainerBackend::selectedDevice() const
{
    return deviceCombo_ ? deviceCombo_->currentText() : QString();
}

QString MaceTrainerBackend::runnerScript(const QString& config) const
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
    s += config;
    if (!config.endsWith(QLatin1Char('\n')))
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

} // namespace calango::gui
