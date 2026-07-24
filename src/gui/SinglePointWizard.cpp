#include "gui/SinglePointWizard.hpp"

#include "core/AseScriptGenerator.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QWidget>

namespace calango::gui {

SinglePointWizard::SinglePointWizard(QWidget* parent)
    : SimulationWizardBase(parent)
{
    buildUi();
    updateSpinEnabled();
}

QString SinglePointWizard::wizardTitle() const
{
    return tr("Single-point Calculation Setup");
}

QWidget* SinglePointWizard::buildCalculatorExtras()
{
    // Electronic-convergence controls, folded into the calculator-settings
    // page (shown for DFT engines via updateCalculatorExtras()).
    convergenceGroup_ = new QGroupBox(tr("Electronic convergence"), this);
    auto* page = convergenceGroup_;
    auto* form = new QFormLayout(page);

    scfTolSpin_ = new QDoubleSpinBox(page);
    scfTolSpin_->setDecimals(8);
    scfTolSpin_->setRange(1e-8, 1.0);
    scfTolSpin_->setValue(1e-4);
    scfTolSpin_->setSuffix(tr(" eV"));
    scfTolSpin_->setToolTip(
        tr("Electronic-energy convergence threshold for the SCF cycle."));
    form->addRow(tr("Energy convergence:"), scfTolSpin_);

    scfStepsSpin_ = new QSpinBox(page);
    scfStepsSpin_->setRange(1, 100000);
    scfStepsSpin_->setValue(100);
    scfStepsSpin_->setToolTip(
        tr("Maximum number of self-consistent-field iterations."));
    form->addRow(tr("Max electronic (SCF) steps:"), scfStepsSpin_);

    spinCheck_ = new QCheckBox(tr("Spin-polarized (collinear)"), page);
    spinCheck_->setToolTip(
        tr("Seed each atom with an initial magnetic moment so the SCF can "
           "converge to a magnetic solution."));
    form->addRow(tr("Spin polarization:"), spinCheck_);
    connect(spinCheck_, &QCheckBox::toggled, this,
            &SinglePointWizard::updateSpinEnabled);

    magMomentEdit_ = new QLineEdit(page);
    magMomentEdit_->setPlaceholderText(QStringLiteral("e.g. 2.2, -2.2, 0, 0"));
    magMomentEdit_->setText(QStringLiteral("1.0"));
    magMomentEdit_->setToolTip(
        tr("Explicit initial magnetic moments (μB) per atom, comma- or "
           "space-separated and in atom order.\nIf fewer values than atoms are "
           "given, the rest are padded with 0.0 automatically; a single value "
           "seeds only the first atom (others 0)."));
    form->addRow(tr("Initial magnetic moments:"), magMomentEdit_);

    smearingCombo_ = new QComboBox(page);
    // Order mirrors core::SmearingMethod.
    smearingCombo_->addItems({tr("None (fixed occupations)"), tr("Gaussian"),
                              tr("Fermi-Dirac"), tr("Methfessel-Paxton")});
    smearingCombo_->setCurrentIndex(
        static_cast<int>(core::SmearingMethod::Gaussian));
    smearingCombo_->setToolTip(
        tr("Occupation-number broadening. Use smearing for metals; None for "
           "insulators and isolated molecules."));
    form->addRow(tr("Smearing method:"), smearingCombo_);
    connect(smearingCombo_, &QComboBox::currentIndexChanged, this,
            &SinglePointWizard::updateSpinEnabled);

    smearingWidthSpin_ = new QDoubleSpinBox(page);
    smearingWidthSpin_->setDecimals(3);
    smearingWidthSpin_->setRange(0.0, 5.0);
    smearingWidthSpin_->setSingleStep(0.05);
    smearingWidthSpin_->setValue(0.1);
    smearingWidthSpin_->setSuffix(tr(" eV"));
    smearingWidthSpin_->setToolTip(
        tr("Broadening width (electronic temperature) for the smearing."));
    form->addRow(tr("Smearing width:"), smearingWidthSpin_);

    updateSpinEnabled();
    return page;
}

void SinglePointWizard::updateCalculatorExtras(core::CalculatorKind kind)
{
    // SCF convergence, spin and smearing are DFT concepts — only relevant for
    // the DFT engines (the classical/ML potentials do a single evaluation).
    const bool isDft = kind == core::CalculatorKind::QuantumEspresso
        || kind == core::CalculatorKind::Vasp
        || kind == core::CalculatorKind::Gpaw
        || kind == core::CalculatorKind::Siesta;
    if (convergenceGroup_)
        convergenceGroup_->setVisible(isDft);
}

void SinglePointWizard::updateSpinEnabled()
{
    const bool spin = spinCheck_ && spinCheck_->isChecked();
    if (magMomentEdit_)
        magMomentEdit_->setEnabled(spin);
    const bool smeared = smearingCombo_
        && smearingCombo_->currentIndex()
            != static_cast<int>(core::SmearingMethod::None);
    if (smearingWidthSpin_)
        smearingWidthSpin_->setEnabled(smeared);
}

core::CalculatorConfig SinglePointWizard::config() const
{
    core::CalculatorConfig c = baseCalculatorConfig();
    c.task = core::TaskKind::SinglePoint;
    c.scfMaxSteps = scfStepsSpin_->value();
    c.scfEnergyTolEv = scfTolSpin_->value();
    c.spinPolarized = spinCheck_->isChecked();
    c.initialMagMomentsCsv = magMomentEdit_->text().trimmed().toStdString();
    c.smearing = static_cast<core::SmearingMethod>(smearingCombo_->currentIndex());
    c.smearingWidthEv = smearingWidthSpin_->value();
    return c;
}

QString SinglePointWizard::generateScript() const
{
    return QString::fromStdString(
        core::AseScriptGenerator::generate(config(), "structure.extxyz"));
}

} // namespace calango::gui
