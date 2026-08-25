#pragma once

#include "gui/GrapheneOxideMcmdWizard.hpp"

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
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
///
/// THE SECOND SCHEME. The reservoir can instead be an ELECTRODE, through
/// Nørskov's computational hydrogen electrode: the user sets a potential
/// (and pH) and μ_H is DERIVED rather than offset. The formulas, the
/// SHE-vs-RHE choice and the reason μ_O stops being a constant live on
/// core::GrapheneOxideMcmdConfig::potentialMode; this class only puts the
/// controls on the page and shows what they imply. Both schemes read the
/// same two cached reference energies — the CHE changes what is done with
/// them, not where they come from.
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
    /// Show only the rows the selected scheme actually reads: Δμ_H is not a
    /// control in CHE mode (the potential fixes μ_H), and U/pH/T are not
    /// controls in manual mode. Hiding beats disabling here — a greyed row
    /// still reads as "a setting this run has", and it does not.
    void syncPotentialModeRows();

    QComboBox* potentialMode_ = nullptr;
    QGroupBox* reservoirBox_ = nullptr;
    QDoubleSpinBox* deltaMuH_ = nullptr;
    QDoubleSpinBox* deltaMuO_ = nullptr;
    QDoubleSpinBox* electrodePotential_ = nullptr;
    QDoubleSpinBox* solutionPh_ = nullptr;
    QDoubleSpinBox* potentialTemperature_ = nullptr;
    /// Set once the user edits the CHE temperature, after which it stops
    /// following the sampling temperature. Same rule as every other
    /// "sensible default that gets out of the way" in this application: the
    /// two ARE one physical temperature, and a user who has said otherwise
    /// has said so on purpose.
    bool potentialTemperatureTouched_ = false;
    QDoubleSpinBox* swapWeight_ = nullptr;
    QDoubleSpinBox* insertWeight_ = nullptr;
    QDoubleSpinBox* deleteWeight_ = nullptr;
    QLabel* potentialSummary_ = nullptr;
};

} // namespace calango::gui
