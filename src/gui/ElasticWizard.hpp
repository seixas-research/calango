#pragma once

#include "core/CalculatorConfig.hpp"
#include "core/ElasticScriptGenerator.hpp"
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

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Electronics -> "Elastic Properties…": the finite-strain method — apply a
/// small homogeneous strain and read off either the stress (primary) or the
/// energy (fallback) at each strained point, then fit C_ij. Reuses the SAME
/// strain-generation core (core::StrainVoigt, core::StrainScriptHelpers) the
/// Piezoelectric Tensor wizard differentiates POLARIZATION over — the two
/// modules share terminology (clamped-/relaxed-ion, strain magnitude, points
/// per component, "Use point-group symmetry") on purpose.
///
/// Unlike PiezoelectricWizard, a ground-state baseline is OPTIONAL rather
/// than mandatory: stress and energy, unlike the Berry phase, are available
/// from ANY ASE calculator, so Stage 1 offers both "(none) — use the current
/// structure and the Calculator Settings below" and any completed GPAW
/// `.gpw` baselines (the one restart route this module reads). Stage 2 (the
/// ordinary Calculator Settings) is therefore always shown — when a baseline
/// IS selected its own calculator is what actually runs, and the Calculator
/// Settings page is left visible but ignored, exactly as flagged by the
/// inheritance note.
class ElasticWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    ElasticWizard(std::shared_ptr<const core::Structure> structure,
                 QWidget* parent = nullptr);

    /// Populate the optional baseline selector with completed Single-Point/
    /// Geometry-Optimization runs that saved their wavefunctions (`.gpw`).
    /// Each entry is (display label, absolute path); a "(none)" entry with
    /// an empty path is always first.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    QString pythonExecutable() const override;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("elastic.py");
    }
    /// Stress and energy are available from every engine here — unlike the
    /// Berry-phase-only Piezoelectric wizard, nothing is excluded — EXCEPT
    /// Calango's own native DFTB (CalangoDftb): it has no stress tensor at
    /// all and only finite-difference forces (see src/dftb/DftbForces.hpp),
    /// so a finite-strain elastic-tensor sweep here would be both wrong
    /// (no stress) and needlessly expensive if driven through forces alone.
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind != core::CalculatorKind::CalangoDftb;
    }
    QStringList calculatorElements() const override;
    bool showsCalculatorStage() const override { return true; }

private Q_SLOTS:
    void updateCostEstimate();
    void onBaselineChanged();

private:
    /// Same detection as PiezoelectricWizard::detectVacuumAxis(): the
    /// geometric guessVacuumAxis() read first, an explicit pbc=False
    /// fallback second.
    void detectVacuumAxis();

    std::vector<int> selectedVoigtComponents() const;
    core::ElasticConfig config() const;

    std::shared_ptr<const core::Structure> structure_;

    QComboBox* baselineCombo_ = nullptr;
    QLabel* inheritanceNote_ = nullptr;
    std::optional<InheritedCalculator> inherited_;

    /// -1 for a bulk 3D structure; 0/1/2 for a detected monolayer/slab —
    /// same sentinel as core::ElasticConfig::vacuumAxis.
    int vacuumAxis_ = -1;
    QLabel* dimensionalityNote_ = nullptr;

    QComboBox* methodCombo_ = nullptr;
    QCheckBox* voigtCheck_[6] = {};
    QDoubleSpinBox* strainSpin_ = nullptr;
    QComboBox* pointsCombo_ = nullptr;
    QCheckBox* relaxIonsCheck_ = nullptr;
    QCheckBox* symmetryCheck_ = nullptr;
    QLabel* costLabel_ = nullptr;
};

} // namespace calango::gui
