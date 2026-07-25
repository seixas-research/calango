#pragma once

#include "gui/SimulationWizardBase.hpp"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

namespace calango::gui {

/// Simulation → "Molecular Dynamics…": a 3-stage wizard. Stage 1 is the shared
/// Calculator Settings (engine, XC functional, cutoff, k-grid); Stage 2 is the
/// dynamics settings (ensemble, temperature, pressure, time step, total steps,
/// friction / coupling times); Stage 3 is the ASE script review.
class MolecularDynamicsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit MolecularDynamicsWizard(QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    /// Calculator Settings lead, then the dynamics settings: the forces the
    /// integrator propagates come from the engine, so choosing it first is the
    /// order the physics is set up in (and matches Geometry Optimization).
    bool settingsStageFirst() const override { return false; }
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("md.py"); }

private Q_SLOTS:
    void updateEnsembleEnabled();

private:
    core::CalculatorConfig config() const;

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
