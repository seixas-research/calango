#pragma once

#include "gui/SimulationWizardBase.hpp"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

namespace calango::gui {

/// Simulation → "Molecular Dynamics…": the standardized 4-stage wizard. Stage
/// 1 is the ensemble + physical parameters; Stages 2–4 are the shared
/// calculator/environment, calculator settings and ASE script review.
class MolecularDynamicsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit MolecularDynamicsWizard(QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
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
