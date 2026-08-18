#pragma once

#include "core/ClusterExpansionScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/CellRelaxationControls.hpp"
#include "gui/ForceConvergenceControl.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

/// "Simulation → Cluster Expansion Calculation…": batch relaxation of an
/// ensemble produced by the Cluster Expansion builder.
///
/// Stage 1 collects the batch settings (which trajectory, relax vs
/// single-point, per-configuration convergence, the concentration axis and
/// the formation-energy references); Stages 2–4 are the shared calculator /
/// environment / script-review flow. The job writes an optimized trajectory
/// plus cluster_expansion.json, which the Results panel turns into a
/// formation-energy convex hull.
class ClusterExpansionWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    /// `frames` is the current document's trajectory — the ensemble to relax.
    /// A single-frame document is accepted but warned about, since a hull
    /// needs a spread of compositions.
    ClusterExpansionWizard(std::vector<std::shared_ptr<core::Structure>> frames,
                           QWidget* parent = nullptr);

    /// Frames the job should stage as its input ensemble.
    const std::vector<std::shared_ptr<core::Structure>>& frames() const
    {
        return frames_;
    }

    /// The design matrix that travels with this ensemble, one correlation row
    /// per frame. Without it the run emits energies with no regressors and no
    /// ECI fit is possible from the result. `degeneracies`, when given
    /// (empty is accepted for an ensemble built before this field existed),
    /// is g_j per frame — EGQCA needs it and the ECI fit does not, so it
    /// rides alongside rather than inside the design matrix.
    void setDesignMatrix(const std::vector<std::vector<double>>& correlations,
                         const std::vector<std::string>& orbitLabels,
                         const std::vector<int>& degeneracies = {});

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override;


private:
    std::vector<std::vector<double>> designMatrix_;
    std::vector<std::string> designLabels_;
    std::vector<int> designDegeneracies_;
    core::ClusterExpansionRunConfig runConfig() const;

    /// Chemical species present across the ensemble, for the concentration
    /// axis selector.
    QStringList ensembleSpecies() const;

    std::vector<std::shared_ptr<core::Structure>> frames_;

    QLabel* summaryLabel_ = nullptr;
    QCheckBox* singlePointCheck_ = nullptr;
    ForceConvergenceControl fmax_;
    QSpinBox* maxStepsSpin_ = nullptr;
    QComboBox* optimizerCombo_ = nullptr;
    QComboBox* concentrationCombo_ = nullptr;
    QCheckBox* endpointReferenceCheck_ = nullptr;
    QDoubleSpinBox* referenceASpin_ = nullptr;
    QDoubleSpinBox* referenceBSpin_ = nullptr;
    QCheckBox* continueOnFailureCheck_ = nullptr;
    /// Variable-cell relaxation for the batch. The SAME control the standalone
    /// Geometry Optimization module uses — same filters, same stress-mask
    /// presets, same Voigt ticks — because a hull whose configurations relaxed
    /// their cells and one whose configurations did not are not comparable,
    /// and a "simplified" version here would be a second answer to the same
    /// question that quietly drifts from the first.
    CellRelaxationControls cell_{this};
};

} // namespace calango::gui
