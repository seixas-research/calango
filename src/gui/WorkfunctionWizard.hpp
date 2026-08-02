#pragma once

#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>

#include <memory>
#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Modules → 2D Materials → "2D Workfunction…": Φ = E_vac − E_F for a slab or
/// 2D sheet, from the planar-averaged electrostatic potential of an inherited
/// ground state.
///
/// A pure post-process: the MANDATORY baseline (a completed Single-Point
/// Calculation whose .gpw the run loads) already holds everything the answer
/// depends on — the converged potential and the Fermi level — so nothing is
/// re-converged here and the Calculator Settings stage is dropped
/// (showsCalculatorStage() == false), exactly as in the Optics wizard's GPAW
/// path. Two stages: Workfunction Settings, then the ASE Script Review.
///
/// The run reports BOTH faces' Φ. With a dipole correction in the baseline an
/// asymmetric slab genuinely has two vacuum levels, one per face; without one,
/// periodic boundary conditions force the two faces onto a single artificial
/// average — the wizard's note says so, because two equal numbers there mean
/// a missing correction, not a symmetric slab.
///
/// GPAW only: the method is reading calc.get_electrostatic_potential() off a
/// saved wavefunction file, which no other engine in this application exposes.
class WorkfunctionWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit WorkfunctionWizard(std::shared_ptr<core::Structure> structure,
                                QWidget* parent = nullptr);

    /// Populate the Stage-1 baseline selector with completed processes that
    /// saved a GPAW ground state (.gpw). The run inherits one; without any
    /// the host refuses to open the wizard.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    /// Interpreter the run binds to: the baseline's own environment when its
    /// calculator.json records one, so the potential is read back by the same
    /// GPAW build that produced the .gpw. Falls back to the standard
    /// per-engine resolution.
    QString pythonExecutable() const override;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("workfunction.py");
    }
    /// GPAW alone — see the class note.
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw;
    }
    bool hasTaskSettingsStage() const override { return true; }
    bool settingsStageFirst() const override { return true; }
    /// The ground state is inherited whole from the baseline, so there is
    /// nothing left for a Calculator Settings stage to set.
    bool showsCalculatorStage() const override { return false; }

private:
    /// Re-read the selected baseline's calculator.json, refresh the
    /// inheritance note and the script preview.
    void onBaselineChanged();

    std::shared_ptr<core::Structure> structure_;
    QComboBox* baselineCombo_ = nullptr;
    QComboBox* vacuumAxisCombo_ = nullptr;
    QDoubleSpinBox* plateauFractionSpin_ = nullptr;
    QLabel* inheritanceNote_ = nullptr;
    /// Provenance of the selected baseline; empty when it carries no sidecar.
    std::optional<InheritedCalculator> inherited_;
};

} // namespace calango::gui
