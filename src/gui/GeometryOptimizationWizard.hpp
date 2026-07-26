#pragma once

#include "gui/GpawElectronicRows.hpp"
#include "gui/SimulationWizardBase.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QSpinBox;

namespace calango::gui {

/// Simulation → "Geometry Optimization…": a 3-stage wizard. Stage 1 is the
/// shared Calculator Settings — for GPAW, the exact same form the Single-Point
/// wizard shows (Mode & Basis Set, Brillouin Zone & k-Points, Electronic
/// Convergence & Smearing, Spin Configurations, Output & Exports), since the
/// SCF a relaxation step runs is the same SCF; Stage 2 is the relaxation
/// settings (optimizer, force convergence, step cap, and optional
/// variable-cell relaxation with a hydrostatic/anisotropic stress mask);
/// Stage 3 is the ASE script review.
class GeometryOptimizationWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit GeometryOptimizationWizard(QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    /// Calculator Settings first, then the relaxation settings page.
    /// Dispersion is offered here: a relaxation path is exactly where the missing long-range attraction changes the geometry it settles into.
    bool showsDispersionToggle() const override { return true; }
    bool settingsStageFirst() const override { return false; }
    /// The GPAW calculator form is shared verbatim with the Single-Point
    /// wizard through GpawElectronicRows — same groups, same rows, same
    /// toggles — so switching between the two setups is not a change of form.
    void buildConvergenceRows(QFormLayout* form) override
    {
        electronic_.buildConvergenceRows(form, this);
    }
    void buildSpinRows(QFormLayout* form) override
    {
        electronic_.buildSpinRows(form, this);
    }
    bool hasConvergenceExtras() const override { return true; }
    bool hasSpinExtras() const override { return true; }
    bool showsGpawSymmetryToggle() const override { return true; }
    bool showsGpawDensityExport() const override { return true; }
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("optimize.py"); }

private Q_SLOTS:
    void updateCellEnabled();

private:
    core::CalculatorConfig config() const;

    QComboBox* optimizerCombo_;
    QDoubleSpinBox* fmaxSpin_;
    QSpinBox* maxStepsSpin_;
    QCheckBox* relaxCellCheck_;
    QComboBox* cellFilterCombo_;
    QComboBox* stressMaskCombo_;   // anisotropic / hydrostatic / custom
    QWidget* voigtRow_;            // custom Voigt mask checkbox row
    QCheckBox* voigtChecks_[6];    // [xx, yy, zz, yz, xz, xy]

    /// Smearing / SCF / spin rows, shared with the Single-Point wizard.
    GpawElectronicRows electronic_;
};

} // namespace calango::gui
