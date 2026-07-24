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

/// Simulation → "Maximally Localized Wannier Functions (MLWF)…": a streamlined
/// 2-stage wizard.
///   Stage 1 — "SCF Process Selection & MLWF Configuration": pick a completed
///     Single-Point baseline whose saved wavefunctions/density (`.gpw`) drive
///     the localization, and set the essential MLWF parameters (trial
///     projections, number of Wannier functions, the disentanglement energy
///     window and the maximum minimization iterations).
///   Stage 2 — "ASE Script Review": review and run the generated script.
///
/// The calculator (engine, XC, cutoff, grid, k-points) and the Conda
/// environment are inherited from the selected baseline — the shared Calculator
/// Settings stage is dropped (showsCalculatorStage() == false) so the user is
/// never asked to redefine them. GPAW restart from the baseline `.gpw` restores
/// every SCF parameter at run time; the sidecar `calculator.json` (written when
/// the baseline ran) supplies the values shown in the inheritance note and the
/// interpreter the run binds to.
class WannierWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    WannierWizard(std::shared_ptr<core::Structure> structure,
                  QWidget* parent = nullptr);

    /// Populate the baseline source selector with completed single-points that
    /// hold the Bloch wavefunctions (GPAW `.gpw`). Each entry is (display
    /// label, absolute path to the origin process directory). Call after
    /// construction, before exec().
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    /// Interpreter the run binds to: the baseline's inherited environment when
    /// available (from its calculator.json), else the standard per-engine
    /// resolution.
    QString pythonExecutable() const override;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("wannier.py");
    }
    bool calculatorAllowed(core::CalculatorKind kind) const override;
    bool hasTaskSettingsStage() const override { return true; }
    bool settingsStageFirst() const override { return true; }
    /// The calculator is inherited from the baseline, so its settings stage is
    /// dropped — this is the strict 2-stage MLWF flow.
    bool showsCalculatorStage() const override { return false; }

private Q_SLOTS:
    /// Re-read the selected baseline's calculator.json and refresh the
    /// inheritance note + script preview.
    void onBaselineChanged();

private:
    std::shared_ptr<core::Structure> structure_;

    // Stage 1 — process selection + MLWF configuration.
    QComboBox* baselineCombo_ = nullptr;   ///< SCF baseline (origin process dir)
    QLabel* inheritedLabel_ = nullptr;     ///< inherited-calculator note
    QComboBox* projectionCombo_ = nullptr; ///< trial-orbital initialization
    QSpinBox* nWannier_ = nullptr;         ///< number of Wannier functions
    QCheckBox* energyWindowCheck_ = nullptr;   ///< enable disentanglement window
    QDoubleSpinBox* energyWindowSpin_ = nullptr; ///< fixedenergy (eV above E_F)
    QSpinBox* maxIterSpin_ = nullptr;      ///< max minimization iterations

    /// Calculator inherited from the currently selected baseline (from its
    /// calculator.json); empty when "none" is selected or the sidecar is
    /// absent (older baselines).
    std::optional<InheritedCalculator> inherited_;
};

} // namespace calango::gui
