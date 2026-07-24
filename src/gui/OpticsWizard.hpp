#pragma once

#include "gui/SimulationWizardBase.hpp"

#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Simulation → "Optical Properties…": a task-first wizard for the linear
/// dielectric-response workflow. Stage 1 collects the response settings (η
/// broadening, the ħω energy window, the number of frequency points and the
/// xx/yy/zz directions to evaluate); Stages 2–3 are the shared Calculator
/// Settings and ASE Script Review. The optics run uses GPAW's response module,
/// so the engine combo is restricted to GPAW.
class OpticsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    OpticsWizard(std::shared_ptr<core::Structure> structure,
                 QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("optics.py");
    }
    /// GPAW only — the linear-response dielectric function is computed by
    /// gpaw.response, which the other engines do not expose.
    bool calculatorAllowed(core::CalculatorKind kind) const override;
    bool hasTaskSettingsStage() const override { return true; }
    bool settingsStageFirst() const override { return true; }

private:
    std::shared_ptr<core::Structure> structure_;

    QDoubleSpinBox* broadeningSpin_ = nullptr;
    QDoubleSpinBox* omegaMinSpin_ = nullptr;
    QDoubleSpinBox* omegaMaxSpin_ = nullptr;
    QSpinBox* npointsSpin_ = nullptr;
    QCheckBox* dirXxCheck_ = nullptr;
    QCheckBox* dirYyCheck_ = nullptr;
    QCheckBox* dirZzCheck_ = nullptr;
};

} // namespace calango::gui
