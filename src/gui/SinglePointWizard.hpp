#pragma once

#include "gui/SimulationWizardBase.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
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
    QWidget* buildCalculatorExtras() override;
    void updateCalculatorExtras(core::CalculatorKind kind) override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("single_point.py");
    }

private Q_SLOTS:
    void updateSpinEnabled();

private:
    core::CalculatorConfig config() const;

    QGroupBox* convergenceGroup_ = nullptr;
    QSpinBox* scfStepsSpin_ = nullptr;
    QDoubleSpinBox* scfTolSpin_ = nullptr;
    QCheckBox* spinCheck_ = nullptr;
    QLineEdit* magMomentEdit_ = nullptr;
    QComboBox* smearingCombo_ = nullptr;
    QDoubleSpinBox* smearingWidthSpin_ = nullptr;
};

} // namespace calango::gui
