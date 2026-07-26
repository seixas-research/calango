#pragma once

#include "gui/SimulationWizardBase.hpp"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

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
};

} // namespace calango::gui
