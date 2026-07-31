#pragma once

#include "core/CutoffConvergenceScriptGenerator.hpp"
#include "gui/GpawElectronicRows.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <vector>

class QDoubleSpinBox;
class QFormLayout;
class QLabel;

namespace calango::gui {

/// Modules → Parameters Convergence → "Plane-wave Cutoff Convergence…": run
/// the same single-point calculation over an ascending list of PW cutoffs and
/// measure how fast the total energy per atom and the maximum force settle.
///
/// Three stages:
///   1. Cutoff Sweep — minimum, maximum and stride of the cutoff list
///   2. Calculator & Convergence Settings — the standard GPAW page (the
///      single-cutoff spin box there is superseded by the sweep)
///   3. ASE Script Review
///
/// The run at the HIGHEST cutoff is the convergence reference — the sweep is
/// judged against the best member of the set, not against an absolute number
/// that a PAW total energy does not have.
class CutoffConvergenceWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit CutoffConvergenceWizard(QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override { return tr("Cutoff Sweep"); }
    QWidget* buildSettingsPage() override;
    /// The plane-wave engines with a first-class sweep script: GPAW
    /// (PW(ecut) per point) and VASP (ENCUT per point, one directory each).
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw
            || kind == core::CalculatorKind::Vasp;
    }
    QString calculatorSettingsHeader() const override
    {
        return tr("Calculator & Convergence Settings");
    }
    // The same GPAW electronic form the Single-point wizard presents — each
    // sweep member IS a single point.
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
    /// The sweep stage defines the cutoffs as a range; a second single-value
    /// cutoff field here would be a control the generated script ignores.
    bool showsPlaneWaveCutoffRow() const override { return false; }

    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("cutoff_convergence.py");
    }

private:
    /// The sweep as an ascending list: min, min+stride, … The maximum is
    /// always included even when the stride does not land on it exactly —
    /// it is the reference, and silently stopping short of it would hand
    /// that role to a lower cutoff than the user asked for.
    std::vector<double> cutoffs() const;
    core::CutoffConvergenceRunConfig runConfig() const;
    /// Keep the "N calculations" line honest about what the spins define.
    void updateSweepSummary();

    QDoubleSpinBox* minCutoffSpin_ = nullptr;
    QDoubleSpinBox* maxCutoffSpin_ = nullptr;
    QDoubleSpinBox* strideSpin_ = nullptr;
    QLabel* sweepSummary_ = nullptr;

    GpawElectronicRows electronic_;
};

} // namespace calango::gui
