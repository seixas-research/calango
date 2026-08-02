#include "gui/WorkfunctionWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/Structure.hpp"
#include "core/WorkfunctionScriptGenerator.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

WorkfunctionWizard::WorkfunctionWizard(
    std::shared_ptr<core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
    // The engine is not asked (the Calculator Settings stage is hidden), but
    // the base class's selection still drives the launch-command template,
    // the calculator provenance and the interpreter fallback — so pin it.
    selectCalculator(core::CalculatorKind::Gpaw);
}

QString WorkfunctionWizard::wizardTitle() const
{
    return tr("2D Workfunction Setup");
}

QString WorkfunctionWizard::settingsHeader() const
{
    return tr("Workfunction Settings");
}

QWidget* WorkfunctionWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // -- Mandatory ground-state baseline -------------------------------------
    // Same contract as the Optics wizard's GPAW path: everything the answer
    // depends on lives in a completed SCF, and re-converging it here would
    // silently measure a different ground state than the one inspected.
    auto* baselineGroup = new QGroupBox(tr("Ground-State Baseline"), page);
    auto* baselineForm = new QFormLayout(baselineGroup);
    auto* baselineNote = new QLabel(
        tr("The work function is read off the FIXED ground state of a "
           "completed Single-Point Calculation — its SCF is inherited, never "
           "re-run. Re-converging here would give a Φ from a different ground "
           "state than the one you validated."),
        baselineGroup);
    baselineNote->setWordWrap(true);
    baselineForm->addRow(baselineNote);
    baselineCombo_ = new QComboBox(baselineGroup);
    baselineForm->addRow(tr("Baseline SCF (.gpw):"), baselineCombo_);
    // What is being inherited, spelled out. With no Calculator Settings stage
    // this is the only place the run's cutoff / xc / k-grid are visible, and
    // they are exactly what decides whether E_F (and so Φ) is converged.
    inheritanceNote_ = new QLabel(baselineGroup);
    inheritanceNote_->setWordWrap(true);
    inheritanceNote_->setTextFormat(Qt::RichText);
    baselineForm->addRow(inheritanceNote_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { onBaselineChanged(); });
    layout->addWidget(baselineGroup);

    // -- Vacuum direction and plateau window ----------------------------------
    auto* vacuumGroup = new QGroupBox(tr("Vacuum Region"), page);
    auto* vacuumForm = new QFormLayout(vacuumGroup);

    vacuumAxisCombo_ = new QComboBox(vacuumGroup);
    vacuumAxisCombo_->addItem(tr("a₁ (x)"), 0);
    vacuumAxisCombo_->addItem(tr("a₂ (y)"), 1);
    vacuumAxisCombo_->addItem(tr("a₃ (z)"), 2);
    const int guessed = calango::gui::guessVacuumAxis(structure_.get());
    vacuumAxisCombo_->setCurrentIndex(guessed >= 0 ? guessed : 2);
    vacuumAxisCombo_->setToolTip(
        tr("Which cell axis carries the vacuum. Seeded from the cell (the "
           "long axis the atoms only partly occupy) but confirm it: getting "
           "it wrong planar-averages across the vacuum instead of the sheet "
           "and reads the \"vacuum level\" from inside the material, "
           "silently."));
    vacuumForm->addRow(tr("Vacuum axis:"), vacuumAxisCombo_);
    connect(vacuumAxisCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    plateauFractionSpin_ = new QDoubleSpinBox(vacuumGroup);
    plateauFractionSpin_->setDecimals(2);
    plateauFractionSpin_->setRange(0.05, 0.40);
    plateauFractionSpin_->setSingleStep(0.05);
    plateauFractionSpin_->setValue(0.15);
    plateauFractionSpin_->setToolTip(
        tr("Fraction of the vacuum gap, measured inward from each cell edge, "
           "over which the plateau flatness |dV̄/dz| is checked. The vacuum "
           "level is only meaningful where V̄(z) is flat — a slope over this "
           "window means the vacuum is too thin, and the run says so instead "
           "of reporting a Φ that would quietly change with the cell size."));
    vacuumForm->addRow(tr("Plateau fraction:"), plateauFractionSpin_);
    connect(plateauFractionSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    layout->addWidget(vacuumGroup);

    auto* intro = new QLabel(
        tr("Compute the work function Φ = E_vac − E_F: the baseline's "
           "electrostatic potential is planar-averaged over the two in-plane "
           "axes, and the vacuum level E_vac is read at the cell edges, where "
           "V̄(z) must be flat. BOTH faces are reported. With a dipole "
           "correction in the baseline an asymmetric slab genuinely has two "
           "vacuum levels — one per face. Without one, periodic boundary "
           "conditions force the two faces onto a single artificial average, "
           "so equal values then mean the correction is missing, not that the "
           "slab is symmetric."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    layout->addStretch(1);
    return page;
}

void WorkfunctionWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    baselineCombo_->clear();
    for (const auto& [label, path] : baselines)
        baselineCombo_->addItem(label, path);
    // No engine to steer to when the list is empty: the host refuses to open
    // the wizard without a .gpw, because there is nothing to read Φ from.
    onBaselineChanged();
}

void WorkfunctionWizard::onBaselineChanged()
{
    inherited_ = applyBaselineProvenance(baselineCombo_, inheritanceNote_);
    refreshPreview();
}

QString WorkfunctionWizard::pythonExecutable() const
{
    // The run restarts the baseline's GPAW, so it should run under the same
    // interpreter (the same GPAW build) that wrote the .gpw when the
    // baseline recorded one.
    if (inherited_ && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

QString WorkfunctionWizard::generateScript() const
{
    core::WorkfunctionConfig cfg;
    if (baselineCombo_)
        cfg.baselineDensityPath =
            baselineCombo_->currentData().toString().toStdString();
    if (vacuumAxisCombo_)
        cfg.vacuumAxis = vacuumAxisCombo_->currentData().toInt();
    if (plateauFractionSpin_)
        cfg.plateauFraction = plateauFractionSpin_->value();
    return QString::fromStdString(core::generateWorkfunctionScript(cfg));
}

} // namespace calango::gui
