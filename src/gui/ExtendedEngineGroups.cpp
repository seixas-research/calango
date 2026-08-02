#include "gui/ExtendedEngineGroups.hpp"

#include "gui/GuiUtils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include <utility>

namespace calango::gui {

namespace {

// Per-installation directories, remembered across sessions exactly as the
// DFTB+ Slater-Koster directory is: a pseudopotential library or a species
// database is installed once per machine, not chosen per run.
constexpr auto kAbinitPseudoKey = "engines/abinitPseudoDir";
constexpr auto kAimsSpeciesKey = "engines/aimsSpeciesDir";
constexpr auto kOpenMxDataKey = "engines/openmxDataPath";
constexpr auto kFleurRootKey = "engines/fleurRoot";

/// A line edit carrying a scientific-notation tolerance. A QDoubleSpinBox
/// cannot show 1e-6 as anything but "0.000001000000", which is the same reason
/// the GPAW convergence thresholds are line edits.
QLineEdit* toleranceEdit(QWidget* parent, const QString& initial)
{
    auto* edit = new QLineEdit(initial, parent);
    auto* validator = new QDoubleValidator(1e-14, 1e-1, 14, edit);
    validator->setNotation(QDoubleValidator::ScientificNotation);
    validator->setLocale(QLocale::c());
    edit->setValidator(validator);
    return edit;
}

/// Read a tolerance back, keeping `fallback` for a value that will not parse.
/// An in-progress edit ("1e-") is not a number yet, and writing 0 into a
/// convergence threshold means "converge to machine zero", which never happens.
double toleranceValue(const QLineEdit* edit, double fallback)
{
    bool ok = false;
    const double value = QLocale::c().toDouble(edit->text(), &ok);
    return ok && value > 0.0 ? value : fallback;
}

} // namespace

ExtendedEngineGroups::ExtendedEngineGroups(QObject* parent)
    : QObject(parent)
{
}

QLineEdit* ExtendedEngineGroups::directoryRow(QWidget* parent, QFormLayout* form,
                                              const QString& label,
                                              const QString& settingsKey,
                                              const QString& placeholder,
                                              const QString& tooltip)
{
    auto* edit = new QLineEdit(parent);
    edit->setText(QSettings().value(settingsKey).toString());
    edit->setPlaceholderText(placeholder);
    edit->setToolTip(tooltip);

    auto* browse = new QPushButton(tr("Browse…"), parent);
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    form->addRow(label, row);

    connect(browse, &QPushButton::clicked, this, [edit, label] {
        const QString path = QFileDialog::getExistingDirectory(
            edit->window(), tr("Select %1").arg(label), edit->text());
        if (!path.isEmpty())
            edit->setText(path); // textChanged persists and refreshes
    });
    connect(edit, &QLineEdit::textChanged, this,
            [this, settingsKey](const QString& path) {
                QSettings().setValue(settingsKey, path.trimmed());
                if (onChanged_)
                    onChanged_();
            });
    return edit;
}

void ExtendedEngineGroups::build(QWidget* parent, QVBoxLayout* layout,
                                 std::function<void()> onChanged)
{
    onChanged_ = std::move(onChanged);
    buildAbinit(parent, layout);
    buildAims(parent, layout);
    buildNwChem(parent, layout);
    buildOpenMx(parent, layout);
    buildFleur(parent, layout);
    buildCp2k(parent, layout);
    buildAmber(parent, layout);
    hideAll();
}

// ---------------------------------------------------------------------------
// ABINIT
// ---------------------------------------------------------------------------
void ExtendedEngineGroups::buildAbinit(QWidget* parent, QVBoxLayout* layout)
{
    abinitGroup_ = new QGroupBox(tr("ABINIT settings"), parent);
    auto* form = new QFormLayout(abinitGroup_);

    auto* note = new QLabel(
        tr("ABINIT is a <b>plane-wave / PAW</b> code, so it reads the shared "
           "plane-wave cutoff and k-point grid above. What it needs on top of "
           "them is a <b>pseudopotential family</b> — the tables it looks each "
           "element up in."),
        abinitGroup_);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    abinitXcCombo_ = new QComboBox(abinitGroup_);
    abinitXcCombo_->setEditable(true);
    abinitXcCombo_->addItems({QStringLiteral("PBE"), QStringLiteral("LDA"),
                              QStringLiteral("PBEsol"), QStringLiteral("PW91"),
                              QStringLiteral("HSE03")});
    form->addRow(tr("XC functional:"), abinitXcCombo_);

    abinitPpsCombo_ = new QComboBox(abinitGroup_);
    abinitPpsCombo_->setEditable(true);
    abinitPpsCombo_->addItems({QStringLiteral("fhi"), QStringLiteral("paw"),
                               QStringLiteral("jth"), QStringLiteral("pot"),
                               QStringLiteral("hgh"), QStringLiteral("hgh.k"),
                               QStringLiteral("tm")});
    abinitPpsCombo_->setToolTip(
        tr("Which FAMILY of pseudopotential tables the run reads:\n\n"
           "fhi — norm-conserving Fritz-Haber\n"
           "paw / jth — PAW datasets (the JTH table is the usual one)\n"
           "pot — Teter\n"
           "hgh / hgh.k — Hartwigsen-Goedecker-Hutter\n"
           "tm — Troullier-Martins\n\n"
           "Not a quality dial with a safe default: which values work at all "
           "is decided by what is installed below, and a family whose files "
           "are missing fails at the first element rather than at the end of "
           "the SCF."));
    form->addRow(tr("Pseudopotential family:"), abinitPpsCombo_);

    abinitPseudoEdit_ = directoryRow(
        abinitGroup_, form, tr("Pseudopotential directory:"), kAbinitPseudoKey,
        tr("/path/to/abinit/pseudos"),
        tr("Directory holding the tables (ASE's `pp_paths`). Per-installation "
           "state, remembered across sessions like a POTCAR root. Empty lets "
           "ASE consult its own ~/.config/ase/config.ini instead."));

    abinitToldfeEdit_ = toleranceEdit(abinitGroup_, QStringLiteral("1e-6"));
    abinitToldfeEdit_->setToolTip(
        tr("toldfe — the SCF total-energy convergence tolerance, in HARTREE "
           "(ABINIT's own unit, unlike the eV this dialog uses elsewhere)."));
    form->addRow(tr("SCF tolerance (toldfe):"), abinitToldfeEdit_);

    abinitNstepSpin_ = new QSpinBox(abinitGroup_);
    abinitNstepSpin_->setRange(1, 10000);
    abinitNstepSpin_->setValue(100);
    abinitNstepSpin_->setToolTip(
        tr("nstep — a runaway guard on the SCF; the tolerance is what "
           "normally ends the cycle."));
    form->addRow(tr("Max SCF steps (nstep):"), abinitNstepSpin_);

    abinitExtraEdit_ = new QPlainTextEdit(abinitGroup_);
    abinitExtraEdit_->setMaximumHeight(70);
    abinitExtraEdit_->setPlaceholderText(
        QStringLiteral("diemac 12.0\nnbdbuf 4"));
    abinitExtraEdit_->setToolTip(
        tr("Extra ABINIT input variables, one `name value` per line, passed "
           "through verbatim.\n\n"
           "ABINIT has several hundred; this is the escape hatch that stops "
           "this page from being a ceiling."));
    form->addRow(tr("Extra input variables:"), abinitExtraEdit_);

    for (QComboBox* combo : {abinitXcCombo_, abinitPpsCombo_})
        connect(combo, &QComboBox::currentTextChanged, this,
                [this] { if (onChanged_) onChanged_(); });
    connect(abinitToldfeEdit_, &QLineEdit::textChanged, this,
            [this] { if (onChanged_) onChanged_(); });
    connect(abinitNstepSpin_, &QSpinBox::valueChanged, this,
            [this] { if (onChanged_) onChanged_(); });
    connect(abinitExtraEdit_, &QPlainTextEdit::textChanged, this,
            [this] { if (onChanged_) onChanged_(); });

    layout->addWidget(abinitGroup_);
}

// ---------------------------------------------------------------------------
// FHI-aims
// ---------------------------------------------------------------------------
void ExtendedEngineGroups::buildAims(QWidget* parent, QVBoxLayout* layout)
{
    aimsGroup_ = new QGroupBox(tr("FHI-aims settings"), parent);
    auto* form = new QFormLayout(aimsGroup_);

    auto* note = new QLabel(
        tr("FHI-aims is <b>all-electron</b>, in numeric atom-centred orbitals: "
           "there is <b>no plane-wave cutoff</b>, and none is offered above. "
           "The basis is the <b>species-defaults tier</b> — light / "
           "intermediate / tight / really_tight are pre-tabulated, "
           "hierarchical basis + integration-grid + accuracy sets, and moving "
           "up a tier is how an aims calculation is converged."),
        aimsGroup_);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    aimsXcCombo_ = new QComboBox(aimsGroup_);
    aimsXcCombo_->setEditable(true);
    aimsXcCombo_->addItems({QStringLiteral("pbe"), QStringLiteral("pbe0"),
                            QStringLiteral("pbesol"), QStringLiteral("hse06"),
                            QStringLiteral("pw-lda"), QStringLiteral("scan")});
    form->addRow(tr("XC functional:"), aimsXcCombo_);

    aimsSpeciesEdit_ = directoryRow(
        aimsGroup_, form, tr("Species defaults:"), kAimsSpeciesKey,
        tr("/path/to/aims/species_defaults/defaults_2020"),
        tr("The species_defaults directory shipped with FHI-aims. The tier "
           "below is a SUBFOLDER of it — the script joins the two — so point "
           "this at the parent, not at `light` itself."));

    aimsTierCombo_ = new QComboBox(aimsGroup_);
    aimsTierCombo_->addItems({QStringLiteral("light"),
                              QStringLiteral("intermediate"),
                              QStringLiteral("tight"),
                              QStringLiteral("really_tight")});
    aimsTierCombo_->setToolTip(
        tr("The basis / grid tier. `light` is the production default for "
           "geometries and is already better than a typical DFT plane-wave "
           "setup; `tight` is what a published energy or a barrier wants. "
           "`really_tight` is a convergence check, not a production setting."));
    form->addRow(tr("Species tier:"), aimsTierCombo_);

    aimsRelativisticCombo_ = new QComboBox(aimsGroup_);
    aimsRelativisticCombo_->addItems({QStringLiteral("atomic_zora scalar"),
                                      QStringLiteral("none")});
    aimsRelativisticCombo_->setToolTip(
        tr("Relativistic treatment. `atomic_zora scalar` is the standard "
           "choice and is REQUIRED past the first rows of the periodic "
           "table — a non-relativistic all-electron run on a 5d element is "
           "not merely less accurate, it is wrong. `none` exists for "
           "light-element benchmarks."));
    form->addRow(tr("Relativistic:"), aimsRelativisticCombo_);

    aimsAccuracyEdit_ = toleranceEdit(aimsGroup_, QStringLiteral("1e-6"));
    aimsAccuracyEdit_->setToolTip(
        tr("sc_accuracy_etot — the SCF total-energy convergence criterion, in "
           "eV."));
    form->addRow(tr("SCF energy accuracy:"), aimsAccuracyEdit_);

    aimsExtraEdit_ = new QPlainTextEdit(aimsGroup_);
    aimsExtraEdit_->setMaximumHeight(70);
    aimsExtraEdit_->setPlaceholderText(
        QStringLiteral("charge_mix_param 0.1\nsc_iter_limit 200"));
    aimsExtraEdit_->setToolTip(
        tr("Extra control.in keywords, one `keyword value` per line."));
    form->addRow(tr("Extra control.in keywords:"), aimsExtraEdit_);

    for (QComboBox* combo :
         {aimsXcCombo_, aimsTierCombo_, aimsRelativisticCombo_})
        connect(combo, &QComboBox::currentTextChanged, this,
                [this] { if (onChanged_) onChanged_(); });
    connect(aimsAccuracyEdit_, &QLineEdit::textChanged, this,
            [this] { if (onChanged_) onChanged_(); });
    connect(aimsExtraEdit_, &QPlainTextEdit::textChanged, this,
            [this] { if (onChanged_) onChanged_(); });

    layout->addWidget(aimsGroup_);
}

// ---------------------------------------------------------------------------
// NWChem
// ---------------------------------------------------------------------------
void ExtendedEngineGroups::buildNwChem(QWidget* parent, QVBoxLayout* layout)
{
    nwchemGroup_ = new QGroupBox(tr("NWChem settings"), parent);
    auto* form = new QFormLayout(nwchemGroup_);

    auto* note = new QLabel(
        tr("NWChem is <b>two codes in one binary</b>, and the theory below "
           "picks which. <b>dft / scf / mp2 / ccsd / tce</b> are Gaussian-basis "
           "<i>molecular</i> methods that ignore the unit cell; "
           "<b>pspw / band / paw</b> are the plane-wave <i>periodic</i> DFT "
           "modules, which ignore the basis set instead."),
        nwchemGroup_);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    nwchemTheoryCombo_ = new QComboBox(nwchemGroup_);
    nwchemTheoryCombo_->addItem(tr("dft — Gaussian-basis DFT (molecular)"),
                                QStringLiteral("dft"));
    nwchemTheoryCombo_->addItem(tr("scf — Hartree-Fock (molecular)"),
                                QStringLiteral("scf"));
    nwchemTheoryCombo_->addItem(tr("mp2 — MP2 (molecular)"),
                                QStringLiteral("mp2"));
    nwchemTheoryCombo_->addItem(tr("ccsd — coupled cluster (molecular)"),
                                QStringLiteral("ccsd"));
    nwchemTheoryCombo_->addItem(tr("tce — tensor contraction engine"),
                                QStringLiteral("tce"));
    nwchemTheoryCombo_->addItem(tr("pspw — plane-wave DFT (periodic, Γ)"),
                                QStringLiteral("pspw"));
    nwchemTheoryCombo_->addItem(tr("band — plane-wave DFT (periodic, k-points)"),
                                QStringLiteral("band"));
    nwchemTheoryCombo_->addItem(tr("paw — plane-wave PAW (periodic)"),
                                QStringLiteral("paw"));
    form->addRow(tr("Theory:"), nwchemTheoryCombo_);

    nwchemPeriodicNote_ = new QLabel(nwchemGroup_);
    nwchemPeriodicNote_->setWordWrap(true);
    nwchemPeriodicNote_->setStyleSheet(QStringLiteral("color: palette(mid);"));
    form->addRow(nwchemPeriodicNote_);

    nwchemXcCombo_ = new QComboBox(nwchemGroup_);
    nwchemXcCombo_->setEditable(true);
    nwchemXcCombo_->addItems({QStringLiteral("b3lyp"), QStringLiteral("pbe0"),
                              QStringLiteral("pbe96"), QStringLiteral("m06-2x"),
                              QStringLiteral("blyp")});
    form->addRow(tr("XC functional:"), nwchemXcCombo_);

    nwchemBasisCombo_ = new QComboBox(nwchemGroup_);
    nwchemBasisCombo_->setEditable(true);
    nwchemBasisCombo_->addItems({QStringLiteral("6-31G*"),
                                 QStringLiteral("6-311++G**"),
                                 QStringLiteral("cc-pVDZ"),
                                 QStringLiteral("cc-pVTZ"),
                                 QStringLiteral("def2-SVP"),
                                 QStringLiteral("def2-TZVP")});
    nwchemBasisCombo_->setToolTip(
        tr("The Gaussian basis set. Read only by the MOLECULAR modules — the "
           "plane-wave ones build their own basis from the cutoff."));
    form->addRow(tr("Basis set:"), nwchemBasisCombo_);

    nwchemMemoryEdit_ = new QLineEdit(QStringLiteral("2000 mb"), nwchemGroup_);
    nwchemMemoryEdit_->setToolTip(
        tr("NWChem's `memory` directive, per process. A correlated method "
           "(mp2 / ccsd / tce) that runs out of it fails partway through "
           "rather than degrading, so it is worth setting deliberately."));
    form->addRow(tr("Memory:"), nwchemMemoryEdit_);

    nwchemExtraEdit_ = new QPlainTextEdit(nwchemGroup_);
    nwchemExtraEdit_->setMaximumHeight(70);
    nwchemExtraEdit_->setPlaceholderText(
        QStringLiteral("maxiter 200\ntolerances tight"));
    form->addRow(tr("Extra directives:"), nwchemExtraEdit_);

    connect(nwchemTheoryCombo_, &QComboBox::currentIndexChanged, this, [this] {
        updateNwChemRows();
        if (onChanged_)
            onChanged_();
    });
    for (QComboBox* combo : {nwchemXcCombo_, nwchemBasisCombo_})
        connect(combo, &QComboBox::currentTextChanged, this,
                [this] { if (onChanged_) onChanged_(); });
    connect(nwchemMemoryEdit_, &QLineEdit::textChanged, this,
            [this] { if (onChanged_) onChanged_(); });
    connect(nwchemExtraEdit_, &QPlainTextEdit::textChanged, this,
            [this] { if (onChanged_) onChanged_(); });

    updateNwChemRows();
    layout->addWidget(nwchemGroup_);
}

void ExtendedEngineGroups::updateNwChemRows()
{
    if (!nwchemGroup_ || !nwchemTheoryCombo_)
        return;
    const QString theory = nwchemTheoryCombo_->currentData().toString();
    const bool planeWave = theory == QLatin1String("pspw")
        || theory == QLatin1String("band") || theory == QLatin1String("paw");
    // Hidden rather than disabled, matching the QE smearing rows: a basis set
    // for a module that does not read one looks broken, not inapplicable.
    setFormRowVisible(nwchemGroup_, nwchemBasisCombo_, !planeWave);
    setFormRowVisible(nwchemGroup_, nwchemXcCombo_,
                      theory == QLatin1String("dft") || planeWave);
    nwchemPeriodicNote_->setText(
        planeWave
            ? tr("Periodic module: the unit cell and the shared k-point grid "
                 "are used, and the Gaussian basis set is not read.")
            : tr("Molecular module: the unit cell is IGNORED. Running this on "
                 "a crystal completes and reports the energy of an isolated "
                 "cluster — pick pspw / band / paw for a periodic system."));
}

// ---------------------------------------------------------------------------
// OpenMX
// ---------------------------------------------------------------------------
void ExtendedEngineGroups::buildOpenMx(QWidget* parent, QVBoxLayout* layout)
{
    openmxGroup_ = new QGroupBox(tr("OpenMX settings"), parent);
    auto* form = new QFormLayout(openmxGroup_);

    auto* note = new QLabel(
        tr("OpenMX uses <b>pseudo-atomic orbitals</b>, so — like SIESTA — it "
           "has <b>no plane-wave basis cutoff</b>. The energy cutoff below is "
           "<code>scf.energycutoff</code>: the <i>real-space grid</i> the "
           "Hartree and exchange-correlation terms are integrated on. Raising "
           "it refines that grid and does not enlarge the basis — the basis is "
           "the PAO set chosen per element from the data path."),
        openmxGroup_);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    openmxXcCombo_ = new QComboBox(openmxGroup_);
    openmxXcCombo_->addItems({QStringLiteral("GGA-PBE"), QStringLiteral("LDA"),
                              QStringLiteral("LSDA-CA"),
                              QStringLiteral("LSDA-PW")});
    form->addRow(tr("XC functional:"), openmxXcCombo_);

    openmxDataEdit_ = directoryRow(
        openmxGroup_, form, tr("DFT data path:"), kOpenMxDataKey,
        tr("/path/to/openmx/DFT_DATA19"),
        tr("The DFT_DATA directory shipped with OpenMX, holding the VPS "
           "pseudopotentials and the PAO basis databases. This is where the "
           "BASIS comes from, so it is not optional."));

    openmxCutoffSpin_ = new QDoubleSpinBox(openmxGroup_);
    openmxCutoffSpin_->setRange(100.0, 20000.0);
    openmxCutoffSpin_->setDecimals(0);
    openmxCutoffSpin_->setSingleStep(100.0);
    openmxCutoffSpin_->setValue(2721.0); // ~200 Ry
    openmxCutoffSpin_->setSuffix(tr(" eV"));
    openmxCutoffSpin_->setToolTip(
        tr("scf.energycutoff — the REAL-SPACE integration grid, not a basis "
           "cutoff. 2721 eV is about 200 Ry, OpenMX's usual production value; "
           "the script converts to the Ry OpenMX actually reads."));
    form->addRow(tr("Grid energy cutoff:"), openmxCutoffSpin_);

    openmxCriterionSpin_ = new QDoubleSpinBox(openmxGroup_);
    openmxCriterionSpin_->setRange(1e-8, 1.0);
    openmxCriterionSpin_->setDecimals(8);
    openmxCriterionSpin_->setValue(1.0e-4);
    openmxCriterionSpin_->setSuffix(tr(" eV"));
    form->addRow(tr("SCF criterion:"), openmxCriterionSpin_);

    openmxMaxIterSpin_ = new QSpinBox(openmxGroup_);
    openmxMaxIterSpin_->setRange(1, 10000);
    openmxMaxIterSpin_->setValue(100);
    form->addRow(tr("Max SCF iterations:"), openmxMaxIterSpin_);

    openmxSolverCombo_ = new QComboBox(openmxGroup_);
    openmxSolverCombo_->addItem(tr("Band — periodic crystal"),
                                QStringLiteral("Band"));
    openmxSolverCombo_->addItem(tr("Cluster — isolated molecule"),
                                QStringLiteral("Cluster"));
    openmxSolverCombo_->addItem(tr("DC — O(N) divide-conquer (large cells)"),
                                QStringLiteral("DC"));
    openmxSolverCombo_->setToolTip(
        tr("scf.EigenvalueSolver. `Band` diagonalizes at each k-point and is "
           "what a crystal needs; `Cluster` is the Γ-only molecular solver; "
           "`DC` is the linear-scaling method for cells too large to "
           "diagonalize, and is approximate."));
    form->addRow(tr("Eigenvalue solver:"), openmxSolverCombo_);

    for (QComboBox* combo : {openmxXcCombo_, openmxSolverCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this] { if (onChanged_) onChanged_(); });
    for (QDoubleSpinBox* spin : {openmxCutoffSpin_, openmxCriterionSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { if (onChanged_) onChanged_(); });
    connect(openmxMaxIterSpin_, &QSpinBox::valueChanged, this,
            [this] { if (onChanged_) onChanged_(); });

    layout->addWidget(openmxGroup_);
}

// ---------------------------------------------------------------------------
// FLEUR
// ---------------------------------------------------------------------------
void ExtendedEngineGroups::buildFleur(QWidget* parent, QVBoxLayout* layout)
{
    fleurGroup_ = new QGroupBox(tr("FLEUR settings"), parent);
    auto* form = new QFormLayout(fleurGroup_);

    auto* note = new QLabel(
        tr("FLEUR is a <b>full-potential LAPW</b> all-electron code. Its ASE "
           "support lives in the separate <code>ase-fleur</code> package "
           "(<code>pip install ase-fleur</code>) — ASE's own "
           "<code>ase.calculators.fleur</code> is a stub that raises and "
           "points there. Install it in the environment mapped to FLEUR under "
           "Preferences → Python &amp; Environments."),
        fleurGroup_);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    fleurXcCombo_ = new QComboBox(fleurGroup_);
    fleurXcCombo_->setEditable(true);
    fleurXcCombo_->addItems({QStringLiteral("pbe"), QStringLiteral("vwn"),
                             QStringLiteral("pw91"), QStringLiteral("pbe0")});
    form->addRow(tr("XC functional:"), fleurXcCombo_);

    fleurKmaxSpin_ = new QDoubleSpinBox(fleurGroup_);
    fleurKmaxSpin_->setRange(1.0, 20.0);
    fleurKmaxSpin_->setDecimals(2);
    fleurKmaxSpin_->setSingleStep(0.1);
    fleurKmaxSpin_->setValue(4.0);
    fleurKmaxSpin_->setSuffix(tr(" bohr⁻¹"));
    fleurKmaxSpin_->setToolTip(
        tr("K_max — the plane-wave cutoff of the INTERSTITIAL region, which is "
           "the convergence parameter of an LAPW calculation.\n\n"
           "A reciprocal LENGTH, not the dimensionless R·K_max other LAPW "
           "codes quote: 3.6–4.0 is a normal starting range, and it has to be "
           "converged against the muffin-tin radii, which shrink as the atoms "
           "get closer."));
    form->addRow(tr("K_max:"), fleurKmaxSpin_);

    fleurRootEdit_ = directoryRow(
        fleurGroup_, form, tr("FLEUR binaries:"), kFleurRootKey,
        tr("/path/to/fleur/build"),
        tr("Directory holding `inpgen` and `fleur` / `fleur_MPI`. Prepended "
           "to PATH by the generated script; leave empty if they are already "
           "on it."));

    fleurConvEdit_ = toleranceEdit(fleurGroup_, QStringLiteral("1e-5"));
    fleurConvEdit_->setToolTip(
        tr("minDistance — the charge-density distance the SCF must reach, in "
           "FLEUR's own units."));
    form->addRow(tr("SCF convergence:"), fleurConvEdit_);

    fleurMaxIterSpin_ = new QSpinBox(fleurGroup_);
    fleurMaxIterSpin_->setRange(1, 1000);
    fleurMaxIterSpin_->setValue(60);
    fleurMaxIterSpin_->setToolTip(tr("itmax — SCF iteration cap."));
    form->addRow(tr("Max iterations:"), fleurMaxIterSpin_);

    connect(fleurXcCombo_, &QComboBox::currentTextChanged, this,
            [this] { if (onChanged_) onChanged_(); });
    connect(fleurKmaxSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { if (onChanged_) onChanged_(); });
    connect(fleurConvEdit_, &QLineEdit::textChanged, this,
            [this] { if (onChanged_) onChanged_(); });
    connect(fleurMaxIterSpin_, &QSpinBox::valueChanged, this,
            [this] { if (onChanged_) onChanged_(); });

    layout->addWidget(fleurGroup_);
}

// ---------------------------------------------------------------------------
// CP2K
// ---------------------------------------------------------------------------
void ExtendedEngineGroups::buildCp2k(QWidget* parent, QVBoxLayout* layout)
{
    cp2kGroup_ = new QGroupBox(tr("CP2K settings"), parent);
    auto* form = new QFormLayout(cp2kGroup_);

    auto* note = new QLabel(
        tr("CP2K is a <b>Gaussian and plane waves</b> code, and the two "
           "cutoffs below are <b>not</b> basis-set parameters. The basis set "
           "IS the Gaussian basis — DZVP → TZVP is how it is improved. The "
           "<b>cutoff</b> is the plane-wave grid the <i>density</i> is mapped "
           "onto, and <b>rel. cutoff</b> decides how the multi-grid assigns "
           "each Gaussian to a grid level; the two are converged together. "
           "Raising the cutoff alone refines a grid while the basis stays "
           "exactly as small."),
        cp2kGroup_);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    cp2kXcCombo_ = new QComboBox(cp2kGroup_);
    cp2kXcCombo_->setEditable(true);
    cp2kXcCombo_->addItems({QStringLiteral("PBE"), QStringLiteral("BLYP"),
                            QStringLiteral("B3LYP"), QStringLiteral("PBE0"),
                            QStringLiteral("LDA")});
    form->addRow(tr("XC functional:"), cp2kXcCombo_);

    // The two grid cutoffs on one row, because they are ONE decision: neither
    // converges without the other, and CP2K's own cutoff-convergence protocol
    // scans them as a pair.
    cp2kCutoffSpin_ = new QDoubleSpinBox(cp2kGroup_);
    cp2kCutoffSpin_->setRange(100.0, 50000.0);
    cp2kCutoffSpin_->setDecimals(0);
    cp2kCutoffSpin_->setSingleStep(500.0);
    cp2kCutoffSpin_->setValue(5442.0); // 400 Ry
    cp2kCutoffSpin_->setSuffix(tr(" eV"));
    cp2kCutoffSpin_->setToolTip(
        tr("CUTOFF — the finest plane-wave grid the density is mapped onto. "
           "5442 eV is 400 Ry, ASE's own default; hard pseudopotentials and "
           "first-row elements want more."));

    cp2kRelCutoffSpin_ = new QDoubleSpinBox(cp2kGroup_);
    cp2kRelCutoffSpin_->setRange(50.0, 10000.0);
    cp2kRelCutoffSpin_->setDecimals(0);
    cp2kRelCutoffSpin_->setSingleStep(100.0);
    cp2kRelCutoffSpin_->setValue(816.0); // 60 Ry
    cp2kRelCutoffSpin_->setSuffix(tr(" eV"));
    cp2kRelCutoffSpin_->setToolTip(
        tr("REL_CUTOFF — the grid a Gaussian of unit exponent is mapped onto, "
           "which is what actually decides how the multi-grid distributes the "
           "basis functions. 816 eV is 60 Ry.\n\n"
           "Converge it TOGETHER with the cutoff: a high cutoff with a low "
           "rel. cutoff still puts most of the basis on a coarse grid, so the "
           "energy stops moving for the wrong reason."));

    auto* cutoffRow = new QWidget(cp2kGroup_);
    auto* cutoffLayout = new QHBoxLayout(cutoffRow);
    cutoffLayout->setContentsMargins(0, 0, 0, 0);
    cutoffLayout->addWidget(new QLabel(tr("cutoff"), cutoffRow));
    cutoffLayout->addWidget(cp2kCutoffSpin_, 1);
    cutoffLayout->addWidget(new QLabel(tr("rel."), cutoffRow));
    cutoffLayout->addWidget(cp2kRelCutoffSpin_, 1);
    form->addRow(tr("Grid cutoffs:"), cutoffRow);

    cp2kBasisCombo_ = new QComboBox(cp2kGroup_);
    cp2kBasisCombo_->setEditable(true);
    cp2kBasisCombo_->addItems({QStringLiteral("DZVP-MOLOPT-SR-GTH"),
                               QStringLiteral("TZVP-MOLOPT-GTH"),
                               QStringLiteral("TZV2P-MOLOPT-GTH"),
                               QStringLiteral("SZV-MOLOPT-GTH")});
    cp2kBasisCombo_->setToolTip(
        tr("The Gaussian basis — this, not the cutoff, is the basis-set "
           "quality knob. The MOLOPT sets are the general-purpose ones; the "
           "SR variants have shorter ranges and are cheaper for dense solids."));
    form->addRow(tr("Basis set:"), cp2kBasisCombo_);

    cp2kBasisFileEdit_ =
        new QLineEdit(QStringLiteral("BASIS_MOLOPT"), cp2kGroup_);
    cp2kBasisFileEdit_->setToolTip(
        tr("The basis-set FILE the name above is looked up in, from CP2K's "
           "data directory. A name that is not in this file fails at setup."));
    form->addRow(tr("Basis set file:"), cp2kBasisFileEdit_);

    cp2kPseudoCombo_ = new QComboBox(cp2kGroup_);
    cp2kPseudoCombo_->setEditable(true);
    cp2kPseudoCombo_->addItems({QStringLiteral("auto"),
                                QStringLiteral("GTH-PBE"),
                                QStringLiteral("GTH-BLYP"),
                                QStringLiteral("GTH-PADE")});
    cp2kPseudoCombo_->setToolTip(
        tr("The GTH pseudopotential. `auto` lets CP2K pick the one matching "
           "the functional, which is right whenever the functional is a "
           "standard one — and a GTH-PADE pseudopotential under a PBE "
           "functional is an inconsistent calculation, not a slightly less "
           "accurate one."));
    form->addRow(tr("Pseudopotential:"), cp2kPseudoCombo_);

    cp2kPotentialFileEdit_ =
        new QLineEdit(QStringLiteral("POTENTIAL"), cp2kGroup_);
    form->addRow(tr("Potential file:"), cp2kPotentialFileEdit_);

    cp2kMaxScfSpin_ = new QSpinBox(cp2kGroup_);
    cp2kMaxScfSpin_->setRange(1, 10000);
    cp2kMaxScfSpin_->setValue(50);
    form->addRow(tr("Max SCF steps:"), cp2kMaxScfSpin_);

    cp2kCommandEdit_ = new QLineEdit(QStringLiteral("cp2k_shell"), cp2kGroup_);
    cp2kCommandEdit_->setToolTip(
        tr("The cp2k_shell command ASE keeps a pipe open to.\n\n"
           "Unlike every other engine here, CP2K is driven through a "
           "PERSISTENT process rather than one binary run per evaluation — "
           "which is what makes it fast inside an MD or relaxation loop, since "
           "the wavefunction is reused between steps."));
    form->addRow(tr("cp2k_shell command:"), cp2kCommandEdit_);

    cp2kExtraEdit_ = new QPlainTextEdit(cp2kGroup_);
    cp2kExtraEdit_->setMaximumHeight(70);
    cp2kExtraEdit_->setPlaceholderText(
        QStringLiteral("&FORCE_EVAL\n  &DFT\n    &SCF\n      &OT\n"
                       "        MINIMIZER DIIS\n      &END OT\n"
                       "    &END SCF\n  &END DFT\n&END FORCE_EVAL"));
    cp2kExtraEdit_->setToolTip(
        tr("Extra CP2K input sections, appended verbatim to the generated "
           "`inp`. CP2K's input has around a thousand keywords; this is the "
           "escape hatch that stops this page from being a ceiling."));
    form->addRow(tr("Extra input sections:"), cp2kExtraEdit_);

    for (QComboBox* combo : {cp2kXcCombo_, cp2kBasisCombo_, cp2kPseudoCombo_})
        connect(combo, &QComboBox::currentTextChanged, this,
                [this] { if (onChanged_) onChanged_(); });
    for (QDoubleSpinBox* spin : {cp2kCutoffSpin_, cp2kRelCutoffSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { if (onChanged_) onChanged_(); });
    for (QLineEdit* edit :
         {cp2kBasisFileEdit_, cp2kPotentialFileEdit_, cp2kCommandEdit_})
        connect(edit, &QLineEdit::textChanged, this,
                [this] { if (onChanged_) onChanged_(); });
    connect(cp2kMaxScfSpin_, &QSpinBox::valueChanged, this,
            [this] { if (onChanged_) onChanged_(); });
    connect(cp2kExtraEdit_, &QPlainTextEdit::textChanged, this,
            [this] { if (onChanged_) onChanged_(); });

    layout->addWidget(cp2kGroup_);
}

// ---------------------------------------------------------------------------
// Amber
// ---------------------------------------------------------------------------
void ExtendedEngineGroups::buildAmber(QWidget* parent, QVBoxLayout* layout)
{
    amberGroup_ = new QGroupBox(tr("Amber settings"), parent);
    auto* form = new QFormLayout(amberGroup_);

    auto* note = new QLabel(
        tr("Amber is an <b>engine</b>, not a force field — and unlike GROMACS "
           "it cannot even type a structure. The physics lives entirely in the "
           "<b>prmtop topology</b>, which carries the atom types, the charges "
           "and every bonded term, and which <code>tleap</code> / "
           "<code>antechamber</code> build beforehand. The topology and this "
           "structure must describe the same atoms in the same order."),
        amberGroup_);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    amberExeEdit_ = new QLineEdit(QStringLiteral("sander -O "), amberGroup_);
    amberExeEdit_->setToolTip(
        tr("The sander invocation. The trailing `-O ` (overwrite) is part of "
           "ASE's own default and is what lets a job directory be re-run; "
           "`pmemd` is the faster drop-in on a licensed installation."));
    form->addRow(tr("Amber executable:"), amberExeEdit_);

    // A FILE picker, not a directory: unlike the pseudopotential libraries
    // above, a prmtop belongs to one system rather than to the installation,
    // so it is not remembered across sessions either.
    amberTopologyEdit_ = new QLineEdit(amberGroup_);
    amberTopologyEdit_->setPlaceholderText(tr("/path/to/system.prmtop"));
    amberTopologyEdit_->setToolTip(
        tr("The prmtop topology. REQUIRED: there is no force field without "
           "it, and the generated script refuses rather than running a system "
           "with no parameters.\n\n"
           "Build one with tleap:\n"
           "    tleap -f leaprc.protein.ff19SB\n"
           "    > mol = loadpdb system.pdb\n"
           "    > saveamberparm mol system.prmtop system.inpcrd"));
    {
        auto* browse = new QPushButton(tr("Browse…"), amberGroup_);
        auto* row = new QWidget(amberGroup_);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(amberTopologyEdit_, 1);
        rowLayout->addWidget(browse);
        form->addRow(tr("Topology (prmtop):"), row);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(
                amberTopologyEdit_->window(), tr("Select Amber Topology"),
                amberTopologyEdit_->text(),
                tr("Amber topology (*.prmtop *.parm7 *.top);;All files (*)"));
            if (!path.isEmpty())
                amberTopologyEdit_->setText(path);
        });
    }

    amberInputEdit_ = new QLineEdit(amberGroup_);
    amberInputEdit_->setPlaceholderText(tr("(generated: mm.in)"));
    amberInputEdit_->setToolTip(
        tr("The sander mdin control file. Left empty, the script writes one "
           "with imin=0 / nstlim=0 — a single energy+force evaluation.\n\n"
           "That default matters: sander's own is a MINIMIZATION, so every "
           "\"force evaluation\" ASE asked for would be a complete relaxation, "
           "and the reported trajectory would be a sequence of already-relaxed "
           "structures."));
    form->addRow(tr("Control file (mdin):"), amberInputEdit_);

    for (QLineEdit* edit :
         {amberExeEdit_, amberTopologyEdit_, amberInputEdit_})
        connect(edit, &QLineEdit::textChanged, this,
                [this] { if (onChanged_) onChanged_(); });

    layout->addWidget(amberGroup_);
}

// ---------------------------------------------------------------------------
// Visibility + config
// ---------------------------------------------------------------------------
void ExtendedEngineGroups::hideAll()
{
    for (QGroupBox* group : {abinitGroup_, aimsGroup_, nwchemGroup_,
                             openmxGroup_, fleurGroup_, cp2kGroup_,
                             amberGroup_}) {
        if (group)
            group->setVisible(false);
    }
}

void ExtendedEngineGroups::updateVisibility(core::CalculatorKind kind)
{
    hideAll();
    QGroupBox* group = nullptr;
    switch (kind) {
    case core::CalculatorKind::Abinit:  group = abinitGroup_; break;
    case core::CalculatorKind::FhiAims: group = aimsGroup_; break;
    case core::CalculatorKind::NwChem:  group = nwchemGroup_; break;
    case core::CalculatorKind::OpenMx:  group = openmxGroup_; break;
    case core::CalculatorKind::Fleur:   group = fleurGroup_; break;
    case core::CalculatorKind::Cp2k:    group = cp2kGroup_; break;
    case core::CalculatorKind::Amber:   group = amberGroup_; break;
    default: break;
    }
    if (group)
        group->setVisible(true);
    if (kind == core::CalculatorKind::NwChem)
        updateNwChemRows();
}

void ExtendedEngineGroups::applyTo(core::CalculatorConfig& c) const
{
    if (!abinitGroup_)
        return; // build() never ran

    c.abinitXc = abinitXcCombo_->currentText().trimmed().toStdString();
    c.abinitPps = abinitPpsCombo_->currentText().trimmed().toStdString();
    c.abinitPseudoDir = abinitPseudoEdit_->text().trimmed().toStdString();
    c.abinitToldfe = toleranceValue(abinitToldfeEdit_, 1.0e-6);
    c.abinitNstep = abinitNstepSpin_->value();
    c.abinitExtra = abinitExtraEdit_->toPlainText().trimmed().toStdString();

    c.aimsXc = aimsXcCombo_->currentText().trimmed().toStdString();
    c.aimsSpeciesDir = aimsSpeciesEdit_->text().trimmed().toStdString();
    c.aimsSpeciesTier = aimsTierCombo_->currentText().toStdString();
    c.aimsRelativistic =
        aimsRelativisticCombo_->currentText().toStdString();
    c.aimsScfAccuracyEv = toleranceValue(aimsAccuracyEdit_, 1.0e-6);
    c.aimsExtra = aimsExtraEdit_->toPlainText().trimmed().toStdString();

    c.nwchemTheory = nwchemTheoryCombo_->currentData().toString().toStdString();
    c.nwchemXc = nwchemXcCombo_->currentText().trimmed().toStdString();
    c.nwchemBasis = nwchemBasisCombo_->currentText().trimmed().toStdString();
    c.nwchemMemory = nwchemMemoryEdit_->text().trimmed().toStdString();
    c.nwchemExtra = nwchemExtraEdit_->toPlainText().trimmed().toStdString();

    c.openmxXc = openmxXcCombo_->currentText().trimmed().toStdString();
    c.openmxDataPath = openmxDataEdit_->text().trimmed().toStdString();
    c.openmxEnergyCutoffEv = openmxCutoffSpin_->value();
    c.openmxScfCriterionEv = openmxCriterionSpin_->value();
    c.openmxScfMaxIter = openmxMaxIterSpin_->value();
    c.openmxEigenSolver =
        openmxSolverCombo_->currentData().toString().toStdString();

    c.fleurXc = fleurXcCombo_->currentText().trimmed().toStdString();
    c.fleurKmax = fleurKmaxSpin_->value();
    c.fleurRoot = fleurRootEdit_->text().trimmed().toStdString();
    c.fleurEnergyConvHtr = toleranceValue(fleurConvEdit_, 1.0e-5);
    c.fleurMaxIterations = fleurMaxIterSpin_->value();

    c.cp2kXc = cp2kXcCombo_->currentText().trimmed().toStdString();
    c.cp2kCutoffEv = cp2kCutoffSpin_->value();
    c.cp2kRelCutoffEv = cp2kRelCutoffSpin_->value();
    c.cp2kBasisSet = cp2kBasisCombo_->currentText().trimmed().toStdString();
    c.cp2kBasisSetFile = cp2kBasisFileEdit_->text().trimmed().toStdString();
    c.cp2kPseudoPotential =
        cp2kPseudoCombo_->currentText().trimmed().toStdString();
    c.cp2kPotentialFile =
        cp2kPotentialFileEdit_->text().trimmed().toStdString();
    c.cp2kMaxScf = cp2kMaxScfSpin_->value();
    c.cp2kCommand = cp2kCommandEdit_->text().trimmed().toStdString();
    c.cp2kExtraInput = cp2kExtraEdit_->toPlainText().trimmed().toStdString();

    c.amberExecutable = amberExeEdit_->text().toStdString();
    c.amberTopologyFile = amberTopologyEdit_->text().trimmed().toStdString();
    c.amberInputFile = amberInputEdit_->text().trimmed().toStdString();
}

} // namespace calango::gui
