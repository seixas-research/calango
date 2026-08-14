#pragma once

#include "core/Structure.hpp"
#include "core/UnfoldingScriptGenerator.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace calango::gui {

class EmbeddedKPathEditor;

/// "Simulation → Effective Bands…": Popescu-Zunger band unfolding of a
/// supercell back onto its primitive Brillouin zone.
///
/// Stage layout (the task stage leads, since the geometry link decides
/// everything downstream — including which lattice the k-path lives on):
///   1  Structure & Geometry Link      — supercell, primitive cell, matrix M
///   2  Calculator & Execution Environment
///   3  Calculator & Unfolding Settings — energy mesh, sigma, XC/convergence
///   4  k-Path & ASE Script Review      — primitive-lattice path + script
class EffectiveBandsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    /// `supercell` is the active document's structure; `openDocuments` are the
    /// other open structures, offered as the primitive-cell reference.
    struct NamedStructure {
        QString name;
        std::shared_ptr<const core::Structure> structure;
    };

    EffectiveBandsWizard(std::shared_ptr<const core::Structure> supercell,
                         std::vector<NamedStructure> openDocuments,
                         QWidget* parent = nullptr);

    /// The primitive cell the job must stage as primitive.extxyz.
    std::shared_ptr<const core::Structure> primitiveStructure() const;

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override;

    QString calculatorSettingsHeader() const override;
    QWidget* buildCalculatorExtras() override;
    QString reviewHeader() const override;
    QWidget* buildReviewExtras() override;

    /// Unfolding needs plane-wave coefficients, so only the DFT backends that
    /// can supply them are offered.
    bool calculatorAllowed(core::CalculatorKind kind) const override;

private:
    core::UnfoldingConfig runConfig() const;
    /// Re-derive M from the two selected cells and refresh the matrix display
    /// and the commensurability verdict.
    void refreshMatrix();
    /// Rebuild the Stage-4 k-path editor for the currently selected primitive
    /// cell — the path lives on ITS lattice, not the supercell's.
    void rebuildKPathEditor();

    std::shared_ptr<const core::Structure> supercell_;
    std::vector<NamedStructure> openDocuments_;

    QLabel* supercellLabel_ = nullptr;
    QComboBox* primitiveCombo_ = nullptr;
    QSpinBox* matrixSpins_[3][3] = {};
    QCheckBox* autoMatrixCheck_ = nullptr;
    QDoubleSpinBox* toleranceSpin_ = nullptr;
    QCheckBox* forceCommensurateCheck_ = nullptr;
    QLabel* matrixVerdict_ = nullptr;

    QDoubleSpinBox* energyMinSpin_ = nullptr;
    QDoubleSpinBox* energyMaxSpin_ = nullptr;
    QSpinBox* energyBinsSpin_ = nullptr;
    QDoubleSpinBox* sigmaSpin_ = nullptr;
    QLineEdit* thresholdEdit_ = nullptr;

    QWidget* kpathHost_ = nullptr;
    EmbeddedKPathEditor* kpath_ = nullptr;
};

} // namespace calango::gui
