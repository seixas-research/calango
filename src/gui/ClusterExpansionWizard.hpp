#pragma once

#include "core/ClusterExpansionScriptGenerator.hpp"
#include "core/Structure.hpp"
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

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override;

private:
    core::ClusterExpansionRunConfig runConfig() const;
    /// Chemical species present across the ensemble, for the concentration
    /// axis selector.
    QStringList ensembleSpecies() const;

    std::vector<std::shared_ptr<core::Structure>> frames_;

    QLabel* summaryLabel_ = nullptr;
    QCheckBox* singlePointCheck_ = nullptr;
    QDoubleSpinBox* fmaxSpin_ = nullptr;
    QSpinBox* maxStepsSpin_ = nullptr;
    QComboBox* optimizerCombo_ = nullptr;
    QComboBox* concentrationCombo_ = nullptr;
    QCheckBox* endpointReferenceCheck_ = nullptr;
    QDoubleSpinBox* referenceASpin_ = nullptr;
    QDoubleSpinBox* referenceBSpin_ = nullptr;
    QCheckBox* continueOnFailureCheck_ = nullptr;
};

} // namespace calango::gui
