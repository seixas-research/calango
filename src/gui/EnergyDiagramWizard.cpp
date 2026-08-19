#include "gui/EnergyDiagramWizard.hpp"

#include "core/EnergyDiagramScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/GpawEigenvaluePeek.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace calango::gui {

EnergyDiagramWizard::EnergyDiagramWizard(std::shared_ptr<core::Structure> structure,
                                        QWidget* parent)
    : SimulationWizardBase(parent), structure_(std::move(structure))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
    onBaselineChanged();
}

void EnergyDiagramWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    for (const auto& [label, dir] : baselines)
        baselineCombo_->addItem(label, dir);
    if (baselineCombo_->count() > 0)
        baselineCombo_->setCurrentIndex(0);
    else
        onBaselineChanged();
}

QString EnergyDiagramWizard::wizardTitle() const
{
    return tr("Energy Diagrams Setup");
}

QString EnergyDiagramWizard::settingsHeader() const
{
    return tr("Baseline & Transitions");
}

QWidget* EnergyDiagramWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Discrete Kohn-Sham levels and, optionally, electric-dipole "
           "transition moments, on top of a completed GPAW single-point on "
           "a NON-PERIODIC (molecular) or Gamma-only structure.\n\n"
           "These are Kohn-Sham EIGENVALUE-DIFFERENCE transitions, not "
           "TDDFT or BSE excitation energies."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* sourceGroup = new QGroupBox(tr("Baseline SCF process"), page);
    auto* sourceForm = new QFormLayout(sourceGroup);

    baselineCombo_ = new QComboBox(sourceGroup);
    baselineCombo_->setToolTip(
        tr("Completed GPAW Single-Point calculations that saved their "
           "wavefunctions (.gpw, mode='all') on a non-periodic or Gamma-"
           "only structure."));
    sourceForm->addRow(tr("Process:"), baselineCombo_);

    inheritedLabel_ = new QLabel(sourceGroup);
    inheritedLabel_->setWordWrap(true);
    inheritedLabel_->setTextFormat(Qt::RichText);
    sourceForm->addRow(tr("Inherited calculator:"), inheritedLabel_);

    baselineSummaryLabel_ = new QLabel(sourceGroup);
    baselineSummaryLabel_->setWordWrap(true);
    baselineSummaryLabel_->setTextFormat(Qt::RichText);
    sourceForm->addRow(tr("Baseline:"), baselineSummaryLabel_);

    periodicityWarningLabel_ = new QLabel(sourceGroup);
    periodicityWarningLabel_->setWordWrap(true);
    periodicityWarningLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    periodicityWarningLabel_->setVisible(false);
    sourceForm->addRow(periodicityWarningLabel_);

    layout->addWidget(sourceGroup);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            &EnergyDiagramWizard::onBaselineChanged);

    auto* transGroup = new QGroupBox(tr("Transition dipoles"), page);
    auto* transForm = new QFormLayout(transGroup);

    transitionsCheck_ =
        new QCheckBox(tr("Compute transition dipole moments"), transGroup);
    transitionsCheck_->setChecked(true);
    transitionsCheck_->setToolTip(
        tr("<psi_i|r|psi_j> between occupied and virtual states, via "
           "GPAW's gpaw.utilities.dipole — the same Gamma-only restriction "
           "as the baseline itself. Allowed/forbidden is a numeric "
           "threshold on the computed oscillator strength, not a point-"
           "group irrep label (see the documentation's Future Work note)."));
    transForm->addRow(transitionsCheck_);

    belowSpin_ = new QSpinBox(transGroup);
    belowSpin_->setRange(1, 50);
    belowSpin_->setValue(5);
    belowSpin_->setToolTip(
        tr("Occupied states below (and including) the HOMO entering the "
           "transition table."));
    transForm->addRow(tr("Occupied states below HOMO:"), belowSpin_);

    aboveSpin_ = new QSpinBox(transGroup);
    aboveSpin_->setRange(1, 50);
    aboveSpin_->setValue(5);
    aboveSpin_->setToolTip(
        tr("Virtual states from the LUMO up entering the transition "
           "table."));
    transForm->addRow(tr("Virtual states above LUMO:"), aboveSpin_);

    thresholdSpin_ = new QDoubleSpinBox(transGroup);
    thresholdSpin_->setDecimals(6);
    thresholdSpin_->setRange(0.0, 1.0);
    thresholdSpin_->setSingleStep(0.0001);
    thresholdSpin_->setValue(1e-4);
    thresholdSpin_->setToolTip(
        tr("Below this oscillator strength a transition is reported "
           "'forbidden'."));
    transForm->addRow(tr("Oscillator-strength threshold:"), thresholdSpin_);

    layout->addWidget(transGroup);

    const auto sync = [this] {
        const bool on = transitionsCheck_ && transitionsCheck_->isChecked();
        belowSpin_->setEnabled(on);
        aboveSpin_->setEnabled(on);
        thresholdSpin_->setEnabled(on);
    };
    sync();
    connect(transitionsCheck_, &QCheckBox::toggled, this, [this, sync] {
        sync();
        refreshPreview();
    });
    connect(belowSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(aboveSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(thresholdSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    layout->addStretch(1);
    return page;
}

void EnergyDiagramWizard::onBaselineChanged()
{
    const QString dir =
        baselineCombo_ ? baselineCombo_->currentData().toString() : QString();
    inherited_ = dir.isEmpty() ? std::nullopt : readCalculatorProvenance(dir);
    baselineUsable_ = false;

    if (inheritedLabel_) {
        inheritedLabel_->setText(
            dir.isEmpty()
                ? tr("<i>No baseline selected.</i>")
                : (inherited_ ? inherited_->summary().toHtmlEscaped()
                              : tr("<i>Restarting from the saved "
                                   "wavefunctions (.gpw).</i>")));
    }

    if (dir.isEmpty()) {
        if (baselineSummaryLabel_)
            baselineSummaryLabel_->setText(
                tr("<i>Select a completed Single-Point Calculation.</i>"));
        if (periodicityWarningLabel_)
            periodicityWarningLabel_->setVisible(false);
        refreshPreview();
        return;
    }

    if (baselineSummaryLabel_)
        baselineSummaryLabel_->setText(tr("Checking the baseline…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const GpawEigenvalueSpectrum spectrum =
        peekGpawEigenvalues(pythonExecutable(), dir);
    QApplication::restoreOverrideCursor();

    if (!spectrum.ok) {
        if (baselineSummaryLabel_)
            baselineSummaryLabel_->setText(
                tr("<span style='color:#d9534f;'>Could not read this "
                   "baseline.</span>"));
        if (periodicityWarningLabel_) {
            periodicityWarningLabel_->setText(spectrum.errorMessage);
            periodicityWarningLabel_->setVisible(true);
        }
        refreshPreview();
        return;
    }

    int maxKpt = 0;
    for (const GpawState& s : spectrum.states)
        maxKpt = std::max(maxKpt, s.kpt);
    const int nkpts = maxKpt + 1;
    baselineUsable_ = (nkpts == 1);

    if (baselineSummaryLabel_)
        baselineSummaryLabel_->setText(
            tr("%1 stored state(s) · %2 k-point(s) · %3")
                .arg(spectrum.states.size())
                .arg(nkpts)
                .arg(spectrum.nspins > 1 ? tr("spin-polarized")
                                        : tr("spin-unpolarized")));

    if (periodicityWarningLabel_) {
        periodicityWarningLabel_->setVisible(!baselineUsable_);
        if (!baselineUsable_)
            periodicityWarningLabel_->setText(
                tr("This baseline stored %1 k-points. Energy Diagrams "
                   "needs a NON-PERIODIC or Gamma-only baseline (a single "
                   "k-point) — the discrete-level picture and the "
                   "transition-dipole formula both assume it. Re-run the "
                   "Single-Point Calculation on a non-periodic structure, "
                   "or with a single Gamma-point k-grid.")
                    .arg(nkpts));
    }
    refreshPreview();
}

QString EnergyDiagramWizard::pythonExecutable() const
{
    if (inherited_ && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

QString EnergyDiagramWizard::generateScript() const
{
    core::EnergyDiagramConfig cfg;
    cfg.baselineDir =
        baselineCombo_ ? baselineCombo_->currentData().toString().toStdString()
                       : std::string();
    cfg.computeTransitions =
        transitionsCheck_ && transitionsCheck_->isChecked();
    cfg.occupiedBandsBelowHomo = belowSpin_ ? belowSpin_->value() : 5;
    cfg.virtualBandsAboveLumo = aboveSpin_ ? aboveSpin_->value() : 5;
    cfg.oscillatorStrengthThreshold =
        thresholdSpin_ ? thresholdSpin_->value() : 1e-4;
    return QString::fromStdString(core::generateEnergyDiagramScript(cfg));
}

} // namespace calango::gui
