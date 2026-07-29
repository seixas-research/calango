#include "gui/SimulationWizardBase.hpp"

#include "gui/SettingsManager.hpp"
#include "gui/HubbardParametersDialog.hpp"

#include "gui/CondaEnvs.hpp"
#include "gui/EnginePresets.hpp"
#include "gui/PythonHighlighter.hpp"
#include "gui/RunCommands.hpp"
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
#include <QGridLayout>
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
/// Where the VASP POTCAR directory is remembered. Under `jobs/` with the other
/// installation-wide run settings rather than under a wizard's own key, because
/// it is shared by every VASP-capable dialog.
const auto kVaspPotcarKey = QStringLiteral("jobs/vaspPotcarPath");

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
    // Optional extra task stage between Calculator Settings and the subclass's
    // own page (Phonon: displacement settings, then the q-path).
    hasSecondSettingsStage_ = !secondSettingsHeader().isEmpty();
    if (hasSecondSettingsStage_) {
        if (QWidget* second = buildSecondSettingsPage())
            stack_->addWidget(second);
        else
            hasSecondSettingsStage_ = false; // no content — drop the stage
    }
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
    // Machine-learning interatomic potentials.
    addCalc(tr("DeepMD-kit (ML potential)"), core::CalculatorKind::DeepMd);
    addCalc(tr("NequIP (ML potential)"), core::CalculatorKind::NequIp);
    addCalc(tr("Allegro (ML potential)"), core::CalculatorKind::Allegro);
    addCalc(tr("CHGNet (universal ML potential)"), core::CalculatorKind::ChgNet);
    addCalc(tr("MatterSim (universal ML potential)"),
            core::CalculatorKind::MatterSim);
    addCalc(tr("FAIRChem / OCP (ML potential)"), core::CalculatorKind::FairChem);
    addCalc(tr("LAMMPS (classical MD)"), core::CalculatorKind::Lammps);
    engineForm->addRow(tr("Calculation engine:"), calcCombo_);
    layout->addWidget(engineWidget_);
    connect(calcCombo_, &QComboBox::currentIndexChanged, this,
            &SimulationWizardBase::updateCalculatorEnabled);

    calcSettingsHint_ = new QLabel(page);
    calcSettingsHint_->setWordWrap(true);
    layout->addWidget(calcSettingsHint_);

    layout->addWidget(buildMaceGroup(page));
    layout->addWidget(buildMlipGroup(page));
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
    layout->addWidget(buildVaspGroup(page));
    layout->addWidget(buildLammpsGroup(page));

    // Subclass-supplied extra settings (e.g. Single-point's convergence group,
    // folded in here when it has no separate Stage 1).
    if (QWidget* extras = buildCalculatorExtras())
        layout->addWidget(extras);

    layout->addStretch(1);
    return page;
}

QWidget* SimulationWizardBase::buildVaspGroup(QWidget* parent)
{
    vaspGroup_ = new QGroupBox(tr("VASP settings"), parent);
    auto* form = new QFormLayout(vaspGroup_);

    // -- POTCAR datasets ----------------------------------------------------
    // First, because without it nothing runs at all. Persisted globally rather
    // than per wizard: it is a property of the installation, not of one job,
    // and every VASP-capable dialog reads the same value.
    auto* potcarRow = new QWidget(vaspGroup_);
    auto* potcarLayout = new QHBoxLayout(potcarRow);
    potcarLayout->setContentsMargins(0, 0, 0, 0);
    vaspPotcarEdit_ = new QLineEdit(potcarRow);
    vaspPotcarEdit_->setPlaceholderText(
        QStringLiteral("/path/to/POTCARs"));
    vaspPotcarEdit_->setText(vaspPotcarDirectory());
    vaspPotcarEdit_->setToolTip(
        tr("Directory holding the PAW datasets — VASP's VASP_PP_PATH.\n\n"
           "Both layouts work: the canonical one with a potpaw_PBE/ level "
           "inside, and the flat one where the element folders sit directly "
           "here (the generated script builds a symlink shim for that case, "
           "because ASE cannot be told to look anywhere else).\n\n"
           "Remembered across sessions and shared by every VASP run."));
    auto* potcarBrowse = new QPushButton(tr("Browse…"), potcarRow);
    potcarLayout->addWidget(vaspPotcarEdit_, 1);
    potcarLayout->addWidget(potcarBrowse);
    form->addRow(tr("POTCAR directory:"), potcarRow);
    connect(potcarBrowse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select the POTCAR Directory"),
            vaspPotcarEdit_->text());
        if (!dir.isEmpty())
            vaspPotcarEdit_->setText(dir);
    });
    connect(vaspPotcarEdit_, &QLineEdit::textChanged, this,
            [this](const QString& text) {
                setVaspPotcarDirectory(text);
                refreshPreview();
            });

    // -- Basis and XC -------------------------------------------------------
    vaspXcCombo_ = new QComboBox(vaspGroup_);
    vaspXcCombo_->setEditable(true);
    vaspXcCombo_->addItems({QStringLiteral("PBE"), QStringLiteral("PBEsol"),
                            QStringLiteral("RPBE"), QStringLiteral("LDA"),
                            QStringLiteral("SCAN"), QStringLiteral("r2SCAN"),
                            QStringLiteral("HSE06")});
    vaspXcCombo_->setToolTip(
        tr("ASE's `xc`, which expands to the matching GGA / METAGGA tag plus "
           "its recommended defaults. The meta-GGAs and HSE06 need the right "
           "POTCAR set and cost far more than the GGAs."));
    form->addRow(tr("XC functional:"), vaspXcCombo_);

    vaspPrecCombo_ = new QComboBox(vaspGroup_);
    // Order matches core::VaspPrecision.
    vaspPrecCombo_->addItems({QStringLiteral("Normal"),
                              QStringLiteral("Accurate"),
                              QStringLiteral("Single")});
    vaspPrecCombo_->setCurrentIndex(
        static_cast<int>(core::VaspPrecision::Accurate));
    vaspPrecCombo_->setToolTip(
        tr("PREC — the FFT grid and augmentation-charge accuracy. Accurate is "
           "the right default: Normal's coarser grid introduces egg-box errors "
           "in forces, which is exactly what a relaxation is sensitive to."));

    vaspAlgoCombo_ = new QComboBox(vaspGroup_);
    // Order matches core::VaspAlgo.
    vaspAlgoCombo_->addItems({QStringLiteral("Normal"), QStringLiteral("Fast"),
                              QStringLiteral("VeryFast"), QStringLiteral("All"),
                              QStringLiteral("Damped")});
    vaspAlgoCombo_->setToolTip(
        tr("ALGO — the electronic minimization.\n\n"
           "Normal (blocked Davidson): robust general default.\n"
           "Fast: Davidson then RMM-DIIS — the usual choice for large "
           "relaxations.\n"
           "VeryFast: RMM-DIIS only; fastest and least stable.\n"
           "All / Damped: for the cases where the others oscillate, and "
           "required for meta-GGA and hybrid functionals."));
    // PREC and ALGO are both "how hard is it trying", so they share a row.
    auto* precRow = new QWidget(vaspGroup_);
    auto* precLayout = new QHBoxLayout(precRow);
    precLayout->setContentsMargins(0, 0, 0, 0);
    precLayout->addWidget(vaspPrecCombo_);
    precLayout->addWidget(new QLabel(tr("ALGO"), precRow));
    precLayout->addWidget(vaspAlgoCombo_);
    precLayout->addStretch(1);
    form->addRow(tr("PREC:"), precRow);

    // -- Electronic convergence --------------------------------------------
    vaspNelmSpin_ = new QSpinBox(vaspGroup_);
    vaspNelmSpin_->setRange(1, 100000);
    vaspNelmSpin_->setValue(500);
    vaspNelmSpin_->setToolTip(
        tr("NELM — maximum electronic (SCF) steps. A runaway guard: EDIFF is "
           "what normally ends the cycle."));
    vaspEdiffEdit_ = new QLineEdit(QStringLiteral("1e-6"), vaspGroup_);
    auto* ediffValidator = new QDoubleValidator(1e-12, 1e-1, 12, vaspEdiffEdit_);
    ediffValidator->setNotation(QDoubleValidator::ScientificNotation);
    ediffValidator->setLocale(QLocale::c());
    vaspEdiffEdit_->setValidator(ediffValidator);
    vaspEdiffEdit_->setToolTip(
        tr("EDIFF — the SCF energy convergence threshold, in eV. 1e-6 for "
           "forces and relaxations; 1e-4 is only enough for a rough total "
           "energy."));
    auto* elecRow = new QWidget(vaspGroup_);
    auto* elecLayout = new QHBoxLayout(elecRow);
    elecLayout->setContentsMargins(0, 0, 0, 0);
    elecLayout->addWidget(vaspNelmSpin_);
    elecLayout->addWidget(new QLabel(tr("EDIFF"), elecRow));
    elecLayout->addWidget(vaspEdiffEdit_, 1);
    form->addRow(tr("NELM:"), elecRow);

    vaspLrealCombo_ = new QComboBox(vaspGroup_);
    vaspLrealCombo_->addItem(tr("Auto — real space (large cells)"),
                             QStringLiteral("Auto"));
    vaspLrealCombo_->addItem(tr("False — reciprocal space (exact)"),
                             QStringLiteral("False"));
    vaspLrealCombo_->setToolTip(
        tr("LREAL — where the projection operators are evaluated. Auto is a "
           "large speed-up above roughly 20 atoms at a small accuracy cost; "
           "False is exact and is what you want for a small cell or a "
           "high-accuracy energy difference."));
    form->addRow(tr("LREAL:"), vaspLrealCombo_);

    // -- Ionic relaxation ---------------------------------------------------
    // Only meaningful for a task with ionic steps; a single point emits
    // NSW = 0 regardless, so the row is hidden rather than shown inert.
    vaspIbrionCombo_ = new QComboBox(vaspGroup_);
    vaspIbrionCombo_->addItem(tr("2 — conjugate gradient"), 2);
    vaspIbrionCombo_->addItem(tr("1 — quasi-Newton (RMM-DIIS)"), 1);
    vaspIbrionCombo_->addItem(tr("3 — damped molecular dynamics"), 3);
    vaspIbrionCombo_->setToolTip(
        tr("IBRION — the ionic relaxation algorithm. CG is robust from a poor "
           "starting geometry; quasi-Newton converges faster once close to the "
           "minimum."));
    vaspIsifCombo_ = new QComboBox(vaspGroup_);
    vaspIsifCombo_->addItem(tr("2 — ions only"), 2);
    vaspIsifCombo_->addItem(tr("3 — ions + cell shape + volume"), 3);
    vaspIsifCombo_->addItem(tr("4 — ions + cell shape"), 4);
    vaspIsifCombo_->setToolTip(
        tr("ISIF — what is allowed to move, and what is computed. A "
           "variable-cell relaxation needs 3 or more; the wizard raises this "
           "automatically when the cell is set to relax."));
    vaspEdiffgSpin_ = new QDoubleSpinBox(vaspGroup_);
    vaspEdiffgSpin_->setRange(-10.0, 10.0);
    vaspEdiffgSpin_->setDecimals(4);
    vaspEdiffgSpin_->setSingleStep(0.005);
    vaspEdiffgSpin_->setValue(-0.02);
    vaspEdiffgSpin_->setToolTip(
        tr("EDIFFG — the ionic convergence criterion. NEGATIVE means a force "
           "threshold in eV/Å (the usual choice); positive means an energy "
           "change in eV."));
    vaspDriverCombo_ = new QComboBox(vaspGroup_);
    // Order matches core::VaspRelaxDriver.
    vaspDriverCombo_->addItem(tr("ASE optimizer (VASP computes forces only)"));
    vaspDriverCombo_->addItem(tr("VASP internal relaxation (IBRION / NSW)"));
    vaspDriverCombo_->setToolTip(
        tr("Who takes the ionic steps. Exactly one side may — both can relax "
           "on their own, and with both enabled every force evaluation ASE "
           "asked for would run a complete VASP relaxation.\n\n"
           "ASE optimizer: VASP is pinned to IBRION = -1, NSW = 0 and just "
           "returns forces. This is the mode the rest of the application is "
           "built around — geometry constraints, the variable-cell filters, "
           "the live streamed trajectory and the per-step metrics all come "
           "from ASE taking the steps.\n\n"
           "VASP internal: the tags below ARE the relaxation and no ASE "
           "optimizer is created. Much faster per step, because VASP keeps the "
           "wavefunction and density between ionic steps instead of restarting "
           "— but the steps happen inside one call, so they cannot be streamed "
           "or constrained from here."));
    form->addRow(tr("Relaxation driver:"), vaspDriverCombo_);
    connect(vaspDriverCombo_, &QComboBox::currentIndexChanged, this, [this] {
        updateVaspRows();
        refreshPreview();
    });

    vaspIonicRow_ = new QWidget(vaspGroup_);
    auto* ionicLayout = new QHBoxLayout(vaspIonicRow_);
    ionicLayout->setContentsMargins(0, 0, 0, 0);
    ionicLayout->addWidget(vaspIbrionCombo_);
    ionicLayout->addWidget(new QLabel(tr("ISIF"), vaspIonicRow_));
    ionicLayout->addWidget(vaspIsifCombo_);
    ionicLayout->addWidget(new QLabel(tr("EDIFFG"), vaspIonicRow_));
    ionicLayout->addWidget(vaspEdiffgSpin_);
    form->addRow(tr("IBRION:"), vaspIonicRow_);

    // -- Output -------------------------------------------------------------
    vaspLchargCheck_ = new QCheckBox(tr("CHGCAR"), vaspGroup_);
    vaspLchargCheck_->setChecked(true);
    vaspLchargCheck_->setToolTip(
        tr("LCHARG — write the charge density. On by default: it is what every "
           "downstream density analysis reads, and re-running an SCF to get it "
           "back costs far more than the file."));
    vaspLwaveCheck_ = new QCheckBox(tr("WAVECAR"), vaspGroup_);
    vaspLwaveCheck_->setToolTip(
        tr("LWAVE — write the wavefunctions. Large, but required to restart "
           "or to post-process bands."));
    vaspLaechgCheck_ = new QCheckBox(tr("AECCAR (Bader)"), vaspGroup_);
    vaspLaechgCheck_->setToolTip(
        tr("LAECHG — write the all-electron core and valence densities, which "
           "a Bader charge analysis needs on top of CHGCAR."));
    vaspLorbitCheck_ = new QCheckBox(tr("Projected DOS"), vaspGroup_);
    vaspLorbitCheck_->setToolTip(
        tr("LORBIT = 11 — site- and l-projected DOS in PROCAR/DOSCAR."));
    auto* outputRow = new QWidget(vaspGroup_);
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    for (QCheckBox* box : {vaspLchargCheck_, vaspLwaveCheck_, vaspLaechgCheck_,
                           vaspLorbitCheck_})
        outputLayout->addWidget(box);
    outputLayout->addStretch(1);
    form->addRow(tr("Write:"), outputRow);

    // -- Parallelization ----------------------------------------------------
    vaspNcoreSpin_ = new QSpinBox(vaspGroup_);
    vaspNcoreSpin_->setRange(0, 4096);
    vaspNcoreSpin_->setSpecialValueText(tr("auto"));
    vaspNcoreSpin_->setToolTip(
        tr("NCORE — cores working on one orbital. Left at auto unless you know "
           "the machine: a wrong value is a performance cliff rather than an "
           "error, so VASP's own choice is the safer default."));
    vaspKparSpin_ = new QSpinBox(vaspGroup_);
    vaspKparSpin_->setRange(0, 4096);
    vaspKparSpin_->setSpecialValueText(tr("auto"));
    vaspKparSpin_->setToolTip(
        tr("KPAR — k-points treated in parallel. The cheapest parallelism "
           "there is when the mesh is dense enough to divide."));
    auto* parallelRow = new QWidget(vaspGroup_);
    auto* parallelLayout = new QHBoxLayout(parallelRow);
    parallelLayout->setContentsMargins(0, 0, 0, 0);
    parallelLayout->addWidget(vaspNcoreSpin_);
    parallelLayout->addWidget(new QLabel(tr("KPAR"), parallelRow));
    parallelLayout->addWidget(vaspKparSpin_);
    parallelLayout->addStretch(1);
    form->addRow(tr("NCORE:"), parallelRow);

    // -- Escape hatch -------------------------------------------------------
    vaspExtraIncarEdit_ = new QPlainTextEdit(vaspGroup_);
    vaspExtraIncarEdit_->setMaximumHeight(70);
    vaspExtraIncarEdit_->setPlaceholderText(
        QStringLiteral("LDAU = .TRUE.\nLDAUU = 4.0 0.0\nNBANDS = 64"));
    vaspExtraIncarEdit_->setToolTip(
        tr("Extra INCAR tags, one per line, applied verbatim on top of "
           "everything above.\n\n"
           "No dialog can cover 300 INCAR flags, and a wizard that tries "
           "becomes a ceiling. Anything typed here is passed straight to the "
           "calculator and is not validated."));
    form->addRow(tr("Extra INCAR tags:"), vaspExtraIncarEdit_);

    for (QComboBox* combo : {vaspXcCombo_, vaspPrecCombo_, vaspAlgoCombo_,
                             vaspLrealCombo_, vaspIbrionCombo_, vaspIsifCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this] { refreshPreview(); });
    connect(vaspXcCombo_, &QComboBox::currentTextChanged, this,
            [this] { refreshPreview(); });
    connect(vaspNelmSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(vaspEdiffEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });
    connect(vaspEdiffgSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    for (QSpinBox* spin : {vaspNcoreSpin_, vaspKparSpin_})
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    for (QCheckBox* box : {vaspLchargCheck_, vaspLwaveCheck_, vaspLaechgCheck_,
                           vaspLorbitCheck_})
        connect(box, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    connect(vaspExtraIncarEdit_, &QPlainTextEdit::textChanged, this,
            [this] { refreshPreview(); });

    return vaspGroup_;
}

void SimulationWizardBase::updateVaspRows()
{
    if (!vaspGroup_ || !vaspIonicRow_)
        return;
    // A single point has no ionic steps, so IBRION/ISIF/EDIFFG describe
    // nothing. Hidden rather than disabled: three greyed-out controls read as
    // "broken", not as "not applicable here".
    auto* form = qobject_cast<QFormLayout*>(vaspGroup_->layout());
    if (!form)
        return;
    const auto setRowVisible = [form](QWidget* field, bool visible) {
        int row = -1;
        QFormLayout::ItemRole role{};
        form->getWidgetPosition(field, &row, &role);
        if (row >= 0)
            form->setRowVisible(row, visible);
    };
    const bool ionic = taskHasIonicSteps();
    // The driver choice only exists for a task that has ionic steps to give
    // to one side or the other.
    setRowVisible(vaspDriverCombo_, ionic);
    // IBRION / ISIF / EDIFFG describe VASP's own relaxation, so they are shown
    // only when VASP is the one relaxing. Under the ASE driver they are not
    // merely irrelevant — writing them is the bug.
    const bool vaspDrives = vaspDriverCombo_
        && vaspDriverCombo_->currentIndex()
            == static_cast<int>(core::VaspRelaxDriver::Vasp);
    setRowVisible(vaspIonicRow_, ionic && vaspDrives);
}

QWidget* SimulationWizardBase::buildLammpsGroup(QWidget* parent)
{
    lammpsGroup_ = new QGroupBox(tr("LAMMPS settings"), parent);
    auto* form = new QFormLayout(lammpsGroup_);

    auto* note = new QLabel(
        tr("LAMMPS is an <b>engine</b>, not a force field: what it computes is "
           "decided entirely by the pair style and coefficients below. Nothing "
           "here validates them — a <code>pair_coeff</code> that does not match "
           "the style, or a potential file for the wrong elements, is a physics "
           "error LAMMPS will run without complaint."),
        lammpsGroup_);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    lammpsInterfaceCombo_ = new QComboBox(lammpsGroup_);
    // Order matches core::LammpsInterface.
    lammpsInterfaceCombo_->addItems({tr("Library (in-process, LAMMPSlib)"),
                                     tr("Executable (per-step, lammpsrun)")});
    lammpsInterfaceCombo_->setToolTip(
        tr("Which of ASE's two LAMMPS interfaces to drive — the choice depends "
           "on how LAMMPS is installed, not on the physics.\n\n"
           "Library: runs LAMMPS in-process through its Python module, with no "
           "file I/O per step. The right choice for MD and relaxation, and what "
           "conda-forge's `lammps` package provides.\n\n"
           "Executable: spawns the `lmp` binary once per force evaluation and "
           "exchanges data files. Works with any build, including a plain "
           "distro package, but pays process startup on every step."));
    form->addRow(tr("Interface:"), lammpsInterfaceCombo_);

    lammpsPairStyleEdit_ = new QLineEdit(lammpsGroup_);
    lammpsPairStyleEdit_->setText(QStringLiteral("lj/cut 10.0"));
    lammpsPairStyleEdit_->setToolTip(
        tr("The `pair_style` line without the keyword, e.g.\n"
           "    lj/cut 10.0\n"
           "    eam/alloy\n"
           "    tersoff\n"
           "    sw\n\n"
           "Everything after the style name is passed through verbatim, which "
           "is how cutoffs and style options reach LAMMPS."));
    form->addRow(tr("Pair style:"), lammpsPairStyleEdit_);

    lammpsPairCoeffEdit_ = new QPlainTextEdit(lammpsGroup_);
    lammpsPairCoeffEdit_->setPlainText(QStringLiteral("* * 0.0103 3.4"));
    lammpsPairCoeffEdit_->setMaximumHeight(70);
    lammpsPairCoeffEdit_->setToolTip(
        tr("One `pair_coeff` line per row, without the keyword, e.g.\n"
           "    * * Cu_u3.eam.alloy Cu\n"
           "    1 1 0.0103 3.4\n\n"
           "Where a line names elements, they must be listed in the type order "
           "the generated script prints as \"LAMMPS species order\" — that "
           "order is derived from the structure, and getting it wrong computes "
           "a different compound rather than failing."));
    form->addRow(tr("Pair coefficients:"), lammpsPairCoeffEdit_);

    lammpsPotentialEdit_ = new QPlainTextEdit(lammpsGroup_);
    lammpsPotentialEdit_->setMaximumHeight(52);
    lammpsPotentialEdit_->setPlaceholderText(
        tr("/path/to/Cu_u3.eam.alloy   (one per line; absolute paths)"));
    lammpsPotentialEdit_->setToolTip(
        tr("Potential files the pair style reads (EAM tables, Tersoff "
           "parameter files…), one per line.\n\n"
           "Absolute paths: the executable interface runs LAMMPS in a scratch "
           "directory and the library interface inherits the process's working "
           "directory, so a relative path resolves against neither reliably."));
    form->addRow(tr("Potential files:"), lammpsPotentialEdit_);

    lammpsExtraEdit_ = new QPlainTextEdit(lammpsGroup_);
    lammpsExtraEdit_->setMaximumHeight(52);
    lammpsExtraEdit_->setPlaceholderText(
        tr("neighbor 2.0 bin        (one LAMMPS command per line)"));
    lammpsExtraEdit_->setToolTip(
        tr("Extra LAMMPS commands appended after the pair setup — neighbor "
           "list settings, `pair_modify`, style-specific `fix` commands. One "
           "per line.\n\n"
           "Only the library interface can apply these directly; with the "
           "executable interface they are emitted as comments, because "
           "lammpsrun builds its own input deck from a parameter dictionary."));
    form->addRow(tr("Extra commands:"), lammpsExtraEdit_);

    lammpsCommandEdit_ = new QLineEdit(lammpsGroup_);
    lammpsCommandEdit_->setPlaceholderText(
        tr("lmp_serial            (blank = $ASE_LAMMPSRUN_COMMAND, then $PATH)"));
    lammpsCommandEdit_->setToolTip(
        tr("The LAMMPS executable, for the Executable interface only. Left "
           "blank, ASE looks at $ASE_LAMMPSRUN_COMMAND and then for `lmp` on "
           "$PATH."));
    form->addRow(tr("LAMMPS binary:"), lammpsCommandEdit_);

    lammpsLogCheck_ = new QCheckBox(tr("Keep the LAMMPS log"), lammpsGroup_);
    lammpsLogCheck_->setChecked(true);
    lammpsLogCheck_->setToolTip(
        tr("Write lammps.log beside the job (library interface) or keep the "
           "scratch files (executable interface).\n\n"
           "On by default: when a pair style rejects its coefficients, that log "
           "is the only place the reason appears — ASE surfaces the failure as "
           "a bare exception."));
    form->addRow(QString(), lammpsLogCheck_);

    // Every control feeds the generated script, so the preview has to follow
    // each of them.
    connect(lammpsInterfaceCombo_, &QComboBox::currentIndexChanged, this,
            [this] { updateLammpsRows(); refreshPreview(); });
    connect(lammpsPairStyleEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });
    connect(lammpsCommandEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });
    for (QPlainTextEdit* edit : {lammpsPairCoeffEdit_, lammpsPotentialEdit_,
                                 lammpsExtraEdit_}) {
        connect(edit, &QPlainTextEdit::textChanged, this,
                [this] { refreshPreview(); });
    }
    connect(lammpsLogCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    updateLammpsRows();
    return lammpsGroup_;
}

void SimulationWizardBase::updateLammpsRows()
{
    if (!lammpsGroup_ || !lammpsInterfaceCombo_)
        return;
    // The binary only exists for the executable interface; showing it while the
    // library interface is selected invites the user to configure something
    // that will be ignored.
    const bool runInterface = lammpsInterfaceCombo_->currentIndex()
        == static_cast<int>(core::LammpsInterface::Run);
    setFormRowVisible(lammpsGroup_, lammpsCommandEdit_, runInterface);
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
            this, tr("Select MACE Model File"),
            SettingsManager::mlPotentialsStartPath(maceModelPathEdit_->text()),
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

QWidget* SimulationWizardBase::buildMlipGroup(QWidget* parent)
{
    // One group serving every non-MACE ML potential: they share the same
    // shape (a model file + a device) and differ only in which extra row
    // applies, so a group box per engine would be five near-identical panels
    // with one row each. updateMlipRows() shows only the selected engine's.
    mlipGroup_ = new QGroupBox(tr("Machine-Learning Potential"), parent);
    auto* form = new QFormLayout(mlipGroup_);

    // -- Model / checkpoint file (label + placeholder retuned per engine) ---
    mlipModelEdit_ = new QLineEdit(mlipGroup_);
    auto* browse = new QPushButton(tr("Browse…"), mlipGroup_);
    mlipModelRow_ = new QWidget(mlipGroup_);
    auto* pathLayout = new QHBoxLayout(mlipModelRow_);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->addWidget(mlipModelEdit_, 1);
    pathLayout->addWidget(browse);
    mlipModelLabel_ = new QLabel(tr("Model file:"), mlipGroup_);
    form->addRow(mlipModelLabel_, mlipModelRow_);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select Model File"),
            SettingsManager::mlPotentialsStartPath(mlipModelEdit_->text()),
            tr("Model files (*.pb *.pt *.pth *.model);;All files (*)"));
        if (!path.isEmpty())
            mlipModelEdit_->setText(path); // textChanged → refreshPreview
    });
    connect(mlipModelEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });

    mlipDeviceCombo_ = new QComboBox(mlipGroup_);
    // Order matches core::MlipDevice.
    mlipDeviceCombo_->addItem(tr("cpu"));
    mlipDeviceCombo_->addItem(tr("cuda (NVIDIA GPU)"));
    mlipDeviceCombo_->addItem(tr("mps (Apple GPU)"));
    mlipDeviceCombo_->setToolTip(
        tr("Device the model runs on. cuda needs a CUDA build of the "
           "framework; mps is Apple-silicon only and has no float64 kernels."));
    form->addRow(tr("Device / GPU:"), mlipDeviceCombo_);

    // -- NequIP / Allegro: training units of the deployed model -------------
    nequipUnitsRow_ = new QWidget(mlipGroup_);
    auto* unitsLayout = new QHBoxLayout(nequipUnitsRow_);
    unitsLayout->setContentsMargins(0, 0, 0, 0);
    nequipEnergyUnitsCombo_ = new QComboBox(nequipUnitsRow_);
    nequipEnergyUnitsCombo_->setEditable(true);
    nequipEnergyUnitsCombo_->addItems({QStringLiteral("eV"),
                                       QStringLiteral("kcal/mol"),
                                       QStringLiteral("Hartree"),
                                       QStringLiteral("meV")});
    nequipLengthUnitsCombo_ = new QComboBox(nequipUnitsRow_);
    nequipLengthUnitsCombo_->setEditable(true);
    nequipLengthUnitsCombo_->addItems({QStringLiteral("Angstrom"),
                                       QStringLiteral("Bohr"),
                                       QStringLiteral("nm")});
    unitsLayout->addWidget(new QLabel(tr("energy"), nequipUnitsRow_));
    unitsLayout->addWidget(nequipEnergyUnitsCombo_, 1);
    unitsLayout->addWidget(new QLabel(tr("length"), nequipUnitsRow_));
    unitsLayout->addWidget(nequipLengthUnitsCombo_, 1);
    nequipUnitsRow_->setToolTip(
        tr("Units the deployed model was TRAINED in. ASE works in eV and Å, so "
           "the calculator rescales by these — a model trained in kcal/mol "
           "reports silently wrong energies if this is left at eV."));
    form->addRow(tr("Model units:"), nequipUnitsRow_);

    // -- CHGNet -------------------------------------------------------------
    chgnetWeightsCombo_ = new QComboBox(mlipGroup_);
    // Order matches core::ChgNetWeights.
    chgnetWeightsCombo_->addItem(tr("0.3.0 (published checkpoint)"));
    chgnetWeightsCombo_->addItem(tr("latest (installed release default)"));
    chgnetWeightsCombo_->setToolTip(
        tr("Pretrained weight set. Pinning 0.3.0 keeps results reproducible "
           "across chgnet upgrades; \"latest\" tracks the installed package."));
    form->addRow(tr("Pretrained weights:"), chgnetWeightsCombo_);

    chgnetStressCheck_ = new QCheckBox(tr("Evaluate stress tensor"), mlipGroup_);
    chgnetStressCheck_->setChecked(true);
    chgnetStressCheck_->setToolTip(
        tr("Required for variable-cell relaxation and any stress analysis. "
           "Turning it off is slightly cheaper per step."));
    form->addRow(chgnetStressCheck_);

    // -- MatterSim ----------------------------------------------------------
    matterSimModelCombo_ = new QComboBox(mlipGroup_);
    // Order matches core::MatterSimModel.
    matterSimModelCombo_->addItem(tr("3M (1M parameters — fast)"));
    matterSimModelCombo_->addItem(tr("100M (5M parameters — accurate)"));
    matterSimModelCombo_->setCurrentIndex(0);
    matterSimModelCombo_->setToolTip(
        tr("Released MatterSim checkpoint. The larger model is markedly more "
           "accurate on unusual chemistries at a few times the cost."));
    form->addRow(tr("Model precision:"), matterSimModelCombo_);

    matterSimThermalCheck_ =
        new QCheckBox(tr("Specify thermodynamic state"), mlipGroup_);
    matterSimThermalCheck_->setToolTip(
        tr("MatterSim is trained across temperature and pressure; tick this to "
           "evaluate it at a specific state rather than the 0 K reference."));
    form->addRow(matterSimThermalCheck_);

    matterSimStateRow_ = new QWidget(mlipGroup_);
    auto* stateLayout = new QHBoxLayout(matterSimStateRow_);
    stateLayout->setContentsMargins(0, 0, 0, 0);
    matterSimTempSpin_ = new QDoubleSpinBox(matterSimStateRow_);
    matterSimTempSpin_->setRange(0.0, 10000.0);
    matterSimTempSpin_->setValue(300.0);
    matterSimTempSpin_->setSuffix(tr(" K"));
    matterSimPressureSpin_ = new QDoubleSpinBox(matterSimStateRow_);
    matterSimPressureSpin_->setRange(0.0, 1000.0);
    matterSimPressureSpin_->setDecimals(3);
    matterSimPressureSpin_->setSuffix(tr(" GPa"));
    stateLayout->addWidget(new QLabel(tr("T"), matterSimStateRow_));
    stateLayout->addWidget(matterSimTempSpin_, 1);
    stateLayout->addWidget(new QLabel(tr("P"), matterSimStateRow_));
    stateLayout->addWidget(matterSimPressureSpin_, 1);
    form->addRow(tr("State:"), matterSimStateRow_);
    connect(matterSimThermalCheck_, &QCheckBox::toggled, this, [this](bool on) {
        matterSimStateRow_->setEnabled(on);
        refreshPreview();
    });
    matterSimStateRow_->setEnabled(false);

    // -- FAIRChem -----------------------------------------------------------
    fairChemModelCombo_ = new QComboBox(mlipGroup_);
    // Order matches core::FairChemModel.
    fairChemModelCombo_->addItem(QStringLiteral("EquiformerV2"));
    fairChemModelCombo_->addItem(QStringLiteral("eSCN"));
    fairChemModelCombo_->setToolTip(
        tr("Architecture of the checkpoint above — it must match, or the "
           "checkpoint fails to load."));
    form->addRow(tr("Model type:"), fairChemModelCombo_);

    for (QComboBox* combo : {mlipDeviceCombo_, chgnetWeightsCombo_,
                             matterSimModelCombo_, fairChemModelCombo_,
                             nequipEnergyUnitsCombo_, nequipLengthUnitsCombo_}) {
        connect(combo, &QComboBox::currentTextChanged, this,
                [this] { refreshPreview(); });
    }
    connect(chgnetStressCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    for (QDoubleSpinBox* spin : {matterSimTempSpin_, matterSimPressureSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });

    return mlipGroup_;
}

void SimulationWizardBase::updateMlipRows()
{
    const auto kind = selectedCalculator();
    const bool nequip = kind == core::CalculatorKind::NequIp
        || kind == core::CalculatorKind::Allegro;
    const bool deepmd = kind == core::CalculatorKind::DeepMd;
    const bool chgnet = kind == core::CalculatorKind::ChgNet;
    const bool matterSim = kind == core::CalculatorKind::MatterSim;
    const bool fairChem = kind == core::CalculatorKind::FairChem;

    // CHGNet and MatterSim ship their own weights, so they need no file.
    const bool needsFile = deepmd || nequip || fairChem;
    setFormRowVisible(mlipGroup_, mlipModelRow_, needsFile);
    if (deepmd) {
        mlipModelLabel_->setText(tr("Frozen model (.pb):"));
        mlipModelEdit_->setPlaceholderText(
            tr("path/to/frozen_model.pb  (or .pth for the PyTorch backend)"));
    } else if (nequip) {
        mlipModelLabel_->setText(tr("Deployed model (.pth):"));
        mlipModelEdit_->setPlaceholderText(
            tr("path/to/deployed.pth  (output of `nequip-deploy build`)"));
    } else if (fairChem) {
        mlipModelLabel_->setText(tr("Checkpoint (.pt):"));
        mlipModelEdit_->setPlaceholderText(
            tr("path/to/checkpoint.pt  (must match the model type below)"));
    }
    setFormRowVisible(mlipGroup_, nequipUnitsRow_, nequip);
    setFormRowVisible(mlipGroup_, chgnetWeightsCombo_, chgnet);
    setFormRowVisible(mlipGroup_, chgnetStressCheck_, chgnet);
    setFormRowVisible(mlipGroup_, matterSimModelCombo_, matterSim);
    setFormRowVisible(mlipGroup_, matterSimThermalCheck_, matterSim);
    setFormRowVisible(mlipGroup_, matterSimStateRow_, matterSim);
    setFormRowVisible(mlipGroup_, fairChemModelCombo_, fairChem);

    // MPS has no float64 kernels in PyTorch, and every backend here is a
    // PyTorch model — warn rather than let the job die on the first pass.
    const bool mps = mlipDeviceCombo_->currentIndex()
        == static_cast<int>(core::MlipDevice::Mps);
    mlipDeviceCombo_->setStyleSheet(mps && deepmd ? QStringLiteral("color: #d9534f;")
                                                  : QString());
    if (mps && deepmd)
        mlipDeviceCombo_->setToolTip(
            tr("DeepMD-kit has no Apple-silicon (MPS) backend — run on the CPU "
               "or a CUDA GPU."));
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
    modeBasisGroup_ = new QGroupBox(tr("Mode && Basis Set"), parent);
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

    // DFT+U and the dispersion correction sit directly under the XC combo:
    // both are corrections TO the chosen functional (a U for the self-interaction
    // a semilocal functional leaves on narrow d/f shells, D4 for the long-range
    // correlation it has none of), so they belong with the functional rather than
    // among the k-point settings they have nothing to do with. Side by side in
    // one row — two short controls do not earn two full-width rows on a page
    // that already scrolls.
    //
    // DFT+U lives behind a button rather than inline: it needs a table, and it
    // is a minority setting that would otherwise crowd every GPAW page.
    hubbardButton_ = new QPushButton(tr("Hubbard parameters…"), modeBasisGroup_);
    hubbardButton_->setToolTip(
        tr("Add an on-site Coulomb repulsion U to a named orbital shell "
           "(GPAW setups={…}). For narrow d/f bands that a semilocal "
           "functional over-delocalizes."));
    connect(hubbardButton_, &QPushButton::clicked, this,
            &SimulationWizardBase::editHubbardParameters);

    // Dispersion: only for the wizards whose answer depends on it.
    dispersionD4Check_ =
        new QCheckBox(tr("van der Waals Correction (DFTD4)"), modeBasisGroup_);
    dispersionD4Check_->setToolTip(
        tr("Wrap the calculator in ASE's DFTD4, adding Grimme's D4 dispersion "
           "energy and forces.\n"
           "Semilocal functionals carry no long-range correlation, so layered "
           "and molecular systems come out under-bound without it. D4 is "
           "charge-dependent, which is what separates it from D3.\n"
           "Needs the dftd4 package in the job environment; the damping "
           "parameters follow the calculator's own functional."));
    connect(dispersionD4Check_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    xcCorrectionsRow_ = new QWidget(modeBasisGroup_);
    auto* xcCorrectionsLayout = new QHBoxLayout(xcCorrectionsRow_);
    xcCorrectionsLayout->setContentsMargins(0, 0, 0, 0);
    xcCorrectionsLayout->addWidget(hubbardButton_);
    if (showsDispersionToggle())
        xcCorrectionsLayout->addWidget(dispersionD4Check_);
    else
        dispersionD4Check_->hide();
    xcCorrectionsLayout->addStretch(1);
    modeForm->addRow(xcCorrectionsRow_);

    // For the script-template DFT backends (Espresso/VASP/Siesta) XC is edited
    // in the generated script; shown for them, hidden for GPAW.
    dftXcNote_ = new QLabel(
        tr("XC functional defaults to PBE in the script (editable in Stage 4)."),
        modeBasisGroup_);
    dftXcNote_->setWordWrap(true);
    modeForm->addRow(dftXcNote_);
    layout->addWidget(modeBasisGroup_);

    // ===== 2. Brillouin Zone & k-Points ===================================
    bzGroup_ = new QGroupBox(tr("Brillouin Zone && k-Points"), parent);
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

    // Γ-centered mesh (gamma=True) — GPAW k-point option, offered for every
    // GPAW wizard.
    gpawGammaCheck_ = new QCheckBox(tr("Gamma-centered Grid"), bzGroup_);
    gpawGammaCheck_->setToolTip(
        tr("Shift the Monkhorst-Pack mesh so it includes the Γ point "
           "(kpts={'size': …, 'gamma': True})."));
    connect(gpawGammaCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    // "Symmetry: off" — GPAW only, and only when the wizard opts in
    // (Single-Point / Geometry Optimization): a symmetry-off run is the
    // recommended MLWF baseline.
    gpawSymmetryOffCheck_ = new QCheckBox(tr("Symmetry: off"), bzGroup_);
    gpawSymmetryOffCheck_->setToolTip(
        tr("Disable point-group symmetry reduction of the k-point set "
           "(symmetry=\"off\") — sample the full, unsymmetrized Brillouin zone "
           "(required when the wavefunctions feed a Maximally Localized "
           "Wannier Functions run)."));
    connect(gpawSymmetryOffCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    // Both are short, self-describing k-point switches, so they share one row
    // instead of consuming two full-width rows of a page that already scrolls.
    gpawBzTogglesRow_ = new QWidget(bzGroup_);
    auto* bzToggleLayout = new QHBoxLayout(gpawBzTogglesRow_);
    bzToggleLayout->setContentsMargins(0, 0, 0, 0);
    bzToggleLayout->addWidget(gpawGammaCheck_);
    if (showsGpawSymmetryToggle())
        bzToggleLayout->addWidget(gpawSymmetryOffCheck_);
    else
        gpawSymmetryOffCheck_->hide();
    bzToggleLayout->addStretch(1);
    bzForm->addRow(gpawBzTogglesRow_);

    // The Hubbard-U editor and the D4 dispersion toggle used to live here.
    // They moved up to "Mode & Basis Set", directly under the XC combo they
    // correct — see buildDftGpawGroups above.

    layout->addWidget(bzGroup_);

    // ===== 3. Electronic Convergence & Smearing ===========================
    convGroup_ = new QGroupBox(tr("Electronic Convergence && Smearing"), parent);
    auto* convForm = new QFormLayout(convGroup_);

    // Subclass smearing / SCF rows first (e.g. Single-point's Fermi-Dirac /
    // Gaussian smearing and SCF tolerance / max steps).
    buildConvergenceRows(convForm);

    gpawEigensolverCombo_ = new QComboBox(convGroup_);
    // Display names, capitalized as the methods are actually written, and in
    // the order a user picks them: the robust default first, then the two
    // alternatives for when it struggles, then the special case.
    //
    // The enum travels as itemData rather than as the row number. Casting the
    // index straight to the enum silently binds the display order to the
    // declaration order, so reordering this list — exactly what this change
    // does — would have selected a different solver than the one named.
    const auto addSolver = [this](const QString& label,
                                   core::GpawEigensolver value) {
        gpawEigensolverCombo_->addItem(label, static_cast<int>(value));
    };
    addSolver(QStringLiteral("Davidson"), core::GpawEigensolver::Davidson);
    addSolver(QStringLiteral("RMM-DIIS"), core::GpawEigensolver::RmmDiis);
    addSolver(QStringLiteral("CG"), core::GpawEigensolver::ConjugateGradient);
    addSolver(QStringLiteral("Direct"), core::GpawEigensolver::Direct);

    // Same sizing rule as the smearing combo directly above it, so the two
    // dropdowns line up instead of one stretching to the margin.
    gpawEigensolverCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    gpawEigensolverCombo_->setSizePolicy(QSizePolicy::Preferred,
                                         QSizePolicy::Fixed);
    gpawEigensolverCombo_->setToolTip(
        tr("davidson: robust general default.\n"
           "cg: slower but very stable — try it when the SCF oscillates.\n"
           "rmm-diis: cheapest per step for large metallic systems.\n"
           "direct: exact diagonalization (LCAO / small systems)."));
    // The solver and the cap on its iterations are one thought: "how the SCF
    // is solved, and how long it may try". The cap is created by the subclass
    // (GpawElectronicRows) but placed here, next to what it caps.
    if (QWidget* steps = gpawScfStepsWidget()) {
        auto* solverRow = new QWidget(convGroup_);
        auto* solverLayout = new QHBoxLayout(solverRow);
        solverLayout->setContentsMargins(0, 0, 0, 0);
        // Stretch on the trailing spacer, not on the combo — matching the
        // smearing row, which is what makes the two the same width.
        solverLayout->addWidget(gpawEigensolverCombo_);
        solverLayout->addWidget(new QLabel(tr("max SCF steps"), solverRow));
        solverLayout->addWidget(steps);
        solverLayout->addStretch(1);
        convForm->addRow(tr("Eigensolver:"), solverRow);
    } else {
        convForm->addRow(tr("Eigensolver:"), gpawEigensolverCombo_);
    }

    gpawMixerCombo_ = new QComboBox(convGroup_);
    gpawMixerCombo_->addItems({QStringLiteral("Mixer"), QStringLiteral("MixerSum"),
                               QStringLiteral("MixerDif")});
    gpawMixerCombo_->setToolTip(
        tr("Mixer: non-magnetic systems.\n"
           "MixerSum: spin-polarized — mixes the total density.\n"
           "MixerDif: spin-polarized — total density + magnetization "
           "separately."));

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
    auto* mixerParamsRow = new QWidget(convGroup_);
    auto* mixerParams = new QHBoxLayout(mixerParamsRow);
    mixerParams->setContentsMargins(0, 0, 0, 0);
    mixerParams->addWidget(new QLabel(tr("beta"), mixerParamsRow));
    mixerParams->addWidget(gpawBetaSpin_);
    mixerParams->addWidget(new QLabel(tr("nmaxold"), mixerParamsRow));
    mixerParams->addWidget(gpawNmaxoldSpin_);
    mixerParams->addWidget(new QLabel(tr("weight"), mixerParamsRow));
    mixerParams->addWidget(gpawWeightSpin_);

    // The mixer kind and the parameters that tune it are one decision, so they
    // share a row: reading "MixerSum · beta 0.05" left to right beats hunting
    // across two rows, and the page keeps a screenful of vertical space.
    gpawMixerRow_ = new QWidget(convGroup_);
    auto* mixerRow = new QHBoxLayout(gpawMixerRow_);
    mixerRow->setContentsMargins(0, 0, 0, 0);
    mixerRow->addWidget(gpawMixerCombo_);
    mixerRow->addWidget(mixerParamsRow);
    mixerRow->addStretch(1);
    convForm->addRow(tr("Density mixer:"), gpawMixerRow_);

    const auto toleranceEdit = [](QWidget* parent, double initial,
                                  double minimum, double maximum,
                                  const QString& tip) {
        auto* edit = new QLineEdit(QString::number(initial, 'g', 6), parent);
        auto* validator = new QDoubleValidator(minimum, maximum, 12, edit);
        validator->setNotation(QDoubleValidator::ScientificNotation);
        validator->setLocale(QLocale::c());
        edit->setValidator(validator);
        edit->setToolTip(tip);
        return edit;
    };
    // The two SCF thresholds are converged together and read as a pair, so
    // they sit side by side under one label rather than on two rows.
    gpawTolRow_ = new QWidget(convGroup_);
    auto* tolRow = new QHBoxLayout(gpawTolRow_);
    tolRow->setContentsMargins(0, 0, 0, 0);
    gpawEigenTolEdit_ = toleranceEdit(
        gpawTolRow_, 4e-8, 1e-12, 1e-2,
        tr("GPAW convergence['eigenstates'] — integrated eigenstate residual, "
           "in eV² per valence electron (e.g. 4e-8)."));
    gpawDensityTolEdit_ = toleranceEdit(
        gpawTolRow_, 1e-4, 1e-9, 1e-1,
        tr("GPAW convergence['density'] — change in the density integrated "
           "over the cell, in electrons per valence electron (e.g. 1e-4)."));
    // Energy leads the row: the three are converged together, and the energy
    // threshold is the one a user sets first and the other two support.
    if (QWidget* energy = gpawEnergyToleranceWidget()) {
        tolRow->addWidget(new QLabel(tr("energy"), gpawTolRow_));
        tolRow->addWidget(energy, 1);
    }
    tolRow->addWidget(new QLabel(tr("eigenstates"), gpawTolRow_));
    tolRow->addWidget(gpawEigenTolEdit_, 1);
    tolRow->addWidget(new QLabel(tr("density"), gpawTolRow_));
    tolRow->addWidget(gpawDensityTolEdit_, 1);
    convForm->addRow(tr("Convergence tolerances:"), gpawTolRow_);
    layout->addWidget(convGroup_);

    // ===== 4. Spin Configurations =========================================
    spinGroup_ = new QGroupBox(tr("Spin Configurations"), parent);
    auto* spinForm = new QFormLayout(spinGroup_);
    // Subclass spin rows (polarization mode + initial magnetic moments).
    buildSpinRows(spinForm);
    layout->addWidget(spinGroup_);

    // ===== 5. Density Exports =============================================
    // Renamed from "Output Files & Density Exports": the group only ever held
    // density exports, and the longer title implied output settings that were
    // not there.
    outputGroup_ = new QGroupBox(tr("Density Exports"), parent);
    auto* outLayout = new QVBoxLayout(outputGroup_);

    gpawDensityExportCheck_ = new QCheckBox(
        tr("Export volumetric fields (.cube) after the SCF"), outputGroup_);
    gpawDensityExportCheck_->setToolTip(
        tr("Write the selected fields to Gaussian cube files in the job "
           "directory. Each is one grid evaluation on the already-converged "
           "calculation, so several cost little more than one."));
    connect(gpawDensityExportCheck_, &QCheckBox::toggled, this, [this](bool on) {
        for (QCheckBox* field : densityFieldChecks_)
            if (field)
                field->setEnabled(on);
        refreshPreview();
    });
    outLayout->addWidget(gpawDensityExportCheck_);

    // Six fields in two columns. A single column would run the group to six
    // rows on a page that already scrolls, and the fields pair naturally:
    // the two densities, then the two potentials-of-a-kind, then the two
    // kinetic-energy-derived ones.
    auto* fieldGrid = new QGridLayout;
    fieldGrid->setContentsMargins(18, 0, 0, 0);
    struct FieldSpec {
        const char* label;
        const char* tip;
    };
    static const FieldSpec kFields[kDensityFieldCount] = {
        {QT_TR_NOOP("All-electron density"),
         QT_TR_NOOP("get_all_electron_density(gridrefinement=2) — the full "
                    "density including the nuclear cusps the PAW "
                    "pseudization smooths away.")},
        {QT_TR_NOOP("Pseudodensity"),
         QT_TR_NOOP("get_pseudo_density() — the smooth valence density the "
                    "SCF actually iterates on.")},
        {QT_TR_NOOP("Spin density"),
         QT_TR_NOOP("n(up) - n(down). Identically zero in a spin-restricted "
                    "run; set Spin polarization to Collinear for a "
                    "meaningful field.")},
        {QT_TR_NOOP("Hartree potential"),
         QT_TR_NOOP("get_electrostatic_potential() — the electrostatic "
                    "potential in eV, the field a work function or a band "
                    "alignment is read off.")},
        {QT_TR_NOOP("ELF"),
         QT_TR_NOOP("Electron localization function, in [0, 1]: where the "
                    "electrons pair up. Lone pairs and bonds show as "
                    "basins.")},
        {QT_TR_NOOP("Kinetic energy density"),
         QT_TR_NOOP("tau(r), the positive-definite kinetic energy density — "
                    "what the ELF and every meta-GGA are built from.")},
    };
    for (int i = 0; i < kDensityFieldCount; ++i) {
        densityFieldChecks_[i] =
            new QCheckBox(tr(kFields[i].label), outputGroup_);
        densityFieldChecks_[i]->setToolTip(tr(kFields[i].tip));
        densityFieldChecks_[i]->setEnabled(false);
        fieldGrid->addWidget(densityFieldChecks_[i], i / 2, i % 2);
    }
    // All-electron on by default: it is the field almost every export is
    // actually after, and it matches what the old single-choice combo
    // defaulted to.
    //
    // Set BEFORE the preview wiring below. This page is still under
    // construction — the ORCA and LAMMPS groups do not exist yet — and
    // refreshPreview() reads every engine's widgets through
    // baseCalculatorConfig(), so a toggled() fired here would dereference a
    // combo that has not been created.
    densityFieldChecks_[0]->setChecked(true);
    for (QCheckBox* field : densityFieldChecks_)
        connect(field, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    outLayout->addLayout(fieldGrid);

    if (!showsGpawDensityExport()) {
        gpawDensityExportCheck_->hide();
        for (QCheckBox* field : densityFieldChecks_)
            field->hide();
    }
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
    // ...and hide the one NOT in play. The cutoff is the plane-wave basis's
    // own convergence parameter; in FD the basis is the real-space grid and in
    // LCAO it is the atomic orbital set, so leaving the row on screen offered a
    // number that changed nothing — the classic "I converged the cutoff and the
    // energy never moved" trap.
    //
    // Only for GPAW: the shared cutoff row also serves Quantum ESPRESSO, VASP
    // and SIESTA, which are plane-wave throughout and always want it.
    // Also hidden when the whole calculator is inherited from a baseline SCF:
    // the cutoff is then fixed by that .gpw and offering it would invite an
    // edit that the restart ignores.
    setRowVisible(cutoffSpin_, mode == core::GpawMode::PlaneWave
                      && !inheritsCalculatorFromBaseline());
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

    // The launch command, above the script it launches. Editable because the
    // last-minute questions ("how many ranks?", "does this node have mpirun?")
    // arrive here, at the point of running, not back in Preferences.
    auto* runRow = new QHBoxLayout;
    runRow->addWidget(new QLabel(tr("Running:"), page));
    runCommandEdit_ = new QLineEdit(page);
    runCommandEdit_->setToolTip(
        tr("Shell command this job launches with, from Preferences → \"Run\".\n"
           "Edit it to change MPI ranks, thread pinning or flags for this run "
           "only — Preferences keeps the saved template.\n"
           "{input} / {output} stay symbolic: ASE substitutes the solver's own "
           "file names."));
    runRow->addWidget(runCommandEdit_, 1);
    scriptLayout->addLayout(runRow);
    connect(runCommandEdit_, &QLineEdit::textEdited, this,
            [this] { runCommandEdited_ = true; });

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
        if (mlipGroup_) mlipGroup_->setVisible(false);
        if (orcaGroup_) orcaGroup_->setVisible(false);
        if (vaspGroup_) vaspGroup_->setVisible(false);
        if (lammpsGroup_) lammpsGroup_->setVisible(false);
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
    // MACE keeps its own group (foundation families / sizes have no analogue
    // in the others); the remaining ML potentials share the generic one.
    const bool isMlip = core::isMlipCalculator(kind) && !isMace;

    // Mode & Basis Set and Brillouin Zone & k-Points host the shared cutoff /
    // k-points, so they show for every DFT engine. Convergence / Spin carry
    // GPAW-only or subclass-injected rows; Output (density export) is
    // GPAW-only.
    modeBasisGroup_->setVisible(isDft);
    bzGroup_->setVisible(isDft);
    convGroup_->setVisible(isGpaw || (isDft && hasConvergenceExtras()));
    spinGroup_->setVisible(isDft && hasSpinExtras());
    outputGroup_->setVisible(isGpaw && showsGpawDensityExport());
    maceGroup_->setVisible(isMace);
    mlipGroup_->setVisible(isMlip);
    if (isMlip)
        updateMlipRows();
    orcaGroup_->setVisible(isOrca);
    if (vaspGroup_) {
        const bool isVasp = kind == core::CalculatorKind::Vasp;
        vaspGroup_->setVisible(isVasp);
        if (isVasp)
            updateVaspRows();
    }
    if (lammpsGroup_) {
        const bool isLammps = kind == core::CalculatorKind::Lammps;
        lammpsGroup_->setVisible(isLammps);
        if (isLammps)
            updateLammpsRows();
    }

    // GPAW-only Brillouin-zone options: Γ-centering and the symmetry toggle
    // share one row, so the row hides as a unit for non-GPAW engines.
    setFormRowVisible(bzGroup_, gpawBzTogglesRow_, isGpaw);

    // The XC note applies only to the script-template DFT backends; GPAW picks
    // XC in its own combo. Mode / grid / basis / XC combo and the density
    // export are GPAW-only rows.
    if (dftXcNote_)
        setFormRowVisible(modeBasisGroup_, dftXcNote_, isDft && !isGpaw);
    setFormRowVisible(modeBasisGroup_, gpawModeCombo_, isGpaw);
    setFormRowVisible(modeBasisGroup_, gpawXcCombo_, isGpaw);
    // The XC corrections (Hubbard U, D4 dispersion) follow the XC combo they
    // correct: both are written into the GPAW calculator (setups={…} / the
    // DFTD4 wrapper around it), so they only apply to the GPAW backend.
    setFormRowVisible(modeBasisGroup_, xcCorrectionsRow_, isGpaw);
    // Grid spacing / LCAO basis are GPAW-only *and* mode-dependent; hide them
    // wholesale for non-GPAW, then let updateGpawRows pick the right one.
    setFormRowVisible(modeBasisGroup_, gpawGridSpacingSpin_, isGpaw);
    setFormRowVisible(modeBasisGroup_, gpawBasisCombo_, isGpaw);
    // The mixer (combo + parameters) and the two tolerances are each one
    // composite row now, so their containers — not the individual fields — are
    // what the form layout can resolve and hide.
    for (QWidget* w : {static_cast<QWidget*>(gpawEigensolverCombo_),
                       gpawMixerRow_, gpawTolRow_})
        setFormRowVisible(convGroup_, w, isGpaw);

    // Baseline inheritance (Electronic Structure): the run restarts from a
    // completed SCF density, so its plane-wave cutoff, XC functional and mode
    // are fixed by that .gpw — hide those controls and show a note instead.
    const bool inheritGpaw = isGpaw && inheritsCalculatorFromBaseline();
    // The cutoff row has exactly one owner. For GPAW that is updateGpawRows(),
    // which is also called on its own when the mode combo changes and so must
    // not be second-guessed here; for the other DFT engines (always plane-wave)
    // it is simply shown. Setting it in both places is how the FD/LCAO hide
    // ended up being undone two lines later.
    if (isGpaw)
        updateGpawRows();
    else
        setFormRowVisible(modeBasisGroup_, cutoffSpin_, isDft);
    if (inheritGpaw) {
        setFormRowVisible(modeBasisGroup_, gpawXcCombo_, false);
        setFormRowVisible(modeBasisGroup_, gpawModeCombo_, false);
    }
    if (baselineInheritNote_)
        baselineInheritNote_->setVisible(inheritGpaw);

    refreshRunCommand();
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

    // -- MLIP backends (DeepMD … FAIRChem) ---------------------------------
    // One model-path field feeds whichever engine is selected, so the value is
    // routed to that engine's config field rather than to all of them.
    c.mlipDevice = static_cast<core::MlipDevice>(mlipDeviceCombo_->currentIndex());
    const std::string modelPath =
        mlipModelEdit_->text().trimmed().toStdString();
    switch (c.calculator) {
    case core::CalculatorKind::DeepMd:
        c.deepmdModelPath = modelPath;
        break;
    case core::CalculatorKind::NequIp:
    case core::CalculatorKind::Allegro:
        c.nequipModelPath = modelPath;
        break;
    case core::CalculatorKind::FairChem:
        c.fairChemCheckpointPath = modelPath;
        break;
    default:
        break;
    }
    c.nequipEnergyUnits =
        nequipEnergyUnitsCombo_->currentText().trimmed().toStdString();
    c.nequipLengthUnits =
        nequipLengthUnitsCombo_->currentText().trimmed().toStdString();
    c.chgnetWeights =
        static_cast<core::ChgNetWeights>(chgnetWeightsCombo_->currentIndex());
    c.chgnetStress = chgnetStressCheck_->isChecked();
    c.matterSimModel =
        static_cast<core::MatterSimModel>(matterSimModelCombo_->currentIndex());
    c.matterSimThermal = matterSimThermalCheck_->isChecked();
    c.matterSimTemperatureK = matterSimTempSpin_->value();
    c.matterSimPressureGPa = matterSimPressureSpin_->value();
    c.fairChemModel =
        static_cast<core::FairChemModel>(fairChemModelCombo_->currentIndex());

    c.gpawMode = static_cast<core::GpawMode>(gpawModeCombo_->currentIndex());
    c.gpawGridSpacing = gpawGridSpacingSpin_->value();
    c.gpawBasis = gpawBasisCombo_->currentText().trimmed().toStdString();
    c.gpawXc = gpawXcCombo_->currentText().trimmed().toStdString();
    c.gpawEigensolver =
        static_cast<core::GpawEigensolver>(
            gpawEigensolverCombo_->currentData().toInt());
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
    c.gpawGammaCentered = gpawGammaCheck_ && gpawGammaCheck_->isChecked();
    c.useHubbardU = hubbardEnabled_ && !hubbardParameters_.empty();
    c.hubbardU = hubbardParameters_;
    c.dispersionD4 =
        dispersionD4Check_ && dispersionD4Check_->isChecked()
        && showsDispersionToggle();
    c.gpawExportDensity =
        gpawDensityExportCheck_ && gpawDensityExportCheck_->isChecked();
    if (gpawDensityTypeCombo_)
        c.gpawDensityType = static_cast<core::GpawDensityType>(
            gpawDensityTypeCombo_->currentIndex());
    // Order matches the kFields table and core::GpawDensityExports.
    if (densityFieldChecks_[0]) {
        c.gpawDensityExports.allElectron = densityFieldChecks_[0]->isChecked();
        c.gpawDensityExports.pseudo = densityFieldChecks_[1]->isChecked();
        c.gpawDensityExports.spin = densityFieldChecks_[2]->isChecked();
        c.gpawDensityExports.hartree = densityFieldChecks_[3]->isChecked();
        c.gpawDensityExports.elf = densityFieldChecks_[4]->isChecked();
        c.gpawDensityExports.kineticEnergy =
            densityFieldChecks_[5]->isChecked();
    }

    if (vaspPotcarEdit_) {
        c.vaspPotcarPath = vaspPotcarEdit_->text().trimmed().toStdString();
        c.vaspXc = vaspXcCombo_->currentText().trimmed().toStdString();
        c.vaspPrec =
            static_cast<core::VaspPrecision>(vaspPrecCombo_->currentIndex());
        c.vaspAlgo = static_cast<core::VaspAlgo>(vaspAlgoCombo_->currentIndex());
        c.vaspNelm = vaspNelmSpin_->value();
        bool ok = false;
        const double ediff =
            QLocale::c().toDouble(vaspEdiffEdit_->text(), &ok);
        if (ok && ediff > 0.0)
            c.vaspEdiff = ediff;
        c.vaspLreal = vaspLrealCombo_->currentData().toString().toStdString();
        c.vaspRelaxDriver =
            static_cast<core::VaspRelaxDriver>(vaspDriverCombo_->currentIndex());
        c.vaspIbrion = vaspIbrionCombo_->currentData().toInt();
        c.vaspIsif = vaspIsifCombo_->currentData().toInt();
        c.vaspEdiffg = vaspEdiffgSpin_->value();
        c.vaspLwave = vaspLwaveCheck_->isChecked();
        c.vaspLcharg = vaspLchargCheck_->isChecked();
        c.vaspLaechg = vaspLaechgCheck_->isChecked();
        c.vaspLorbit = vaspLorbitCheck_->isChecked();
        c.vaspNcore = vaspNcoreSpin_->value();
        c.vaspKpar = vaspKparSpin_->value();
        c.vaspExtraIncar =
            vaspExtraIncarEdit_->toPlainText().trimmed().toStdString();
    }

    c.orcaMethod = orcaMethodCombo_->currentText().trimmed().toStdString();
    c.orcaBasis = orcaBasisCombo_->currentText().trimmed().toStdString();
    c.charge = chargeSpin_->value();
    c.multiplicity = multiplicitySpin_->value();

    // -- LAMMPS -------------------------------------------------------------
    // The three multi-line fields are split on newlines with blanks dropped, so
    // a trailing empty line does not become an empty LAMMPS command (which the
    // engine rejects with a parse error rather than ignoring).
    if (lammpsInterfaceCombo_) {
        const auto lines = [](const QPlainTextEdit* edit) {
            std::vector<std::string> out;
            if (!edit)
                return out;
            const QStringList parts = edit->toPlainText().split(
                QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                const QString trimmed = part.trimmed();
                if (!trimmed.isEmpty())
                    out.push_back(trimmed.toStdString());
            }
            return out;
        };
        c.lammpsInterface = static_cast<core::LammpsInterface>(
            lammpsInterfaceCombo_->currentIndex());
        c.lammpsPairStyle =
            lammpsPairStyleEdit_->text().trimmed().toStdString();
        c.lammpsPairCoeff = lines(lammpsPairCoeffEdit_);
        c.lammpsPotentialFiles = lines(lammpsPotentialEdit_);
        c.lammpsExtraCommands = lines(lammpsExtraEdit_);
        c.lammpsCommand = lammpsCommandEdit_->text().trimmed().toStdString();
        c.lammpsKeepLog = lammpsLogCheck_->isChecked();
    }
    return c;
}

QString SimulationWizardBase::runCommand() const
{
    return runCommandEdit_ ? runCommandEdit_->text().trimmed() : QString();
}

void SimulationWizardBase::refreshRunCommand()
{
    if (!runCommandEdit_ || runCommandEdited_)
        return; // the user's own command wins over the engine's template
    RunCommands::Context context;
    context.pythonExecutable = pythonExecutable();
    context.scriptFile = QStringLiteral("run.py");
    context.cores = RunCommands::cores();
    runCommandEdit_->setText(
        RunCommands::displayCommand(selectedCalculator(), context));
}

void SimulationWizardBase::editHubbardParameters()
{
    HubbardParametersDialog dialog(hubbardEnabled_, hubbardParameters_,
                                   calculatorElements(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    hubbardEnabled_ = dialog.isEnabled();
    hubbardParameters_ = dialog.parameters();
    refreshPreview();
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
    if (hasSecondSettingsStage_)
        titles << secondSettingsHeader();
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
        if (stage_ == reviewStage_) {
            refreshPreview();    // (re)generate on arriving at the review stage
            refreshRunCommand(); // and re-resolve for the engine now selected
        }
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

QString SimulationWizardBase::vaspPotcarDirectory()
{
    return QSettings().value(kVaspPotcarKey).toString();
}

void SimulationWizardBase::setVaspPotcarDirectory(const QString& path)
{
    QSettings().setValue(kVaspPotcarKey, path.trimmed());
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
