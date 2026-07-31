#pragma once

#include "gui/GpawElectronicRows.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <memory>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::core {
class Structure;
}

class QFormLayout;

namespace calango::gui {

/// Simulation → "Molecular Dynamics…": a 3-stage wizard. Stage 1 is the shared
/// Calculator Settings (engine, XC functional, cutoff, k-grid); Stage 2 is the
/// dynamics settings (ensemble, temperature, pressure, time step, total steps,
/// friction / coupling times); Stage 3 is the ASE script review.
class MolecularDynamicsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    /// `structure` is the system being propagated. It is only read to populate
    /// the "Geometry constraints…" editor and to seed the Hubbard editor's
    /// element completer; passing null leaves the per-atom constraint table
    /// empty.
    explicit MolecularDynamicsWizard(
        std::shared_ptr<const core::Structure> structure = nullptr,
        QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QStringList calculatorElements() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    /// Calculator Settings lead, then the dynamics settings: the forces the
    /// integrator propagates come from the engine, so choosing it first is the
    /// order the physics is set up in (and matches Geometry Optimization).
    /// Dispersion is offered here: the forces that drive the dynamics are what the correction changes.
    bool showsDispersionToggle() const override { return true; }
    bool settingsStageFirst() const override { return false; }
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("md.py"); }


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
    bool taskHasIonicSteps() const override { return true; }
    bool hasSpinExtras() const override { return true; }

private Q_SLOTS:
    void updateEnsembleEnabled();
    /// "Geometry constraints…": open the editor and keep its result.
    void editConstraints();

private:
    core::CalculatorConfig config() const;
    /// One-line description of the active constraints, shown next to the
    /// button so the page says what is held without being reopened.
    void refreshConstraintSummary();

    std::shared_ptr<const core::Structure> structure_;

    /// Shared GPAW electronic-structure controls (see the hooks above).

    GpawElectronicRows electronic_;
    /// Frozen degrees of freedom. Owned here rather than in the dialog, which
    /// is constructed on demand and destroyed on close.
    std::vector<core::GeometryConstraint> constraints_;
    QLabel* constraintSummary_ = nullptr;

    QComboBox* ensembleCombo_;
    QDoubleSpinBox* temperatureSpin_;
    QDoubleSpinBox* pressureSpin_; // bar (converted to GPa in config())
    QDoubleSpinBox* timestepSpin_;
    QDoubleSpinBox* frictionSpin_;
    QDoubleSpinBox* tautSpin_;
    QDoubleSpinBox* taupSpin_;
    QSpinBox* stepsSpin_;
    QSpinBox* sampleSpin_;
};

} // namespace calango::gui
