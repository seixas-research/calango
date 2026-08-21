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
    /// The one module that offers Calango's own DFT engine.
    ///
    /// A single point is where it belongs first: it is the calculation the
    /// engine is being built to do, it needs no optimizer or dynamics wrapped
    /// around it, and it is the smallest thing whose answer can be checked
    /// against another code. Everything else stays on the base class's default
    /// until the engine can actually serve it — an engine offered in a wizard
    /// whose run path cannot dispatch to it is worse than one not offered.
    ///
    /// CalangoDftb (the native Slater-Koster engine) belongs here for the
    /// same reason, and for an additional one specific to it: its forces are
    /// finite-difference (see src/dftb/DftbForces.hpp), so a single point —
    /// one evaluation, not a hot optimizer/MD loop calling get_forces()
    /// hundreds of times — is the only task that cost is acceptable for; see
    /// SimulationWizardBase::calculatorAllowed()'s own doc for why it is
    /// excluded everywhere else by default.
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::CalangoDft
            || kind == core::CalculatorKind::CalangoDftb
            || GpawElectronicWizard::calculatorAllowed(kind);
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
