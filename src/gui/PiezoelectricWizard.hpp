#pragma once

#include "core/CalculatorConfig.hpp"
#include "core/PiezoelectricScriptGenerator.hpp"
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
class QTableWidget;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Electronics -> "Piezoelectric Tensor…": the strain-polarization
/// finite-difference method, through the same GPAW Berry-phase evaluation
/// Born Effective Charges uses (core::polarizationPhaseCFunction), here
/// differentiated over cell strain instead of atomic displacement.
///
/// Stage 1 is the piezoelectric-specific settings (which Voigt strain
/// components, strain magnitude, points per component, clamped- vs.
/// relaxed-ion, symmetry, and an optional elastic stiffness for the e -> d
/// conversion); Stages 2-3 are the shared Calculator Settings and ASE Script
/// Review. Shaped exactly like BornChargesWizard: a mandatory `.gpw`
/// baseline supplies both the reference geometry and the calculator every
/// strained run is rebuilt from, so there is no Calculator Settings stage of
/// its own.
///
/// Only GPAW is offered — the method differentiates the Berry-phase
/// polarization, which is the one backend here that can evaluate it.
class PiezoelectricWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    PiezoelectricWizard(std::shared_ptr<const core::Structure> structure,
                        QWidget* parent = nullptr);

    /// Populate the Stage-1 baseline selector with completed Single-Point
    /// Calculations that saved their wavefunctions (`.gpw`). Each entry is
    /// (display label, absolute path to the .gpw). The baseline is
    /// MANDATORY, exactly as in BornChargesWizard.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

    /// Interpreter the run binds to: the baseline's own environment when its
    /// provenance records one.
    QString pythonExecutable() const override;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("piezoelectric.py");
    }
    /// Berry-phase polarization is a GPAW capability here, same as Born
    /// Charges.
    bool calculatorAllowed(core::CalculatorKind kind) const override
    {
        return kind == core::CalculatorKind::Gpaw;
    }
    QStringList calculatorElements() const override;
    /// The calculator is restored whole from the baseline.
    bool showsCalculatorStage() const override { return false; }

private Q_SLOTS:
    /// Re-derive the "N component(s) x M SCF runs" estimate.
    void updateCostEstimate();
    /// Re-read the selected baseline's calculator.json and refresh the
    /// inheritance note.
    void onBaselineChanged();

private:
    /// Detect a 2D/monolayer structure and disable the Voigt checkboxes
    /// that would strain its vacuum axis. Called once from the constructor
    /// (the structure never changes after that): core::guessVacuumAxis's
    /// geometric read (a large fractional-coordinate gap along one axis) is
    /// the primary signal, since a monolayer built with pbc=[True,True,True]
    /// and a vacuum gap — the ordinary ASE slab convention — carries no
    /// other sign of being 2D; an explicit pbc=False on an axis the geometry
    /// missed is the fallback.
    void detectVacuumAxis();

    /// 0-based Voigt indices (0..5) currently checked; empty means all six
    /// for a bulk structure, or every IN-PLANE component for a 2D one (the
    /// generator's own default — see core::inPlaneVoigtComponents). Every
    /// box unchecked is treated the same as every box checked/available, so
    /// the wizard can never silently generate an empty stencil.
    std::vector<int> selectedVoigtComponents() const;
    /// The 6x6 elastic stiffness table, parsed into GPa, if the "Convert to
    /// d_ij" box is checked and every cell holds a number; std::nullopt
    /// otherwise (including "checked but incomplete", which refuses rather
    /// than silently treating a blank cell as zero).
    std::optional<std::array<std::array<double, 6>, 6>> elasticStiffness() const;
    core::PiezoelectricConfig config() const;

    std::shared_ptr<const core::Structure> structure_;

    QComboBox* baselineCombo_ = nullptr;
    QLabel* inheritanceNote_ = nullptr;
    std::optional<InheritedCalculator> inherited_;

    /// -1 for a bulk 3D structure; 0/1/2 when detectVacuumAxis() finds a
    /// monolayer/slab. Same sentinel as core::PiezoelectricConfig::
    /// vacuumAxis, which this is copied into verbatim.
    int vacuumAxis_ = -1;
    QLabel* dimensionalityNote_ = nullptr;

    QCheckBox* voigtCheck_[6] = {};
    QDoubleSpinBox* strainSpin_ = nullptr;
    QComboBox* pointsCombo_ = nullptr;
    QCheckBox* relaxIonsCheck_ = nullptr;
    QCheckBox* symmetryCheck_ = nullptr;
    QCheckBox* elasticCheck_ = nullptr;
    QTableWidget* elasticTable_ = nullptr;
    QLabel* costLabel_ = nullptr;
};

} // namespace calango::gui
