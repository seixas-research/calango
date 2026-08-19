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

namespace calango::core {
class Structure;
}

namespace calango::gui {

class EnergyWindowWidget;

/// Electronics -> "Local Density of States (LDOS)…": a single-stage wizard
/// (settings + review, no separate calculator stage — the SCF is entirely
/// inherited from the selected baseline, like WannierWizard).
///
/// Unlike Wannier, LDOS has no "fresh SCF" fallback: it is a pure post-
/// process on an ALREADY-COMPLETED GPAW single-point (a plain sum over the
/// stored pseudo-wavefunctions), so a baseline is mandatory and the
/// standalone menu route refuses to open the wizard at all when none
/// exists (MainWindow::showLdos()) rather than offering a "(none)" choice
/// here that could never work.
///
/// LDOS also carries no "Symmetry: off" pre-condition the way Wannier does:
/// it sums over whatever k-points the baseline actually stored, weighted by
/// GPAW's own (already symmetry-aware) k-point weights, so a symmetry-
/// reduced baseline is exactly as usable as a full-zone one.
class LdosWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    LdosWizard(std::shared_ptr<core::Structure> structure,
              QWidget* parent = nullptr);

    /// Completed GPAW single-points that saved their wavefunctions, as
    /// (display label, absolute job-directory path) pairs — the same shape
    /// MainWindow::gpawBaselines() already produces for Wannier. Call after
    /// construction, before exec().
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    QString pythonExecutable() const override;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("ldos.py");
    }
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        // The wavefunction-access layer (AseScriptGenerator's
        // gpawRestartFromBaselineScript / gpawWaveFunctionHelperScript) is
        // GPAW-only, and setDensityBaselines() is only ever fed GPAW
        // baselines (MainWindow::gpawBaselines()), so there is no
        // non-GPAW entry to refuse in-wizard the way WannierWizard does.
        return kind == core::CalculatorKind::Gpaw;
    }
    bool hasTaskSettingsStage() const override { return true; }
    bool settingsStageFirst() const override { return true; }
    bool showsCalculatorStage() const override { return false; }

private Q_SLOTS:
    /// Re-read the selected baseline's provenance, peek its eigenvalue
    /// spectrum (gui::peekGpawEigenvalues) and refresh the energy-window
    /// widget + spin selector.
    void onBaselineChanged();
    void applyPreset(bool occupied);
    void syncSpinBoxesFromWidget(double minEv, double maxEv);
    void syncWidgetFromSpinBoxes();

private:
    std::shared_ptr<core::Structure> structure_;

    QComboBox* baselineCombo_ = nullptr;
    QLabel* inheritedLabel_ = nullptr;
    QLabel* baselineSummaryLabel_ = nullptr;

    EnergyWindowWidget* energyWindow_ = nullptr;
    QDoubleSpinBox* minSpin_ = nullptr;
    QDoubleSpinBox* maxSpin_ = nullptr;
    QCheckBox* relativeCheck_ = nullptr;
    QComboBox* spinCombo_ = nullptr;
    QDoubleSpinBox* presetWidthSpin_ = nullptr;
    QLabel* peekErrorLabel_ = nullptr;

    std::optional<InheritedCalculator> inherited_;
    double fermiLevelEv_ = 0.0;
    bool spectrumLoaded_ = false;
    /// Guards syncSpinBoxesFromWidget()/syncWidgetFromSpinBoxes() against
    /// re-entrant feedback while one is updating the other.
    bool syncing_ = false;
};

} // namespace calango::gui
