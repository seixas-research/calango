#pragma once

#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>

#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QSpinBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Simulation → "Optical Properties…" and Modules → 2D Materials → "2D
/// Optics…": a task-first wizard for the linear dielectric-response workflow.
///
/// Two engines, chosen on Stage 1:
///
/// GPAW — selects the MANDATORY ground-state baseline (a completed
/// Single-Point Calculation whose .gpw the run loads and evaluates at fixed
/// density) and computes ε(ω) with gpaw.response.df.
///
/// VASP — self-contained: a normal SCF followed by the standard LOPTICS
/// protocol (exact-diagonalization restart at fixed density with enlarged
/// NBANDS, CSHIFT broadening, NEDOS frequency grid; ALGO=Eigenval when the
/// functional carries exact exchange). Its ground-state knobs (ENCUT,
/// k-grid, XC) live in a compact group on the same stage.
///
/// Both engines share the response settings (broadening, ħω window,
/// frequency-point count, xx/yy/zz directions, optional denser optics
/// k-mesh) and write the same optics.json, so one results window serves
/// both. Stage 2 is the shared ASE Script Review.
///
/// The Calculator Settings stage is dropped (showsCalculatorStage() == false),
/// as in the MLWF wizard: for GPAW every ground-state parameter is restored
/// from the baseline .gpw, and for VASP the few that matter are asked on
/// Stage 1 where the engine is chosen.
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
    /// GPAW (linear-response module) and VASP (LOPTICS protocol) — the only
    /// engines here with a frequency-dependent dielectric function.
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
    /// Show the engine's own groups and rows (baseline vs. VASP ground
    /// state; the GPAW-only integration options), retitle the shared labels
    /// (η vs. CSHIFT, points vs. NEDOS) and sync the base class's engine
    /// selection so the run command and interpreter resolve for the engine
    /// actually chosen here.
    void onEngineChanged();
    core::CalculatorKind selectedEngine() const;

    std::shared_ptr<core::Structure> structure_;
    bool twoDimensional_ = false;
    QComboBox* engineCombo_ = nullptr;
    QGroupBox* baselineGroup_ = nullptr;
    QGroupBox* vaspGroup_ = nullptr;
    QDoubleSpinBox* vaspEncutSpin_ = nullptr;
    QSpinBox* vaspKptSpins_[3] = {nullptr, nullptr, nullptr};
    QComboBox* vaspXcCombo_ = nullptr;
    /// Additional empty bands as % of occupied — engine-independent, feeds
    /// GPAW's NSCF band count and VASP's LOPTICS NBANDS alike.
    QSpinBox* emptyBandsSpin_ = nullptr;
    QFormLayout* responseForm_ = nullptr;
    QComboBox* baselineCombo_ = nullptr;
    QComboBox* vacuumAxisCombo_ = nullptr;
    QLabel* inheritanceNote_ = nullptr;
    /// Provenance of the selected baseline; empty when it carries no sidecar.
    std::optional<InheritedCalculator> inherited_;

    QDoubleSpinBox* broadeningSpin_ = nullptr;
    QDoubleSpinBox* omegaMinSpin_ = nullptr;
    QDoubleSpinBox* omegaMaxSpin_ = nullptr;
    QSpinBox* npointsSpin_ = nullptr;
    QSpinBox* responseKptsSpin_[3] = {nullptr, nullptr, nullptr};
    QCheckBox* ibzCheck_ = nullptr;
    QCheckBox* tetrahedronCheck_ = nullptr;
    QCheckBox* dirXxCheck_ = nullptr;
    QCheckBox* dirYyCheck_ = nullptr;
    QCheckBox* dirZzCheck_ = nullptr;
};

} // namespace calango::gui
