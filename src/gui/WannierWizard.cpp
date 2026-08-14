#include "gui/WannierWizard.hpp"

#include "core/Structure.hpp"
#include "core/WannierScriptGenerator.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

WannierWizard::WannierWizard(std::shared_ptr<core::Structure> structure,
                             QWidget* parent)
    : SimulationWizardBase(parent), structure_(std::move(structure))
{
    buildUi();
    // GPAW is the fully supported backend for the localization; this also fixes
    // the (hidden) engine used by the fresh-SCF fallback and the env resolution
    // when a baseline carries no inherited interpreter.
    selectCalculator(core::CalculatorKind::Gpaw);
    onBaselineChanged();
}

void WannierWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    for (const auto& [label, dir] : baselines)
        baselineCombo_->addItem(label, dir);
    // Default to the first real baseline (index 0 is the "none" fallback) so
    // the inherited calculator is active out of the box — inheritance is the
    // intended path. setCurrentIndex triggers onBaselineChanged().
    if (!baselines.isEmpty())
        baselineCombo_->setCurrentIndex(1);
    else
        onBaselineChanged();
}

QString WannierWizard::wizardTitle() const
{
    return tr("Wannierization Setup");
}

QString WannierWizard::settingsHeader() const
{
    return tr("SCF Process Selection & Wannier Configuration");
}

QWidget* WannierWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Run the Marzari-Vanderbilt localization on top of a completed "
           "Single-Point calculation."),
        page);
    intro->setWordWrap(true);
    intro->setToolTip(
        tr("The calculator engine and its parameters (XC functional, cutoff, "
           "grid, k-points) and the Conda environment are inherited from the "
           "selected baseline, so there is nothing to redefine.\n\n"
           "For a correct localization the baseline should have been run with "
           "k-point symmetry off."));
    layout->addWidget(intro);

    // --- SCF process selection --------------------------------------------
    auto* sourceGroup = new QGroupBox(tr("Baseline SCF process"), page);
    auto* sourceForm = new QFormLayout(sourceGroup);

    baselineCombo_ = new QComboBox(sourceGroup);
    baselineCombo_->addItem(tr("(none — run a fresh SCF with symmetry off)"),
                            QString());
    baselineCombo_->setToolTip(
        tr("Completed Single-Point calculations that saved the Bloch "
           "wavefunctions (GPAW .gpw). The localization restarts from that "
           "process directory and inherits its calculator; picking 'none' runs "
           "a fresh SCF first."));
    sourceForm->addRow(tr("Process:"), baselineCombo_);

    inheritedLabel_ = new QLabel(sourceGroup);
    inheritedLabel_->setWordWrap(true);
    inheritedLabel_->setTextFormat(Qt::RichText);
    sourceForm->addRow(tr("Inherited calculator:"), inheritedLabel_);

    // What that run actually stored, as opposed to what it was asked for. The
    // bands and the k-mesh are the two numbers every Wannier parameter below is
    // chosen against — the Wannier count cannot exceed the bands, and the
    // localization quality is set by the mesh — and neither was visible here
    // before, so both were guesses.
    baselineSummaryLabel_ = new QLabel(sourceGroup);
    baselineSummaryLabel_->setWordWrap(true);
    baselineSummaryLabel_->setTextFormat(Qt::RichText);
    sourceForm->addRow(tr("Baseline:"), baselineSummaryLabel_);

    // The pre-condition. Its own label rather than a clause appended to the
    // line above, because it is the one thing on this page that can make the
    // run impossible and it has to survive being read at a glance.
    symmetryWarningLabel_ = new QLabel(sourceGroup);
    symmetryWarningLabel_->setWordWrap(true);
    symmetryWarningLabel_->setTextFormat(Qt::RichText);
    symmetryWarningLabel_->setVisible(false);
    sourceForm->addRow(symmetryWarningLabel_);

    layout->addWidget(sourceGroup);

    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            &WannierWizard::onBaselineChanged);

    // --- MLWF parameters ---------------------------------------------------
    auto* mlwfGroup = new QGroupBox(tr("Wannier localization parameters"), page);
    auto* form = new QFormLayout(mlwfGroup);

    // Trial-orbital initialization. The untranslated key stored as itemData is
    // what generateScript() maps onto ASE's `initialwannier` argument.
    projectionCombo_ = new QComboBox(mlwfGroup);
    projectionCombo_->addItem(tr("Automatic (orbitals)"),
                              QStringLiteral("orbitals"));
    projectionCombo_->addItem(tr("Bloch"), QStringLiteral("bloch"));
    projectionCombo_->addItem(tr("Random"), QStringLiteral("random"));
    projectionCombo_->addItem(QStringLiteral("s"), QStringLiteral("s"));
    projectionCombo_->addItem(QStringLiteral("p"), QStringLiteral("p"));
    projectionCombo_->addItem(QStringLiteral("d"), QStringLiteral("d"));
    projectionCombo_->addItem(QStringLiteral("sp3"), QStringLiteral("sp3"));
    projectionCombo_->addItem(QStringLiteral("dxy"), QStringLiteral("dxy"));
    projectionCombo_->setToolTip(
        tr("Initial trial projections seeding the localization. The atomic "
           "sets (s, p, d, sp3, dxy) fall back to ASE's 'orbitals' initializer, "
           "which derives the projections from the atomic orbitals."));
    form->addRow(tr("Initial trial projections:"), projectionCombo_);

    nWannier_ = new QSpinBox(mlwfGroup);
    nWannier_->setRange(1, 512);
    nWannier_->setValue(4);
    nWannier_->setToolTip(
        tr("Number of Wannier functions to localize (typically the number of "
           "occupied bands / valence orbitals)."));
    form->addRow(tr("Wannier functions:"), nWannier_);

    // How the FIXED (frozen) part of the Hilbert space is chosen. ASE takes
    // `fixedenergy` OR `fixedstates` and raises if given both, so this is one
    // three-way choice rather than two independent toggles.
    fixedModeCombo_ = new QComboBox(mlwfGroup);
    fixedModeCombo_->addItem(
        tr("Exactly the Wannier count (no extra freedom)"),
        static_cast<int>(core::WannierConfig::FixedStatesMode::FromWannierCount));
    fixedModeCombo_->addItem(
        tr("Every state below an energy window"),
        static_cast<int>(core::WannierConfig::FixedStatesMode::EnergyWindow));
    fixedModeCombo_->addItem(
        tr("An explicit number of bands"),
        static_cast<int>(core::WannierConfig::FixedStatesMode::BandCount));
    fixedModeCombo_->setToolTip(
        tr("Which states are FIXED — reproduced exactly by the Wannier "
           "manifold — as opposed to the extra degrees of freedom that are "
           "free to mix.\n\n"
           "• Wannier count — ASE fixes exactly as many states as there are "
           "Wannier functions. No disentanglement.\n"
           "• Energy window — ASE's fixedenergy: everything below a cutoff is "
           "fixed, so the count follows the band structure and may differ "
           "between k-points.\n"
           "• Band count — ASE's fixedstates: the same explicit number at "
           "every k-point.\n\n"
           "The last two are mutually exclusive in ASE, which is why this is "
           "one selector."));
    form->addRow(tr("Fixed states:"), fixedModeCombo_);

    energyWindowSpin_ = new QDoubleSpinBox(mlwfGroup);
    energyWindowSpin_->setDecimals(2);
    energyWindowSpin_->setRange(-100.0, 100.0);
    energyWindowSpin_->setSingleStep(0.5);
    energyWindowSpin_->setValue(0.0);
    energyWindowSpin_->setSuffix(tr(" eV"));
    // The reference level is NOT unconditionally E_F. ASE's choose_states()
    // uses the conduction band minimum whenever the system has a gap and the
    // cutoff is >= 0.01 eV. The label used to promise "above E_F", which is
    // wrong for every semiconductor — and wrong invisibly, since the run
    // succeeds and simply fixes a different set of states than intended.
    energyWindowSpin_->setToolTip(
        tr("Energy cutoff below which states are kept fixed.\n\n"
           "Measured from the CONDUCTION BAND MINIMUM when the system has a "
           "gap (> 0.01 eV) and this value is at least 0.01 eV; measured from "
           "the Fermi level otherwise — for a metal, or for a cutoff below "
           "0.01 eV. That is ASE's rule (choose_states in ase/dft/wannier.py), "
           "not a convention chosen here.\n\n"
           "So on silicon, 2.0 eV means 2 eV above the conduction band "
           "minimum, not 2 eV above E_F."));
    energyWindowLabel_ = new QLabel(tr("Energy window (from CBM / E_F):"),
                                    mlwfGroup);
    form->addRow(energyWindowLabel_, energyWindowSpin_);

    fixedStatesSpin_ = new QSpinBox(mlwfGroup);
    fixedStatesSpin_->setRange(1, 4096);
    fixedStatesSpin_->setValue(4);
    fixedStatesSpin_->setToolTip(
        tr("ASE's fixedstates: how many bands are fixed at every k-point. Must "
           "not exceed the number of Wannier functions — ASE computes the "
           "extra degrees of freedom as (Wannier functions − fixed states) and "
           "a negative value fails during setup."));
    fixedStatesLabel_ = new QLabel(tr("Fixed bands:"), mlwfGroup);
    form->addRow(fixedStatesLabel_, fixedStatesSpin_);

    maxIterSpin_ = new QSpinBox(mlwfGroup);
    maxIterSpin_->setRange(1, 100000);
    maxIterSpin_->setValue(50);
    maxIterSpin_->setToolTip(
        tr("Maximum Marzari-Vanderbilt minimization iterations. The run still "
           "exits early once the spread functional Ω converges."));
    form->addRow(tr("Max. minimization iterations:"), maxIterSpin_);
    layout->addWidget(mlwfGroup);

    // Only the row belonging to the selected mode is shown: the other one is
    // not merely inapplicable, it is a value ASE would refuse alongside.
    const auto syncFixedMode = [this] {
        const auto mode = static_cast<core::WannierConfig::FixedStatesMode>(
            fixedModeCombo_->currentData().toInt());
        const bool energy =
            mode == core::WannierConfig::FixedStatesMode::EnergyWindow;
        const bool count =
            mode == core::WannierConfig::FixedStatesMode::BandCount;
        energyWindowLabel_->setVisible(energy);
        energyWindowSpin_->setVisible(energy);
        fixedStatesLabel_->setVisible(count);
        fixedStatesSpin_->setVisible(count);
    };
    syncFixedMode();

    connect(projectionCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });
    connect(nWannier_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(fixedModeCombo_, &QComboBox::currentIndexChanged, this,
            [this, syncFixedMode] {
                syncFixedMode();
                refreshPreview();
            });
    connect(energyWindowSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(fixedStatesSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(maxIterSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    layout->addStretch(1);
    return page;
}

void WannierWizard::onBaselineChanged()
{
    const QString dir =
        baselineCombo_ ? baselineCombo_->currentData().toString() : QString();
    inherited_ = dir.isEmpty() ? std::nullopt : readCalculatorProvenance(dir);

    if (inheritedLabel_) {
        if (dir.isEmpty()) {
            inheritedLabel_->setText(
                tr("<i>No baseline — a fresh GPAW SCF (symmetry off) will be "
                   "run before the localization.</i>"));
        } else if (inherited_) {
            QString note = inherited_->summary().toHtmlEscaped();
            if (!inherited_->condaEnv.isEmpty())
                note += tr(" · env %1")
                            .arg(inherited_->condaEnv.toHtmlEscaped());
            // The symmetry verdict is no longer appended here: it is a
            // pre-condition, not a footnote on the calculator description, and
            // refreshBaselineSummary() below states it from what the run
            // stored rather than from what it was asked for.
            inheritedLabel_->setText(note);
        } else {
            inheritedLabel_->setText(
                tr("<i>Restarting from the saved wavefunctions (.gpw); "
                   "parameters come from the restart file.</i>"));
            inheritedLabel_->setToolTip(
                tr("This baseline carries no calculator.json, so its "
                   "parameters are taken straight from the restart file."));
        }
    }
    refreshBaselineSummary(dir);
    refreshPreview();
}

void WannierWizard::refreshBaselineSummary(const QString& dir)
{
    if (!baselineSummaryLabel_ || !symmetryWarningLabel_)
        return;

    baselineSummary_ = dir.isEmpty()
        ? core::BaselineSummary{}
        : core::readBaselineSummary(dir.toStdString());

    if (dir.isEmpty()) {
        // No baseline: the wizard runs its own SCF, and that script sets
        // symmetry="off" itself (WannierScriptGenerator), so there is no
        // pre-condition left to check.
        baselineSummaryLabel_->setText(
            tr("<i>A fresh SCF will be run with <b>Symmetry: off</b>.</i>"));
        baselineSummaryLabel_->setToolTip(
            tr("Without a baseline the generated script runs its own ground "
               "state and forces symmetry=\"off\" on it, so the full "
               "Brillouin-zone k-set the localization needs is guaranteed."));
        symmetryWarningLabel_->setVisible(false);
        return;
    }

    const core::BaselineSummary& b = baselineSummary_;
    QStringList facts;
    facts << (b.bands > 0 ? tr("Bands: <b>%1</b>").arg(b.bands)
                          : tr("Bands: <b>unknown</b>"));
    if (b.kpts[0] > 0) {
        QString k = tr("k-points: <b>%1×%2×%3</b>")
                        .arg(b.kpts[0])
                        .arg(b.kpts[1])
                        .arg(b.kpts[2]);
        // The two counts are the whole story: how many the mesh has, and how
        // many the run actually kept.
        if (b.bzPoints > 0 && b.ibzPoints > 0)
            k += tr(" — %1 of %2 kept").arg(b.ibzPoints).arg(b.bzPoints);
        facts << k;
    } else {
        facts << tr("k-points: <b>unknown</b>");
    }

    // The check, in the form the user is looking for.
    switch (b.symmetry) {
    case core::SymmetryState::Off:
        facts << tr("<span style='color:#3c9a5f;'>Symmetry: <b>off</b> ✓</span>");
        break;
    case core::SymmetryState::On:
        facts << tr("<span style='color:#d9534f;'>Symmetry: <b>on</b></span>");
        break;
    case core::SymmetryState::Unknown:
        facts << tr("<span style='color:#d08a4a;'>Symmetry: <b>unknown</b></span>");
        break;
    }
    baselineSummaryLabel_->setText(facts.join(QStringLiteral(" · ")));
    baselineSummaryLabel_->setToolTip(
        tr("Read back from what this run recorded.\n\n%1")
            .arg(QString::fromStdString(b.evidence)));

    // Why the full zone, stated once and reused by both branches: it is the
    // reason the check exists, and a warning that only says "turn this on"
    // teaches nothing.
    const QString why =
        tr("Wannierization needs the <b>full</b> Brillouin-zone k-set: it "
           "builds overlaps between neighbouring k-points across the whole "
           "mesh, and a ground state that stored only the irreducible wedge "
           "has no state to offer at most of them.");

    switch (b.symmetry) {
    case core::SymmetryState::Off:
        symmetryWarningLabel_->setVisible(false);
        break;
    case core::SymmetryState::On: {
        QString detail;
        if (b.bzPoints > 0 && b.ibzPoints > 0)
            detail = tr(" This baseline kept <b>%1 of its %2</b> k-points.")
                         .arg(b.ibzPoints)
                         .arg(b.bzPoints);
        symmetryWarningLabel_->setText(
            QStringLiteral("<span style='color:#d9534f;'>⚠ %1</span>")
                .arg(tr("<b>\"Symmetry: off\" is required.</b> This "
                        "Single-point Calculation was run <i>with</i> "
                        "symmetry.%1<br>%2<br>Re-run the Single-Point "
                        "Calculation with <b>Symmetry: off</b>.")
                         .arg(detail, why)));
        symmetryWarningLabel_->setVisible(true);
        break;
    }
    case core::SymmetryState::Unknown:
        // Cautionary, not alarming, and explicitly about not knowing — saying
        // "symmetry is on" here would be a claim the evidence does not support.
        symmetryWarningLabel_->setText(
            QStringLiteral("<span style='color:#d08a4a;'>⚠ %1</span>")
                .arg(tr("<b>Could not determine the symmetry setting</b> of "
                        "this calculation.<br>%1<br>If it was not run with "
                        "<b>Symmetry: off</b>, the localization will fail. "
                        "Hover the line above to see what was looked at.")
                         .arg(why)));
        symmetryWarningLabel_->setVisible(true);
        break;
    }
}

QString WannierWizard::pythonExecutable() const
{
    // Bind the interpreter/env the baseline ran under, so the localization uses
    // the same Conda environment as its SCF. Fall back to the standard
    // per-engine resolution when the baseline carries no provenance.
    if (inherited_ && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

bool WannierWizard::calculatorAllowed(core::CalculatorKind kind) const
{
    // DFT engines only: the localization needs the Bloch wavefunctions from a
    // self-consistent ground state. GPAW drives ase.dft.wannier directly;
    // Quantum ESPRESSO / SIESTA select their own env + SCF. (The engine combo
    // is not shown — the calculator is inherited — but this still constrains
    // the fresh-SCF fallback and the default selection.)
    return kind == core::CalculatorKind::Gpaw
        || kind == core::CalculatorKind::QuantumEspresso
        || kind == core::CalculatorKind::Siesta;
}

QString WannierWizard::generateScript() const
{
    core::WannierConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    // Inherit the engine from the baseline's provenance when available, so the
    // fresh-SCF fallback (no baseline) and any engine-tagged comment match what
    // the baseline used.
    if (inherited_ && inherited_->engineKind >= 0)
        cfg.calculator.calculator =
            static_cast<core::CalculatorKind>(inherited_->engineKind);
    cfg.baselineDir =
        baselineCombo_ ? baselineCombo_->currentData().toString().toStdString()
                       : std::string();
    cfg.nWannier = nWannier_ ? nWannier_->value() : 4;
    cfg.initialWannier =
        projectionCombo_
            ? projectionCombo_->currentData().toString().toStdString()
            : std::string("orbitals");
    cfg.fixedMode = fixedModeCombo_
        ? static_cast<core::WannierConfig::FixedStatesMode>(
              fixedModeCombo_->currentData().toInt())
        : core::WannierConfig::FixedStatesMode::FromWannierCount;
    cfg.energyWindowEv = energyWindowSpin_ ? energyWindowSpin_->value() : 0.0;
    cfg.fixedStates = fixedStatesSpin_ ? fixedStatesSpin_->value() : 0;
    cfg.maxIterations = maxIterSpin_ ? maxIterSpin_->value() : 50;
    return QString::fromStdString(core::generateWannierScript(cfg));
}

} // namespace calango::gui
