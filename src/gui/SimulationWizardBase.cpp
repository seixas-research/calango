#include "gui/SimulationWizardBase.hpp"

#include "gui/CondaEnvs.hpp"
#include "gui/EnginePresets.hpp"
#include "gui/PythonHighlighter.hpp"
#include "gui/ScriptStaging.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QTextStream>
#include <QVBoxLayout>

namespace calango::gui {

namespace {
const auto kEnvSettingsKey = QStringLiteral("jobs/environmentPath");

/// Hide/show the QFormLayout row (label + field) that `field` occupies inside
/// `group`'s form layout. No-op if the group has no form layout or the field
/// isn't in it.
void setFormRowVisible(QGroupBox* group, QWidget* field, bool visible)
{
    if (!group || !field)
        return;
    auto* form = qobject_cast<QFormLayout*>(group->layout());
    if (!form)
        return;
    int row = -1;
    QFormLayout::ItemRole role{};
    form->getWidgetPosition(field, &row, &role);
    if (row >= 0)
        form->setRowVisible(row, visible);
}
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
    settingsFirst_ = settingsStageFirst();
    stack_ = new QStackedWidget(this);
    // Build order matters beyond layout: buildSettingsPage() may query
    // controls the calculator page owns, so a wizard that places its settings
    // page later also gets it constructed later.
    showsCalculatorStage_ = showsCalculatorStage();
    if (hasSettingsStage_ && settingsFirst_)
        stack_->addWidget(buildSettingsPage());
    // The engine is selected at the top of the Calculator Settings stage; the
    // Conda environment is resolved silently from Preferences (no env stage).
    // The page is always constructed (the shared config accessors read its
    // widgets), but a wizard that inherits its calculator from a baseline
    // (MLWF) drops it from the stack for a strict 2-stage flow.
    QWidget* calculatorPage = buildCalculatorPage();
    if (showsCalculatorStage_)
        stack_->addWidget(calculatorPage);
    else
        calculatorPage->hide();
    if (hasSettingsStage_ && !settingsFirst_)
        stack_->addWidget(buildSettingsPage());
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
// Calculator Settings — engine selection + per-engine backend knobs
// ---------------------------------------------------------------------------
QWidget* SimulationWizardBase::buildCalculatorPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // Engine selection now lives at the top of this page (the separate
    // "Calculator & Execution Environment" stage was removed). The Conda
    // environment for the chosen engine is resolved silently from Preferences
    // → "Python & Environments" (see pythonExecutable()). Wrapped in a container
    // so a wizard that locks the engine (Electronic Structure) can hide it
    // wholesale via showsEngineAndDftControls().
    engineWidget_ = new QWidget(page);
    auto* engineForm = new QFormLayout(engineWidget_);
    engineForm->setContentsMargins(0, 0, 0, 0);
    calcCombo_ = new QComboBox(engineWidget_);
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
    engineForm->addRow(tr("Calculation engine:"), calcCombo_);
    layout->addWidget(engineWidget_);
    connect(calcCombo_, &QComboBox::currentIndexChanged, this,
            &SimulationWizardBase::updateCalculatorEnabled);

    calcSettingsHint_ = new QLabel(page);
    calcSettingsHint_->setWordWrap(true);
    layout->addWidget(calcSettingsHint_);

    layout->addWidget(buildMaceGroup(page));
    // Thematic DFT/GPAW group boxes (Mode & Basis Set; Brillouin Zone &
    // k-Points; Electronic Convergence & Smearing; Output & Exports). Shared
    // cutoff/k-points live in the first two groups.
    buildDftGpawGroups(page, layout);

    // Shown only when the cutoff/XC/mode are inherited from a baseline SCF
    // (Electronic Structure wizard); hidden otherwise.
    baselineInheritNote_ = new QLabel(
        tr("Plane-wave cutoff, XC functional and mode are inherited from the "
           "selected baseline SCF and cannot be changed here."),
        page);
    baselineInheritNote_->setWordWrap(true);
    baselineInheritNote_->setVisible(false);
    layout->addWidget(baselineInheritNote_);

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

QWidget* SimulationWizardBase::buildMaceGroup(QWidget* parent)
{
    maceGroup_ = new QGroupBox(tr("MACE settings"), parent);
    auto* form = new QFormLayout(maceGroup_);

    maceModelCombo_ = new QComboBox(maceGroup_);
    // Order matches core::MaceModelSource.
    maceModelCombo_->addItems({tr("MACE-MP-0 (materials)"),
                               tr("MACE-OFF (organic molecules)"),
                               tr("Custom trained model")});
    form->addRow(tr("Model:"), maceModelCombo_);

    maceSizeCombo_ = new QComboBox(maceGroup_);
    maceSizeCombo_->addItems({QStringLiteral("small"), QStringLiteral("medium"),
                              QStringLiteral("large")});
    maceSizeCombo_->setCurrentIndex(1);
    form->addRow(tr("Model size:"), maceSizeCombo_);

    // Weights file: required for "Custom trained model", optional for the
    // foundation families (where it pins a downloaded checkpoint so a run
    // does not silently change when the cached model is updated upstream).
    maceModelPathEdit_ = new QLineEdit(maceGroup_);
    maceModelPathEdit_->setPlaceholderText(
        tr("path/to/weights.model or .pt (e.g. mace-off23-small.model)"));
    maceBrowseButton_ = new QPushButton(tr("Browse…"), maceGroup_);
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(maceModelPathEdit_, 1);
    pathRow->addWidget(maceBrowseButton_);
    form->addRow(tr("Model file:"), pathRow);
    connect(maceBrowseButton_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select MACE Model File"), maceModelPathEdit_->text(),
            tr("MACE models (*.model *.pt *.pth);;All files (*)"));
        if (!path.isEmpty()) {
            maceModelPathEdit_->setText(path);
            refreshPreview();
        }
    });
    maceModelPathHint_ = new QLabel(maceGroup_);
    maceModelPathHint_->setWordWrap(true);
    form->addRow(QString(), maceModelPathHint_);

    macePrecisionCombo_ = new QComboBox(maceGroup_);
    // Order matches core::MacePrecision.
    macePrecisionCombo_->addItem(tr("float64 (double — default)"));
    macePrecisionCombo_->addItem(tr("float32 (single — faster, lower memory)"));
    macePrecisionCombo_->setToolTip(
        tr("MACE's default_dtype. float64 reproduces the training checkpoint "
           "exactly and is what tight force convergence and vibrational "
           "analysis need; float32 is roughly twice as fast (especially on "
           "GPU) with ~1e-4 eV/Å noise in the forces."));
    form->addRow(tr("Precision:"), macePrecisionCombo_);

    maceDeviceCombo_ = new QComboBox(maceGroup_);
    maceDeviceCombo_->addItems({QStringLiteral("cpu"), QStringLiteral("cuda (GPU)"),
                                QStringLiteral("mps (Apple GPU)")});
    maceDeviceCombo_->setToolTip(
        tr("cuda: NVIDIA GPU (needs a CUDA build of PyTorch).\n"
           "mps: Apple-silicon GPU — note that PyTorch's MPS backend has no "
           "float64 support, so pair it with float32."));
    form->addRow(tr("Device / GPU:"), maceDeviceCombo_);

    // Any of these changes the generated calculator block.
    for (QComboBox* combo : {maceModelCombo_, maceSizeCombo_, macePrecisionCombo_,
                             maceDeviceCombo_}) {
        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            updateMaceRows();
            refreshPreview();
        });
    }
    connect(maceModelPathEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });

    updateMaceRows();
    return maceGroup_;
}

void SimulationWizardBase::updateMaceRows()
{
    const bool custom = maceModelCombo_->currentIndex()
        == static_cast<int>(core::MaceModelSource::CustomFile);
    // A custom checkpoint carries its own architecture — the size keyword is
    // meaningless there.
    maceSizeCombo_->setEnabled(!custom);
    maceModelPathHint_->setText(
        custom ? tr("Required: MACECalculator loads these weights directly.")
               : tr("Optional: leave empty to download and cache the "
                    "foundation model, or point at a checkpoint to pin it."));

    // MPS has no float64 kernels in PyTorch; warn rather than silently
    // generating a script that dies at the first forward pass.
    const bool mps = maceDeviceCombo_->currentText().startsWith(QStringLiteral("mps"));
    const bool float64 = macePrecisionCombo_->currentIndex()
        == static_cast<int>(core::MacePrecision::Float64);
    macePrecisionCombo_->setStyleSheet(mps && float64
                                           ? QStringLiteral("color: #d9534f;")
                                           : QString());
    macePrecisionCombo_->setToolTip(
        mps && float64
            ? tr("PyTorch's MPS backend does not implement float64 — select "
                 "float32, or run on the CPU.")
            : tr("MACE's default_dtype. float64 reproduces the training "
                 "checkpoint exactly; float32 is roughly twice as fast."));
}

void SimulationWizardBase::buildDftGpawGroups(QWidget* parent,
                                              QVBoxLayout* layout)
{
    // ===== 1. Mode & Basis Set ============================================
    modeBasisGroup_ = new QGroupBox(tr("Mode & Basis Set"), parent);
    auto* modeForm = new QFormLayout(modeBasisGroup_);

    gpawModeCombo_ = new QComboBox(modeBasisGroup_);
    gpawModeCombo_->addItem(tr("FD — finite difference (real-space grid)"));
    gpawModeCombo_->addItem(tr("PW — plane waves"));
    gpawModeCombo_->addItem(tr("LCAO — atomic-orbital basis"));
    gpawModeCombo_->setCurrentIndex(static_cast<int>(core::GpawMode::PlaneWave));
    gpawModeCombo_->setToolTip(
        tr("FD: robust for molecules and slabs, no cutoff to converge.\n"
           "PW: the usual choice for periodic solids (uses the plane-wave "
           "cutoff below).\n"
           "LCAO: fastest and lightest, least accurate."));
    modeForm->addRow(tr("Mode:"), gpawModeCombo_);

    // Plane-wave cutoff — shared with the other DFT engines (their PW cutoff).
    cutoffSpin_ = new QDoubleSpinBox(modeBasisGroup_);
    cutoffSpin_->setRange(100.0, 2000.0);
    cutoffSpin_->setValue(500.0);
    cutoffSpin_->setSuffix(tr(" eV"));
    modeForm->addRow(tr("Plane-wave cutoff:"), cutoffSpin_);
    connect(cutoffSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    gpawGridSpacingSpin_ = new QDoubleSpinBox(modeBasisGroup_);
    gpawGridSpacingSpin_->setRange(0.05, 0.50);
    gpawGridSpacingSpin_->setDecimals(3);
    gpawGridSpacingSpin_->setSingleStep(0.01);
    gpawGridSpacingSpin_->setValue(0.20);
    gpawGridSpacingSpin_->setSuffix(tr(" Å"));
    gpawGridSpacingSpin_->setToolTip(
        tr("Real-space grid spacing h (FD mode). 0.18–0.20 Å is typical."));
    modeForm->addRow(tr("Grid spacing h:"), gpawGridSpacingSpin_);

    gpawBasisCombo_ = new QComboBox(modeBasisGroup_);
    gpawBasisCombo_->setEditable(true);
    gpawBasisCombo_->addItems({QStringLiteral("dzp"), QStringLiteral("dz"),
                               QStringLiteral("sz"), QStringLiteral("szp")});
    modeForm->addRow(tr("LCAO basis:"), gpawBasisCombo_);

    gpawXcCombo_ = new QComboBox(modeBasisGroup_);
    gpawXcCombo_->setEditable(true);
    gpawXcCombo_->addItems({QStringLiteral("PBE"), QStringLiteral("LDA"),
                            QStringLiteral("revPBE"), QStringLiteral("RPBE"),
                            QStringLiteral("PBEsol"), QStringLiteral("HSE06"),
                            QStringLiteral("B3LYP"), QStringLiteral("SCAN"),
                            QStringLiteral("r2SCAN")});
    gpawXcCombo_->setToolTip(
        tr("The hybrids (HSE06, B3LYP) and meta-GGAs (SCAN, r2SCAN) need a "
           "GPAW build with libxc, and are far more expensive than the GGAs."));
    modeForm->addRow(tr("XC functional:"), gpawXcCombo_);

    // For the script-template DFT backends (Espresso/VASP/Siesta) XC is edited
    // in the generated script; shown for them, hidden for GPAW.
    dftXcNote_ = new QLabel(
        tr("XC functional defaults to PBE in the script (editable in Stage 4)."),
        modeBasisGroup_);
    dftXcNote_->setWordWrap(true);
    modeForm->addRow(dftXcNote_);
    layout->addWidget(modeBasisGroup_);

    // ===== 2. Brillouin Zone & k-Points ===================================
    bzGroup_ = new QGroupBox(tr("Brillouin Zone & k-Points"), parent);
    auto* bzForm = new QFormLayout(bzGroup_);

    auto* kptRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        kptSpins_[i] = new QSpinBox(bzGroup_);
        kptSpins_[i]->setRange(1, 64);
        kptSpins_[i]->setValue(7);
        kptRow->addWidget(kptSpins_[i]);
        if (i < 2)
            kptRow->addWidget(new QLabel(QStringLiteral("×"), bzGroup_));
        connect(kptSpins_[i], &QSpinBox::valueChanged, this, [this] {
            calculatorKgridChanged();
            refreshPreview();
        });
    }
    kptRow->addStretch(1);
    bzForm->addRow(tr("k-point grid (Monkhorst-Pack):"), kptRow);

    // "Symmetry: off" — GPAW only, and only when the wizard opts in
    // (Single-Point): a symmetry-off run is the recommended MLWF baseline.
    gpawSymmetryOffCheck_ = new QCheckBox(
        tr("Symmetry: off  (symmetry=\"off\")"), bzGroup_);
    gpawSymmetryOffCheck_->setToolTip(
        tr("Disable point-group symmetry reduction of the k-point set — sample "
           "the full, unsymmetrized Brillouin zone (required when the "
           "wavefunctions feed a Maximally Localized Wannier Functions run)."));
    connect(gpawSymmetryOffCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    if (showsGpawSymmetryToggle())
        bzForm->addRow(tr("k-point symmetry:"), gpawSymmetryOffCheck_);
    else
        gpawSymmetryOffCheck_->hide();
    layout->addWidget(bzGroup_);

    // ===== 3. Electronic Convergence & Smearing ===========================
    convGroup_ = new QGroupBox(tr("Electronic Convergence & Smearing"), parent);
    auto* convForm = new QFormLayout(convGroup_);

    // Subclass smearing / SCF rows first (e.g. Single-point's Fermi-Dirac /
    // Gaussian smearing and SCF tolerance / max steps).
    buildConvergenceRows(convForm);

    gpawEigensolverCombo_ = new QComboBox(convGroup_);
    gpawEigensolverCombo_->addItems({QStringLiteral("davidson"),
                                     QStringLiteral("cg"),
                                     QStringLiteral("rmm-diis"),
                                     QStringLiteral("direct")});
    gpawEigensolverCombo_->setToolTip(
        tr("davidson: robust general default.\n"
           "cg: slower but very stable — try it when the SCF oscillates.\n"
           "rmm-diis: cheapest per step for large metallic systems.\n"
           "direct: exact diagonalization (LCAO / small systems)."));
    convForm->addRow(tr("Eigensolver:"), gpawEigensolverCombo_);

    gpawMixerCombo_ = new QComboBox(convGroup_);
    gpawMixerCombo_->addItems({QStringLiteral("Mixer"), QStringLiteral("MixerSum"),
                               QStringLiteral("MixerDif")});
    gpawMixerCombo_->setToolTip(
        tr("Mixer: non-magnetic systems.\n"
           "MixerSum: spin-polarized — mixes the total density.\n"
           "MixerDif: spin-polarized — total density + magnetization "
           "separately."));
    convForm->addRow(tr("Density mixer:"), gpawMixerCombo_);

    gpawBetaSpin_ = new QDoubleSpinBox(convGroup_);
    gpawBetaSpin_->setRange(0.001, 1.0);
    gpawBetaSpin_->setDecimals(3);
    gpawBetaSpin_->setSingleStep(0.01);
    gpawBetaSpin_->setValue(0.05);
    gpawBetaSpin_->setToolTip(
        tr("Linear mixing (damping) parameter. Metals and magnetic systems "
           "often need 0.02–0.05."));
    gpawNmaxoldSpin_ = new QSpinBox(convGroup_);
    gpawNmaxoldSpin_->setRange(1, 20);
    gpawNmaxoldSpin_->setValue(5);
    gpawNmaxoldSpin_->setToolTip(
        tr("Number of previous densities kept for the Pulay mixing history."));
    gpawWeightSpin_ = new QDoubleSpinBox(convGroup_);
    gpawWeightSpin_->setRange(1.0, 500.0);
    gpawWeightSpin_->setDecimals(1);
    gpawWeightSpin_->setValue(50.0);
    gpawWeightSpin_->setToolTip(
        tr("Metric weight damping long-wavelength charge sloshing."));
    gpawMixerParamsRow_ = new QWidget(convGroup_);
    auto* mixerRow = new QHBoxLayout(gpawMixerParamsRow_);
    mixerRow->setContentsMargins(0, 0, 0, 0);
    mixerRow->addWidget(new QLabel(tr("beta"), gpawMixerParamsRow_));
    mixerRow->addWidget(gpawBetaSpin_);
    mixerRow->addWidget(new QLabel(tr("nmaxold"), gpawMixerParamsRow_));
    mixerRow->addWidget(gpawNmaxoldSpin_);
    mixerRow->addWidget(new QLabel(tr("weight"), gpawMixerParamsRow_));
    mixerRow->addWidget(gpawWeightSpin_);
    mixerRow->addStretch(1);
    convForm->addRow(tr("Mixer parameters:"), gpawMixerParamsRow_);

    const auto toleranceEdit = [this](double initial, double minimum,
                                      double maximum, const QString& tip) {
        auto* edit =
            new QLineEdit(QString::number(initial, 'g', 6), convGroup_);
        auto* validator = new QDoubleValidator(minimum, maximum, 12, edit);
        validator->setNotation(QDoubleValidator::ScientificNotation);
        validator->setLocale(QLocale::c());
        edit->setValidator(validator);
        edit->setToolTip(tip);
        return edit;
    };
    gpawEigenTolEdit_ = toleranceEdit(
        4e-8, 1e-12, 1e-2,
        tr("GPAW convergence['eigenstates'] — integrated eigenstate residual, "
           "in eV² per valence electron (e.g. 4e-8)."));
    convForm->addRow(tr("Eigenstate tolerance:"), gpawEigenTolEdit_);
    gpawDensityTolEdit_ = toleranceEdit(
        1e-4, 1e-9, 1e-1,
        tr("GPAW convergence['density'] — change in the density integrated "
           "over the cell, in electrons per valence electron (e.g. 1e-4)."));
    convForm->addRow(tr("Density tolerance:"), gpawDensityTolEdit_);
    layout->addWidget(convGroup_);

    // ===== 4. Output & Exports ============================================
    outputGroup_ = new QGroupBox(tr("Output & Exports"), parent);
    auto* outForm = new QFormLayout(outputGroup_);

    // Subclass output rows first (spin polarization, magnetic moments).
    buildOutputRows(outForm);

    gpawDensityExportCheck_ = new QCheckBox(
        tr("Export Electron Density (.cube)"), outputGroup_);
    gpawDensityExportCheck_->setToolTip(
        tr("After the SCF, write the all-electron density to density.cube "
           "(a standard Gaussian cube volumetric file)."));
    connect(gpawDensityExportCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    if (showsGpawDensityExport())
        outForm->addRow(tr("Density export:"), gpawDensityExportCheck_);
    else
        gpawDensityExportCheck_->hide();
    layout->addWidget(outputGroup_);

    // -- Live preview wiring for the GPAW controls -------------------------
    connect(gpawModeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        updateGpawRows();
        refreshPreview();
    });
    for (QComboBox* combo : {gpawBasisCombo_, gpawXcCombo_, gpawEigensolverCombo_,
                             gpawMixerCombo_}) {
        connect(combo, &QComboBox::currentTextChanged, this,
                [this] { refreshPreview(); });
    }
    for (QDoubleSpinBox* spin : {gpawGridSpacingSpin_, gpawBetaSpin_,
                                 gpawWeightSpin_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    }
    for (QLineEdit* edit : {gpawEigenTolEdit_, gpawDensityTolEdit_}) {
        connect(edit, &QLineEdit::textChanged, this, [this] { refreshPreview(); });
    }
    connect(gpawNmaxoldSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    updateGpawRows();
}

void SimulationWizardBase::updateGpawRows()
{
    // GPAW takes exactly one of ecut / h / basis; show the one in play.
    const auto mode = static_cast<core::GpawMode>(gpawModeCombo_->currentIndex());
    const bool fd = mode == core::GpawMode::FiniteDifference;
    const bool lcao = mode == core::GpawMode::Lcao;
    // Grid spacing (FD) and LCAO basis live in the "Mode & Basis Set" group.
    auto* form = qobject_cast<QFormLayout*>(modeBasisGroup_->layout());
    if (!form)
        return;
    const auto setRowVisible = [form](QWidget* field, bool visible) {
        int row = -1;
        QFormLayout::ItemRole role{};
        form->getWidgetPosition(field, &row, &role);
        if (row >= 0)
            form->setRowVisible(row, visible);
    };
    setRowVisible(gpawGridSpacingSpin_, fd);
    setRowVisible(gpawBasisCombo_, lcao);
}

// ---------------------------------------------------------------------------
// Stage 4 — Script Review
// ---------------------------------------------------------------------------
QWidget* SimulationWizardBase::buildReviewPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // A wizard may fold another control into this stage (the Effective Bands
    // k-path). A splitter rather than a fixed split: the two want very
    // different amounts of room depending on what the user is doing.
    QWidget* extras = buildReviewExtras();
    QSplitter* splitter = nullptr;
    if (extras) {
        splitter = new QSplitter(Qt::Vertical, page);
        splitter->addWidget(extras);
        layout->addWidget(splitter, 1);
    }

    auto* scriptPane = new QWidget(page);
    auto* scriptLayout = new QVBoxLayout(scriptPane);
    scriptLayout->setContentsMargins(0, 0, 0, 0);
    auto* header = new QHBoxLayout;
    header->addWidget(new QLabel(tr("Generated ASE script (editable):"), page));
    header->addStretch(1);
    auto* regenerate = new QPushButton(tr("Regenerate"), page);
    header->addWidget(regenerate);
    scriptLayout->addLayout(header);
    preview_ = new QPlainTextEdit(scriptPane);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new PythonHighlighter(preview_->document());
    scriptLayout->addWidget(preview_, 1);
    if (splitter) {
        splitter->addWidget(scriptPane);
        splitter->setStretchFactor(0, 3); // the 3D picker wants the room
        splitter->setStretchFactor(1, 2);
    } else {
        layout->addWidget(scriptPane, 1);
    }
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

    // A wizard that locks the engine (Electronic Structure) hides the entire
    // standard calculator chrome — only its buildCalculatorExtras() content
    // (baseline + PDOS + k-path) is shown.
    const auto setGroups = [this](bool v) {
        for (QGroupBox* g : {modeBasisGroup_, bzGroup_, convGroup_, outputGroup_})
            if (g)
                g->setVisible(v);
    };
    if (!showsEngineAndDftControls()) {
        if (engineWidget_) engineWidget_->setVisible(false);
        if (calcSettingsHint_) calcSettingsHint_->setVisible(false);
        setGroups(false);
        if (maceGroup_) maceGroup_->setVisible(false);
        if (orcaGroup_) orcaGroup_->setVisible(false);
        if (baselineInheritNote_) baselineInheritNote_->setVisible(false);
        updateCalculatorExtras(kind);
        return;
    }

    const bool isDft = kind == core::CalculatorKind::QuantumEspresso
        || kind == core::CalculatorKind::Vasp || kind == core::CalculatorKind::Gpaw
        || kind == core::CalculatorKind::Siesta;
    const bool isMace = kind == core::CalculatorKind::Mace;
    const bool isOrca = kind == core::CalculatorKind::Orca;
    const bool isGpaw = kind == core::CalculatorKind::Gpaw;

    // Mode & Basis Set and Brillouin Zone & k-Points host the shared cutoff /
    // k-points, so they show for every DFT engine. Convergence & Output carry
    // GPAW-only or subclass-injected rows, so they show for GPAW or when the
    // subclass contributed rows (e.g. Single-point's smearing / spin).
    modeBasisGroup_->setVisible(isDft);
    bzGroup_->setVisible(isDft);
    convGroup_->setVisible(isGpaw || (isDft && hasConvergenceExtras()));
    outputGroup_->setVisible((isGpaw && showsGpawDensityExport())
                             || (isDft && hasOutputExtras()));
    maceGroup_->setVisible(isMace);
    orcaGroup_->setVisible(isOrca);

    // The XC note applies only to the script-template DFT backends; GPAW picks
    // XC in its own combo. Mode / grid / basis / XC combo and the density
    // export are GPAW-only rows.
    if (dftXcNote_)
        setFormRowVisible(modeBasisGroup_, dftXcNote_, isDft && !isGpaw);
    setFormRowVisible(modeBasisGroup_, gpawModeCombo_, isGpaw);
    setFormRowVisible(modeBasisGroup_, gpawXcCombo_, isGpaw);
    // Grid spacing / LCAO basis are GPAW-only *and* mode-dependent; hide them
    // wholesale for non-GPAW, then let updateGpawRows pick the right one.
    setFormRowVisible(modeBasisGroup_, gpawGridSpacingSpin_, isGpaw);
    setFormRowVisible(modeBasisGroup_, gpawBasisCombo_, isGpaw);
    for (QWidget* w : {static_cast<QWidget*>(gpawEigensolverCombo_),
                       static_cast<QWidget*>(gpawMixerCombo_),
                       static_cast<QWidget*>(gpawEigenTolEdit_),
                       static_cast<QWidget*>(gpawDensityTolEdit_)})
        setFormRowVisible(convGroup_, w, isGpaw);
    setFormRowVisible(convGroup_, gpawMixerParamsRow_, isGpaw);
    if (isGpaw)
        updateGpawRows();

    // Baseline inheritance (Electronic Structure): the run restarts from a
    // completed SCF density, so its plane-wave cutoff, XC functional and mode
    // are fixed by that .gpw — hide those controls and show a note instead.
    const bool inheritGpaw = isGpaw && inheritsCalculatorFromBaseline();
    setFormRowVisible(modeBasisGroup_, cutoffSpin_, !inheritGpaw);
    if (inheritGpaw) {
        setFormRowVisible(modeBasisGroup_, gpawXcCombo_, false);
        setFormRowVisible(modeBasisGroup_, gpawModeCombo_, false);
    }
    if (baselineInheritNote_)
        baselineInheritNote_->setVisible(inheritGpaw);

    if (calcSettingsHint_)
        calcSettingsHint_->setText(
            (isDft || isMace || isOrca)
                ? tr("Settings for %1:").arg(calcCombo_->currentText())
                : tr("%1 has no additional settings — continue to the script "
                     "review.").arg(calcCombo_->currentText()));
    updateCalculatorExtras(kind);
}

int SimulationWizardBase::calculatorKpoint(int axis) const
{
    if (axis < 0 || axis >= 3 || !kptSpins_[axis])
        return 1;
    return kptSpins_[axis]->value();
}

core::CalculatorKind SimulationWizardBase::selectedCalculator() const
{
    return static_cast<core::CalculatorKind>(calcCombo_->currentData().toInt());
}

void SimulationWizardBase::selectCalculator(core::CalculatorKind kind)
{
    if (!calcCombo_)
        return;
    const int index = calcCombo_->findData(static_cast<int>(kind));
    if (index >= 0)
        calcCombo_->setCurrentIndex(index);
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
    c.maceModelPath = maceModelPathEdit_->text().trimmed().toStdString();
    // Device combo carries a friendly suffix; keep only the device token.
    c.maceDevice =
        maceDeviceCombo_->currentText().section(QLatin1Char(' '), 0, 0).toStdString();
    c.macePrecision =
        static_cast<core::MacePrecision>(macePrecisionCombo_->currentIndex());

    c.gpawMode = static_cast<core::GpawMode>(gpawModeCombo_->currentIndex());
    c.gpawGridSpacing = gpawGridSpacingSpin_->value();
    c.gpawBasis = gpawBasisCombo_->currentText().trimmed().toStdString();
    c.gpawXc = gpawXcCombo_->currentText().trimmed().toStdString();
    c.gpawEigensolver =
        static_cast<core::GpawEigensolver>(gpawEigensolverCombo_->currentIndex());
    c.gpawMixer = static_cast<core::GpawMixerKind>(gpawMixerCombo_->currentIndex());
    c.gpawMixerBeta = gpawBetaSpin_->value();
    c.gpawMixerNmaxold = gpawNmaxoldSpin_->value();
    c.gpawMixerWeight = gpawWeightSpin_->value();
    // An in-progress edit ("1e-") is not a number yet; keep the last valid
    // value rather than writing 0 into the script.
    bool ok = false;
    if (const double v = gpawEigenTolEdit_->text().toDouble(&ok); ok && v > 0.0)
        c.gpawConvEigenstates = v;
    if (const double v = gpawDensityTolEdit_->text().toDouble(&ok); ok && v > 0.0)
        c.gpawConvDensity = v;
    c.gpawSymmetryOff =
        gpawSymmetryOffCheck_ && gpawSymmetryOffCheck_->isChecked();
    c.gpawExportDensity =
        gpawDensityExportCheck_ && gpawDensityExportCheck_->isChecked();

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
    if (hasSettingsStage_ && settingsFirst_)
        titles << settingsHeader();
    if (showsCalculatorStage_)
        titles << calculatorSettingsHeader();
    if (hasSettingsStage_ && !settingsFirst_)
        titles << settingsHeader();
    titles << reviewHeader();
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
    if (onReview)
        runLocalButton_->setDefault(true);
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
    // The script imports CalangoLog, so the helper module is exported beside
    // it — an exported script stays runnable standalone.
    QString error;
    if (!writeScriptWithLogger(path, script(), &error))
        QMessageBox::warning(this, tr("Export Script"), error);
}

QString SimulationWizardBase::script() const
{
    return preview_->toPlainText();
}

QString SimulationWizardBase::pythonExecutable() const
{
    // Environment is bound silently from the Preferences → "Python &
    // Environments" per-engine mapping. Resolution order: the engine's preset,
    // then the last global env, then the embedded interpreter. An unset preset
    // resolves to "" (CondaEnvs::resolvePython), i.e. the active $PATH / embedded
    // python.
    QString env = EnginePresets::envFor(selectedCalculator());
    if (env.trimmed().isEmpty())
        env = QSettings().value(kEnvSettingsKey).toString();
    const QString resolved = CondaEnvs::resolvePython(env);
    if (!resolved.isEmpty())
        return resolved;
    return QString::fromStdString(pybridge::PythonEngine::instance().executable());
}

namespace {
const char* gpawModeTag(core::GpawMode mode)
{
    switch (mode) {
    case core::GpawMode::PlaneWave:
        return "PW";
    case core::GpawMode::FiniteDifference:
        return "FD";
    case core::GpawMode::Lcao:
        return "LCAO";
    }
    return "PW";
}
} // namespace

QString SimulationWizardBase::calculatorProvenanceJson() const
{
    const core::CalculatorConfig c = baseCalculatorConfig();
    QJsonObject o;
    o.insert(QStringLiteral("engine"),
             QString::fromStdString(core::toString(c.calculator)));
    o.insert(QStringLiteral("engine_kind"), static_cast<int>(c.calculator));
    o.insert(QStringLiteral("xc"), QString::fromStdString(c.gpawXc));
    o.insert(QStringLiteral("cutoff_ev"), c.planeWaveCutoffEv);
    o.insert(QStringLiteral("mode"), QLatin1String(gpawModeTag(c.gpawMode)));
    o.insert(QStringLiteral("grid_spacing"), c.gpawGridSpacing);
    o.insert(QStringLiteral("kpts"),
             QJsonArray{c.kpts[0], c.kpts[1], c.kpts[2]});
    o.insert(QStringLiteral("symmetry_off"), c.gpawSymmetryOff);
    o.insert(QStringLiteral("python"), pythonExecutable());
    o.insert(QStringLiteral("conda_env"),
             EnginePresets::envFor(c.calculator));
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

std::optional<SimulationWizardBase::InheritedCalculator>
SimulationWizardBase::readCalculatorProvenance(const QString& jobDir)
{
    QFile file(jobDir + QStringLiteral("/calculator.json"));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (o.isEmpty())
        return std::nullopt;

    InheritedCalculator ic;
    ic.engine = o.value(QStringLiteral("engine")).toString();
    ic.engineKind = o.value(QStringLiteral("engine_kind")).toInt(-1);
    ic.xc = o.value(QStringLiteral("xc")).toString();
    ic.cutoffEv = o.value(QStringLiteral("cutoff_ev")).toDouble();
    ic.mode = o.value(QStringLiteral("mode")).toString();
    ic.gridSpacing = o.value(QStringLiteral("grid_spacing")).toDouble();
    const QJsonArray k = o.value(QStringLiteral("kpts")).toArray();
    for (int i = 0; i < 3 && i < k.size(); ++i)
        ic.kpts[i] = k.at(i).toInt();
    ic.symmetryOff = o.value(QStringLiteral("symmetry_off")).toBool();
    ic.pythonExecutable = o.value(QStringLiteral("python")).toString();
    ic.condaEnv = o.value(QStringLiteral("conda_env")).toString();
    return ic;
}

QString SimulationWizardBase::InheritedCalculator::summary() const
{
    QString s = engine.isEmpty() ? QStringLiteral("calculator") : engine;
    if (!xc.isEmpty())
        s += QStringLiteral(" · XC %1").arg(xc);
    if (mode == QLatin1String("PW") && cutoffEv > 0.0)
        s += QStringLiteral(" · PW %1 eV").arg(cutoffEv, 0, 'g', 4);
    else if (mode == QLatin1String("FD") && gridSpacing > 0.0)
        s += QStringLiteral(" · FD h=%1 Å").arg(gridSpacing, 0, 'g', 3);
    else if (mode == QLatin1String("LCAO"))
        s += QStringLiteral(" · LCAO");
    if (kpts[0] > 0)
        s += QStringLiteral(" · k %1×%2×%3")
                 .arg(kpts[0])
                 .arg(kpts[1])
                 .arg(kpts[2]);
    if (symmetryOff)
        s += QStringLiteral(" · symmetry off");
    return s;
}

} // namespace calango::gui
