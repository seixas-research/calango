#include "gui/RamanIrWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/Structure.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <set>

namespace calango::gui {

RamanIrWizard::RamanIrWizard(std::shared_ptr<const core::Structure> structure,
                             QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
    updateCostEstimate();
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
           "route to it. Supplying a Born Charges run is therefore what turns "
           "the IR column on — without one the phonons and the Raman spectrum "
           "are computed as usual and every IR intensity is reported as "
           "zero.<br><br>"
           "<b>Raman</b> activity is built from ∂χ/∂Q, the change in "
           "<i>polarizability</i> — the same response the Optics module "
           "evaluates, taken in the static limit and differentiated by finite "
           "displacement."),
        page);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    // -- Inherited runs -----------------------------------------------------
    auto* sourcesGroup = new QGroupBox(tr("Inherited Results"), page);
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

    // -- What to compute ----------------------------------------------------
    auto* methodGroup = new QGroupBox(tr("Method"), page);
    auto* methodForm = new QFormLayout(methodGroup);

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
    return page;
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
    // State the real cost. The Raman toggle changes it by a large factor, and
    // that is the decision this page exists to inform.
    if (ramanCheck_ && ramanCheck_->isChecked()) {
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
    const QString gpw =
        baselineCombo_ ? baselineCombo_->currentData().toString() : QString();
    // The combo holds the .gpw; the provenance sidecar sits in its job dir.
    const QString dir =
        gpw.isEmpty() ? QString() : QFileInfo(gpw).absolutePath();
    inherited_ = dir.isEmpty() ? std::nullopt : readCalculatorProvenance(dir);

    if (inheritanceNote_) {
        if (inherited_) {
            QString note = tr("Inherited: %1")
                               .arg(inherited_->summary().toHtmlEscaped());
            if (!inherited_->condaEnv.isEmpty())
                note += tr(" — env <code>%1</code>")
                            .arg(inherited_->condaEnv.toHtmlEscaped());
            inheritanceNote_->setText(note);
        } else if (gpw.isEmpty()) {
            inheritanceNote_->clear();
        } else {
            inheritanceNote_->setText(
                tr("This baseline carries no <code>calculator.json</code>, so "
                   "its parameters cannot be shown. GPAW still restores them "
                   "from the <code>.gpw</code> at run time."));
        }
    }
    updateCostEstimate();
    refreshPreview();
}

QString RamanIrWizard::pythonExecutable() const
{
    if (inherited_ && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

core::RamanIrConfig RamanIrWizard::config() const
{
    core::RamanIrConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    cfg.calculator.task = core::TaskKind::SinglePoint;
    if (baselineCombo_)
        cfg.baselinePath =
            baselineCombo_->currentData().toString().toStdString();
    if (bornCombo_)
        cfg.bornChargesPath =
            bornCombo_->currentData().toString().toStdString();
    if (opticsCombo_)
        cfg.opticsPath = opticsCombo_->currentData().toString().toStdString();
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
