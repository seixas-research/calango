#include "gui/SimulationWizardBase.hpp"

#include "gui/CalculatorParameters.hpp"
#include "gui/GpawElectronicRows.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/SettingsManager.hpp"
#include "gui/HubbardParametersDialog.hpp"

#include "gui/CondaEnvs.hpp"
#include "gui/EnginePresets.hpp"
#include "gui/PythonHighlighter.hpp"
#include "gui/RunCommands.hpp"
#include "gui/ScriptStaging.hpp"
#include "gui/VaspPotcarPreflight.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
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
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

namespace calango::gui {

namespace {
/// Where the VASP POTCAR directory is remembered. Under `jobs/` with the other
/// installation-wide run settings rather than under a wizard's own key, because
/// it is shared by every VASP-capable dialog.
/// Where the wizard's own POTCAR field used to persist the path, before it
/// moved to Preferences → External Files. Read as a fallback so an existing
/// configuration keeps working; never written.
const auto kLegacyVaspPotcarKey = QStringLiteral("jobs/vaspPotcarPath");
/// Where the DFTB+ Slater-Koster directory persists. Per-installation state
/// like the VASP POTCAR root — the parameter set is downloaded once and every
/// DFTB+ run wants the same one — but edited here in the wizard, because
/// Preferences → External Files has no DFTB entry to defer to.
const auto kDftbSlakoKey = QStringLiteral("jobs/dftbSlakoDir");

// setFormRowVisible() used to live here as a file-local helper. It moved to
// GuiUtils when the second set of engine groups needed it — the same rule the
// rest of that file follows: an identical private copy in two places is a fix
// that only lands in one of them.
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
        if (!preflightVaspPotcar() || !preflightSecondary())
            return;
        // Calango's own engine has no script to launch — it runs in this
        // process. Distinguished here rather than at the host so a wizard
        // whose host installed no runner still cannot silently stage a
        // run.py against a calculator Python has never heard of.
        action_ = usesNativeEngine() ? Action::RunNativeEngine
                                     : Action::RunLocal;
        accept();
    });
    connect(runRemoteButton_, &QPushButton::clicked, this, [this] {
        if (!preflightVaspPotcar() || !preflightSecondary())
            return;
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
    // The engine list, GROUPED BY FAMILY: the ab-initio codes first, then the
    // semi-empirical ones, then the machine-learning potentials, then the
    // classical force fields and the engines that run them.
    //
    // The order is defined here and not by core::CalculatorKind, whose values
    // are serialized into saved projects and are therefore append-only — a menu
    // that followed the enum would put every engine added after 2025 at the
    // bottom forever, and it already had ML potentials, DFT codes and test
    // potentials interleaved in the order somebody happened to write them.
    //
    // Within each family the entries are ordered by how commonly they are the
    // answer rather than alphabetically: someone opening this dropdown is
    // usually looking for GPAW, Quantum ESPRESSO or VASP, and making them read
    // past ABINIT and CP2K to find one is a cost paid on every single run.
    //
    // A subclass may restrict the list (the Electronic Bands wizard offers only
    // DFT-capable calculators), so only allowed kinds appear.
    //
    // Separators are therefore PENDING rather than inserted: separate() only
    // records that the next entry starts a new family, and the divider is
    // written when — and if — that entry actually arrives. Inserting eagerly
    // leaves a divider at the end of a list whose remaining families were all
    // filtered out, which is both an empty section header and an extra row in
    // count() that every caller counting engines has to know to subtract.
    bool separatorPending = false;
    const auto addCalc = [this, &separatorPending](
                             const QString& label, core::CalculatorKind kind) {
        if (!calculatorAllowed(kind))
            return;
        if (separatorPending && calcCombo_->count() > 0)
            calcCombo_->insertSeparator(calcCombo_->count());
        separatorPending = false;
        calcCombo_->addItem(label, static_cast<int>(kind));
    };
    const auto separate = [&separatorPending] { separatorPending = true; };

    // -- Ab initio / DFT ----------------------------------------------------
    // GPAW first, and therefore the default selection in every wizard that
    // allows it: a combo box opens on index 0, so whatever leads this list is
    // what an unmodified run uses. That position previously belonged to the
    // built-in engine because it needs no external code installed — which is
    // true and was the wrong criterion. The built-in engine is a SCAFFOLD that
    // produces no energy (see CalculatorKind::CalangoDft), so leading with it
    // meant the out-of-the-box run of every module was the one that cannot
    // return a number. It now sits at the very bottom of the list, past the
    // classical potentials; see the end of this function.
    addCalc(tr("GPAW (DFT)"), core::CalculatorKind::Gpaw);
    addCalc(tr("Quantum ESPRESSO (DFT)"), core::CalculatorKind::QuantumEspresso);
    addCalc(tr("VASP (DFT)"), core::CalculatorKind::Vasp);
    addCalc(tr("ABINIT (plane-wave / PAW DFT)"), core::CalculatorKind::Abinit);
    addCalc(tr("CP2K (Gaussian and plane waves)"), core::CalculatorKind::Cp2k);
    addCalc(tr("FHI-aims (all-electron DFT)"), core::CalculatorKind::FhiAims);
    addCalc(tr("SIESTA (numerical-orbital DFT)"), core::CalculatorKind::Siesta);
    addCalc(tr("OpenMX (pseudo-atomic-orbital DFT)"),
            core::CalculatorKind::OpenMx);
    addCalc(tr("FLEUR (full-potential LAPW)"), core::CalculatorKind::Fleur);
    addCalc(tr("NWChem (quantum chemistry / plane-wave DFT)"),
            core::CalculatorKind::NwChem);
    addCalc(tr("ORCA (quantum chemistry)"), core::CalculatorKind::Orca);
    separate();

    // -- Semi-empirical / tight binding -------------------------------------
    // An electronic structure, but from a fitted parameterization rather than
    // from a basis and a functional — between the ab-initio codes above and the
    // potentials below, and priced accordingly.
    addCalc(tr("xTB (semi-empirical tight binding)"), core::CalculatorKind::Xtb);
    addCalc(tr("DFTB+ (tight binding DFT)"), core::CalculatorKind::DftbPlus);
    separate();

    // -- Machine-learning interatomic potentials ----------------------------
    addCalc(tr("MACE (ML potential)"), core::CalculatorKind::Mace);
    addCalc(tr("CHGNet (universal ML potential)"), core::CalculatorKind::ChgNet);
    addCalc(tr("MatterSim (universal ML potential)"),
            core::CalculatorKind::MatterSim);
    addCalc(tr("FAIRChem / OCP (ML potential)"), core::CalculatorKind::FairChem);
    addCalc(tr("NequIP (ML potential)"), core::CalculatorKind::NequIp);
    addCalc(tr("Allegro (ML potential)"), core::CalculatorKind::Allegro);
    addCalc(tr("DeepMD-kit (ML potential)"), core::CalculatorKind::DeepMd);
    separate();

    // -- Classical force fields and the engines that run them ---------------
    addCalc(tr("LAMMPS (classical MD)"), core::CalculatorKind::Lammps);
    addCalc(tr("GROMACS (biomolecular MM)"), core::CalculatorKind::Gromacs);
    addCalc(tr("Amber (biomolecular MM)"), core::CalculatorKind::Amber);
    addCalc(tr("EMT (fast test potential)"), core::CalculatorKind::EMT);
    addCalc(tr("ASAP (fast EMT / OpenKIM)"), core::CalculatorKind::Asap);
    addCalc(tr("Lennard-Jones"), core::CalculatorKind::LennardJones);
    separate();

    // -- Experimental -------------------------------------------------------
    // Calango's own engine, last in the list and in its own section. It is the
    // only entry that needs nothing installed, and the only one that cannot
    // yet return an energy: its basis generation, integration grid, matrix
    // assembly and eigensolver are unimplemented, so a run reports what is
    // missing and produces no result. Bottom placement is therefore not a
    // ranking of ambition but a statement about what happens if you pick it
    // without reading — and the label carries the same warning, so the
    // information does not depend on noticing which section it is in.
    addCalc(tr("Calango Native DFT (experimental)"),
            core::CalculatorKind::CalangoDft);
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
    layout->addWidget(buildEspressoGroup(page));
    layout->addWidget(buildSiestaGroup(page));
    layout->addWidget(buildLammpsGroup(page));
    layout->addWidget(buildXtbGroup(page));
    layout->addWidget(buildDftbGroup(page));
    layout->addWidget(buildGromacsGroup(page));
    // ABINIT, FHI-aims, NWChem, OpenMX, FLEUR, CP2K and Amber. Held in their
    // own class rather than as seven more group builders here: this file
    // already carries eight of them, and what those seven need from it is only
    // the shared cutoff / k-grid rows above.
    extendedEngines_.build(page, layout, [this] { refreshPreview(); });

    // Subclass-supplied extra settings (e.g. Single-point's convergence group,
    // folded in here when it has no separate Stage 1).
    if (QWidget* extras = buildCalculatorExtras())
        layout->addWidget(extras);

    layout->addStretch(1);
    return page;
}

QWidget* SimulationWizardBase::buildEspressoGroup(QWidget* parent)
{
    qeGroup_ = new QGroupBox(tr("Quantum ESPRESSO settings"), parent);
    auto* form = new QFormLayout(qeGroup_);

    qePseudoNote_ = new QLabel(qeGroup_);
    qePseudoNote_->setWordWrap(true);
    qePseudoNote_->setTextFormat(Qt::RichText);
    form->addRow(tr("Pseudopotentials:"), qePseudoNote_);

    // The dual cutoff, on one row, because it is ONE decision. Which ratio is
    // right depends on the pseudopotential family and nothing else in the UI
    // knows that, so the two live side by side with the note that says so.
    qeEcutwfcSpin_ = new QDoubleSpinBox(qeGroup_);
    qeEcutwfcSpin_->setRange(5.0, 400.0);
    qeEcutwfcSpin_->setDecimals(1);
    qeEcutwfcSpin_->setSingleStep(5.0);
    qeEcutwfcSpin_->setValue(60.0);
    qeEcutwfcSpin_->setSuffix(tr(" Ry"));
    qeEcutwfcSpin_->setToolTip(
        tr("ecutwfc — the plane-wave cutoff for the WAVEFUNCTIONS.\n\n"
           "In Rydberg, which is what pw.x reads. Converge it against total "
           "energy differences, not against the absolute energy: the absolute "
           "value keeps falling long after every quantity you care about has "
           "stopped moving."));

    qeEcutrhoSpin_ = new QDoubleSpinBox(qeGroup_);
    qeEcutrhoSpin_->setRange(0.0, 3000.0);
    qeEcutrhoSpin_->setDecimals(1);
    qeEcutrhoSpin_->setSingleStep(20.0);
    qeEcutrhoSpin_->setValue(0.0);
    qeEcutrhoSpin_->setSuffix(tr(" Ry"));
    qeEcutrhoSpin_->setSpecialValueText(tr("auto (4 × ecutwfc)"));
    qeEcutrhoSpin_->setToolTip(
        tr("ecutrho — the cutoff for the CHARGE DENSITY, on QE's second grid.\n\n"
           "Auto means QE's own default of 4 × ecutwfc. That is correct for "
           "NORM-CONSERVING pseudopotentials and badly under-converged for "
           "ultrasoft or PAW, whose augmentation charges are much harder than "
           "the wavefunctions and want 8–12 × ecutwfc.\n\n"
           "This is the QE parameter with no GPAW or VASP counterpart, and "
           "leaving it at the default with a USPP library is the single most "
           "common way a QE run comes out quietly wrong."));

    auto* cutoffRow = new QWidget(qeGroup_);
    auto* cutoffLayout = new QHBoxLayout(cutoffRow);
    cutoffLayout->setContentsMargins(0, 0, 0, 0);
    cutoffLayout->addWidget(new QLabel(tr("ecutwfc"), cutoffRow));
    cutoffLayout->addWidget(qeEcutwfcSpin_, 1);
    cutoffLayout->addWidget(new QLabel(tr("ecutrho"), cutoffRow));
    cutoffLayout->addWidget(qeEcutrhoSpin_, 1);
    form->addRow(tr("Cutoffs:"), cutoffRow);

    qeDualNote_ = new QLabel(qeGroup_);
    qeDualNote_->setWordWrap(true);
    qeDualNote_->setStyleSheet(QStringLiteral("color: palette(mid);"));
    form->addRow(qeDualNote_);

    qeInputDftCombo_ = new QComboBox(qeGroup_);
    qeInputDftCombo_->setEditable(true);
    qeInputDftCombo_->addItems({QStringLiteral("pbe"), QStringLiteral("pbesol"),
                                QStringLiteral("pz"), QStringLiteral("blyp"),
                                QStringLiteral("scan"), QStringLiteral("vdw-df2"),
                                QStringLiteral("hse")});
    qeInputDftCombo_->setToolTip(
        tr("input_dft — overrides the functional the pseudopotentials were "
           "generated with. Leave it matching the library unless you know why "
           "you are deviating: a PBE pseudopotential used with an LDA "
           "input_dft is not an LDA calculation, it is an inconsistent one."));
    form->addRow(tr("XC functional:"), qeInputDftCombo_);

    qeOccupationsCombo_ = new QComboBox(qeGroup_);
    // Order mirrors core::QeOccupations.
    qeOccupationsCombo_->addItems(
        {tr("smearing — metals"), tr("fixed — insulators"),
         tr("tetrahedra (Blöchl)"), tr("tetrahedra_opt")});
    qeOccupationsCombo_->setToolTip(
        tr("occupations. `fixed` is only legal for a system with a gap and a "
           "known electron count; the tetrahedron methods need no width and "
           "are the DOS-quality choice, but cannot be used for a relaxation "
           "because they give no forces."));
    form->addRow(tr("Occupations:"), qeOccupationsCombo_);

    qeSmearingCombo_ = new QComboBox(qeGroup_);
    // Order mirrors core::QeSmearing.
    qeSmearingCombo_->addItems({tr("Marzari-Vanderbilt (cold)"),
                                tr("Gaussian"), tr("Methfessel-Paxton"),
                                tr("Fermi-Dirac")});
    qeSmearingCombo_->setToolTip(
        tr("Marzari-Vanderbilt (\"cold\") is QE's recommended default: it "
           "gives a free energy close to the zero-temperature one over a wide "
           "range of widths.\n\n"
           "Fermi-Dirac is the odd one out — it is a physical electronic "
           "temperature, not a convergence aid, so use it only when you mean "
           "to model one."));
    form->addRow(tr("Smearing:"), qeSmearingCombo_);

    qeDegaussSpin_ = new QDoubleSpinBox(qeGroup_);
    qeDegaussSpin_->setRange(0.0001, 1.0);
    qeDegaussSpin_->setDecimals(4);
    qeDegaussSpin_->setSingleStep(0.005);
    qeDegaussSpin_->setValue(0.01);
    qeDegaussSpin_->setSuffix(tr(" Ry"));
    qeDegaussSpin_->setToolTip(
        tr("degauss — the smearing width, in Rydberg. 0.01–0.02 Ry is typical "
           "for a metal with cold smearing."));
    form->addRow(tr("Smearing width:"), qeDegaussSpin_);

    qeConvThrEdit_ = new QLineEdit(QStringLiteral("1e-8"), qeGroup_);
    qeConvThrEdit_->setToolTip(
        tr("conv_thr — the SCF convergence threshold in Ry. 1e-8 is a normal "
           "single-point value; a phonon or Born-charge calculation wants "
           "1e-10 or tighter, because it differentiates this quantity."));
    form->addRow(tr("SCF threshold (conv_thr):"), qeConvThrEdit_);

    for (QComboBox* combo :
         {qeInputDftCombo_, qeOccupationsCombo_, qeSmearingCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            updateEspressoRows();
            refreshPreview();
        });
    for (QDoubleSpinBox* spin : {qeEcutwfcSpin_, qeEcutrhoSpin_, qeDegaussSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) {
            updateEspressoRows();
            refreshPreview();
        });
    connect(qeConvThrEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });
    return qeGroup_;
}

void SimulationWizardBase::updateEspressoRows()
{
    if (!qeGroup_)
        return;
    auto* form = qobject_cast<QFormLayout*>(qeGroup_->layout());
    if (!form)
        return;
    const auto occupations = static_cast<core::QeOccupations>(
        qeOccupationsCombo_->currentIndex());
    const bool smears = core::qeUsesSmearing(occupations);
    // Hidden rather than disabled: `smearing` and `degauss` alongside
    // `occupations = fixed` are keys pw.x ignores, and a greyed-out control
    // reads as broken rather than as inapplicable.
    setFormRowVisible(qeGroup_, qeSmearingCombo_, smears);
    setFormRowVisible(qeGroup_, qeDegaussSpin_, smears);

    if (qeDualNote_) {
        const double wfc = qeEcutwfcSpin_->value();
        const double rho = qeEcutrhoSpin_->value();
        qeDualNote_->setText(
            rho <= 0.0
                ? tr("ecutrho defaults to %1 Ry (4 × ecutwfc) — right for "
                     "norm-conserving pseudopotentials, too soft for "
                     "ultrasoft/PAW.")
                      .arg(4.0 * wfc, 0, 'f', 1)
                : tr("Dual = %1 × ecutwfc.").arg(rho / std::max(wfc, 1e-9),
                                                 0, 'f', 1));
    }
    if (qePseudoNote_) {
        const QString path = espressoPseudoDirectory();
        qePseudoNote_->setText(
            path.isEmpty()
                ? tr("<i>Not configured.</i> Set the library in "
                     "Preferences → External Files; the generated script "
                     "writes a placeholder <tt>pseudo_dir</tt> until then.")
                : QStringLiteral("<tt>%1</tt>").arg(path.toHtmlEscaped()));
    }
}

QWidget* SimulationWizardBase::buildSiestaGroup(QWidget* parent)
{
    siestaGroup_ = new QGroupBox(tr("SIESTA settings"), parent);
    auto* form = new QFormLayout(siestaGroup_);

    // Both rows are read-only reports, not inputs. The pseudopotential library
    // and the solver binary describe the MACHINE, not this calculation, so they
    // are configured once in Preferences and every wizard reads the answer —
    // asking again per module is how two runs end up pointing at two different
    // libraries without anyone noticing.
    siestaPseudoNote_ = new QLabel(siestaGroup_);
    siestaPseudoNote_->setWordWrap(true);
    siestaPseudoNote_->setTextFormat(Qt::RichText);
    form->addRow(tr("Pseudopotentials:"), siestaPseudoNote_);

    siestaBinaryNote_ = new QLabel(siestaGroup_);
    siestaBinaryNote_->setWordWrap(true);
    siestaBinaryNote_->setTextFormat(Qt::RichText);
    form->addRow(tr("Solver binary:"), siestaBinaryNote_);

    siestaXcCombo_ = new QComboBox(siestaGroup_);
    siestaXcCombo_->setEditable(true);
    // ASE's Siesta.allowed_xc, in the order a user is likely to want them.
    siestaXcCombo_->addItems({QStringLiteral("PBE"), QStringLiteral("PBEsol"),
                              QStringLiteral("revPBE"), QStringLiteral("RPBE"),
                              QStringLiteral("BLYP"), QStringLiteral("PW91"),
                              QStringLiteral("PZ"), QStringLiteral("CA"),
                              QStringLiteral("PW92"), QStringLiteral("DRSLL"),
                              QStringLiteral("VV")});
    siestaXcCombo_->setToolTip(
        tr("SIESTA's XC.functional / XC.authors pair, which ASE resolves from "
           "this one name. PZ, CA and PW92 are the LDA parametrizations; DRSLL "
           "and VV are van der Waals functionals."));
    form->addRow(tr("XC functional:"), siestaXcCombo_);

    siestaBasisTypeCombo_ = new QComboBox(siestaGroup_);
    // Order mirrors core::SiestaBasisType.
    siestaBasisTypeCombo_->addItems(
        {tr("split — split valence (default)"), tr("splitgauss"), tr("nodes"),
         tr("nonodes"), tr("filteret")});
    siestaBasisTypeCombo_->setToolTip(
        tr("PAO.BasisType — HOW the multiple-zeta orbitals are generated. "
           "`split` is the standard scheme and what almost every published "
           "SIESTA calculation uses; the others exist for specific "
           "convergence studies."));
    form->addRow(tr("Basis type:"), siestaBasisTypeCombo_);

    siestaBasisSizeCombo_ = new QComboBox(siestaGroup_);
    siestaBasisSizeCombo_->addItems({QStringLiteral("SZ"), QStringLiteral("SZP"),
                                     QStringLiteral("DZ"), QStringLiteral("DZP"),
                                     QStringLiteral("TZP")});
    siestaBasisSizeCombo_->setCurrentText(QStringLiteral("DZP"));
    siestaBasisSizeCombo_->setToolTip(
        tr("How many orbitals per valence shell, and whether polarization "
           "orbitals are added:\n\n"
           "  SZ   single-ζ — qualitative only\n"
           "  SZP  single-ζ + polarization\n"
           "  DZ   double-ζ\n"
           "  DZP  double-ζ + polarization — the standard production basis\n"
           "  TZP  triple-ζ + polarization\n\n"
           "This is the parameter that plays the role a plane-wave cutoff "
           "plays elsewhere: it is what you converge."));
    form->addRow(tr("Basis size:"), siestaBasisSizeCombo_);

    siestaEnergyShiftSpin_ = new QDoubleSpinBox(siestaGroup_);
    siestaEnergyShiftSpin_->setRange(0.001, 5.0);
    siestaEnergyShiftSpin_->setDecimals(3);
    siestaEnergyShiftSpin_->setSingleStep(0.01);
    siestaEnergyShiftSpin_->setValue(0.27);
    siestaEnergyShiftSpin_->setSuffix(tr(" eV"));
    siestaEnergyShiftSpin_->setToolTip(
        tr("PAO.EnergyShift — the energy by which confinement raises each "
           "orbital, which is what fixes its cutoff radius.\n\n"
           "SMALLER means longer-ranged orbitals, a better basis and a more "
           "expensive run: it is the second knob to turn after the basis size. "
           "SIESTA's default is 0.02 Ry ≈ 0.27 eV; 0.001–0.01 Ry is a "
           "converged-basis regime."));
    form->addRow(tr("Energy shift:"), siestaEnergyShiftSpin_);

    siestaMeshCutoffSpin_ = new QDoubleSpinBox(siestaGroup_);
    siestaMeshCutoffSpin_->setRange(10.0, 5000.0);
    siestaMeshCutoffSpin_->setDecimals(1);
    siestaMeshCutoffSpin_->setSingleStep(25.0);
    siestaMeshCutoffSpin_->setValue(300.0);
    siestaMeshCutoffSpin_->setSuffix(tr(" eV"));
    siestaMeshCutoffSpin_->setToolTip(
        tr("MeshCutoff — the fineness of the REAL-SPACE GRID the Hartree and "
           "exchange-correlation terms are integrated on.\n\n"
           "This is not a basis-set parameter and raising it does not improve "
           "the basis. It is the closest thing SIESTA has to a plane-wave "
           "cutoff only in its units; 200–400 eV is typical, and an "
           "under-converged mesh shows up as an \"egg-box\" force error as "
           "atoms move across grid points."));
    form->addRow(tr("Mesh cutoff:"), siestaMeshCutoffSpin_);

    auto* note = new QLabel(
        tr("SIESTA has no plane-wave cutoff: its basis is a finite set of "
           "numerical atomic orbitals, converged through the basis size and "
           "the energy shift above."),
        siestaGroup_);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    form->addRow(note);

    for (QComboBox* combo :
         {siestaXcCombo_, siestaBasisTypeCombo_, siestaBasisSizeCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this] { refreshPreview(); });
    for (QDoubleSpinBox* spin :
         {siestaEnergyShiftSpin_, siestaMeshCutoffSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this](double) { refreshPreview(); });
    return siestaGroup_;
}

void SimulationWizardBase::setStructurePeriodic(bool periodic)
{
    structurePeriodic_ = periodic;
    updateXtbRows();
}

void SimulationWizardBase::updateSiestaRows()
{
    if (!siestaPseudoNote_)
        return;
    const QString path = siestaPseudoDirectory();
    siestaPseudoNote_->setText(
        path.isEmpty()
            ? tr("<i>Not configured.</i> Set the library in "
                 "Preferences → External Files. SIESTA cannot start without "
                 "one, so the generated script says so rather than guessing a "
                 "path.")
            : QStringLiteral("<tt>%1</tt>").arg(path.toHtmlEscaped()));

    if (!siestaBinaryNote_)
        return;
    const QString binary = CondaEnvs::findExecutable(
        QStringLiteral("siesta"),
        EnginePresets::envFor(core::CalculatorKind::Siesta));
    if (binary.isEmpty()) {
        siestaBinaryNote_->setText(
            tr("<i>No Conda environment provides one.</i> The run command falls "
               "back to <tt>siesta</tt> on <tt>$PATH</tt> — correct for a "
               "module-loaded or system build. Otherwise "
               "<tt>conda install -c conda-forge siesta</tt>, or set the "
               "command in Preferences → Run Commands."));
        return;
    }
    const QString env = CondaEnvs::environmentProviding(
        QStringLiteral("siesta"),
        EnginePresets::envFor(core::CalculatorKind::Siesta));
    const bool parallel =
        !CondaEnvs::executableIn(env, QStringLiteral("mpirun")).isEmpty()
        || !CondaEnvs::executableIn(env, QStringLiteral("mpiexec")).isEmpty();
    siestaBinaryNote_->setText(
        parallel
            ? tr("<tt>%1</tt><br>Launched with that environment's own MPI.")
                  .arg(binary.toHtmlEscaped())
            : tr("<tt>%1</tt><br>That environment ships no MPI launcher, so "
                 "this build is treated as <b>serial</b> and run on one core. "
                 "A <tt>nompi</tt> build started under <tt>mpirun</tt> would "
                 "run N identical copies over each other's files rather than "
                 "one parallel calculation.")
                  .arg(binary.toHtmlEscaped()));
}

QWidget* SimulationWizardBase::buildVaspGroup(QWidget* parent)
{
    // What is left in this group after the refactor: the tags that have no
    // counterpart in any other engine. Everything that DOES have one moved to
    // the shared thematic groups above, beside its equivalent —
    //
    //   XC functional  -> "Mode & Basis Set", on the plane-wave cutoff row
    //   ALGO, NELM     -> "Electronic Convergence & Smearing", as the
    //                     eigensolver and its step cap
    //   EDIFF          -> the same group's convergence-tolerance row
    //   Γ-centering    -> "Brillouin Zone & k-Points"
    //   POTCAR path    -> Preferences → External Files (see below)
    //
    // so that a user reading down the page finds each decision where the same
    // decision lives for GPAW, rather than in a VASP-shaped form of its own.
    vaspGroup_ = new QGroupBox(tr("VASP settings"), parent);
    auto* form = new QFormLayout(vaspGroup_);

    // -- PAW datasets -------------------------------------------------------
    // No path field here any more. VASP_PP_PATH is a property of the
    // INSTALLATION, not of a run: the library is licensed and unpacked once,
    // and a per-wizard copy was one more place to set it, forget it, and get a
    // plausible number out of the wrong dataset. It now has exactly one home,
    // Preferences → External Files, which is also where the Quantum ESPRESSO
    // and SIESTA libraries are set. This row only reports what is configured.
    vaspPotcarNote_ = new QLabel(vaspGroup_);
    vaspPotcarNote_->setWordWrap(true);
    vaspPotcarNote_->setTextFormat(Qt::RichText);
    form->addRow(tr("PAW datasets:"), vaspPotcarNote_);

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
    form->addRow(tr("PREC:"), vaspPrecCombo_);

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

    vaspHdf5CompressCheck_
        = new QCheckBox(tr("Compress to HDF5 (replaces CHGCAR/AECCAR)"),
                        vaspGroup_);
    vaspHdf5CompressCheck_->setToolTip(
        tr("Convert CHGCAR and any AECCAR files this run writes into "
           "Calango's compressed HDF5 container (chunked, gzip) once VASP "
           "finishes, and delete the originals — off by default, since a "
           "converted file needs an HDF5-aware reader. Calango reads its own "
           ".h5 back exactly like the CHGCAR it replaced."));
    connect(vaspHdf5CompressCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    form->addRow(QString(), vaspHdf5CompressCheck_);

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

    // Re-read the dataset path every time this group is shown rather than
    // caching it at construction: Preferences can be opened and changed while
    // the wizard is up, and a stale "not configured" here would send the user
    // looking for a field that no longer exists.
    if (vaspPotcarNote_) {
        const QString path = vaspPotcarDirectory();
        vaspPotcarNote_->setText(
            path.isEmpty()
                ? tr("<b style='color:#d9534f;'>Not configured.</b> Set "
                     "<i>VASP (VASP_PP_PATH)</i> in "
                     "<b>Preferences → External Files</b>. Without it the run "
                     "falls back to whatever VASP_PP_PATH the shell already "
                     "exports, which may be nothing at all.")
                : tr("<tt>%1</tt><br/><i>From Preferences → External "
                     "Files.</i>").arg(path.toHtmlEscaped()));
    }
}

bool SimulationWizardBase::preflightVaspPotcar()
{
    if (selectedCalculator() != core::CalculatorKind::Vasp)
        return true;
    const core::CalculatorConfig cfg = baseCalculatorConfig();
    const auto result = checkVaspPotcar(
        QString::fromStdString(cfg.vaspPotcarPath), suggestionElements());
    if (result.ok)
        return true;
    QMessageBox::warning(
        this, tr("VASP POTCAR directory"),
        tr("%1\n\nNothing was launched. This is a LOCAL check — a remote "
           "job's cluster profile may configure its own POTCAR directory "
           "(HPC panel → Scheduler → VASP POTCAR directory) that lives on "
           "a machine this check cannot see; that path is validated when "
           "the job actually runs, the same way this one was checked here.")
            .arg(result.errorMessage));
    return false;
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

QWidget* SimulationWizardBase::buildXtbGroup(QWidget* parent)
{
    xtbGroup_ = new QGroupBox(tr("xTB settings"), parent);
    auto* form = new QFormLayout(xtbGroup_);

    auto* note = new QLabel(
        tr("xTB is a <b>semi-empirical tight-binding</b> method: fast, "
           "parameterized across most of the periodic table, and right for "
           "screening and pre-relaxing molecules and molecular crystals — "
           "not a DFT replacement. Expect qualitative energetics, not "
           "benchmark accuracy."),
        xtbGroup_);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    // Whether THIS structure can be run at all. xtb-python evaluates isolated
    // systems only, and the way it declines a periodic one depends on the
    // method: GFN1/GFN2 raise, GFN-FF segfaults on a 2D cell. The generated
    // script refuses either way — this is so the refusal is not a surprise
    // after the job has been submitted.
    xtbPeriodicNote_ = new QLabel(xtbGroup_);
    xtbPeriodicNote_->setWordWrap(true);
    xtbPeriodicNote_->setTextFormat(Qt::RichText);
    form->addRow(xtbPeriodicNote_);

    xtbMethodCombo_ = new QComboBox(xtbGroup_);
    // Item text is the exact `method=` string the xtb ASE calculator takes,
    // so no mapping table can drift from the label.
    xtbMethodCombo_->addItems({QStringLiteral("GFN2-xTB"),
                               QStringLiteral("GFN1-xTB"),
                               QStringLiteral("GFN-FF")});
    xtbMethodCombo_->setToolTip(
        tr("GFN2-xTB: the current tight-binding method — multipole "
           "electrostatics and D4 dispersion built in. The default.\n"
           "GFN1-xTB: the earlier parameterization; kept for comparability "
           "with published GFN1 results.\n"
           "GFN-FF: a generic force field, not tight binding — fastest by "
           "far, no electronic structure at all."));
    form->addRow(tr("Method:"), xtbMethodCombo_);

    xtbAccuracySpin_ = new QDoubleSpinBox(xtbGroup_);
    xtbAccuracySpin_->setRange(0.0001, 1000.0);
    xtbAccuracySpin_->setDecimals(4);
    xtbAccuracySpin_->setValue(1.0);
    xtbAccuracySpin_->setToolTip(
        tr("xTB's single accuracy multiplier — LOWER is tighter. It scales "
           "the SCC convergence thresholds and integral cutoffs together; "
           "1.0 is the calibrated default, 0.01 a tight setting for "
           "frequencies."));
    form->addRow(tr("Accuracy:"), xtbAccuracySpin_);

    xtbTempSpin_ = new QDoubleSpinBox(xtbGroup_);
    xtbTempSpin_->setRange(0.0, 10000.0);
    xtbTempSpin_->setDecimals(1);
    xtbTempSpin_->setValue(300.0);
    xtbTempSpin_->setSuffix(tr(" K"));
    xtbTempSpin_->setToolTip(
        tr("Electronic temperature of the tight-binding Fermi smearing. "
           "300 K is part of the GFN parameterization, not a convergence "
           "knob — raise it only to push a stubborn SCC through a "
           "near-degenerate gap."));
    form->addRow(tr("Electronic temperature:"), xtbTempSpin_);

    xtbMaxIterSpin_ = new QSpinBox(xtbGroup_);
    xtbMaxIterSpin_->setRange(1, 10000);
    xtbMaxIterSpin_->setValue(250);
    xtbMaxIterSpin_->setToolTip(
        tr("SCC iteration cap — a runaway guard, not a target; the accuracy "
           "setting is what normally ends the cycle."));
    form->addRow(tr("Max SCC iterations:"), xtbMaxIterSpin_);

    connect(xtbMethodCombo_, &QComboBox::currentIndexChanged, this, [this] {
        updateXtbRows();
        refreshPreview();
    });
    for (QDoubleSpinBox* spin : {xtbAccuracySpin_, xtbTempSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    connect(xtbMaxIterSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    updateXtbRows();
    return xtbGroup_;
}

void SimulationWizardBase::updateXtbRows()
{
    if (!xtbGroup_ || !xtbMethodCombo_)
        return;

    // Whether this structure can be run at all, before any question of which
    // knobs apply. xtb-python evaluates isolated systems only.
    if (xtbPeriodicNote_) {
        xtbPeriodicNote_->setVisible(structurePeriodic_);
        xtbPeriodicNote_->setText(
            structurePeriodic_
                ? tr("<b style=\"color:#e06c5a\">This structure is periodic, "
                     "and xTB cannot evaluate it.</b> The in-process "
                     "xtb-python API supports isolated systems only: GFN1 and "
                     "GFN2 refuse a cell outright, and GFN-FF crashes on a 2D "
                     "one rather than refusing. The generated script stops "
                     "with that message instead of starting the "
                     "calculation.<br>Clear the periodic boundary conditions "
                     "for a molecule or cluster, or use GPAW / Quantum "
                     "ESPRESSO / SIESTA / an MLIP for a solid.")
                : QString());
    }
    // GFN-FF is a force field: no electrons, so an electronic temperature and
    // an SCC cap would be knobs that change nothing.
    const bool electronic =
        xtbMethodCombo_->currentText() != QLatin1String("GFN-FF");
    setFormRowVisible(xtbGroup_, xtbTempSpin_, electronic);
    setFormRowVisible(xtbGroup_, xtbMaxIterSpin_, electronic);
}

QWidget* SimulationWizardBase::buildDftbGroup(QWidget* parent)
{
    dftbGroup_ = new QGroupBox(tr("DFTB+ settings"), parent);
    auto* form = new QFormLayout(dftbGroup_);

    auto* note = new QLabel(
        tr("DFTB+ needs a <b>Slater-Koster parameter set</b> (mio, 3ob, … "
           "from dftb.org): the pairwise .skf tables are the "
           "parameterization, so element coverage is decided by the set, "
           "not by the code. The k-point grid comes from the shared "
           "Brillouin-zone controls above."),
        dftbGroup_);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    dftbSlakoEdit_ = new QLineEdit(dftbGroup_);
    dftbSlakoEdit_->setText(QSettings().value(kDftbSlakoKey).toString());
    dftbSlakoEdit_->setPlaceholderText(tr("/path/to/slako/mio-1-1"));
    dftbSlakoEdit_->setToolTip(
        tr("Directory holding the .skf tables. Exported as DFTB_PREFIX with "
           "a trailing slash — ASE joins '<El>-<El>.skf' onto it verbatim. "
           "Remembered across sessions: the set is installed once, like a "
           "pseudopotential library."));
    auto* browse = new QPushButton(tr("Browse…"), dftbGroup_);
    dftbSlakoRow_ = new QWidget(dftbGroup_);
    auto* slakoLayout = new QHBoxLayout(dftbSlakoRow_);
    slakoLayout->setContentsMargins(0, 0, 0, 0);
    slakoLayout->addWidget(dftbSlakoEdit_, 1);
    slakoLayout->addWidget(browse);
    form->addRow(tr("Slater-Koster directory:"), dftbSlakoRow_);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(
            this, tr("Select Slater-Koster Directory"), dftbSlakoEdit_->text());
        if (!path.isEmpty())
            dftbSlakoEdit_->setText(path); // textChanged persists + refreshes
    });
    connect(dftbSlakoEdit_, &QLineEdit::textChanged, this,
            [this](const QString& path) {
                QSettings().setValue(kDftbSlakoKey, path.trimmed());
                refreshPreview();
            });

    dftbSccCheck_ = new QCheckBox(tr("Self-consistent charges (SCC)"),
                                  dftbGroup_);
    dftbSccCheck_->setChecked(true);
    dftbSccCheck_->setToolTip(
        tr("Iterate the Mulliken charges to self-consistency (SCC-DFTB, also "
           "called DFTB2). Off is the original non-SCC method: one shot, "
           "much faster, and wrong wherever charge transfer matters."));
    form->addRow(dftbSccCheck_);

    dftbSccTolEdit_ = new QLineEdit(QStringLiteral("1e-5"), dftbGroup_);
    auto* tolValidator = new QDoubleValidator(1e-12, 1e-1, 12, dftbSccTolEdit_);
    tolValidator->setNotation(QDoubleValidator::ScientificNotation);
    tolValidator->setLocale(QLocale::c());
    dftbSccTolEdit_->setValidator(tolValidator);
    dftbSccTolEdit_->setToolTip(
        tr("SCCTolerance — the charge convergence threshold (in electrons). "
           "1e-5 is DFTB+'s own default; forces for a relaxation want the "
           "charges tight, so loosen it only for rough single points."));
    form->addRow(tr("SCC tolerance:"), dftbSccTolEdit_);

    dftbMaxSccSpin_ = new QSpinBox(dftbGroup_);
    dftbMaxSccSpin_->setRange(1, 10000);
    dftbMaxSccSpin_->setValue(100);
    dftbMaxSccSpin_->setToolTip(
        tr("MaxSCCIterations — a runaway guard; the tolerance is what "
           "normally ends the cycle."));
    form->addRow(tr("Max SCC iterations:"), dftbMaxSccSpin_);

    dftbFillingTempSpin_ = new QDoubleSpinBox(dftbGroup_);
    dftbFillingTempSpin_->setRange(0.0, 10000.0);
    dftbFillingTempSpin_->setDecimals(1);
    dftbFillingTempSpin_->setValue(0.0);
    dftbFillingTempSpin_->setSuffix(tr(" K"));
    dftbFillingTempSpin_->setSpecialValueText(tr("0 (no smearing)"));
    dftbFillingTempSpin_->setToolTip(
        tr("Fermi filling temperature. 0 keeps DFTB+'s zero-temperature "
           "occupations; a few hundred K helps a metallic or small-gap SCC "
           "converge. The script converts K to Hartree, the unit DFTB+ "
           "actually reads."));
    form->addRow(tr("Filling temperature:"), dftbFillingTempSpin_);

    connect(dftbSccCheck_, &QCheckBox::toggled, this, [this] {
        updateDftbRows();
        refreshPreview();
    });
    connect(dftbSccTolEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });
    connect(dftbMaxSccSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(dftbFillingTempSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    updateDftbRows();
    return dftbGroup_;
}

void SimulationWizardBase::updateDftbRows()
{
    if (!dftbGroup_ || !dftbSccCheck_)
        return;
    // Hidden rather than disabled, matching the QE smearing rows: a tolerance
    // for a cycle that does not run reads as broken, not as inapplicable.
    const bool scc = dftbSccCheck_->isChecked();
    setFormRowVisible(dftbGroup_, dftbSccTolEdit_, scc);
    setFormRowVisible(dftbGroup_, dftbMaxSccSpin_, scc);
}

QWidget* SimulationWizardBase::buildGromacsGroup(QWidget* parent)
{
    gromacsGroup_ = new QGroupBox(tr("GROMACS settings"), parent);
    auto* form = new QFormLayout(gromacsGroup_);

    auto* note = new QLabel(
        tr("GROMACS is an <b>engine</b>, not a force field — and the force "
           "field must be able to <i>type</i> this structure: pdb2gmx builds "
           "the topology from its residue database, so proteins, water and "
           "known ligands work, while a bare inorganic crystal has no "
           "residue entry and will not run. This targets (bio)molecular "
           "systems."),
        gromacsGroup_);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    gromacsForceFieldCombo_ = new QComboBox(gromacsGroup_);
    gromacsForceFieldCombo_->setEditable(true);
    // The names pdb2gmx -ff accepts for its bundled force fields; editable
    // because a local .ff directory is addressed the same way.
    gromacsForceFieldCombo_->addItems({QStringLiteral("oplsaa"),
                                       QStringLiteral("amber03"),
                                       QStringLiteral("amber96"),
                                       QStringLiteral("charmm27"),
                                       QStringLiteral("gromos54a7")});
    gromacsForceFieldCombo_->setToolTip(
        tr("pdb2gmx -ff. Which residues can be typed — and how well — is a "
           "property of this choice, not of GROMACS."));
    form->addRow(tr("Force field:"), gromacsForceFieldCombo_);

    gromacsWaterCombo_ = new QComboBox(gromacsGroup_);
    // The pdb2gmx -water vocabulary; "none" is legal and means no solvent
    // topology is generated.
    gromacsWaterCombo_->addItems({QStringLiteral("spc"), QStringLiteral("spce"),
                                  QStringLiteral("tip3p"),
                                  QStringLiteral("tip4p"),
                                  QStringLiteral("none")});
    gromacsWaterCombo_->setToolTip(
        tr("pdb2gmx -water — the water model the topology is built with. "
           "Match it to the force field's own validation (CHARMM was "
           "parameterized against TIP3P, GROMOS against SPC)."));
    form->addRow(tr("Water model:"), gromacsWaterCombo_);

    gromacsGmxEdit_ = new QLineEdit(QStringLiteral("gmx"), gromacsGroup_);
    gromacsGmxEdit_->setToolTip(
        tr("The gmx wrapper binary. Every GROMACS tool (pdb2gmx, grompp, "
           "mdrun, energy, traj) is a subcommand of it, so one path "
           "configures them all. Blank falls back to $ASE_GROMACS_COMMAND."));
    form->addRow(tr("gmx executable:"), gromacsGmxEdit_);

    gromacsMdpEdit_ = new QPlainTextEdit(gromacsGroup_);
    gromacsMdpEdit_->setMaximumHeight(70);
    gromacsMdpEdit_->setPlaceholderText(
        QStringLiteral("rvdw = 1.0\ncoulombtype = PME"));
    gromacsMdpEdit_->setToolTip(
        tr("Extra .mdp parameters, one `key = value` per line, applied on "
           "top of the generated defaults.\n\n"
           "The same escape hatch as VASP's extra INCAR tags: no dialog "
           "covers the full .mdp vocabulary, and anything typed here is "
           "passed through unvalidated."));
    form->addRow(tr("Extra .mdp parameters:"), gromacsMdpEdit_);

    for (QComboBox* combo : {gromacsForceFieldCombo_, gromacsWaterCombo_})
        connect(combo, &QComboBox::currentTextChanged, this,
                [this] { refreshPreview(); });
    connect(gromacsGmxEdit_, &QLineEdit::textChanged, this,
            [this] { refreshPreview(); });
    connect(gromacsMdpEdit_, &QPlainTextEdit::textChanged, this,
            [this] { refreshPreview(); });

    return gromacsGroup_;
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

    // MACE-MP-0's optional dispersion head: a constructor flag of mace_mp
    // only, so updateMaceRows() hides the row for MACE-OFF and custom models.
    maceDispersionCheck_ = new QCheckBox(tr("Dispersion"), maceGroup_);
    maceDispersionCheck_->setChecked(false);
    maceDispersionCheck_->setToolTip(
        tr("mace_mp(dispersion=True): add the D3(BJ) van der Waals correction "
           "the MACE-MP-0 foundation model ships.\n"
           "The bare model has no long-range dispersion, so layered and "
           "molecular-crystal systems come out under-bound without it. Needs "
           "the torch-dftd package in the job environment."));
    connect(maceDispersionCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    form->addRow(maceDispersionCheck_);

    // Weights file: only "Custom trained model" needs one (the foundation
    // families download and cache theirs). A dropdown over the ML models
    // directory configured in Preferences, editable so a path can still be
    // typed, with Browse… for checkpoints living elsewhere.
    maceModelFileCombo_ = new QComboBox(maceGroup_);
    maceModelFileCombo_->setEditable(true);
    maceModelFileCombo_->lineEdit()->setPlaceholderText(
        tr("path/to/weights.model or .pt"));
    for (const QString& path : SettingsManager::mlModelFiles())
        maceModelFileCombo_->addItem(QFileInfo(path).fileName(), path);
    maceModelFileCombo_->setCurrentIndex(-1);
    maceBrowseButton_ = new QPushButton(tr("Browse…"), maceGroup_);
    maceModelFileRow_ = new QWidget(maceGroup_);
    auto* pathLayout = new QHBoxLayout(maceModelFileRow_);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->addWidget(maceModelFileCombo_, 1);
    pathLayout->addWidget(maceBrowseButton_);
    form->addRow(tr("Model file:"), maceModelFileRow_);
    connect(maceBrowseButton_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select MACE Model File"),
            SettingsManager::mlPotentialsStartPath(
                maceModelFileCombo_->currentText()),
            tr("MACE models (*.model *.pt *.pth);;All files (*)"));
        if (!path.isEmpty()) {
            maceModelFileCombo_->setCurrentText(path);
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
    connect(maceModelFileCombo_, &QComboBox::editTextChanged, this,
            [this] { refreshPreview(); });

    updateMaceRows();
    return maceGroup_;
}

QString SimulationWizardBase::maceModelFilePath() const
{
    if (!maceModelFileCombo_)
        return {};
    // A picked list entry resolves to its stored absolute path; anything the
    // user typed or browsed to is taken verbatim.
    const int index = maceModelFileCombo_->currentIndex();
    if (index >= 0
        && maceModelFileCombo_->currentText()
            == maceModelFileCombo_->itemText(index))
        return maceModelFileCombo_->itemData(index).toString();
    return maceModelFileCombo_->currentText().trimmed();
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

    // No stress toggle: the old "Evaluate stress tensor" checkbox drove
    // CHGNetCalculator's stress_weight, which is the GPa -> eV/Å³ conversion
    // factor rather than an on/off switch — see chgnetStress in
    // CalculatorConfig for the full story. CHGNet always computes stress.

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
        // Either generation's inference artifact: nequip >= 0.7 compiles one
        // with `nequip-compile`, the older line deployed TorchScript with
        // `nequip-deploy build`. The generated script binds both loaders.
        mlipModelLabel_->setText(tr("Packaged model:"));
        mlipModelEdit_->setPlaceholderText(
            tr("path/to/model.nequip.pt2  (nequip-compile) or deployed .pth"));
    } else if (fairChem) {
        mlipModelLabel_->setText(tr("Checkpoint (.pt):"));
        mlipModelEdit_->setPlaceholderText(
            tr("path/to/checkpoint.pt  (must match the model type below)"));
    }
    setFormRowVisible(mlipGroup_, nequipUnitsRow_, nequip);
    setFormRowVisible(mlipGroup_, chgnetWeightsCombo_, chgnet);
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
    const auto source = static_cast<core::MaceModelSource>(
        maceModelCombo_->currentIndex());
    const bool custom = source == core::MaceModelSource::CustomFile;
    // The foundation families take a size keyword and download their own
    // weights, so they show the size row and nothing about files; a custom
    // checkpoint carries its own architecture, so the size row disappears and
    // the model-file dropdown (fed from the ML models directory in
    // Preferences) appears instead.
    setFormRowVisible(maceGroup_, maceSizeCombo_, !custom);
    setFormRowVisible(maceGroup_, maceModelFileRow_, custom);
    setFormRowVisible(maceGroup_, maceModelPathHint_, custom);
    // Dispersion is a mace_mp constructor flag — MP-0 only.
    setFormRowVisible(maceGroup_, maceDispersionCheck_,
                      source == core::MaceModelSource::FoundationMP);
    maceModelPathHint_->setText(
        maceModelFileCombo_->count() > 0
            ? tr("Required: MACECalculator loads these weights directly. The "
                 "list shows the model files in the ML potentials directory "
                 "(Preferences).")
            : tr("Required: MACECalculator loads these weights directly. Set "
                 "the ML potentials directory in Preferences to list your "
                 "models here, or Browse…"));

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
    connect(cutoffSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    // VASP's XC functional sits on this row rather than down in the VASP
    // group, beside the cutoff it is chosen with: ENCUT and the functional are
    // one decision (a meta-GGA or a hybrid wants both a harder cutoff and the
    // matching POTCAR set), and reading them a group apart is what made the
    // VASP page feel like two unrelated forms.
    //
    // Created here, where it is shown, rather than in buildVaspGroup() — a
    // widget parented to one group box and laid out in another is a thing
    // nobody finds twice. GPAW keeps its own XC combo on the row below,
    // because GPAW's list is different (it carries the vdW functionals).
    vaspXcCombo_ = new QComboBox(modeBasisGroup_);
    vaspXcCombo_->setEditable(true);
    vaspXcCombo_->addItems({QStringLiteral("PBE"), QStringLiteral("PBEsol"),
                            QStringLiteral("RPBE"), QStringLiteral("LDA"),
                            QStringLiteral("SCAN"), QStringLiteral("r2SCAN"),
                            QStringLiteral("HSE06")});
    vaspXcCombo_->setToolTip(
        tr("ASE's `xc`, which expands to the matching GGA / METAGGA tag plus "
           "its recommended defaults. The meta-GGAs and HSE06 need the right "
           "POTCAR set and cost far more than the GGAs."));
    vaspXcLabel_ = new QLabel(tr("XC functional"), modeBasisGroup_);

    cutoffRow_ = new QWidget(modeBasisGroup_);
    auto* cutoffLayout = new QHBoxLayout(cutoffRow_);
    cutoffLayout->setContentsMargins(0, 0, 0, 0);
    cutoffLayout->addWidget(cutoffSpin_);
    cutoffLayout->addWidget(vaspXcLabel_);
    cutoffLayout->addWidget(vaspXcCombo_, 1);
    cutoffLayout->addStretch(1);
    modeForm->addRow(tr("Plane-wave cutoff:"), cutoffRow_);

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
                            QStringLiteral("r2SCAN"),
                            QStringLiteral("vdW-DF"), QStringLiteral("vdW-DF2"),
                            QStringLiteral("vdW-DF-cx"),
                            QStringLiteral("optPBE-vdW"),
                            QStringLiteral("optB88-vdW"),
                            QStringLiteral("BEEF-vdW"), QStringLiteral("VV10"),
                            QStringLiteral("rVV10")});
    gpawXcCombo_->setToolTip(
        tr("The hybrids (HSE06, B3LYP) and meta-GGAs (SCAN, r2SCAN) need a "
           "GPAW build with libxc, and are far more expensive than the GGAs.\n"
           "The van der Waals functionals (vdW-DF family, VV10, rVV10) carry "
           "their own non-local correlation — no D4 correction needed — and "
           "require GPAW compiled with libvdwxc."));
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
        tr("Couple the calculator with DFTD4 through ASE's SumCalculator, "
           "adding Grimme's D4 dispersion energy and forces.\n"
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
    // Hidden once at build time when the wizard's sweep stage owns the mesh
    // (K-points Convergence): unlike the cutoff row this one is never
    // re-toggled per engine — bzGroup_ visibility handles that wholesale — so
    // there is no dynamic site to gate.
    if (!showsKpointGridRow()) {
        int row = -1;
        QFormLayout::ItemRole role{};
        bzForm->getLayoutPosition(kptRow, &row, &role);
        if (row >= 0)
            bzForm->setRowVisible(row, false);
    }

    // Γ-centered mesh. Offered for every plane-wave DFT engine, not just GPAW:
    // an even-numbered Monkhorst-Pack mesh misses Γ whoever computes it, and a
    // hexagonal cell wants the offset grid to keep the mesh on the symmetry
    // the lattice actually has. GPAW writes it as
    // kpts={'size': …, 'gamma': True}, VASP as a Gamma-centered KPOINTS block.
    gpawGammaCheck_ = new QCheckBox(tr("Gamma-centered Grid"), bzGroup_);
    gpawGammaCheck_->setToolTip(
        tr("Shift the Monkhorst-Pack mesh so it includes the Γ point.\n\n"
           "Worth setting for a hexagonal or trigonal cell, where the "
           "unshifted mesh breaks the lattice's own symmetry, and whenever a "
           "downstream step needs Γ in the set (band structures, Wannier "
           "functions, a Γ-point phonon)."));
    connect(gpawGammaCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    // "Symmetry: off" — GPAW only, and only when the wizard opts in
    // (Single-Point / Geometry Optimization): a symmetry-off run is the
    // recommended MLWF baseline.
    gpawSymmetryOffCheck_ = new QCheckBox(tr("Symmetry: off"), bzGroup_);
    gpawSymmetryOffCheck_->setToolTip(
        tr("Disable point-group symmetry reduction of the k-point set "
           "(symmetry=\"off\") — sample the full, unsymmetrized Brillouin zone "
           "(required when the wavefunctions feed a Wannier Functions run)."));
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
    // "Direct LCAO", not "Direct": it is GPAW's DirectLCAO, registered under
    // the name "lcao", and it is the ONLY solver that runs in LCAO mode. The
    // bare label read like a general-purpose exact diagonalization and invited
    // exactly the pairing that cannot work.
    addSolver(QStringLiteral("Direct LCAO"), core::GpawEigensolver::Direct);

    // Same sizing rule as the smearing combo directly above it, so the two
    // dropdowns line up instead of one stretching to the margin.
    gpawEigensolverCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    gpawEigensolverCombo_->setSizePolicy(QSizePolicy::Preferred,
                                         QSizePolicy::Fixed);
    gpawEigensolverCombo_->setToolTip(
        tr("Davidson: robust general default.\n"
           "CG: slower but very stable — try it when the SCF oscillates.\n"
           "RMM-DIIS: cheapest per step for large metallic systems.\n"
           "Direct LCAO: direct diagonalization, LCAO mode only.\n\n"
           "The choice is tied to the basis, not free: the first three "
           "iterate wavefunctions on a real-space grid or a plane-wave "
           "basis, which an LCAO calculation does not have. GPAW enforces "
           "the pairing, so in LCAO mode the solver is Direct LCAO and "
           "outside it Direct LCAO does not apply. The generated script "
           "follows the mode and says so when it does."));
    // VASP's counterpart to the eigensolver combo, shown in its place when
    // VASP is the engine. ALGO *is* VASP's eigensolver selector — the names
    // are just VASP's own, and each one names the algorithm it selects — so it
    // belongs on this row rather than buried in a separate "VASP settings"
    // group two boxes further down, which is where it used to sit.
    vaspAlgoCombo_ = new QComboBox(convGroup_);
    // Order matches core::VaspAlgo.
    vaspAlgoCombo_->addItem(tr("Normal — blocked Davidson"));
    vaspAlgoCombo_->addItem(tr("Fast — Davidson, then RMM-DIIS"));
    vaspAlgoCombo_->addItem(tr("VeryFast — RMM-DIIS"));
    vaspAlgoCombo_->addItem(tr("All — conjugate gradient (CG)"));
    vaspAlgoCombo_->addItem(tr("Damped — damped velocity friction"));
    vaspAlgoCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    vaspAlgoCombo_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    vaspAlgoCombo_->setToolTip(
        tr("ALGO — the electronic minimization algorithm.\n\n"
           "Normal (blocked Davidson): robust general default.\n"
           "Fast: Davidson then RMM-DIIS — the usual choice for large "
           "relaxations.\n"
           "VeryFast: RMM-DIIS only; fastest and least stable.\n"
           "All / Damped: for the cases where the others oscillate, and "
           "required for meta-GGA and hybrid functionals."));

    // NELM is the same quantity as GPAW's max SCF steps, but VASP's default
    // differs and the two are stored separately, so each engine brings its
    // own spin box to the same position on the row.
    vaspNelmSpin_ = new QSpinBox(convGroup_);
    vaspNelmSpin_->setRange(1, 100000);
    vaspNelmSpin_->setValue(500);
    vaspNelmSpin_->setToolTip(
        tr("NELM — maximum electronic (SCF) steps. A runaway guard: EDIFF is "
           "what normally ends the cycle."));

    // The solver and the cap on its iterations are one thought: "how the SCF
    // is solved, and how long it may try". The GPAW cap is created by the
    // subclass (GpawElectronicRows) but placed here, next to what it caps.
    eigensolverRow_ = new QWidget(convGroup_);
    auto* solverLayout = new QHBoxLayout(eigensolverRow_);
    solverLayout->setContentsMargins(0, 0, 0, 0);
    // Stretch on the trailing spacer, not on the combos — matching the
    // smearing row, which is what makes them the same width.
    solverLayout->addWidget(gpawEigensolverCombo_);
    solverLayout->addWidget(vaspAlgoCombo_);
    scfStepsLabel_ = new QLabel(tr("max SCF steps"), eigensolverRow_);
    solverLayout->addWidget(scfStepsLabel_);
    if (QWidget* steps = gpawScfStepsWidget())
        solverLayout->addWidget(steps);
    solverLayout->addWidget(vaspNelmSpin_);
    solverLayout->addStretch(1);
    convForm->addRow(tr("Eigensolver:"), eigensolverRow_);

    // EDIFF, VASP's SCF energy threshold, on the "Convergence tolerances" row
    // where GPAW's three thresholds live — same physical quantity, same place
    // on the page, whichever engine is selected.
    vaspEdiffEdit_ = new QLineEdit(QStringLiteral("1e-6"), convGroup_);
    auto* ediffValidator = new QDoubleValidator(1e-12, 1e-1, 12, vaspEdiffEdit_);
    ediffValidator->setNotation(QDoubleValidator::ScientificNotation);
    ediffValidator->setLocale(QLocale::c());
    vaspEdiffEdit_->setValidator(ediffValidator);
    vaspEdiffEdit_->setToolTip(
        tr("EDIFF — the SCF energy convergence threshold, in eV. 1e-6 for "
           "forces and relaxations; 1e-4 is only enough for a rough total "
           "energy."));
    vaspTolRow_ = new QWidget(convGroup_);
    auto* vaspTolLayout = new QHBoxLayout(vaspTolRow_);
    vaspTolLayout->setContentsMargins(0, 0, 0, 0);
    vaspTolLayout->addWidget(new QLabel(tr("EDIFF"), vaspTolRow_));
    vaspTolLayout->addWidget(vaspEdiffEdit_, 1);
    vaspTolLayout->addStretch(1);
    convForm->addRow(tr("Convergence tolerance:"), vaspTolRow_);

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

    hdf5CompressCheck_ = new QCheckBox(
        tr("Compress to HDF5 (replaces the .cube files)"), outputGroup_);
    hdf5CompressCheck_->setToolTip(
        tr("Convert every field written above into Calango's compressed HDF5 "
           "container (chunked, gzip) once the run finishes, and delete the "
           "plain .cube — off by default, since a converted file needs an "
           "HDF5-aware reader. Calango reads its own .h5 back exactly like "
           "the .cube it replaced, in the Volumetric Data dock and every "
           "isosurface/slice viewer."));
    connect(hdf5CompressCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    outLayout->addWidget(hdf5CompressCheck_);

    if (!showsGpawDensityExport()) {
        gpawDensityExportCheck_->hide();
        for (QCheckBox* field : densityFieldChecks_)
            field->hide();
        hdf5CompressCheck_->hide();
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
    // ... and hidden outright for a wizard whose sweep stage owns the cutoff
    // (Cutoff Convergence) — there the row would be a control the generated
    // script ignores.
    setRowVisible(cutoffRow_, mode == core::GpawMode::PlaneWave
                      && !inheritsCalculatorFromBaseline()
                      && showsPlaneWaveCutoffRow());
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
        // The QE and SIESTA groups belong in this list for the same reason as
        // every other engine group: a wizard that locks its engine
        // (Electronic Structure) shows none of the standard calculator
        // chrome, and a group left visible here does not merely look wrong —
        // it adds its own height to the page's minimum, which is what the
        // dialog test's laptop-height budget measures.
        if (qeGroup_) qeGroup_->setVisible(false);
        if (siestaGroup_) siestaGroup_->setVisible(false);
        if (lammpsGroup_) lammpsGroup_->setVisible(false);
        if (xtbGroup_) xtbGroup_->setVisible(false);
        if (dftbGroup_) dftbGroup_->setVisible(false);
        if (gromacsGroup_) gromacsGroup_->setVisible(false);
        extendedEngines_.hideAll();
        if (baselineInheritNote_) baselineInheritNote_->setVisible(false);
        updateCalculatorExtras(kind);
        return;
    }

    // The engines that share the standard DFT chrome (the "Mode & Basis Set"
    // and "Electronic Convergence" groups built around GPAW's controls). NOT
    // the same as "is this an ab-initio code": ABINIT, FHI-aims, OpenMX, FLEUR
    // and NWChem each keep their convergence and basis settings in their own
    // group, because what those settings ARE differs — a species-defaults tier
    // and a plane-wave cutoff are not the same kind of knob, and offering one
    // in the other's row is how SIESTA once ended up with its real-space mesh
    // labelled "plane-wave cutoff".
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
    // Both of the group's rows (the k-grid and the Γ/symmetry toggles) are
    // owned by the sweep stage in a wizard that hides the k-grid row
    // (K-points Convergence), which would leave an empty titled box here —
    // so the group hides as a whole with them.
    // Every engine that samples the Brillouin zone with the shared
    // Monkhorst-Pack row defers to these controls for its k-grid, whether or
    // not it shares any of the other DFT chrome — DFTB+ does not, and neither
    // do ABINIT, FHI-aims, OpenMX or FLEUR, but all five need a mesh. The list
    // lives in core::usesKpointGrid() so this and the generated scripts cannot
    // disagree about who gets one.
    bzGroup_->setVisible(core::usesKpointGrid(kind) && showsKpointGridRow());
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
    if (xtbGroup_) {
        const bool isXtb = kind == core::CalculatorKind::Xtb;
        xtbGroup_->setVisible(isXtb);
        if (isXtb)
            updateXtbRows();
    }
    if (dftbGroup_) {
        const bool isDftb = kind == core::CalculatorKind::DftbPlus;
        dftbGroup_->setVisible(isDftb);
        if (isDftb)
            updateDftbRows();
    }
    if (gromacsGroup_)
        gromacsGroup_->setVisible(kind == core::CalculatorKind::Gromacs);
    // ABINIT / FHI-aims / NWChem / OpenMX / FLEUR / CP2K / Amber — one group
    // each, shown for its own engine and hidden otherwise.
    extendedEngines_.updateVisibility(kind);

    const bool isVasp = kind == core::CalculatorKind::Vasp;
    const bool isEspresso = kind == core::CalculatorKind::QuantumEspresso;
    const bool isSiesta = kind == core::CalculatorKind::Siesta;
    if (qeGroup_) {
        qeGroup_->setVisible(isEspresso);
        if (isEspresso)
            updateEspressoRows();
    }
    if (siestaGroup_) {
        siestaGroup_->setVisible(isSiesta);
        if (isSiesta)
            updateSiestaRows();
    }

    // The smearing menu is engine-specific — VASP has no ISMEAR for several of
    // the schemes GPAW runs — so the rows are refiltered whenever the engine
    // changes rather than offering a choice that would be silently replaced.
    if (GpawElectronicRows* rows = electronicRows())
        rows->setCalculatorKind(kind);

    // Brillouin-zone toggles. Γ-centering is offered for every plane-wave DFT
    // engine (an even mesh misses Γ whoever computes it); "Symmetry: off" is
    // GPAW's own keyword and stays GPAW-only. The row hides as a unit when
    // neither applies — and for a wizard whose sweep stage owns the mesh
    // (K-points Convergence), where Γ-centering is defined with the rest of
    // the sweep and a second toggle here would be a control the script ignores.
    const bool showsGamma =
        core::usesPlaneWaveCutoff(kind) && showsKpointGridRow();
    setFormRowVisible(bzGroup_, gpawBzTogglesRow_, showsGamma);
    if (gpawSymmetryOffCheck_)
        gpawSymmetryOffCheck_->setVisible(isGpaw && showsGpawSymmetryToggle());

    // The XC note is gone for every engine now. It said the functional
    // "defaults to PBE in the script (editable in Stage 4)", which was true
    // only while Espresso and SIESTA had no functional control of their own.
    // Both now pick XC in their own group, so the note would contradict a
    // dropdown three rows away — worse than no note at all.
    if (dftXcNote_)
        setFormRowVisible(modeBasisGroup_, dftXcNote_, false);

    // "Mode & Basis Set" carries GPAW's mode/grid/basis and the shared
    // plane-wave cutoff row. With the cutoff row withdrawn there is nothing in
    // it but an empty titled box, so the group appears for exactly the engines
    // that take that cutoff — GPAW, VASP and ABINIT. QE's cutoff is a PAIR and
    // lives in its own group; SIESTA, FHI-aims, OpenMX and FLEUR have no
    // plane-wave basis at all.
    modeBasisGroup_->setVisible(core::usesPlaneWaveCutoff(kind));

    // Smearing lives in QE's own group (its names and its Ry width are QE's,
    // not GPAW's), so the shared convergence group is withdrawn for it too;
    // SIESTA's SCF controls are in its group for the same reason.
    if (convGroup_ && (isEspresso || isSiesta))
        convGroup_->setVisible(false);
    // VASP's XC functional shares the plane-wave cutoff row; the label and the
    // combo hide together for the other engines, leaving the cutoff alone on
    // the row it started as.
    if (vaspXcLabel_)
        vaspXcLabel_->setVisible(isVasp);
    if (vaspXcCombo_)
        vaspXcCombo_->setVisible(isVasp);
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
    setFormRowVisible(convGroup_, gpawMixerRow_, isGpaw);
    setFormRowVisible(convGroup_, gpawTolRow_, isGpaw);
    // EDIFF stands where GPAW shows its three thresholds, so exactly one of
    // the two tolerance rows is ever up.
    setFormRowVisible(convGroup_, vaspTolRow_, isVasp);

    // One "Eigensolver:" row serving both engines: GPAW's solver combo and its
    // step cap, or VASP's ALGO and NELM. Shown for either, with the other
    // engine's widgets hidden inside it.
    setFormRowVisible(convGroup_, eigensolverRow_, isGpaw || isVasp);
    if (gpawEigensolverCombo_)
        gpawEigensolverCombo_->setVisible(isGpaw);
    if (QWidget* steps = gpawScfStepsWidget())
        steps->setVisible(isGpaw);
    if (vaspAlgoCombo_)
        vaspAlgoCombo_->setVisible(isVasp);
    if (vaspNelmSpin_)
        vaspNelmSpin_->setVisible(isVasp);
    if (scfStepsLabel_)
        scfStepsLabel_->setVisible(isGpaw || isVasp);

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
        // Only the engines that genuinely expand in plane waves get this
        // row (core::usesPlaneWaveCutoff): VASP's ENCUT and ABINIT's ecut mean
        // exactly what GPAW's PW(ecut) does.
        //
        // Everyone else is withdrawn from it, and not as a cosmetic choice.
        // QE's cutoff is a PAIR (ecutwfc + ecutrho) and lives with its partner
        // in the QE group. SIESTA, FHI-aims, OpenMX and FLEUR have no
        // plane-wave basis at all, and the row used to map silently onto
        // whatever their nearest parameter was — for SIESTA, its real-space
        // mesh — so raising it to converge "the basis" refined a grid while
        // the basis stayed exactly as small.
        setFormRowVisible(modeBasisGroup_, cutoffRow_,
                          core::usesPlaneWaveCutoff(kind)
                              && showsPlaneWaveCutoffRow());
    if (inheritGpaw) {
        setFormRowVisible(modeBasisGroup_, gpawXcCombo_, false);
        setFormRowVisible(modeBasisGroup_, gpawModeCombo_, false);
    }
    if (baselineInheritNote_)
        baselineInheritNote_->setVisible(inheritGpaw);

    refreshRunCommand();
    if (calcSettingsHint_) {
        // "No additional settings" is now true only of the parameter-free
        // potentials (EMT, Lennard-Jones, ASAP): every other engine has a
        // group of its own on this page, and telling the user there is nothing
        // to configure while a group box sits directly below saying otherwise
        // is worse than saying nothing.
        const bool configurable =
            kind != core::CalculatorKind::EMT
            && kind != core::CalculatorKind::LennardJones
            && kind != core::CalculatorKind::Asap;
        calcSettingsHint_->setText(
            configurable
                ? tr("Settings for %1:").arg(calcCombo_->currentText())
                : tr("%1 has no additional settings — continue to the script "
                     "review.").arg(calcCombo_->currentText()));
    }
    // Engine decided (whether by construction or by the combo): pull any
    // per-element suggested cutoff / k-grid for it. After the row updates,
    // so a suggestion lands in controls already shaped for this engine.
    applySuggestedParameters();
    updateCalculatorExtras(kind);
    // The generated script is a function of the engine, so switching it
    // must regenerate the preview — otherwise script() serves the previous
    // engine's text until some other control happens to change. Guarded:
    // this also runs during buildUi(), before the review page exists.
    if (preview_)
        refreshPreview();
}

void SimulationWizardBase::enterOrchestrationMode()
{
    if (runLocalButton_) {
        runLocalButton_->setText(tr("Save process node"));
        runLocalButton_->setToolTip(
            tr("Commit this configuration to the orchestration node. Nothing "
               "runs now — execution happens when the pipeline is sent to "
               "processes."));
    }
    if (runRemoteButton_)
        runRemoteButton_->hide();
}

void SimulationWizardBase::setStructureElements(const QStringList& symbols)
{
    structureElements_ = symbols;
    applySuggestedParameters();
}

QStringList SimulationWizardBase::suggestionElements() const
{
    const QStringList own = calculatorElements();
    return own.isEmpty() ? structureElements_ : own;
}

void SimulationWizardBase::applySuggestedParameters()
{
    if (!cutoffSpin_ || !kptSpins_[0])
        return; // calculator page not built yet
    // Inherited calculators lock these knobs to the baseline .gpw; writing a
    // suggestion into hidden controls would silently diverge from it.
    if (inheritsCalculatorFromBaseline())
        return;
    const CalculatorParameters::Suggestion suggestion =
        CalculatorParameters::suggestionFor(selectedCalculator(),
                                            suggestionElements());
    if (suggestion.planeWaveCutoffEv)
        cutoffSpin_->setValue(*suggestion.planeWaveCutoffEv);
    if (suggestion.kpts)
        for (int axis = 0; axis < 3; ++axis)
            kptSpins_[axis]->setValue((*suggestion.kpts)[axis]);
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

    // -- Quantum ESPRESSO ---------------------------------------------------
    if (qeEcutwfcSpin_) {
        c.qeEcutwfcRy = qeEcutwfcSpin_->value();
        c.qeEcutrhoRy = qeEcutrhoSpin_->value();
        c.qeInputDft = qeInputDftCombo_->currentText().trimmed().toStdString();
        c.qeOccupations =
            static_cast<core::QeOccupations>(qeOccupationsCombo_->currentIndex());
        c.qeSmearing =
            static_cast<core::QeSmearing>(qeSmearingCombo_->currentIndex());
        c.qeDegaussRy = qeDegaussSpin_->value();
        bool parsed = false;
        const double threshold = qeConvThrEdit_->text().toDouble(&parsed);
        // A field that will not parse keeps the default rather than writing 0,
        // which pw.x reads as "converge to machine zero" and never reaches.
        c.qeConvThrRy = parsed && threshold > 0.0 ? threshold : 1.0e-8;
        c.espressoPseudoDir = espressoPseudoDirectory().toStdString();
    }

    // -- SIESTA -------------------------------------------------------------
    if (siestaBasisSizeCombo_) {
        c.siestaXc = siestaXcCombo_->currentText().trimmed().toStdString();
        c.siestaBasisType =
            static_cast<core::SiestaBasisType>(siestaBasisTypeCombo_->currentIndex());
        c.siestaBasisSize = siestaBasisSizeCombo_->currentText().toStdString();
        c.siestaEnergyShiftEv = siestaEnergyShiftSpin_->value();
        c.siestaMeshCutoffEv = siestaMeshCutoffSpin_->value();
        c.siestaPseudoDir = siestaPseudoDirectory().toStdString();
    }
    c.maceSource =
        static_cast<core::MaceModelSource>(maceModelCombo_->currentIndex());
    c.maceSize = maceSizeCombo_->currentText().toStdString();
    // Only a custom run names a checkpoint; the foundation families download
    // and cache their own weights (their model-file row is hidden).
    c.maceModelPath = c.maceSource == core::MaceModelSource::CustomFile
        ? maceModelFilePath().toStdString()
        : std::string();
    c.maceDispersion = c.maceSource == core::MaceModelSource::FoundationMP
        && maceDispersionCheck_ && maceDispersionCheck_->isChecked();
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
    // Machine-level, so it comes from Preferences rather than from this
    // wizard — the same reasoning as the pseudopotential libraries.
    c.gpawBasisDir = QSettings()
                         .value(SettingsManager::kGpawLcaoBasisDir)
                         .toString()
                         .trimmed()
                         .toStdString();
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
    c.kptsGammaCentered = gpawGammaCheck_ && gpawGammaCheck_->isChecked();
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
    // Only one of the two is ever visible (one engine active at a time), and
    // the hidden one's checked state never changed from its unchecked
    // default, so OR-ing both is exactly "whichever engine's box is showing".
    c.compressDensityToHdf5
        = (hdf5CompressCheck_ && hdf5CompressCheck_->isChecked())
        || (vaspHdf5CompressCheck_ && vaspHdf5CompressCheck_->isChecked());

    if (vaspGroup_) {
        // From Preferences, not from this page — the wizard no longer offers a
        // field for it (see vaspPotcarDirectory()).
        c.vaspPotcarPath = vaspPotcarDirectory().trimmed().toStdString();
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

    // -- xTB ----------------------------------------------------------------
    if (xtbMethodCombo_) {
        // The combo's item text IS the method string the calculator takes
        // (see buildXtbGroup), so it passes through unmapped.
        c.xtbMethod = xtbMethodCombo_->currentText().toStdString();
        c.xtbAccuracy = xtbAccuracySpin_->value();
        c.xtbElectronicTemperatureK = xtbTempSpin_->value();
        c.xtbMaxIterations = xtbMaxIterSpin_->value();
    }

    // -- DFTB+ --------------------------------------------------------------
    if (dftbSccCheck_) {
        c.dftbSlakoDir = dftbSlakoEdit_->text().trimmed().toStdString();
        c.dftbScc = dftbSccCheck_->isChecked();
        // An in-progress edit ("1e-") is not a number yet; keep the default
        // rather than writing 0, which DFTB+ reads as "converge the charges
        // to machine zero" and never reaches.
        bool sccOk = false;
        const double sccTol =
            QLocale::c().toDouble(dftbSccTolEdit_->text(), &sccOk);
        if (sccOk && sccTol > 0.0)
            c.dftbSccTolerance = sccTol;
        c.dftbMaxSccIterations = dftbMaxSccSpin_->value();
        c.dftbFillingTemperatureK = dftbFillingTempSpin_->value();
    }

    // -- GROMACS ------------------------------------------------------------
    if (gromacsForceFieldCombo_) {
        c.gromacsForceField =
            gromacsForceFieldCombo_->currentText().trimmed().toStdString();
        c.gromacsWaterModel =
            gromacsWaterCombo_->currentText().trimmed().toStdString();
        c.gromacsExecutable = gromacsGmxEdit_->text().trimmed().toStdString();
        c.gromacsExtraMdp =
            gromacsMdpEdit_->toPlainText().trimmed().toStdString();
    }

    // -- ABINIT / FHI-aims / NWChem / OpenMX / FLEUR / CP2K / Amber ---------
    extendedEngines_.applyTo(c);
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
    // The review page — and with it `preview_` — is built LAST, after every
    // settings page. Any control created before then that refreshes on
    // construction (a setChecked(), a setValue(), a row appended to a table)
    // arrives here with `preview_` still null, and every wizard is free to add
    // one. This crashed the Electronic Structure wizard the moment its fatband
    // table seeded its default rows.
    //
    // Returning early is not merely a null guard: the subclass whose
    // generateScript() we would call is itself mid-construction, so the script
    // it produced would be built from half-initialized controls. The page is
    // regenerated on entry anyway, so nothing is lost by skipping it here.
    if (!preview_)
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
    // One file, nothing beside it: the script carries its own logging block,
    // so what lands on disk here is what runs on a cluster.
    QString error;
    if (!writeScript(path, script(), &error))
        QMessageBox::warning(this, tr("Export Script"), error);
}

QString SimulationWizardBase::script() const
{
    return preview_->toPlainText();
}

QString SimulationWizardBase::vaspPotcarDirectory()
{
    // Preferences → External Files. Falls back to the key the wizard's own
    // POTCAR field used to write, so a user upgrading from a release that had
    // that field does not silently lose the path they already set.
    const QString configured =
        QSettings().value(SettingsManager::kPseudopotentialsVasp).toString().trimmed();
    return configured.isEmpty()
        ? QSettings().value(kLegacyVaspPotcarKey).toString().trimmed()
        : configured;
}

QString SimulationWizardBase::espressoPseudoDirectory()
{
    return QSettings()
        .value(SettingsManager::kPseudopotentialsEspresso)
        .toString()
        .trimmed();
}

QString SimulationWizardBase::siestaPseudoDirectory()
{
    return QSettings()
        .value(SettingsManager::kPseudopotentialsSiesta)
        .toString()
        .trimmed();
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
        env = QSettings().value(SettingsManager::kEnvironmentPath).toString();
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
    // Recorded so a downstream wizard can state the band count without the
    // engine log. Only when it was set explicitly: GPAW's own default is a
    // function of the system, so writing a 0 here would be a claim, not a fact.
    if (!c.gpawNbands.empty()) {
        bool ok = false;
        const int n = QString::fromStdString(c.gpawNbands).toInt(&ok);
        if (ok && n > 0)
            o.insert(QStringLiteral("nbands"), n);
    }
    o.insert(QStringLiteral("python"), pythonExecutable());
    o.insert(QStringLiteral("conda_env"),
             EnginePresets::envFor(c.calculator));
    // Read back by MainWindow::compressDensityFilesIfRequested() once the
    // job finishes — the ONLY thing tying a completed job directory back to
    // this run's CalculatorConfig, since nothing else survives past launch.
    o.insert(QStringLiteral("compress_density_hdf5"), c.compressDensityToHdf5);
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

std::optional<SimulationWizardBase::InheritedCalculator>
SimulationWizardBase::applyBaselineProvenance(const QComboBox* baselineCombo,
                                              QLabel* note)
{
    const QString gpw =
        baselineCombo ? baselineCombo->currentData().toString() : QString();
    // The combo holds the .gpw; the provenance sidecar sits in its job dir.
    const QString dir =
        gpw.isEmpty() ? QString() : QFileInfo(gpw).absolutePath();
    const auto inherited =
        dir.isEmpty() ? std::nullopt : readCalculatorProvenance(dir);

    if (note) {
        if (inherited) {
            QString text = tr("Inherited: %1")
                               .arg(inherited->summary().toHtmlEscaped());
            if (!inherited->condaEnv.isEmpty())
                text += tr(" — env <code>%1</code>")
                            .arg(inherited->condaEnv.toHtmlEscaped());
            note->setText(text);
        } else if (gpw.isEmpty()) {
            note->clear();
        } else {
            note->setText(
                tr("This baseline carries no <code>calculator.json</code>, so "
                   "its parameters cannot be shown. GPAW still restores them "
                   "from the <code>.gpw</code> at run time."));
        }
    }
    return inherited;
}

QString SimulationWizardBase::constraintSummaryText(
    const std::vector<core::GeometryConstraint>& constraints,
    const QString& emptyText)
{
    if (constraints.empty())
        return emptyText;
    int fixedAtoms = 0;
    int regions = 0;
    for (const core::GeometryConstraint& rule : constraints) {
        if (rule.selection == core::GeometryConstraint::Selection::Region)
            ++regions;
        else
            fixedAtoms += static_cast<int>(rule.indices.size());
    }
    QStringList parts;
    if (fixedAtoms > 0)
        parts << tr("%n atom(s)", nullptr, fixedAtoms);
    if (regions > 0)
        parts << tr("%n region(s)", nullptr, regions);
    return tr("Constrained: %1.").arg(parts.join(tr(", ")));
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
