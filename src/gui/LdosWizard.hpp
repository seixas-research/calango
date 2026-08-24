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
class QGroupBox;
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

    /// Completed single-points this LDOS can post-process, as (display
    /// label, absolute job-directory path) pairs — the same shape
    /// MainWindow::gpawBaselines() already produces for Wannier. Call after
    /// construction, before exec().
    ///
    /// Both engines go in the same list. Which one a given entry IS is read
    /// back from its own `calculator.json` when it is selected, not encoded
    /// here: the wizard's whole shape (which spectrum reader runs, which
    /// script is generated, which controls are shown) follows from the
    /// PARENT's engine, and asking the user to restate it would be one more
    /// thing to get wrong.
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
        // Two routes, and only two. GPAW sums the stored pseudo-
        // wavefunctions itself (gpawWaveFunctionHelperScript); VASP has
        // LPARD, which does the same selection internally from a WAVECAR.
        // No other engine Calango drives exposes a state-resolved density
        // at all, so there is nothing else to allow.
        return kind == core::CalculatorKind::Gpaw
            || kind == core::CalculatorKind::Vasp;
    }
    /// The parent's WAVECAR, staged as `baseline.WAVECAR` so a REMOTE LPARD
    /// run finds the orbitals on the cluster. Empty on the GPAW route,
    /// which restarts a `.gpw` instead.
    QString baselineWavecarPathToStage() const override;

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
    QWidget* spinRowWidget_ = nullptr;
    QDoubleSpinBox* presetWidthSpin_ = nullptr;
    QLabel* peekErrorLabel_ = nullptr;

    /// VASP-only controls, hidden entirely on a GPAW parent. LSEPB/LSEPK
    /// have no GPAW counterpart at all — the GPAW path sums into one grid
    /// and could not split it afterwards.
    QGroupBox* vaspGroup_ = nullptr;
    QCheckBox* separateBandsCheck_ = nullptr;
    QCheckBox* separateKpointsCheck_ = nullptr;
    QLabel* recomputeNoteLabel_ = nullptr;

    /// The parent's engine, read back from its `calculator.json`. Drives
    /// which spectrum reader runs and which script is generated.
    core::CalculatorKind baselineEngine_ = core::CalculatorKind::Gpaw;

    std::optional<InheritedCalculator> inherited_;
    double fermiLevelEv_ = 0.0;
    bool spectrumLoaded_ = false;
    /// Guards syncSpinBoxesFromWidget()/syncWidgetFromSpinBoxes() against
    /// re-entrant feedback while one is updating the other.
    bool syncing_ = false;
};

} // namespace calango::gui
