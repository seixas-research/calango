#include "gui/GwWizard.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

GwWizard::GwWizard(QWidget* parent) : SimulationWizardBase(parent)
{
    buildUi();
}

QString GwWizard::wizardTitle() const
{
    return tr("GW Calculations Setup");
}

QString GwWizard::settingsHeader() const
{
    return tr("Quasiparticle (G₀W₀) Settings");
}

QWidget* GwWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("G₀W₀ replaces the DFT exchange-correlation potential with the "
           "self-energy: E_qp = ε_DFT + Z·[Σ(ε_DFT) − V_xc]."),
        page);
    intro->setWordWrap(true);
    intro->setToolTip(
        tr("A one-shot correction to a specific ground state. Nothing here is "
           "self-consistent, and the result is only meaningful relative to the "
           "baseline it corrects."));
    layout->addWidget(intro);

    // -- Engine + baseline ---------------------------------------------------
    auto* baselineGroup = new QGroupBox(tr("Engine && Baseline"), page);
    auto* baselineForm = new QFormLayout(baselineGroup);

    engineCombo_ = new QComboBox(baselineGroup);
    // Order matches core::GwEngine.
    engineCombo_->addItem(tr("GPAW — native G₀W₀ (from .gpw)"));
    engineCombo_->addItem(tr("Yambo — G₀W₀ (from a Quantum ESPRESSO .save)"));
    baselineForm->addRow(tr("Engine:"), engineCombo_);
    connect(engineCombo_, &QComboBox::currentIndexChanged, this,
            &GwWizard::updateEngine);

    baselineCombo_ = new QComboBox(baselineGroup);
    baselineForm->addRow(tr("Baseline SCF:"), baselineCombo_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    engineNoteLabel_ = new QLabel(baselineGroup);
    engineNoteLabel_->setWordWrap(true);
    baselineForm->addRow(engineNoteLabel_);
    layout->addWidget(baselineGroup);

    // -- Convergence ---------------------------------------------------------
    auto* convergenceGroup = new QGroupBox(tr("Screening && Convergence"), page);
    auto* convergenceForm = new QFormLayout(convergenceGroup);

    frequencyCombo_ = new QComboBox(convergenceGroup);
    // Order matches core::GwFrequencyTreatment.
    frequencyCombo_->addItem(tr("Plasmon-pole approximation"));
    frequencyCombo_->addItem(tr("Full frequency (real axis)"));
    frequencyCombo_->setToolTip(
        tr("How the frequency dependence of the screened interaction W is "
           "treated.\n"
           "Plasmon-pole collapses it to one effective plasmon per G-vector "
           "pair: cheap, and accurate for sp semiconductors.\n"
           "Real-axis integrates the full frequency dependence — several times "
           "the cost, and the honest choice for metals or anything with "
           "low-energy structure in the loss function."));
    convergenceForm->addRow(tr("Frequency treatment:"), frequencyCombo_);

    cutoffSpin_ = new QDoubleSpinBox(convergenceGroup);
    cutoffSpin_->setRange(10.0, 2000.0);
    cutoffSpin_->setDecimals(0);
    cutoffSpin_->setSingleStep(10.0);
    cutoffSpin_->setValue(100.0);
    cutoffSpin_->setSuffix(tr(" eV"));
    cutoffSpin_->setToolTip(
        tr("Cutoff of the screened interaction / dielectric matrix. This is "
           "THE convergence parameter of a GW calculation: quasiparticle gaps "
           "drift by several tenths of an eV with it, far more than with most "
           "DFT knobs. Converge it jointly with the band count below — raising "
           "one alone is the classic way to get a plausible wrong answer."));
    convergenceForm->addRow(tr("Screening cutoff:"), cutoffSpin_);

    bandsSpin_ = new QSpinBox(convergenceGroup);
    bandsSpin_->setRange(0, 100000);
    bandsSpin_->setValue(0);
    bandsSpin_->setSpecialValueText(tr("auto (8× occupied)"));
    bandsSpin_->setToolTip(
        tr("Bands included in the polarizability and self-energy sums. 0 "
           "derives a starting value from the baseline's electron count."));
    convergenceForm->addRow(tr("Bands in the sums:"), bandsSpin_);

    belowSpin_ = new QSpinBox(convergenceGroup);
    belowSpin_->setRange(1, 200);
    belowSpin_->setValue(4);
    // A QFormLayout row header is a QLabel, so the subscript is typeset rather
    // than spelled with a literal underscore — as everywhere else E_F appears.
    convergenceForm->addRow(tr("Corrected bands below E<sub>F</sub>:"),
                            belowSpin_);
    aboveSpin_ = new QSpinBox(convergenceGroup);
    aboveSpin_->setRange(1, 200);
    aboveSpin_->setValue(4);
    aboveSpin_->setToolTip(
        tr("How many bands either side of the Fermi level receive a "
           "quasiparticle correction. The band edges are what a gap "
           "renormalization needs; correcting the whole spectrum costs far "
           "more for information rarely used."));
    convergenceForm->addRow(tr("Corrected bands above E<sub>F</sub>:"),
                            aboveSpin_);

    coresSpin_ = new QSpinBox(convergenceGroup);
    coresSpin_->setRange(1, 4096);
    coresSpin_->setValue(1);
    coresSpin_->setToolTip(
        tr("MPI ranks for the yambo executable. GPAW instead inherits its rank "
           "count from the launch command in Preferences → Run."));
    convergenceForm->addRow(tr("Yambo MPI ranks:"), coresSpin_);
    layout->addWidget(convergenceGroup);
    layout->addStretch(1);

    for (QComboBox* combo : {frequencyCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this] { refreshPreview(); });
    connect(cutoffSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    for (QSpinBox* spin : {bandsSpin_, belowSpin_, aboveSpin_, coresSpin_})
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this] { refreshPreview(); });

    updateEngine();
    return page;
}

void GwWizard::setBaselines(const QList<QPair<QString, QString>>& gpaw,
                            const QList<QPair<QString, QString>>& espresso)
{
    gpawBaselines_ = gpaw;
    espressoBaselines_ = espresso;
    // Open on an engine that actually has a baseline: defaulting to GPAW when
    // only a Quantum ESPRESSO run exists shows an empty list and reads as a
    // bug rather than as a missing prerequisite.
    if (gpawBaselines_.isEmpty() && !espressoBaselines_.isEmpty())
        engineCombo_->setCurrentIndex(static_cast<int>(core::GwEngine::Yambo));
    updateEngine();
}

void GwWizard::updateEngine()
{
    if (!engineCombo_ || !baselineCombo_)
        return;
    const auto engine = static_cast<core::GwEngine>(engineCombo_->currentIndex());
    const bool yambo = engine == core::GwEngine::Yambo;
    const auto& baselines = yambo ? espressoBaselines_ : gpawBaselines_;

    baselineCombo_->clear();
    for (const auto& [label, path] : baselines)
        baselineCombo_->addItem(label, path);

    coresSpin_->setEnabled(yambo);
    bandsSpin_->setEnabled(!yambo); // yambo takes its band count from its input

    if (baselines.isEmpty()) {
        engineNoteLabel_->setText(
            yambo ? tr("⚠ No completed Quantum ESPRESSO calculation with a "
                       "saved .save directory was found. Yambo starts from "
                       "pw.x output — run a Quantum ESPRESSO Single-Point "
                       "Calculation first.")
                  : tr("⚠ No completed GPAW calculation with a saved .gpw was "
                       "found. Run a GPAW Single-Point Calculation with "
                       "wavefunction export first."));
        engineNoteLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    } else {
        engineNoteLabel_->setText(
            yambo ? tr("The run converts the .save with p2y, generates the "
                       "G₀W₀ input with yambo -g n -p p, executes it, and "
                       "parses the quasiparticle report.")
                  : tr("The run restarts the .gpw, adds empty bands at fixed "
                       "density, then applies gpaw.response.g0w0.G0W0."));
        engineNoteLabel_->setStyleSheet(QString());
    }
    refreshPreview();
}

std::optional<SimulationWizardBase::InheritedCalculator>
GwWizard::selectedProvenance() const
{
    if (!baselineCombo_ || !engineCombo_)
        return std::nullopt;
    const QString path = baselineCombo_->currentData().toString();
    if (path.isEmpty())
        return std::nullopt;
    // GPAW entries name the restart file; Yambo entries name the job directory
    // that holds the QE .save. calculator.json lives in the job directory
    // either way.
    const QFileInfo info(path);
    return readCalculatorProvenance(info.isDir() ? info.absoluteFilePath()
                                                 : info.absolutePath());
}

QString GwWizard::pythonExecutable() const
{
    const auto inherited = selectedProvenance();
    if (inherited && !inherited->pythonExecutable.isEmpty())
        return inherited->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

core::GwConfig GwWizard::config() const
{
    core::GwConfig cfg;
    cfg.engine = static_cast<core::GwEngine>(engineCombo_->currentIndex());
    cfg.frequency =
        static_cast<core::GwFrequencyTreatment>(frequencyCombo_->currentIndex());
    cfg.baselinePath = baselineCombo_->currentData().toString().toStdString();
    cfg.screeningCutoffEv = cutoffSpin_->value();
    cfg.bands = bandsSpin_->value();
    cfg.correctedBandsBelow = belowSpin_->value();
    cfg.correctedBandsAbove = aboveSpin_->value();
    cfg.yamboCores = coresSpin_->value();
    return cfg;
}

QString GwWizard::generateScript() const
{
    return QString::fromStdString(core::generateGwScript(config()));
}

} // namespace calango::gui
