#pragma once

#include "gui/GpawElectronicRows.hpp"
#include "gui/SimulationWizardBase.hpp"

class QFormLayout;

namespace calango::gui {

/// Simulation → "Single-point Calculation…": a streamlined 2-stage wizard.
/// Stage 1 is "Calculator & Convergence Settings" — the engine-specific knobs
/// plus the electronic convergence controls (energy convergence, max electronic
/// steps, spin polarization / magnetic moments, smearing) folded into the
/// shared thematic GPAW group boxes and shown only for DFT engines; Stage 2 is
/// the ASE script review. There is no separate task-settings stage
/// (hasTaskSettingsStage() == false).
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
    /// smearing + SCF tolerance/steps into "Electronic Convergence && Smearing",
    /// spin polarization mode + magnetic moments into "Spin Configurations".
    /// Both are built by the shared GpawElectronicRows, so the Geometry
    /// Optimization wizard presents the identical GPAW form.
    void buildConvergenceRows(QFormLayout* form) override
    {
        electronic_.buildConvergenceRows(form, this);
    }
    void buildSpinRows(QFormLayout* form) override
    {
        electronic_.buildSpinRows(form, this);
    }
    // Created by `electronic_`, positioned by the base class: the SCF energy
    // tolerance belongs on the tolerance row and the step cap beside the
    // eigensolver, and both of those rows are the base class's to lay out.
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

    GpawElectronicRows electronic_;
};

} // namespace calango::gui
