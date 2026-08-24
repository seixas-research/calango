#pragma once

#include "gui/GrapheneOxideMcmdWizard.hpp"

namespace calango::gui {

/// "GO/MC-Opt" — Monte Carlo over a graphene oxide decoration in which every
/// proposed group move is relaxed to a LOCAL MINIMUM before it is judged.
///
/// The sibling of GO/MCMD, and deliberately a thin subclass of it rather than
/// a second wizard: the two ask the same questions about the substrate, the
/// move set, the calculator and the output, and differ in exactly one — how a
/// proposal is relaxed. `relaxationMode()` is that one difference, and the
/// base class branches on it when it builds its settings page. Two wizards
/// would be forty controls to keep in step for the sake of one enum.
///
/// WHY THE MODULE EXISTS. GO/MCMD relaxes a proposal with a short
/// thermostatted burst, which is cheap and samples the thermal ensemble — but
/// a relocated group is rebuilt from its recipe and arrives carrying placement
/// strain, and a burst too short to drain it hands Metropolis a trial energy
/// biased uphill by an amount that says nothing about whether the new site is
/// better. Measured on a real 200-cycle run: median trial ΔE +3.9 eV = 150 kT,
/// three moves accepted, none after cycle 32. Relaxing both states to a
/// minimum removes that term by construction.
///
/// WHAT IT COSTS, and it is not free. An optimization is an unbounded number
/// of force evaluations where a burst is a fixed handful, so a cycle is more
/// expensive and its cost varies. And the walk is over local MINIMA rather
/// than over a canonical ensemble — the temperature is a Metropolis parameter
/// here and nothing else. This is the module for "which arrangement is lowest
/// in energy"; GO/MCMD is the module for "what does this sheet do at 300 K".
class GrapheneOxideMcOptWizard : public GrapheneOxideMcmdWizard {
    Q_OBJECT

public:
    explicit GrapheneOxideMcOptWizard(QWidget* parent = nullptr);

protected:
    core::GoMcRelaxation relaxationMode() const override
    {
        return core::GoMcRelaxation::Optimization;
    }

    QString wizardTitle() const override;
    QString settingsHeader() const override
    {
        return tr("MC-Opt Settings");
    }
    QString exportFileName() const override
    {
        return QStringLiteral("run_mc_opt.py");
    }
};

} // namespace calango::gui
