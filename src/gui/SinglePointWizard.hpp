#pragma once

#include "gui/GpawElectronicWizard.hpp"

class QFormLayout;

namespace calango::gui {

/// Simulation → "Single-point Calculation…": a streamlined 2-stage wizard.
/// Stage 1 is "Calculator & Convergence Settings" — the engine-specific knobs
/// plus the electronic convergence controls (energy convergence, max electronic
/// steps, spin polarization / magnetic moments, smearing) folded into the
/// shared thematic GPAW group boxes and shown only for DFT engines; Stage 2 is
/// the ASE script review. There is no separate task-settings stage
/// (hasTaskSettingsStage() == false).
class SinglePointWizard : public GpawElectronicWizard {
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
    /// Expose the GPAW "Symmetry: off" and "Export Charge Density" toggles —
    /// a symmetry-off Single-Point is the recommended MLWF baseline.
    bool showsGpawSymmetryToggle() const override { return true; }
    bool showsGpawDensityExport() const override { return true; }
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("single_point.py");
    }

private:
    core::CalculatorConfig config() const;

};

} // namespace calango::gui
