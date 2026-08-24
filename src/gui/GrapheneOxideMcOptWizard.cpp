#include "gui/GrapheneOxideMcOptWizard.hpp"

namespace calango::gui {

GrapheneOxideMcOptWizard::GrapheneOxideMcOptWizard(QWidget* parent)
    // DeferUi: the base must NOT build the UI, or its own virtuals win over
    // this class's and GO/MC-Opt comes out as GO/MCMD. buildUi() below is the
    // most-derived call the base class asks for.
    : GrapheneOxideMcmdWizard(DeferUi{}, parent)
{
    buildUi();
}

QString GrapheneOxideMcOptWizard::wizardTitle() const
{
    return tr("GO/MC-Opt — Monte Carlo with Geometry Optimization");
}

} // namespace calango::gui
