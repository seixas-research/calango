#include "gui/ForceConvergenceControl.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>

namespace calango::gui {

namespace {
/// core::CalculatorConfig::fmax's own default. Kept as a local constant
/// rather than pulled from CalculatorConfig.hpp: this class is GUI-only and
/// has no other reason to depend on core::, and the two are checked to agree
/// by ForceConvergenceControlTest.
constexpr double kDefaultFmaxEvPerA = 0.05;
} // namespace

void ForceConvergenceControl::build(QFormLayout* form, QWidget* parent,
                                     const QString& label)
{
    spin_ = new QDoubleSpinBox(parent);
    // 4 decimals / a 0.0001 step: fmax criteria tighter than the previous
    // 3-decimal fields could express (e.g. 0.0005 eV/Å, routine for
    // phonon- or elastic-property-grade relaxations) could not be entered
    // at all before this.
    spin_->setDecimals(4);
    spin_->setRange(0.0001, 2.0);
    spin_->setSingleStep(0.0001);
    spin_->setValue(kDefaultFmaxEvPerA);
    spin_->setSuffix(QStringLiteral(" eV/Å"));
    form->addRow(label, spin_);
}

double ForceConvergenceControl::value() const
{
    return spin_ ? spin_->value() : kDefaultFmaxEvPerA;
}

void ForceConvergenceControl::setValue(double fmax)
{
    if (spin_)
        spin_->setValue(fmax);
}

} // namespace calango::gui
