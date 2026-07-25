#pragma once

#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>

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

/// Simulation → "Optical Properties…" and Modules → 2D Materials → "2D
/// Optics…": a task-first wizard for the linear dielectric-response workflow.
///
/// Stage 1 selects the MANDATORY ground-state baseline — a completed
/// Single-Point Calculation whose .gpw the run loads and evaluates at fixed
/// density — and collects the response settings (η broadening, the ħω window,
/// the frequency-point count and the xx/yy/zz directions). Stage 2 is the
/// shared ASE Script Review. GPAW only: the response module is what computes
/// the dielectric function.
///
/// The Calculator Settings stage is dropped (showsCalculatorStage() == false),
/// as in the MLWF wizard: every ground-state parameter is restored from the
/// baseline .gpw, so asking for a cutoff or k-grid here would present knobs
/// that cannot affect the run.
///
/// In 2D mode the wizard additionally asks which axis carries the vacuum and
/// derives the sheet observables. That question has no sensible default the
/// code can infer reliably, and getting it wrong silently rescales every 2D
/// quantity — so it is asked rather than guessed (the guess only seeds it).
class OpticsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    /// `twoDimensional` switches the wizard to the 2D-sheet variant: it also
    /// asks for the vacuum axis and derives the 2D observables (absorbance,
    /// σ₂D, α₂D) from the same dielectric function.
    OpticsWizard(std::shared_ptr<core::Structure> structure,
                 bool twoDimensional = false, QWidget* parent = nullptr);

    /// Populate the Stage-1 baseline selector with completed processes that
    /// saved a GPAW ground state (.gpw). The optics run inherits one; without
    /// any the host refuses to open the wizard.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    /// Interpreter the run binds to: the baseline's own environment when its
    /// calculator.json records one, so the response is evaluated by the same
    /// GPAW build that produced the density. Falls back to the standard
    /// per-engine resolution.
    QString pythonExecutable() const override;

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
    /// The ground state is inherited whole from the baseline, so there is
    /// nothing left for a Calculator Settings stage to set.
    bool showsCalculatorStage() const override { return false; }

private:
    /// The vacuum axis of a slab, guessed from the cell: the longest axis whose
    /// atoms occupy only a fraction of it. -1 when nothing looks like a slab.
    int guessVacuumAxis() const;

    /// Re-read the selected baseline's calculator.json, refresh the inheritance
    /// note and the script preview.
    void onBaselineChanged();

    std::shared_ptr<core::Structure> structure_;
    bool twoDimensional_ = false;
    QComboBox* baselineCombo_ = nullptr;
    QComboBox* vacuumAxisCombo_ = nullptr;
    QLabel* inheritanceNote_ = nullptr;
    /// Provenance of the selected baseline; empty when it carries no sidecar.
    std::optional<InheritedCalculator> inherited_;

    QDoubleSpinBox* broadeningSpin_ = nullptr;
    QDoubleSpinBox* omegaMinSpin_ = nullptr;
    QDoubleSpinBox* omegaMaxSpin_ = nullptr;
    QSpinBox* npointsSpin_ = nullptr;
    QCheckBox* tetrahedronCheck_ = nullptr;
    QCheckBox* dirXxCheck_ = nullptr;
    QCheckBox* dirYyCheck_ = nullptr;
    QCheckBox* dirZzCheck_ = nullptr;
};

} // namespace calango::gui
