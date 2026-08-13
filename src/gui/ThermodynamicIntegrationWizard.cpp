#include "gui/ThermodynamicIntegrationWizard.hpp"

#include "core/Structure.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/SettingsManager.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace calango::gui {

using core::TiLambdaSchedule;
using core::TiQuadrature;
using core::TiReference;

ThermodynamicIntegrationWizard::ThermodynamicIntegrationWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : GpawElectronicWizard(parent)
    , structure_(std::move(structure))
{
    // One directory per RUN, fixed at construction: every job of a split run
    // writes its per-window files here, and it is the only thing they share.
    // Fixed here rather than derived per script so that regenerating the
    // preview does not silently point half the jobs at a different directory.
    resultsDirectory_ =
        QDir(SettingsManager::simulationsDirectory())
            .filePath(QStringLiteral("ti_%1")
                          .arg(QDateTime::currentDateTime().toString(
                              QStringLiteral("yyyyMMdd_hhmmss"))));
    buildUi();
    updateReferenceRows();
    updateEnsembleRows();
}

QString ThermodynamicIntegrationWizard::wizardTitle() const
{
    return tr("Thermodynamic Integration — Free Energy");
}

QString ThermodynamicIntegrationWizard::settingsHeader() const
{
    return tr("Integration Path & Sampling");
}

QStringList ThermodynamicIntegrationWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* ThermodynamicIntegrationWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form_ = form;

    // ---- Reference system -------------------------------------------------
    referenceCombo_ = new QComboBox(page);
    referenceCombo_->setObjectName(QStringLiteral("tiReferenceCombo"));
    // Item data carries the enum, so the combo order is free to change without
    // silently re-mapping onto a different reference system.
    referenceCombo_->addItem(tr("Ideal gas (Sackur-Tetrode) — liquids"),
                             static_cast<int>(TiReference::IdealGas));
    referenceCombo_->addItem(tr("Einstein crystal (Frenkel-Ladd) — solids"),
                             static_cast<int>(TiReference::EinsteinCrystal));
    referenceCombo_->addItem(tr("Lennard-Jones fluid"),
                             static_cast<int>(TiReference::LennardJonesFluid));
    referenceCombo_->setToolTip(
        tr("The system the path starts from. Its free energy has to be known "
           "in CLOSED FORM, or the absolute number at the end is not "
           "absolute.\n\n"
           "Ideal gas: exact, and the natural reference for a liquid. It is "
           "also the one with the worst endpoint behaviour — nothing keeps "
           "atoms apart at λ → 0.\n\n"
           "Einstein crystal: exact, and free of the endpoint singularity "
           "because every atom is tethered to its lattice site. The standard "
           "solid reference.\n\n"
           "Lennard-Jones fluid: NO exact closed form exists at liquid "
           "density. Calango evaluates it from the exact second virial "
           "coefficient in the dilute regime only, and otherwise refuses "
           "rather than quoting an unverifiable equation-of-state fit."));
    form->addRow(tr("Reference system:"), referenceCombo_);
    connect(referenceCombo_, &QComboBox::currentIndexChanged, this,
            &ThermodynamicIntegrationWizard::updateReferenceRows);

    referenceNote_ = new QLabel(page);
    referenceNote_->setWordWrap(true);
    referenceNote_->setTextFormat(Qt::RichText);
    form->addRow(referenceNote_);

    einsteinSpringSpin_ = new QDoubleSpinBox(page);
    einsteinSpringSpin_->setRange(0.001, 1000.0);
    einsteinSpringSpin_->setDecimals(3);
    einsteinSpringSpin_->setValue(1.0);
    einsteinSpringSpin_->setSuffix(tr(" eV/Å²"));
    einsteinSpringSpin_->setToolTip(
        tr("Stiffness of the harmonic tether. A real choice, not a detail: too "
           "soft and the tethered crystal melts, which breaks the "
           "reversibility of the path; too stiff and ⟨U_target − U_ref⟩ is "
           "enormous at λ → 1 and the quadrature loses precision. The usual "
           "recipe is to match the mean-squared displacement of the real "
           "crystal at this temperature."));
    form->addRow(tr("Einstein spring constant:"), einsteinSpringSpin_);

    einsteinFixComCheck_ = new QCheckBox(
        tr("Hold the centre of mass fixed (and apply the finite-size "
           "correction)"),
        page);
    einsteinFixComCheck_->setChecked(true);
    einsteinFixComCheck_->setToolTip(
        tr("A rigid translation of the whole crystal costs the target nothing "
           "and is quadratically penalized by the springs, so the two "
           "Hamiltonians differ by a soft mode the tether kills. Fixing the "
           "centre of mass removes it. The matching O(ln N) finite-size term "
           "is then part of the closed-form reference free energy — this "
           "checkbox drives BOTH the constraint in the run and the correction "
           "in the assembly, so they cannot disagree."));
    form->addRow(QString(), einsteinFixComCheck_);

    ljEpsilonSpin_ = new QDoubleSpinBox(page);
    ljEpsilonSpin_->setRange(1.0e-5, 10.0);
    ljEpsilonSpin_->setDecimals(5);
    ljEpsilonSpin_->setValue(0.0104);
    ljEpsilonSpin_->setSuffix(tr(" eV"));
    form->addRow(tr("Lennard-Jones ε:"), ljEpsilonSpin_);

    ljSigmaSpin_ = new QDoubleSpinBox(page);
    ljSigmaSpin_->setRange(0.1, 20.0);
    ljSigmaSpin_->setDecimals(3);
    ljSigmaSpin_->setValue(3.4);
    ljSigmaSpin_->setSuffix(tr(" Å"));
    form->addRow(tr("Lennard-Jones σ:"), ljSigmaSpin_);

    ljCutoffSpin_ = new QDoubleSpinBox(page);
    ljCutoffSpin_->setRange(1.0, 50.0);
    ljCutoffSpin_->setDecimals(2);
    ljCutoffSpin_->setValue(10.0);
    ljCutoffSpin_->setSuffix(tr(" Å"));
    form->addRow(tr("Lennard-Jones cut-off:"), ljCutoffSpin_);

    // ---- The λ path -------------------------------------------------------
    scheduleCombo_ = new QComboBox(page);
    scheduleCombo_->setObjectName(QStringLiteral("tiScheduleCombo"));
    scheduleCombo_->addItem(
        tr("Gauss-Legendre nodes (interior — avoids both endpoints)"),
        static_cast<int>(TiLambdaSchedule::GaussLegendre));
    scheduleCombo_->addItem(tr("Uniform (includes λ = 0 and λ = 1)"),
                            static_cast<int>(TiLambdaSchedule::Uniform));
    scheduleCombo_->addItem(tr("Power law — clustered towards λ = 0"),
                            static_cast<int>(TiLambdaSchedule::PowerLaw));
    scheduleCombo_->addItem(tr("Clustered at both ends"),
                            static_cast<int>(TiLambdaSchedule::ClusteredEnds));
    scheduleCombo_->setToolTip(
        tr("Where the λ windows sit. This is a PHYSICS choice, not a numerical "
           "one: with linear coupling and a target that has a hard repulsive "
           "core, ⟨∂U/∂λ⟩ diverges as λ → 0 against an ideal gas, and a "
           "uniform grid integrates that divergence without noticing.\n\n"
           "Gauss-Legendre never evaluates λ = 0 or λ = 1 and already clusters "
           "towards both ends, which is why it is the default."));
    form->addRow(tr("λ schedule:"), scheduleCombo_);
    connect(scheduleCombo_, &QComboBox::currentIndexChanged, this,
            &ThermodynamicIntegrationWizard::updateReferenceRows);

    scheduleExponentSpin_ = new QDoubleSpinBox(page);
    scheduleExponentSpin_->setRange(1.0, 8.0);
    scheduleExponentSpin_->setDecimals(2);
    scheduleExponentSpin_->setSingleStep(0.25);
    scheduleExponentSpin_->setValue(2.0);
    scheduleExponentSpin_->setToolTip(
        tr("How hard the schedule bunches its windows. 1 is a uniform grid for "
           "either clustering law; larger values push windows towards the "
           "endpoint(s). Ignored by the Uniform and Gauss-Legendre schedules, "
           "which have no free parameter."));
    form->addRow(tr("Clustering exponent:"), scheduleExponentSpin_);
    connect(scheduleExponentSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double) { refreshPathSummary(); });

    quadratureCombo_ = new QComboBox(page);
    quadratureCombo_->setObjectName(QStringLiteral("tiQuadratureCombo"));
    quadratureCombo_->addItem(tr("Gauss-Legendre"),
                              static_cast<int>(TiQuadrature::GaussLegendre));
    quadratureCombo_->addItem(tr("Simpson (uniform grid only)"),
                              static_cast<int>(TiQuadrature::Simpson));
    quadratureCombo_->addItem(tr("Trapezoid"),
                              static_cast<int>(TiQuadrature::Trapezoid));
    quadratureCombo_->setToolTip(
        tr("The rule that turns the per-window averages into ΔF. Gauss-"
           "Legendre needs the Gauss-Legendre nodes and is refused on any "
           "other grid rather than silently reweighted; Simpson needs a "
           "uniform grid and falls back to the trapezoid on anything else, "
           "saying so."));
    form->addRow(tr("Quadrature:"), quadratureCombo_);
    connect(quadratureCombo_, &QComboBox::currentIndexChanged, this,
            &ThermodynamicIntegrationWizard::updateReferenceRows);

    windowsSpin_ = new QSpinBox(page);
    windowsSpin_->setObjectName(QStringLiteral("tiWindowsSpin"));
    windowsSpin_->setRange(2, 200);
    windowsSpin_->setValue(12);
    windowsSpin_->setToolTip(
        tr("Number of λ windows. Each is an independent MD run, so this "
           "multiplies the cost — and it is also the knob that fixes the "
           "quadrature error, which Calango reports separately from the "
           "statistical one so you can tell which of the two to spend on."));
    form->addRow(tr("λ windows:"), windowsSpin_);
    connect(windowsSpin_, &QSpinBox::valueChanged, this,
            [this](int) { updateReferenceRows(); });

    jobsSpin_ = new QSpinBox(page);
    jobsSpin_->setObjectName(QStringLiteral("tiJobsSpin"));
    jobsSpin_->setRange(1, 200);
    jobsSpin_->setValue(1);
    jobsSpin_->setToolTip(
        tr("Split the windows across this many jobs. The windows are "
           "independent, so they can be dispatched separately; every job "
           "writes into the same results directory and whichever finishes last "
           "assembles the summary.\n\n"
           "A job that dies leaves its windows missing, and the run is then "
           "reported as INCOMPLETE with no free energy — an integral over the "
           "surviving windows is a different integral, not a noisier one.\n\n"
           "Splitting withdraws the hysteresis check: reversing the sweep "
           "needs one sequential chain of windows."));
    form->addRow(tr("Dispatch as separate jobs:"), jobsSpin_);
    connect(jobsSpin_, &QSpinBox::valueChanged, this,
            [this](int) { updateReferenceRows(); });

    hysteresisCheck_ = new QCheckBox(
        tr("Also sweep λ backwards (hysteresis check)"), page);
    hysteresisCheck_->setObjectName(QStringLiteral("tiHysteresisCheck"));
    hysteresisCheck_->setToolTip(
        tr("Traverse the path in both directions and compare. A reversible "
           "path must give the same ΔF either way; it will not if the windows "
           "were under-equilibrated, if the system changed phase somewhere "
           "along the path, or if the coupling drove it through a barrier. "
           "None of those looks like noise, and this is the standard way to "
           "see them.\n\nDoubles the cost, and needs one sequential chain of "
           "windows — so it is withdrawn when the run is split across jobs."));
    form->addRow(QString(), hysteresisCheck_);
    connect(hysteresisCheck_, &QCheckBox::toggled, this,
            [this](bool) { refreshPathSummary(); });

    // ---- Dynamics ---------------------------------------------------------
    ensembleCombo_ = new QComboBox(page);
    ensembleCombo_->setObjectName(QStringLiteral("tiEnsembleCombo"));
    // NVE is absent on purpose, not by oversight: ⟨∂U/∂λ⟩ has to be a CANONICAL
    // average at a fixed temperature. A microcanonical window samples a
    // different ensemble at every λ (the coupling changes the potential energy,
    // so the kinetic energy — and therefore the temperature — moves with it),
    // and the integral over such a path is not a free-energy difference at any
    // single T.
    ensembleCombo_->addItem(tr("NVT — Langevin"),
                            static_cast<int>(core::MdEnsemble::LangevinNVT));
    ensembleCombo_->addItem(tr("NVT — Andersen"),
                            static_cast<int>(core::MdEnsemble::AndersenNVT));
    ensembleCombo_->addItem(tr("NVT — Berendsen"),
                            static_cast<int>(core::MdEnsemble::BerendsenNVT));
    ensembleCombo_->addItem(
        tr("NVT — Nosé-Hoover chain"),
        static_cast<int>(core::MdEnsemble::NoseHooverChainNVT));
    ensembleCombo_->addItem(tr("NPT — Berendsen"),
                            static_cast<int>(core::MdEnsemble::BerendsenNPT));
    ensembleCombo_->addItem(
        tr("NPT — Parrinello-Rahman (Nosé-Hoover)"),
        static_cast<int>(core::MdEnsemble::MelchionnaNPT));
    ensembleCombo_->setToolTip(
        tr("The ensemble each window samples in. NVE is not offered: "
           "⟨∂U/∂λ⟩ must be a canonical average at ONE temperature, and a "
           "microcanonical window sits at a different temperature for every "
           "λ.\n\nNVT is the usual choice even for a liquid at a given "
           "pressure: equilibrate at NPT first, then integrate at the "
           "resulting volume. Under NPT the cell breathes, and the reference "
           "free energy — which is a function of V — no longer has a single "
           "volume to be evaluated at."));
    form->addRow(tr("Ensemble:"), ensembleCombo_);
    connect(ensembleCombo_, &QComboBox::currentIndexChanged, this,
            &ThermodynamicIntegrationWizard::updateEnsembleRows);

    temperatureSpin_ = new QDoubleSpinBox(page);
    temperatureSpin_->setRange(1.0, 100000.0);
    temperatureSpin_->setValue(300.0);
    temperatureSpin_->setSuffix(tr(" K"));
    form->addRow(tr("Temperature:"), temperatureSpin_);
    connect(temperatureSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double) { refreshPathSummary(); });

    pressureSpin_ = new QDoubleSpinBox(page);
    pressureSpin_->setRange(0.0, 1.0e7);
    pressureSpin_->setDecimals(3);
    pressureSpin_->setValue(1.0);
    pressureSpin_->setSuffix(tr(" bar"));
    pressureSpin_->setToolTip(
        tr("External pressure. Used by the NPT barostat, and — always — for "
           "the PV term of G = F + PV."));
    form->addRow(tr("Pressure:"), pressureSpin_);

    timestepSpin_ = new QDoubleSpinBox(page);
    timestepSpin_->setRange(0.01, 20.0);
    timestepSpin_->setValue(1.0);
    timestepSpin_->setSuffix(tr(" fs"));
    form->addRow(tr("Time step:"), timestepSpin_);
    connect(timestepSpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double) { refreshPathSummary(); });

    frictionSpin_ = new QDoubleSpinBox(page);
    frictionSpin_->setDecimals(4);
    frictionSpin_->setRange(0.0001, 10.0);
    frictionSpin_->setSingleStep(0.005);
    frictionSpin_->setValue(0.01);
    frictionSpin_->setSuffix(tr(" fs⁻¹"));
    form->addRow(tr("Friction (Langevin):"), frictionSpin_);

    tautSpin_ = new QDoubleSpinBox(page);
    tautSpin_->setRange(1.0, 100000.0);
    tautSpin_->setValue(100.0);
    tautSpin_->setSuffix(tr(" fs"));
    form->addRow(tr("Thermostat coupling:"), tautSpin_);

    taupSpin_ = new QDoubleSpinBox(page);
    taupSpin_->setRange(1.0, 1000000.0);
    taupSpin_->setValue(1000.0);
    taupSpin_->setSuffix(tr(" fs"));
    form->addRow(tr("Barostat coupling:"), taupSpin_);

    equilibrationSpin_ = new QSpinBox(page);
    equilibrationSpin_->setObjectName(QStringLiteral("tiEquilibrationSpin"));
    equilibrationSpin_->setRange(0, 100000000);
    equilibrationSpin_->setValue(2000);
    equilibrationSpin_->setToolTip(
        tr("Steps discarded at the start of each window, before any sample is "
           "taken.\n\nNot optional in practice. Averaging over the "
           "equilibration transient biases every window in the SAME direction "
           "— each one is still relaxing towards its own λ-coupled ensemble — "
           "so the bias survives the λ integral instead of cancelling, and it "
           "looks nothing like noise on a plot."));
    form->addRow(tr("Equilibration steps / window:"), equilibrationSpin_);
    connect(equilibrationSpin_, &QSpinBox::valueChanged, this,
            [this](int) { refreshPathSummary(); });

    productionSpin_ = new QSpinBox(page);
    productionSpin_->setObjectName(QStringLiteral("tiProductionSpin"));
    productionSpin_->setRange(10, 100000000);
    productionSpin_->setValue(10000);
    form->addRow(tr("Production steps / window:"), productionSpin_);
    connect(productionSpin_, &QSpinBox::valueChanged, this,
            [this](int) { refreshPathSummary(); });

    sampleSpin_ = new QSpinBox(page);
    sampleSpin_->setRange(1, 1000000);
    sampleSpin_->setValue(10);
    sampleSpin_->setToolTip(
        tr("Record ⟨∂U/∂λ⟩ every N production steps. The whole series is "
           "written out, not just its mean: the autocorrelation time — and so "
           "the only honest error bar — cannot be recovered from moments."));
    form->addRow(tr("Sampling interval:"), sampleSpin_);
    connect(sampleSpin_, &QSpinBox::valueChanged, this,
            [this](int) { refreshPathSummary(); });

    endpointWarning_ = new QLabel(page);
    endpointWarning_->setWordWrap(true);
    endpointWarning_->setTextFormat(Qt::RichText);
    form->addRow(endpointWarning_);

    pathSummary_ = new QLabel(page);
    pathSummary_->setWordWrap(true);
    pathSummary_->setTextFormat(Qt::RichText);
    form->addRow(pathSummary_);

    // The module's cold entry point. Setting up a run and reading a finished
    // one are the same errand often enough — "what did I get last time?" is
    // asked while choosing this time's windows — and the Simulation menu no
    // longer carries a second action for it, so the button lives where the
    // question is asked. The wizard only ASKS: it emits, and the host owns the
    // file dialog and the viewer.
    auto* loadResultsButton = new QPushButton(tr("Load Results…"), page);
    loadResultsButton->setObjectName(QStringLiteral("tiLoadResultsButton"));
    loadResultsButton->setToolTip(
        tr("Open the ti.json of a finished run — one from an earlier session, "
           "or brought back from a cluster — without launching anything"));
    // Not the default button: Return on this page belongs to the wizard's own
    // Next, and a file dialog appearing instead would be a trap.
    loadResultsButton->setAutoDefault(false);
    loadResultsButton->setDefault(false);
    connect(loadResultsButton, &QPushButton::clicked, this,
            &ThermodynamicIntegrationWizard::loadResultsRequested);
    form->addRow(QString(), loadResultsButton);

    updateReferenceRows();
    updateEnsembleRows();
    return page;
}

core::TiReference ThermodynamicIntegrationWizard::selectedReference() const
{
    return referenceCombo_
        ? static_cast<TiReference>(referenceCombo_->currentData().toInt())
        : TiReference::IdealGas;
}

core::TiLambdaSchedule
ThermodynamicIntegrationWizard::selectedSchedule() const
{
    return scheduleCombo_
        ? static_cast<TiLambdaSchedule>(scheduleCombo_->currentData().toInt())
        : TiLambdaSchedule::GaussLegendre;
}

core::TiQuadrature ThermodynamicIntegrationWizard::selectedQuadrature() const
{
    return quadratureCombo_
        ? static_cast<TiQuadrature>(quadratureCombo_->currentData().toInt())
        : TiQuadrature::GaussLegendre;
}

void ThermodynamicIntegrationWizard::updateReferenceRows()
{
    // pathSummary_ is the LAST widget the page builds, so testing it is a test
    // that the page is finished. Several of the controls below are connected to
    // this slot before the ones it reads exist, and this is the documented
    // crash shape in this codebase: a wizard whose constructor emitted a signal
    // into a slot reading widgets it had not created yet.
    if (!form_ || !referenceCombo_ || !pathSummary_)
        return;
    const TiReference reference = selectedReference();
    const bool einstein = reference == TiReference::EinsteinCrystal;
    const bool lj = reference == TiReference::LennardJonesFluid;

    form_->setRowVisible(einsteinSpringSpin_, einstein);
    form_->setRowVisible(einsteinFixComCheck_, einstein);
    form_->setRowVisible(ljEpsilonSpin_, lj);
    form_->setRowVisible(ljSigmaSpin_, lj);
    form_->setRowVisible(ljCutoffSpin_, lj);

    const TiLambdaSchedule schedule = selectedSchedule();
    scheduleExponentSpin_->setEnabled(
        schedule == TiLambdaSchedule::PowerLaw
        || schedule == TiLambdaSchedule::ClusteredEnds);

    // Gauss-Legendre quadrature is only defined on Gauss-Legendre nodes, so the
    // two controls are not independent. Rather than accept the combination and
    // let the core silently fall back to a trapezoid, the schedule wins and the
    // quadrature follows it — one decision, made once.
    if (schedule == TiLambdaSchedule::GaussLegendre) {
        quadratureCombo_->setCurrentIndex(
            quadratureCombo_->findData(
                static_cast<int>(TiQuadrature::GaussLegendre)));
        quadratureCombo_->setEnabled(false);
    } else {
        if (selectedQuadrature() == TiQuadrature::GaussLegendre)
            quadratureCombo_->setCurrentIndex(quadratureCombo_->findData(
                static_cast<int>(TiQuadrature::Simpson)));
        quadratureCombo_->setEnabled(true);
    }

    // Splitting withdraws hysteresis: a reversed sweep needs one sequential
    // chain of windows, and a job that owns a slice has no chain to reverse.
    const bool split = jobsSpin_ && jobsSpin_->value() > 1;
    hysteresisCheck_->setEnabled(!split);
    if (split && hysteresisCheck_->isChecked())
        hysteresisCheck_->setChecked(false);
    if (jobsSpin_ && windowsSpin_)
        jobsSpin_->setMaximum(std::max(1, windowsSpin_->value()));

    QString note;
    switch (reference) {
    case TiReference::IdealGas:
        note = tr("F/Nk<sub>B</sub>T = ln(ρΛ³) − 1, with Λ the thermal de "
                  "Broglie wavelength and the N! of indistinguishability "
                  "included exactly. No free parameters, and no caveat.");
        break;
    case TiReference::EinsteinCrystal:
        note = tr("F = 3Nk<sub>B</sub>T ln(βħω) with ω = √(α/m). Exact, and "
                  "free of the endpoint singularity — tethered atoms never "
                  "overlap.");
        break;
    case TiReference::LennardJonesFluid:
        note = tr("<b>No exact closed form exists.</b> Calango evaluates the "
                  "excess free energy from the EXACT second virial "
                  "coefficient, which is quantitative only for ρ* ≲ 0.05; at "
                  "liquid density it refuses and asks for an excess free "
                  "energy from an ideal-gas → LJ integration instead of "
                  "quoting a fit it cannot verify.");
        break;
    }
    referenceNote_->setText(note);

    refreshPathSummary();
}

void ThermodynamicIntegrationWizard::updateEnsembleRows()
{
    if (!ensembleCombo_ || !taupSpin_)
        return;
    const auto ensemble =
        static_cast<core::MdEnsemble>(ensembleCombo_->currentData().toInt());
    frictionSpin_->setEnabled(ensemble == core::MdEnsemble::LangevinNVT);
    const bool usesTaut = ensemble == core::MdEnsemble::BerendsenNVT
        || ensemble == core::MdEnsemble::NoseHooverChainNVT
        || ensemble == core::MdEnsemble::BerendsenNPT
        || ensemble == core::MdEnsemble::MelchionnaNPT;
    tautSpin_->setEnabled(usesTaut);
    taupSpin_->setEnabled(core::isConstantPressure(ensemble));
    refreshPathSummary();
}

void ThermodynamicIntegrationWizard::refreshPathSummary()
{
    if (!pathSummary_ || !endpointWarning_ || !windowsSpin_ || !hysteresisCheck_
        || !equilibrationSpin_ || !productionSpin_ || !sampleSpin_
        || !ensembleCombo_)
        return;

    const int windows = windowsSpin_->value();
    const auto lambdas = core::lambdaSchedule(selectedSchedule(), windows,
                                              scheduleExponentSpin_->value());
    QStringList shown;
    for (std::size_t i = 0; i < lambdas.size() && shown.size() < 4; ++i)
        shown << QString::number(lambdas[i], 'f', 4);
    if (lambdas.size() > 5)
        shown << QStringLiteral("…");
    if (!lambdas.empty() && lambdas.size() > 4)
        shown << QString::number(lambdas.back(), 'f', 4);

    const int sweeps = hysteresisCheck_->isChecked() ? 2 : 1;
    const double picoseconds = static_cast<double>(windows) * sweeps
        * (equilibrationSpin_->value() + productionSpin_->value())
        * timestepSpin_->value() * 1.0e-3;
    const long long samples = static_cast<long long>(productionSpin_->value())
        / std::max(1, sampleSpin_->value());

    pathSummary_->setText(
        tr("λ = %1 — %2 window(s)%3, %4 samples each, %5 ps of MD in total, "
           "across %6 job(s).")
            .arg(shown.join(QStringLiteral(", ")))
            .arg(windows)
            .arg(sweeps == 2 ? tr(" in each direction") : QString())
            .arg(samples)
            .arg(picoseconds, 0, 'f', picoseconds < 100.0 ? 1 : 0)
            .arg(jobCount()));

    // The one combination that produces a wrong answer while looking fine.
    QStringList problems;
    if (selectedReference() == TiReference::IdealGas && !lambdas.empty()
        && lambdas.front() <= 1.0e-12)
        problems
            << tr("This path samples <b>λ = 0 against an ideal gas</b>. There "
                  "the atoms are non-interacting and free to overlap, and the "
                  "target Hamiltonian charges an unbounded energy for it — "
                  "⟨∂U/∂λ⟩ diverges and the quadrature integrates the shoulder "
                  "of that divergence without any sign that it did. Use the "
                  "Gauss-Legendre or power-law schedule, or start from the "
                  "Einstein / Lennard-Jones reference instead.");
    if (equilibrationSpin_->value() == 0)
        problems << tr("With <b>no equilibration</b>, every window averages "
                       "over its own relaxation transient. That bias points "
                       "the same way in every window, so it survives the λ "
                       "integral instead of cancelling — and it does not look "
                       "like noise.");
    if (core::isConstantPressure(static_cast<core::MdEnsemble>(
            ensembleCombo_->currentData().toInt())))
        problems << tr("Under <b>NPT</b> the cell volume moves, and the "
                       "reference free energy is a function of V — there is no "
                       "single volume to evaluate it at. Equilibrate at NPT, "
                       "then integrate at the resulting volume under NVT.");
    endpointWarning_->setText(
        problems.isEmpty()
            ? QString()
            : QStringLiteral("<span style='color:#c0392b;'>%1</span>")
                  .arg(problems.join(QStringLiteral("<br><br>"))));
    endpointWarning_->setVisible(!problems.isEmpty());

    // Safe here and nowhere earlier: refreshPreview() returns immediately while
    // the review stage (and `preview_`) does not exist yet, and the guards
    // above have already established that this page is finished.
    refreshPreview();
}

core::TiRunConfig ThermodynamicIntegrationWizard::runConfig() const
{
    core::TiRunConfig config;
    config.calculator = baseCalculatorConfig();
    electronic_.applyTo(config.calculator);
    config.calculator.task = core::TaskKind::MolecularDynamics;
    config.calculator.ensemble =
        static_cast<core::MdEnsemble>(ensembleCombo_->currentData().toInt());
    config.calculator.temperatureK = temperatureSpin_->value();
    config.calculator.pressureGPa = pressureSpin_->value() * 1.0e-4; // bar → GPa
    config.calculator.timestepFs = timestepSpin_->value();
    config.calculator.frictionPerFs = frictionSpin_->value();
    config.calculator.tautFs = tautSpin_->value();
    config.calculator.taupFs = taupSpin_->value();

    config.reference = selectedReference();
    config.referenceParameters.einsteinSpringEvPerA2 =
        einsteinSpringSpin_->value();
    config.referenceParameters.einsteinFixedCenterOfMass =
        einsteinFixComCheck_->isChecked();
    config.referenceParameters.ljEpsilonEv = ljEpsilonSpin_->value();
    config.referenceParameters.ljSigmaA = ljSigmaSpin_->value();
    config.ljCutoffA = ljCutoffSpin_->value();

    config.schedule = selectedSchedule();
    config.quadrature = selectedQuadrature();
    config.scheduleExponent = scheduleExponentSpin_->value();
    config.windows = windowsSpin_->value();
    config.equilibrationSteps = equilibrationSpin_->value();
    config.productionSteps = productionSpin_->value();
    config.sampleInterval = sampleSpin_->value();
    config.hysteresis = hysteresisCheck_->isChecked();
    config.resultsDir = resultsDirectory_.toStdString();
    return config;
}

int ThermodynamicIntegrationWizard::jobCount() const
{
    if (!jobsSpin_ || !windowsSpin_)
        return 1;
    return std::max(1, std::min(jobsSpin_->value(), windowsSpin_->value()));
}

QStringList ThermodynamicIntegrationWizard::scripts() const
{
    QStringList result;
    const int jobs = jobCount();
    if (jobs <= 1) {
        // script(), not generateScript(): the review stage's text is what the
        // user may have hand-edited, and running a freshly generated copy
        // instead would silently discard the edit.
        result << script();
        return result;
    }
    const core::TiRunConfig base = runConfig();
    for (const auto& slice :
         core::ThermodynamicIntegrationScriptGenerator::splitWindows(
             base.windows, jobs)) {
        core::TiRunConfig job = base;
        job.windowIndices = slice;
        result << QString::fromStdString(
            core::ThermodynamicIntegrationScriptGenerator::generate(job));
    }
    return result;
}

QString ThermodynamicIntegrationWizard::generateScript() const
{
    // ALWAYS the whole path, never a slice — even with splitting turned on.
    //
    // This is what a host that knows nothing about scripts() runs, and the
    // orchestration canvas is exactly such a host: one node is one process
    // there. A preview that showed slice 0 would hand that canvas a script
    // covering a twelfth of the integral, which would complete successfully
    // and report an incomplete path forever.
    return QString::fromStdString(
        core::ThermodynamicIntegrationScriptGenerator::generate(runConfig()));
}

} // namespace calango::gui
