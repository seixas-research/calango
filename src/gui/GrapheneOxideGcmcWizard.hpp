#pragma once

#include "gui/GrapheneOxideMcmdWizard.hpp"

class QDoubleSpinBox;
class QLabel;

namespace calango::gui {

/// "GO Grand Canonical MC" — Monte Carlo over a graphene oxide decoration in
/// which the number of functional groups is NOT conserved.
///
/// The third member of the GO Monte Carlo family, and a thin subclass of the
/// first for the same reason GO/MC-Opt is: the substrate, the move
/// mechanics, the calculator, the three-file output and the live tabs are
/// identical, and one enum plus four numbers is the whole difference. Two
/// wizards would be forty controls to keep in step.
///
/// WHAT IT ADDS. GO/MCMD and GO/MC-Opt relocate a fixed inventory: whatever
/// decoration the builder produced is the decoration the run samples
/// arrangements of. This module opens the inventory to a reservoir, so the
/// moves also INSERT and DELETE groups and the composition becomes a sampled
/// quantity. That is the difference between "where do these groups sit?" and
/// "how many should there be at all?".
///
/// THE REFERENCES. Δμ_H and Δμ_O are set relative to
///
///     μ_H⁰ = ½ E_tot(H₂)
///     μ_O⁰ = E_tot(H₂O) − E_tot(H₂)
///
/// hydrogen from the H₂ molecule and oxygen from water in equilibrium with
/// it — the standard humid-environment reference. Both are computed by the
/// run itself, with THIS run's calculator and settings, because the
/// acceptance criterion subtracts a reference from a sheet energy: a
/// reference from a different engine or cutoff leaves a per-species constant
/// in every decision that has nothing to do with the chemistry. They are
/// cached by calculator signature, so a scan over Δμ pays for them once.
class GrapheneOxideGcmcWizard : public GrapheneOxideMcmdWizard {
    Q_OBJECT

public:
    explicit GrapheneOxideGcmcWizard(QWidget* parent = nullptr);

protected:
    /// Optimization by default: relaxing both sides of the Metropolis test
    /// to a minimum is what removed the placement-strain bias that made
    /// GO/MCMD's trial deltas 150 kT, and an insertion arrives with MORE
    /// placement strain than a relocation, not less.
    core::GoMcRelaxation relaxationMode() const override;

    QString wizardTitle() const override;
    QString settingsHeader() const override
    {
        return tr("Grand Canonical Settings");
    }
    QString exportFileName() const override
    {
        return QStringLiteral("run_go_gcmc.py");
    }

    /// The base page, plus the reservoir group.
    QWidget* buildSettingsPage() override;
    /// Everything the base emits, with the grand-canonical fields filled in.
    core::GrapheneOxideMcmdConfig collectConfig() const override;

private:
    void refreshPotentialSummary();

    QDoubleSpinBox* deltaMuH_ = nullptr;
    QDoubleSpinBox* deltaMuO_ = nullptr;
    QDoubleSpinBox* swapWeight_ = nullptr;
    QDoubleSpinBox* insertWeight_ = nullptr;
    QDoubleSpinBox* deleteWeight_ = nullptr;
    QLabel* potentialSummary_ = nullptr;
};

} // namespace calango::gui
