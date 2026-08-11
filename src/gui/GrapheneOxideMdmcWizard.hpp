#pragma once

#include "core/GrapheneOxideMdmcScriptGenerator.hpp"
#include "gui/SimulationWizardBase.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

/// The optional refinement step of the Graphene Oxide builder: hybrid
/// MD / Monte Carlo annealing of the functional-group ARRANGEMENT.
///
/// Reached from the builder's stage 3, on a structure the builder has just
/// produced. It is a separate wizard rather than another page of that dialog
/// because this is where a CALCULATOR is chosen, and the calculator stage —
/// every engine, its backend knobs, the environment resolution, the script
/// review, run-local / run-remote, and the calculator.json provenance a
/// downstream job inherits — is SimulationWizardBase's, already built and
/// already correct. Re-implementing an engine picker inside the builder to
/// keep the flow in one window would have been a second, worse copy of the
/// most-used page in the application.
///
/// Stages: Environment → Calculator Settings → MDMC Settings → Script Review.
/// The task page comes after the calculator because the cost of the run is
/// cycles × MD steps × one energy evaluation, and what that costs is a fact
/// about the engine — the page says so, in seconds, once the engine is known.
class GrapheneOxideMdmcWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit GrapheneOxideMdmcWizard(QWidget* parent = nullptr);

    /// Seed the run from the structure the builder produced. `groupCount` and
    /// the site counts are used only to size the defaults and warn about a
    /// substrate with nothing to move.
    void setSubstrate(int functionalGroups, int basalCarbons, int edgeCarbons,
                      bool periodic);

    /// The configuration the review stage generated its script from — the
    /// host needs it to stage the input structure under the right name.
    const core::GrapheneOxideMdmcConfig& mdmcConfig() const { return config_; }

protected:
    QString wizardTitle() const override;
    QString settingsHeader() const override { return tr("MDMC Settings"); }
    QWidget* buildSettingsPage() override;
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("run_mdmc.py");
    }
    /// The task page follows the calculator page: the cost estimate it shows
    /// is meaningless before the engine is known.
    bool settingsStageFirst() const override { return false; }

private Q_SLOTS:
    void refreshCost();

private:
    core::GrapheneOxideMdmcConfig collectConfig() const;

    QDoubleSpinBox* temperature_ = nullptr;
    QSpinBox* cycles_ = nullptr;
    QSpinBox* mdSteps_ = nullptr;
    QDoubleSpinBox* timestep_ = nullptr;
    QDoubleSpinBox* friction_ = nullptr;
    QComboBox* ensemble_ = nullptr;
    QDoubleSpinBox* pressure_ = nullptr;
    QCheckBox* bothFaces_ = nullptr;
    QSpinBox* seed_ = nullptr;
    QSpinBox* snapshotInterval_ = nullptr;
    /// Live-viewport throttle, in MC cycles; 0 is headless.
    QSpinBox* viewportEvery_ = nullptr;
    QCheckBox* streamMdFrames_ = nullptr;
    QLabel* costLabel_ = nullptr;
    QLabel* substrateLabel_ = nullptr;

    int groupCount_ = 0;
    int basalCarbons_ = 0;
    int edgeCarbons_ = 0;
    bool periodic_ = true;

    mutable core::GrapheneOxideMdmcConfig config_;
};

} // namespace calango::gui
