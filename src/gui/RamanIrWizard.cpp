#include "gui/RamanIrWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/Structure.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

RamanIrWizard::RamanIrWizard(std::shared_ptr<const core::Structure> structure,
                             QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
    // Every stage exists now, so the engine-dependent visibility, the cost
    // estimate and the hidden base engine selection can be brought into their
    // initial state together.
    onEngineChanged();
}

QString RamanIrWizard::wizardTitle() const
{
    return tr("Raman and IR Spectroscopy Setup");
}

QString RamanIrWizard::settingsHeader() const
{
    return tr("Vibrational Spectroscopy Settings");
}

QStringList RamanIrWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* RamanIrWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Both spectra describe the <b>same Γ-point phonons</b>; they differ "
           "only in which electronic response couples to a mode.<br><br>"
           "<b>Infrared</b> intensity is the change in macroscopic "
           "<i>polarization</i> a mode produces, "
           "|Σ<sub>k</sub> Z*<sub>k</sub>·e<sub>k</sub>/√M<sub>k</sub>|². "
           "In a periodic crystal there is no molecular dipole to "
           "differentiate, so the Born effective charges Z* are the only "
           "route to it. Under GPAW they are inherited — supplying a Born "
           "Charges run is what turns the IR column on, and without one the "
           "phonons and the Raman spectrum are computed as usual with every IR "
           "intensity reported as zero. VASP and Quantum ESPRESSO compute Z* "
           "in the same linear-response run that gives the force constants, so "
           "there is nothing to select.<br><br>"
           "<b>Raman</b> activity is built from ∂χ/∂Q, the change in "
           "<i>polarizability</i> — the same response the Optics module "
           "evaluates, taken in the static limit and differentiated by finite "
           "displacement."),
        page);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    // -- Engine ---------------------------------------------------------------
    // Chosen here rather than on a Calculator Settings stage, for the same
    // reason as in the Optics wizard: the engines need entirely different
    // stage-1 content — a set of inherited-run selectors against a compact
    // self-contained ground-state group — so the choice has to precede the
    // page rather than follow it.
    auto* engineRow = new QHBoxLayout;
    engineRow->addWidget(new QLabel(tr("Engine:"), page));
    engineCombo_ = new QComboBox(page);
    engineCombo_->addItem(
        tr("GPAW — finite displacements about an inherited ground state"),
        static_cast<int>(core::CalculatorKind::Gpaw));
    engineCombo_->addItem(
        tr("VASP — DFPT (IBRION=8 + LEPSILON), self-contained"),
        static_cast<int>(core::CalculatorKind::Vasp));
    engineCombo_->addItem(
        tr("Quantum ESPRESSO — one ph.x run (epsil + lraman)"),
        static_cast<int>(core::CalculatorKind::QuantumEspresso));
    engineCombo_->setToolTip(
        tr("How much of the answer one run can produce differs sharply "
           "between the three.\n\n"
           "GPAW displaces the ions: 6N force evaluations for the Hessian and "
           "6N more self-consistent runs for ∂α/∂u, with Z* inherited from a "
           "separate Born Effective Charges job.\n\n"
           "VASP gets the force constants, every Z* and ε∞ from a single "
           "linear-response run, then needs 6N displaced LEPSILON runs for the "
           "Raman half (it computes no Raman tensor of its own).\n\n"
           "Quantum ESPRESSO gets all of it — the Raman tensor included, as an "
           "analytic third-order response — from one ph.x run, provided the "
           "pseudopotentials are norm-conserving."));
    engineRow->addWidget(engineCombo_, 1);
    layout->addLayout(engineRow);
    connect(engineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { onEngineChanged(); });

    // -- Inherited runs (GPAW) ----------------------------------------------
    sourcesGroup_ = new QGroupBox(tr("Inherited Results"), page);
    auto* sourcesGroup = sourcesGroup_;
    auto* sourcesForm = new QFormLayout(sourcesGroup);

    baselineCombo_ = new QComboBox(sourcesGroup);
    baselineCombo_->setToolTip(
        tr("The converged geometry the displacements are taken about, and the "
           "calculator every displaced run is rebuilt from — so all of them "
           "use the settings the ground state was validated with."));
    sourcesForm->addRow(tr("Baseline SCF (.gpw):"), baselineCombo_);
    inheritanceNote_ = new QLabel(sourcesGroup);
    inheritanceNote_->setWordWrap(true);
    inheritanceNote_->setTextFormat(Qt::RichText);
    sourcesForm->addRow(inheritanceNote_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { onBaselineChanged(); });

    bornCombo_ = new QComboBox(sourcesGroup);
    // Optional, and first in the list so that is the default: the phonons and
    // the Raman spectrum need nothing from Z*, and requiring a Born Charges
    // run to get either of them made a second, expensive calculation the price
    // of admission for results that do not depend on it.
    bornCombo_->addItem(tr("(none — Raman and phonons only, no IR)"), QString());
    bornCombo_->setToolTip(
        tr("A completed Born Effective Charges run on this structure. "
           "Optional: it is what makes the INFRARED intensities computable, "
           "and nothing else in this module depends on it.\n\n"
           "When supplied it must cover EVERY atom: each one contributes to "
           "every IR intensity, and a partial Z* set would silently zero those "
           "contributions — producing a plausible spectrum with the wrong "
           "intensities. The generated script refuses rather than doing that."));
    sourcesForm->addRow(tr("Born charges (Z*):"), bornCombo_);
    connect(bornCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    opticsCombo_ = new QComboBox(sourcesGroup);
    opticsCombo_->addItem(tr("(none — use default broadening)"), QString());
    opticsCombo_->setToolTip(
        tr("Optional, and used for its SETTINGS rather than its numbers: the "
           "broadening η the dielectric response was validated with on this "
           "material.\n\n"
           "A finished spectrum at the equilibrium geometry cannot supply a "
           "derivative, so the dielectric tensors themselves are recomputed at "
           "each displaced geometry either way."));
    sourcesForm->addRow(tr("Optics reference:"), opticsCombo_);
    connect(opticsCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });
    layout->addWidget(sourcesGroup);

    // -- Self-contained ground state (VASP / QE) -----------------------------
    // Neither engine inherits anything: both converge their own SCF and then
    // solve the linear response on top of it, so the handful of knobs that
    // decide whether the response is trustworthy are asked here.
    //
    vaspGroup_ = new QGroupBox(tr("VASP Ground State"), page);
    auto* vaspForm = new QFormLayout(vaspGroup_);
    auto* vaspNote = new QLabel(
        tr("Self-contained. One IBRION=8 run with LEPSILON returns the force "
           "constants, every ion's Z* and ε∞ together — so the infrared "
           "spectrum costs a single job, and no Born Charges run has to be "
           "selected. EDIFF is forced to 1e-8: linear response is a "
           "derivative of the ground state, so its noise is the SCF's noise "
           "amplified. POTCARs come from Preferences → External Files."),
        vaspGroup_);
    vaspNote->setWordWrap(true);
    vaspForm->addRow(vaspNote);
    vaspEncutSpin_ = new QDoubleSpinBox(vaspGroup_);
    vaspEncutSpin_->setRange(100.0, 2000.0);
    vaspEncutSpin_->setDecimals(0);
    vaspEncutSpin_->setValue(500.0);
    vaspEncutSpin_->setSuffix(tr(" eV"));
    vaspEncutSpin_->setToolTip(
        tr("ENCUT for every step of the job — the DFPT run and, when Raman is "
           "on, each displaced LEPSILON run. A Raman tensor is a DIFFERENCE of "
           "two ε∞ values, so a cutoff that changed between them would not "
           "cancel: it would be the answer."));
    vaspForm->addRow(tr("Plane-wave cutoff (ENCUT):"), vaspEncutSpin_);
    vaspXcCombo_ = new QComboBox(vaspGroup_);
    vaspXcCombo_->addItems({QStringLiteral("PBE"), QStringLiteral("PBEsol"),
                            QStringLiteral("LDA"), QStringLiteral("SCAN")});
    vaspXcCombo_->setToolTip(
        tr("Exchange-correlation functional. LEPSILON is a semilocal "
           "linear-response path; a hybrid functional is not supported by it."));
    vaspForm->addRow(tr("XC functional:"), vaspXcCombo_);
    layout->addWidget(vaspGroup_);
    connect(vaspEncutSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(vaspXcCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    espressoGroup_ = new QGroupBox(tr("Quantum ESPRESSO Ground State"), page);
    auto* qeForm = new QFormLayout(espressoGroup_);
    auto* qeNote = new QLabel(
        tr("Self-contained, and the cheapest of the three: one <code>ph.x</code> "
           "run at q = 0 returns the force constants, Z*, ε∞ and — with "
           "<code>lraman</code> — the Raman tensor as an analytic third-order "
           "response, so there is no displacement amplitude to trade off "
           "against SCF noise.<br><br>"
           "<b>The Raman half needs NORM-CONSERVING pseudopotentials.</b> "
           "<code>lraman</code> is not implemented for ultrasoft or PAW sets; "
           "<code>ph.x</code> declines rather than approximating, and the run "
           "stops with that message. The infrared half is unaffected. Edit the "
           "pseudopotential map in the generated script before running."),
        espressoGroup_);
    qeNote->setWordWrap(true);
    qeNote->setTextFormat(Qt::RichText);
    qeForm->addRow(qeNote);
    qeEcutwfcSpin_ = new QDoubleSpinBox(espressoGroup_);
    qeEcutwfcSpin_->setRange(10.0, 400.0);
    qeEcutwfcSpin_->setDecimals(0);
    qeEcutwfcSpin_->setValue(80.0);
    qeEcutwfcSpin_->setSuffix(tr(" Ry"));
    qeEcutwfcSpin_->setToolTip(
        tr("ecutwfc — the wavefunction cutoff, in Rydberg (what pw.x reads). "
           "DFPT wants it converged more tightly than a total energy does."));
    qeForm->addRow(tr("Wavefunction cutoff (ecutwfc):"), qeEcutwfcSpin_);
    qeEcutrhoSpin_ = new QDoubleSpinBox(espressoGroup_);
    qeEcutrhoSpin_->setRange(0.0, 3200.0);
    qeEcutrhoSpin_->setDecimals(0);
    qeEcutrhoSpin_->setValue(0.0);
    qeEcutrhoSpin_->setSpecialValueText(tr("auto (4 × ecutwfc)"));
    qeEcutrhoSpin_->setSuffix(tr(" Ry"));
    qeEcutrhoSpin_->setToolTip(
        tr("ecutrho — the charge-density cutoff. \"auto\" leaves QE's 4 × "
           "ecutwfc default, which is right for the norm-conserving sets this "
           "module's Raman half requires anyway."));
    qeForm->addRow(tr("Density cutoff (ecutrho):"), qeEcutrhoSpin_);
    layout->addWidget(espressoGroup_);
    connect(qeEcutwfcSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(qeEcutrhoSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    // The k-grid means the same thing to both self-contained engines, so it
    // lives in one group serving both rather than as a copy inside each.
    samplingGroup_ = new QGroupBox(tr("Brillouin-Zone Sampling"), page);
    auto* samplingForm = new QFormLayout(samplingGroup_);
    auto* kptRow = new QHBoxLayout;
    for (int axis = 0; axis < 3; ++axis) {
        kptSpins_[axis] = new QSpinBox(samplingGroup_);
        kptSpins_[axis]->setRange(1, 64);
        kptSpins_[axis]->setValue(7);
        kptSpins_[axis]->setToolTip(
            tr("Monkhorst-Pack divisions for the ground state the response is "
               "built on. Z* and ε∞ are Brillouin-zone integrals and converge "
               "more slowly with k-points than the total energy does — an "
               "underconverged mesh shows up as an acoustic sum-rule residual "
               "rather than as an error."));
        kptRow->addWidget(kptSpins_[axis]);
        connect(kptSpins_[axis], &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    }
    kptRow->addStretch(1);
    samplingForm->addRow(tr("k-point grid:"), kptRow);
    layout->addWidget(samplingGroup_);

    // -- What to compute ----------------------------------------------------
    auto* methodGroup = new QGroupBox(tr("Method"), page);
    auto* methodForm = new QFormLayout(methodGroup);
    methodForm_ = methodForm;

    ramanCheck_ = new QCheckBox(tr("Compute the Raman spectrum"), methodGroup);
    ramanCheck_->setChecked(true);
    ramanCheck_->setToolTip(
        tr("The IR spectrum needs only the force constants and the inherited "
           "Z*. The Raman spectrum additionally evaluates the static "
           "dielectric tensor at all 6N displaced geometries, which dominates "
           "the run time.\n\n"
           "Turn it off for an IR-only job — the difference is typically a "
           "factor of three to five."));
    methodForm->addRow(QString(), ramanCheck_);
    connect(ramanCheck_, &QCheckBox::toggled, this, [this] {
        updateCostEstimate();
        refreshPreview();
    });

    displacementSpin_ = new QDoubleSpinBox(methodGroup);
    displacementSpin_->setRange(0.001, 0.20);
    displacementSpin_->setDecimals(3);
    displacementSpin_->setSingleStep(0.005);
    displacementSpin_->setValue(0.01);
    displacementSpin_->setSuffix(tr(" Å"));
    displacementSpin_->setToolTip(
        tr("Amplitude of the ± displacement used for both the force constants "
           "and the polarizability derivative.\n\n"
           "Squeezed between two errors: too large leaves the harmonic / "
           "linear regime the derivatives are defined in, too small buries the "
           "differences in SCF noise."));
    methodForm->addRow(tr("Displacement δ:"), displacementSpin_);
    connect(displacementSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    costLabel_ = new QLabel(methodGroup);
    costLabel_->setWordWrap(true);
    costLabel_->setTextFormat(Qt::RichText);
    methodForm->addRow(QString(), costLabel_);
    layout->addWidget(methodGroup);

    // -- Spectrum ------------------------------------------------------------
    // These change only the REPORTED spectrum, not the physics: the mode
    // frequencies and activities are computed once and the experiment-specific
    // factors applied afterwards, so a different laser line does not mean a
    // different run.
    auto* spectrumGroup = new QGroupBox(tr("Spectrum"), page);
    auto* spectrumForm = new QFormLayout(spectrumGroup);

    laserSpin_ = new QDoubleSpinBox(spectrumGroup);
    laserSpin_->setRange(200.0, 2000.0);
    laserSpin_->setDecimals(1);
    laserSpin_->setValue(532.0);
    laserSpin_->setSuffix(tr(" nm"));
    laserSpin_->setToolTip(
        tr("Excitation wavelength, through the (ω_L − ω)⁴ scattering "
           "prefactor of the Stokes intensity. 532 nm is the usual green "
           "line.\n\n"
           "It scales the reported intensities; the mode activities "
           "themselves are laser-independent and are reported separately."));
    spectrumForm->addRow(tr("Laser wavelength:"), laserSpin_);

    temperatureSpin_ = new QDoubleSpinBox(spectrumGroup);
    temperatureSpin_->setRange(0.0, 3000.0);
    temperatureSpin_->setDecimals(1);
    temperatureSpin_->setValue(300.0);
    temperatureSpin_->setSuffix(tr(" K"));
    temperatureSpin_->setToolTip(
        tr("Sample temperature, through the Bose occupation factor n(ω) + 1 "
           "of the Stokes intensity."));
    spectrumForm->addRow(tr("Temperature:"), temperatureSpin_);

    broadeningSpin_ = new QDoubleSpinBox(spectrumGroup);
    broadeningSpin_->setRange(0.1, 100.0);
    broadeningSpin_->setDecimals(1);
    broadeningSpin_->setValue(4.0);
    broadeningSpin_->setSuffix(tr(" cm⁻¹"));
    broadeningSpin_->setToolTip(
        tr("Lorentzian half-width each mode is drawn with. Purely "
           "presentational — the discrete mode list is written to the results "
           "file unbroadened."));
    spectrumForm->addRow(tr("Broadening:"), broadeningSpin_);

    freqMinSpin_ = new QDoubleSpinBox(spectrumGroup);
    freqMinSpin_->setRange(0.0, 10000.0);
    freqMinSpin_->setDecimals(0);
    freqMinSpin_->setValue(0.0);
    freqMinSpin_->setSuffix(tr(" cm⁻¹"));
    spectrumForm->addRow(tr("Frequency from:"), freqMinSpin_);

    freqMaxSpin_ = new QDoubleSpinBox(spectrumGroup);
    freqMaxSpin_->setRange(10.0, 10000.0);
    freqMaxSpin_->setDecimals(0);
    freqMaxSpin_->setValue(1600.0);
    freqMaxSpin_->setSuffix(tr(" cm⁻¹"));
    spectrumForm->addRow(tr("Frequency to:"), freqMaxSpin_);

    npointsSpin_ = new QSpinBox(spectrumGroup);
    npointsSpin_->setRange(64, 20000);
    npointsSpin_->setValue(1600);
    spectrumForm->addRow(tr("Grid points:"), npointsSpin_);

    for (QDoubleSpinBox* spin : {laserSpin_, temperatureSpin_, broadeningSpin_,
                                 freqMinSpin_, freqMaxSpin_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    }
    connect(npointsSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    layout->addWidget(spectrumGroup);

    layout->addStretch(1);
    // Initial visibility only — the full onEngineChanged() also refreshes the
    // preview and syncs the base engine combo, neither of which exists yet
    // while this page is being built (it is constructed first).
    vaspGroup_->setVisible(false);
    espressoGroup_->setVisible(false);
    samplingGroup_->setVisible(false);
    return page;
}

core::CalculatorKind RamanIrWizard::selectedEngine() const
{
    return engineCombo_ ? static_cast<core::CalculatorKind>(
               engineCombo_->currentData().toInt())
                        : core::CalculatorKind::Gpaw;
}

void RamanIrWizard::onEngineChanged()
{
    const core::CalculatorKind engine = selectedEngine();
    const bool gpaw = engine == core::CalculatorKind::Gpaw;
    const bool espresso = engine == core::CalculatorKind::QuantumEspresso;
    // Keep the base class's (hidden) engine selection in step, so the launch
    // command template, the calculator provenance and the interpreter all
    // resolve for the engine actually chosen here.
    selectCalculator(engine);

    if (sourcesGroup_)
        sourcesGroup_->setVisible(gpaw);
    if (vaspGroup_)
        vaspGroup_->setVisible(engine == core::CalculatorKind::Vasp);
    if (espressoGroup_)
        espressoGroup_->setVisible(espresso);
    if (samplingGroup_)
        samplingGroup_->setVisible(!gpaw);

    // Quantum ESPRESSO's answer is an analytic derivative, so there is no
    // displacement to choose. Leaving the control visible would be offering a
    // knob the generated script does not read.
    if (methodForm_ && displacementSpin_) {
        int row = -1;
        QFormLayout::ItemRole role{};
        methodForm_->getWidgetPosition(displacementSpin_, &row, &role);
        if (row >= 0)
            methodForm_->setRowVisible(row, !espresso);
    }

    updateCostEstimate();
    refreshPreview();
}

void RamanIrWizard::updateCostEstimate()
{
    if (!costLabel_)
        return;
    const int atoms = structure_ ? static_cast<int>(structure_->size()) : 0;
    if (atoms <= 0) {
        costLabel_->setText(tr("<i>No structure loaded.</i>"));
        return;
    }
    const int displacements = 6 * atoms;
    const bool raman = ramanCheck_ && ramanCheck_->isChecked();
    // State the real cost. It differs between the engines by orders of
    // magnitude, not by a little, and that is the decision this page exists to
    // inform — a user who reads "one run" for QE and "6N runs" for VASP has
    // learned the thing that actually decides which to use.
    switch (selectedEngine()) {
    case core::CalculatorKind::QuantumEspresso:
        costLabel_->setText(
            raman
                ? tr("<b>%1 atoms</b> → <b>one</b> ph.x run. The force "
                     "constants, Z*, ε∞ and the Raman tensor all come out of "
                     "the same linear response.<br>"
                     "<i>Needs norm-conserving pseudopotentials for the Raman "
                     "half.</i>")
                      .arg(atoms)
                : tr("<b>%1 atoms</b> → <b>one</b> ph.x run for the force "
                     "constants and Z*. Turning Raman off saves nothing here: "
                     "ph.x returns the Raman tensor from the run it was doing "
                     "anyway.")
                      .arg(atoms));
        return;
    case core::CalculatorKind::Vasp:
        costLabel_->setText(
            raman
                ? tr("<b>%1 atoms</b> → one DFPT run for the force constants "
                     "and Z*, plus <b>%2 displaced LEPSILON runs</b> for "
                     "∂α/∂u.<br>"
                     "<i>The Raman half is the entire cost of this job — VASP "
                     "computes no Raman tensor of its own.</i>")
                      .arg(atoms)
                      .arg(displacements)
                : tr("<b>%1 atoms</b> → <b>one</b> DFPT run. IBRION=8 with "
                     "LEPSILON returns the force constants, every Z* and ε∞ "
                     "together, so the infrared spectrum needs nothing else.")
                      .arg(atoms));
        return;
    default:
        break;
    }
    if (raman) {
        costLabel_->setText(
            tr("<b>%1 atoms</b> → %2 displaced force evaluations for the "
               "Hessian, plus %2 self-consistent runs each followed by six "
               "dielectric evaluations for ∂α/∂Q.<br>"
               "<i>The Raman half dominates: budget accordingly.</i>")
                .arg(atoms)
                .arg(displacements));
        return;
    }
    costLabel_->setText(
        tr("<b>%1 atoms</b> → %2 displaced force evaluations for the Hessian. "
           "The Z* tensors are inherited, so no further self-consistent work "
           "is needed for the IR spectrum.")
            .arg(atoms)
            .arg(displacements));
}

void RamanIrWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    baselineCombo_->clear();
    for (const auto& [label, path] : baselines)
        baselineCombo_->addItem(label, path);
    // With no .gpw the GPAW route has nothing to displace about. Steering to a
    // self-contained engine beats refusing to open the module: VASP and QE do
    // not need a baseline at all, and a user with neither a .gpw nor those
    // codes learns that from the disabled entry's tooltip rather than from a
    // dialog that simply says no.
    if (baselines.isEmpty() && engineCombo_) {
        const int gpawIndex = engineCombo_->findData(
            static_cast<int>(core::CalculatorKind::Gpaw));
        if (auto* model =
                qobject_cast<QStandardItemModel*>(engineCombo_->model());
            model && gpawIndex >= 0) {
            model->item(gpawIndex)->setEnabled(false);
            engineCombo_->setItemData(
                gpawIndex,
                tr("Needs a completed GPAW Single-Point Calculation that saved "
                   "its wavefunctions (.gpw) — run one first, or use VASP / "
                   "Quantum ESPRESSO, which converge their own ground state."),
                Qt::ToolTipRole);
        }
        engineCombo_->setCurrentIndex(engineCombo_->findData(
            static_cast<int>(core::CalculatorKind::Vasp)));
    }
    onBaselineChanged();
}

void RamanIrWizard::setBornChargesResults(
    const QList<QPair<QString, QString>>& results)
{
    if (!bornCombo_)
        return;
    bornCombo_->clear();
    // The "none" entry is re-added here, not just at construction: clear()
    // drops it, and without it a user who HAS Born runs available could no
    // longer choose to skip the IR column.
    bornCombo_->addItem(tr("(none — Raman and phonons only, no IR)"), QString());
    for (const auto& [label, path] : results)
        bornCombo_->addItem(label, path);
    // Default to a real Z* set when one exists: the module computes both
    // spectra by preference, and a user who opened it with a Born run already
    // finished almost certainly wants the IR column.
    if (!results.isEmpty())
        bornCombo_->setCurrentIndex(1);
    refreshPreview();
}

void RamanIrWizard::setOpticsResults(
    const QList<QPair<QString, QString>>& results)
{
    if (!opticsCombo_)
        return;
    for (const auto& [label, path] : results)
        opticsCombo_->addItem(label, path);
    refreshPreview();
}

void RamanIrWizard::onBaselineChanged()
{
    inherited_ = applyBaselineProvenance(baselineCombo_, inheritanceNote_);
    updateCostEstimate();
    refreshPreview();
}

QString RamanIrWizard::pythonExecutable() const
{
    // The baseline's own interpreter binds the GPAW route only — the
    // self-contained engines have no baseline and resolve through the standard
    // per-engine mapping.
    if (selectedEngine() == core::CalculatorKind::Gpaw && inherited_
        && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

core::RamanIrConfig RamanIrWizard::config() const
{
    const core::CalculatorKind engine = selectedEngine();
    core::RamanIrConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    cfg.calculator.calculator = engine;
    cfg.calculator.task = core::TaskKind::SinglePoint;
    // The inherited-run selections describe the GPAW route only. Carrying them
    // into a self-contained run would put a path in the script that nothing
    // there reads — and, in the Born-charges case, would suggest an inherited
    // Z* where the run computes its own.
    if (engine == core::CalculatorKind::Gpaw) {
        if (baselineCombo_)
            cfg.baselinePath =
                baselineCombo_->currentData().toString().toStdString();
        if (bornCombo_)
            cfg.bornChargesPath =
                bornCombo_->currentData().toString().toStdString();
        if (opticsCombo_)
            cfg.opticsPath =
                opticsCombo_->currentData().toString().toStdString();
    } else {
        for (int axis = 0; axis < 3; ++axis)
            if (kptSpins_[axis])
                cfg.calculator.kpts[axis] = kptSpins_[axis]->value();
    }
    if (engine == core::CalculatorKind::Vasp) {
        if (vaspEncutSpin_)
            cfg.calculator.planeWaveCutoffEv = vaspEncutSpin_->value();
        if (vaspXcCombo_)
            cfg.calculator.vaspXc = vaspXcCombo_->currentText().toStdString();
        cfg.calculator.vaspPotcarPath = vaspPotcarDirectory().toStdString();
    } else if (engine == core::CalculatorKind::QuantumEspresso) {
        if (qeEcutwfcSpin_)
            cfg.calculator.qeEcutwfcRy = qeEcutwfcSpin_->value();
        if (qeEcutrhoSpin_)
            cfg.calculator.qeEcutrhoRy = qeEcutrhoSpin_->value();
        cfg.calculator.espressoPseudoDir =
            espressoPseudoDirectory().toStdString();
    }
    cfg.computeRaman = ramanCheck_ && ramanCheck_->isChecked();
    cfg.displacement = displacementSpin_ ? displacementSpin_->value() : 0.01;
    cfg.laserWavelengthNm = laserSpin_ ? laserSpin_->value() : 532.0;
    cfg.temperatureK = temperatureSpin_ ? temperatureSpin_->value() : 300.0;
    cfg.broadeningCm = broadeningSpin_ ? broadeningSpin_->value() : 4.0;
    cfg.frequencyMinCm = freqMinSpin_ ? freqMinSpin_->value() : 0.0;
    cfg.frequencyMaxCm = freqMaxSpin_ ? freqMaxSpin_->value() : 1600.0;
    cfg.npoints = npointsSpin_ ? npointsSpin_->value() : 1600;
    return cfg;
}

QString RamanIrWizard::generateScript() const
{
    return QString::fromStdString(core::generateRamanIrScript(config()));
}

} // namespace calango::gui
