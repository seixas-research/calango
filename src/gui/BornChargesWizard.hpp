#pragma once

#include "core/BornChargesScriptGenerator.hpp"
#include "core/CalculatorConfig.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Electronics → "Born Effective Charges…": the standardized multi-stage wizard
/// for the Z* post-process.
///
/// Stage 1 is the Born-charge settings (displacement amplitude, which atoms,
/// the acoustic sum rule); Stages 2–3 are the shared Calculator Settings and
/// ASE Script Review. The Conda environment is bound silently per engine from
/// Preferences → "Python & Environments".
///
/// Only GPAW is offered: the method differentiates the Berry-phase
/// polarization, and that is the one backend here that can evaluate it.
class BornChargesWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    BornChargesWizard(std::shared_ptr<const core::Structure> structure,
                      QWidget* parent = nullptr);

    /// Populate the Stage-1 baseline selector with completed Single-Point
    /// Calculations that saved their wavefunctions (`.gpw`). Each entry is
    /// (display label, absolute path to the .gpw). Call after construction,
    /// before exec(). The baseline is MANDATORY.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    /// Interpreter the run binds to: the baseline's own environment when its
    /// provenance records one, so the displaced runs execute against the same
    /// GPAW build that produced the ground state.
    QString pythonExecutable() const override;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("born_charges.py");
    }
    /// Berry-phase polarization is a GPAW capability here.
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw;
    }
    QStringList calculatorElements() const override;
    /// The calculator is restored whole from the baseline, so there is nothing
    /// left for a Calculator Settings stage to ask — exactly as in Optics.
    bool showsCalculatorStage() const override { return false; }

private Q_SLOTS:
    /// Re-derive the "N atoms × 6 SCF runs" estimate from the atom selection.
    void updateCostEstimate();
    /// Re-read the selected baseline's calculator.json and refresh the
    /// inheritance note.
    void onBaselineChanged();

private:
    /// 0-based atom indices parsed from the selection field; empty = all.
    std::vector<int> selectedAtoms() const;
    core::BornChargesConfig config() const;

    std::shared_ptr<const core::Structure> structure_;

    QComboBox* baselineCombo_ = nullptr;
    QLabel* inheritanceNote_ = nullptr;
    /// Provenance of the selected baseline; empty when it carries no sidecar.
    std::optional<InheritedCalculator> inherited_;

    QDoubleSpinBox* displacementSpin_ = nullptr;
    QLineEdit* atomsEdit_ = nullptr;
    QCheckBox* sumRuleCheck_ = nullptr;
    QLabel* costLabel_ = nullptr;
};

} // namespace calango::gui
