#include "gui/GpawElectronicRows.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QSpinBox>
#include <QWidget>

namespace calango::gui {

namespace {
// The rows are not a QObject, so tr() is spelled against the wizard base's
// context — the strings belong to the calculator pages that show them.
QString tr(const char* source)
{
    return QCoreApplication::translate("calango::gui::SimulationWizardBase",
                                       source);
}
} // namespace

void GpawElectronicRows::buildConvergenceRows(QFormLayout* form,
                                              QObject* owner)
{
    // Widgets are parented to the group box that owns `form`.
    QWidget* parent = form->parentWidget();

    smearingCombo_ = new QComboBox(parent);
    // Order mirrors core::SmearingMethod.
    smearingCombo_->addItems({tr("None (fixed occupations)"), tr("Gaussian"),
                              tr("Fermi-Dirac"), tr("Methfessel-Paxton")});
    // Fermi-Dirac by default: it is the physical occupation function at an
    // electronic temperature, it is what GPAW's own default resolves to, and
    // unlike Gaussian it converges to a free energy that the reported total
    // energy is actually consistent with.
    smearingCombo_->setCurrentIndex(
        static_cast<int>(core::SmearingMethod::FermiDirac));
    smearingCombo_->setToolTip(
        tr("Occupation-number broadening. Use smearing for metals; None for "
           "insulators and isolated molecules."));
    // Half width: the entries are short ("Fermi-Dirac", "Gaussian") and the
    // combo was stretching to fill the row, pushing the width spin box that
    // belongs beside it out to the margin.
    smearingCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    smearingCombo_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QObject::connect(smearingCombo_, &QComboBox::currentIndexChanged, owner,
                     [this] { updateEnabled(); });

    smearingWidthSpin_ = new QDoubleSpinBox(parent);
    smearingWidthSpin_->setDecimals(3);
    smearingWidthSpin_->setRange(0.0, 5.0);
    smearingWidthSpin_->setSingleStep(0.05);
    smearingWidthSpin_->setValue(0.1);
    smearingWidthSpin_->setSuffix(tr(" eV"));
    smearingWidthSpin_->setToolTip(
        tr("Broadening width σ (electronic temperature) for the smearing."));

    // Method and width are one decision — a smearing without its width is
    // half an answer — so they share a row instead of stacking two.
    auto* smearingRow = new QWidget(parent);
    auto* smearingLayout = new QHBoxLayout(smearingRow);
    smearingLayout->setContentsMargins(0, 0, 0, 0);
    // Stretch factor 1 on the trailing spacer rather than on the combo, so the
    // combo keeps its content width instead of absorbing the whole row.
    smearingLayout->addWidget(smearingCombo_);
    smearingLayout->addWidget(new QLabel(tr("width σ"), smearingRow));
    smearingLayout->addWidget(smearingWidthSpin_);
    smearingLayout->addStretch(1);
    form->addRow(tr("Smearing method:"), smearingRow);

    scfTolSpin_ = new QDoubleSpinBox(parent);
    scfTolSpin_->setDecimals(8);
    scfTolSpin_->setRange(1e-8, 1.0);
    scfTolSpin_->setValue(1e-4);
    scfTolSpin_->setSuffix(tr(" eV"));
    scfTolSpin_->setToolTip(
        tr("Electronic-energy convergence threshold for the SCF cycle."));
    // NOT added to this form: the base class places it on the "Convergence
    // tolerances" row beside the eigenstates and density thresholds, which are
    // the two it is converged together with. Parented here so it is owned
    // either way.

    scfStepsSpin_ = new QSpinBox(parent);
    scfStepsSpin_->setRange(1, 100000);
    // 500 rather than 100: a magnetic or metallic system routinely needs a few
    // hundred iterations, and a cap that stops a converging SCF costs the whole
    // run while saving nothing — the convergence criteria are what end it.
    scfStepsSpin_->setValue(500);
    scfStepsSpin_->setToolTip(
        tr("Maximum number of self-consistent-field iterations. This is a "
           "runaway guard, not a target: the convergence thresholds are what "
           "normally end the cycle."));
    // Also placed by the base class, beside the eigensolver: how the SCF is
    // solved and how long it is allowed to try are one thought.

    updateEnabled();
}

void GpawElectronicRows::buildSpinRows(QFormLayout* form, QObject* owner)
{
    QWidget* parent = form->parentWidget();

    spinModeCombo_ = new QComboBox(parent);
    // Order matches core::SpinMode.
    spinModeCombo_->addItem(tr("Unpolarized (spin-restricted)"));
    spinModeCombo_->addItem(tr("Collinear Spin-Polarized (↑/↓)"));
    spinModeCombo_->addItem(tr("Non-Collinear Spin (spinors)"));
    spinModeCombo_->setToolTip(
        tr("Unpolarized: no spin degree of freedom.\n"
           "Collinear: spin-up / spin-down densities (scalar magnetic "
           "moments).\n"
           "Non-Collinear: spinor magnetism (vector moments; the list below is "
           "applied along +z)."));
    form->addRow(tr("Spin polarization:"), spinModeCombo_);
    QObject::connect(spinModeCombo_, &QComboBox::currentIndexChanged, owner,
                     [this] { updateEnabled(); });

    // The per-atom initial moments used to be typed here as a comma-separated
    // list. They are not: they are a property OF THE STRUCTURE, they are set
    // per atom in Edit Structure (which knows which atom is which), and they
    // travel with the staged structure file. A second copy in a text box was a
    // second source of truth that silently won — a user who set moments on the
    // right atoms in the editor got the box's "1.0" instead.
    momentsNote_ = new QLabel(
        tr("<i>Taken from the structure. Set them per atom in "
           "<b>Edit Structure…</b> → Spin polarization; they travel with the "
           "staged geometry as its initial magnetic moments.</i>"),
        parent);
    momentsNote_->setWordWrap(true);
    momentsNote_->setTextFormat(Qt::RichText);
    form->addRow(tr("Initial magnetic moments:"), momentsNote_);

    updateEnabled();
}

void GpawElectronicRows::updateEnabled()
{
    // The moments note is only meaningful when spin is enabled (collinear or
    // non-collinear, i.e. any mode past Unpolarized).
    const bool spin = spinModeCombo_
        && spinModeCombo_->currentIndex()
            != static_cast<int>(core::SpinMode::Unpolarized);
    if (momentsNote_)
        momentsNote_->setEnabled(spin);
    const bool smeared = smearingCombo_
        && smearingCombo_->currentIndex()
            != static_cast<int>(core::SmearingMethod::None);
    if (smearingWidthSpin_)
        smearingWidthSpin_->setEnabled(smeared);
}

QWidget* GpawElectronicRows::energyToleranceWidget() const
{
    return scfTolSpin_;
}

QWidget* GpawElectronicRows::scfStepsWidget() const
{
    return scfStepsSpin_;
}

void GpawElectronicRows::applyTo(core::CalculatorConfig& config) const
{
    if (scfStepsSpin_)
        config.scfMaxSteps = scfStepsSpin_->value();
    if (scfTolSpin_)
        config.scfEnergyTolEv = scfTolSpin_->value();
    if (spinModeCombo_) {
        config.spinMode =
            static_cast<core::SpinMode>(spinModeCombo_->currentIndex());
        // Keep the boolean in sync for the many callers that only read it.
        config.spinPolarized = config.spinMode != core::SpinMode::Unpolarized;
    }
    // No moments are written here: they live on the structure now and reach the
    // run through the staged geometry file.
    if (smearingCombo_)
        config.smearing =
            static_cast<core::SmearingMethod>(smearingCombo_->currentIndex());
    if (smearingWidthSpin_)
        config.smearingWidthEv = smearingWidthSpin_->value();
}

} // namespace calango::gui
