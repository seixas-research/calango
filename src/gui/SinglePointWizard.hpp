#pragma once

#include "gui/SimulationWizardBase.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QSpinBox;

namespace calango::gui {

/// Simulation → "Single-point Calculation…": a streamlined 3-stage wizard.
/// Stage 1 is the shared Calculator & Execution Environment; Stage 2 is
/// "Calculator & Convergence Settings" — the engine-specific knobs plus the
/// electronic convergence controls (energy convergence, max electronic steps,
/// spin polarization / magnetic moments, smearing) folded in and shown only
/// for DFT engines; Stage 3 is the ASE script review. There is no separate
/// task-settings stage (hasTaskSettingsStage() == false).
class SinglePointWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit SinglePointWizard(QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override { return QString(); } // unused
    QWidget* buildSettingsPage() override { return nullptr; }      // unused
    bool hasTaskSettingsStage() const override { return false; }
    QString calculatorSettingsHeader() const override
    {
        return tr("Calculator & Convergence Settings");
    }
    /// The electronic controls fold into the shared thematic GPAW group boxes:
    /// smearing + SCF tolerance/steps into "Electronic Convergence & Smearing",
    /// spin polarization + magnetic moments into "Output & Exports".
    void buildConvergenceRows(QFormLayout* form) override;
    void buildOutputRows(QFormLayout* form) override;
    bool hasConvergenceExtras() const override { return true; }
    bool hasOutputExtras() const override { return true; }
    /// Expose the GPAW "Symmetry: off" and "Export Electron Density" toggles —
    /// a symmetry-off Single-Point is the recommended MLWF baseline.
    bool showsGpawSymmetryToggle() const override { return true; }
    bool showsGpawDensityExport() const override { return true; }
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("single_point.py");
    }

private Q_SLOTS:
    void updateSpinEnabled();

private:
    core::CalculatorConfig config() const;

    QSpinBox* scfStepsSpin_ = nullptr;
    QDoubleSpinBox* scfTolSpin_ = nullptr;
    QCheckBox* spinCheck_ = nullptr;
    QLineEdit* magMomentEdit_ = nullptr;
    QComboBox* smearingCombo_ = nullptr;
    QDoubleSpinBox* smearingWidthSpin_ = nullptr;
};

} // namespace calango::gui
