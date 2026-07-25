#include "gui/SinglePointWizard.hpp"

#include "core/AseScriptGenerator.hpp"

namespace calango::gui {

SinglePointWizard::SinglePointWizard(QWidget* parent)
    : SimulationWizardBase(parent)
{
    buildUi();
    electronic_.updateEnabled();
}

QString SinglePointWizard::wizardTitle() const
{
    return tr("Single-point Calculation Setup");
}

core::CalculatorConfig SinglePointWizard::config() const
{
    core::CalculatorConfig c = baseCalculatorConfig();
    c.task = core::TaskKind::SinglePoint;
    electronic_.applyTo(c);
    return c;
}

QString SinglePointWizard::generateScript() const
{
    return QString::fromStdString(
        core::AseScriptGenerator::generate(config(), "structure.extxyz"));
}

} // namespace calango::gui
