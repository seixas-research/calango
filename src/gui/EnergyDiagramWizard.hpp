#pragma once

#include "core/CalculatorConfig.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Electronics -> "Energy Diagrams…": discrete Kohn-Sham levels and (where
/// requested) electric-dipole transitions for a NON-PERIODIC or Gamma-only
/// baseline — "Electronic Structure" for molecules, in the sense that both
/// are ground-state readouts of a completed GPAW baseline, but this one has
/// no k-path (there is nothing to disperse along) and its transitions have
/// no analogue in a periodic band structure.
///
/// Single-stage like LdosWizard: the calculator is entirely inherited, no
/// separate Calculator Settings page.
class EnergyDiagramWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    EnergyDiagramWizard(std::shared_ptr<core::Structure> structure,
                        QWidget* parent = nullptr);

    /// Completed GPAW single-points that saved their wavefunctions — same
    /// shape as LdosWizard::setDensityBaselines(). Periodicity (more than
    /// one stored k-point) is checked IN-WIZARD once a baseline is picked,
    /// not filtered out of this list — the same "advisory UI check, script
    /// re-verifies" split every baseline-inheriting wizard here uses.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    QString pythonExecutable() const override;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("energy_diagram.py");
    }
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw;
    }
    bool hasTaskSettingsStage() const override { return true; }
    bool settingsStageFirst() const override { return true; }
    bool showsCalculatorStage() const override { return false; }

private Q_SLOTS:
    void onBaselineChanged();

private:
    std::shared_ptr<core::Structure> structure_;

    QComboBox* baselineCombo_ = nullptr;
    QLabel* inheritedLabel_ = nullptr;
    QLabel* baselineSummaryLabel_ = nullptr;
    QLabel* periodicityWarningLabel_ = nullptr;

    QCheckBox* transitionsCheck_ = nullptr;
    QSpinBox* belowSpin_ = nullptr;
    QSpinBox* aboveSpin_ = nullptr;
    QDoubleSpinBox* thresholdSpin_ = nullptr;

    std::optional<InheritedCalculator> inherited_;
    /// Whether the baseline's peek confirmed a single stored k-point —
    /// drives periodicityWarningLabel_ only. Like every other pre-flight
    /// check in this app, this is advisory: it does not disable Run. The
    /// generated script re-checks unconditionally and is what actually
    /// enforces it (see EnergyDiagramScriptGenerator's own RuntimeError).
    bool baselineUsable_ = false;
};

} // namespace calango::gui
