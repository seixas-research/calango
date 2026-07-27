#pragma once

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
