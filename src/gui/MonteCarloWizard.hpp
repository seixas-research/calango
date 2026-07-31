#pragma once

#include "gui/GpawElectronicRows.hpp"
#include "gui/SimulationWizardBase.hpp"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

class QFormLayout;

namespace calango::gui {

/// Simulation → "Monte Carlo Simulation…": the standardized 4-stage wizard.
/// Stage 1 selects the method (Basin Hopping or Swap-atoms) and its physical
/// parameters (steps, temperature, perturbation amplitude / swap probability,
/// energy convergence); Stages 2–4 are the shared calculator/environment,
/// calculator settings and ASE script review. Both methods emit a standalone,
/// editable ASE Metropolis script run locally or remotely.
class MonteCarloWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit MonteCarloWizard(QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    /// Dispersion is offered here: acceptance depends on energy differences between configurations.
    bool showsDispersionToggle() const override { return true; }
    QString exportFileName() const override { return QStringLiteral("monte_carlo.py"); }


    // The shared GPAW electronic-structure form: smearing (method + width),
    // eigensolver + SCF step cap, the three convergence tolerances and the spin
    // configuration. Injected here so this wizard's GPAW page is the SAME page
    // the Single-Point and Geometry Optimization setups present — an SCF is an
    // SCF whichever task drives it, and a second layout for the same settings
    // is how the two drift apart.
    /// The engine drives which smearing methods are offered, so the base
    /// needs a handle on these rows to refilter them.
    GpawElectronicRows* electronicRows() override { return &electronic_; }
    void buildConvergenceRows(QFormLayout* form) override
    {
        electronic_.buildConvergenceRows(form, this);
    }
    void buildSpinRows(QFormLayout* form) override
    {
        electronic_.buildSpinRows(form, this);
    }
    QWidget* gpawEnergyToleranceWidget() override
    {
        return electronic_.energyToleranceWidget();
    }
    QWidget* gpawScfStepsWidget() override
    {
        return electronic_.scfStepsWidget();
    }
    bool hasConvergenceExtras() const override { return true; }
    bool hasSpinExtras() const override { return true; }

private Q_SLOTS:
    void updateMethodEnabled();

private:
    QString buildBasinHoppingScript() const;
    QString buildSwapScript() const;

    QComboBox* methodCombo_;
    QSpinBox* stepsSpin_;
    QDoubleSpinBox* temperatureSpin_;
    QDoubleSpinBox* displacementSpin_; // Basin Hopping perturbation dr (Å)
    QDoubleSpinBox* swapProbSpin_;     // Swap-atoms swap probability
    QDoubleSpinBox* fmaxSpin_;         // Basin Hopping local-opt force tol
    QComboBox* optimizerCombo_;        // Basin Hopping local optimizer
    QSpinBox* seedSpin_;
    /// baseCalculatorConfig() plus the shared GPAW electronic rows. Folded
    /// into one accessor so the two script paths cannot disagree about them.
    core::CalculatorConfig electronicCalculatorConfig() const;

    /// Shared GPAW electronic-structure controls.
    GpawElectronicRows electronic_;
};

} // namespace calango::gui
