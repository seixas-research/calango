#include "gui/GpawElectronicRows.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
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
    smearingCombo_->setCurrentIndex(
        static_cast<int>(core::SmearingMethod::Gaussian));
    smearingCombo_->setToolTip(
        tr("Occupation-number broadening. Use smearing for metals; None for "
           "insulators and isolated molecules."));
    form->addRow(tr("Smearing method:"), smearingCombo_);
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
    form->addRow(tr("Smearing width σ:"), smearingWidthSpin_);

    scfTolSpin_ = new QDoubleSpinBox(parent);
    scfTolSpin_->setDecimals(8);
    scfTolSpin_->setRange(1e-8, 1.0);
    scfTolSpin_->setValue(1e-4);
    scfTolSpin_->setSuffix(tr(" eV"));
    scfTolSpin_->setToolTip(
        tr("Electronic-energy convergence threshold for the SCF cycle."));
    form->addRow(tr("Energy convergence:"), scfTolSpin_);

    scfStepsSpin_ = new QSpinBox(parent);
    scfStepsSpin_->setRange(1, 100000);
    scfStepsSpin_->setValue(100);
    scfStepsSpin_->setToolTip(
        tr("Maximum number of self-consistent-field iterations."));
    form->addRow(tr("Max electronic (SCF) steps:"), scfStepsSpin_);

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

    magMomentEdit_ = new QLineEdit(parent);
    magMomentEdit_->setPlaceholderText(QStringLiteral("e.g. 2.2, -2.2, 0, 0"));
    magMomentEdit_->setText(QStringLiteral("1.0"));
    magMomentEdit_->setToolTip(
        tr("Explicit initial magnetic moments (μB) per atom, comma- or "
           "space-separated and in atom order.\nIf fewer values than atoms are "
           "given, the rest are padded with 0.0 automatically; a single value "
           "seeds only the first atom (others 0)."));
    form->addRow(tr("Initial magnetic moments:"), magMomentEdit_);

    updateEnabled();
}

void GpawElectronicRows::updateEnabled()
{
    // Magnetic moments are only meaningful when spin is enabled (collinear or
    // non-collinear, i.e. any mode past Unpolarized).
    const bool spin = spinModeCombo_
        && spinModeCombo_->currentIndex()
            != static_cast<int>(core::SpinMode::Unpolarized);
    if (magMomentEdit_)
        magMomentEdit_->setEnabled(spin);
    const bool smeared = smearingCombo_
        && smearingCombo_->currentIndex()
            != static_cast<int>(core::SmearingMethod::None);
    if (smearingWidthSpin_)
        smearingWidthSpin_->setEnabled(smeared);
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
    if (magMomentEdit_)
        config.initialMagMomentsCsv =
            magMomentEdit_->text().trimmed().toStdString();
    if (smearingCombo_)
        config.smearing =
            static_cast<core::SmearingMethod>(smearingCombo_->currentIndex());
    if (smearingWidthSpin_)
        config.smearingWidthEv = smearingWidthSpin_->value();
}

} // namespace calango::gui
