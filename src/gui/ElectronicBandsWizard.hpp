#pragma once

#include "gui/SimulationWizardBase.hpp"

#include <memory>

class QCheckBox;
class QLabel;
class QSpinBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Simulation → "Electronic Bands / PDOS…": the standardized 4-stage wizard
/// built on a prior single-point (SCF) baseline. Stage 1 is a dedicated
/// k-Path Definition stage — it embeds the Brillouin Zone & k-Path Builder
/// (via "Define with Brillouin Zone Builder…") to lay out the high-symmetry
/// pathway (Γ → X → M → Γ …). Stages 2–4 are the shared calculator /
/// environment (backend), calculator settings (SCF cutoff + k-grid) and ASE
/// script review. The engine choice maps to the electronic backend:
/// GPAW → GPAW, Quantum ESPRESSO → Espresso, everything else → free electrons.
class ElectronicBandsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    ElectronicBandsWizard(std::shared_ptr<const core::Structure> structure,
                          QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("bands.py"); }
    /// Only DFT-capable electronic-structure engines (GPAW, SIESTA, VASP,
    /// Quantum ESPRESSO); the empirical/ML calculators can't produce bands.
    bool calculatorAllowed(core::CalculatorKind kind) const override;

private:
    std::shared_ptr<const core::Structure> structure_;

    class KPathSelector* kpath_ = nullptr;
    QSpinBox* npointsSpin_ = nullptr;
    QSpinBox* valenceSpin_ = nullptr;
    QCheckBox* pdosCheck_ = nullptr;
};

} // namespace calango::gui
