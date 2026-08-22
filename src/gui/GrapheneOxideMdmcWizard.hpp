#pragma once

#include "core/GrapheneOxideMdmcScriptGenerator.hpp"
#include "gui/SimulationWizardBase.hpp"

namespace calango::core {
class Structure;
}

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

/// Modules → Graphene Oxide → "GO-MDMC…": hybrid MD / Monte Carlo annealing
/// of a functional-group ARRANGEMENT.
///
/// INPUT is a "Graphene Oxide Build" — a structure carrying the persisted
/// classification core::GrapheneOxideBuilder::build() writes onto it (see
/// setInputBuild()) — not a structure this wizard produces itself. Formerly
/// this was reached only as an optional third stage of the builder, chained
/// immediately onto whatever it had just built; it is now a standalone
/// module precisely so ONE build can be refined by SEVERAL independent runs
/// (different temperature, seed, cycle count) without repeating the
/// generation step. The host (MainWindow::openGoMdmc()) is responsible for
/// selecting an eligible build and — critically — for staging a COPY of it
/// as the current document before this wizard ever runs: the generated
/// script only ever reads the structure.extxyz stageJob() writes out at
/// launch time, so nothing this wizard does can mutate the build it started
/// from, and the same build can feed this wizard again for another run.
///
/// It is a separate wizard from the builder because this is where a
/// CALCULATOR is chosen, and the calculator stage — every engine, its
/// backend knobs, the environment resolution, the script review, run-local /
/// run-remote, and the calculator.json provenance a downstream job inherits —
/// is SimulationWizardBase's, already built and already correct.
/// Re-implementing an engine picker inside the builder to keep the flow in
/// one window would have been a second, worse copy of the most-used page in
/// the application.
///
/// Stages: Environment → Calculator Settings → MDMC Settings → Script Review.
/// The task page comes after the calculator because the cost of the run is
/// cycles × MD steps × one energy evaluation, and what that costs is a fact
/// about the engine — the page says so, in seconds, once the engine is known.
class GrapheneOxideMdmcWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit GrapheneOxideMdmcWizard(QWidget* parent = nullptr);

    /// Seed the run from a Graphene Oxide Build: `structure` must satisfy
    /// core::GrapheneOxideBuilder::hasClassification() (the host checks this
    /// BEFORE constructing the wizard — see MainWindow::openGoMdmc() — so a
    /// plain structure never reaches here). Every quantity this wizard needs
    /// — the functional-group count, the basal/edge carbon split, whether
    /// the substrate is periodic, and whether hydroxyls were placed as
    /// antiposition pairs — is read directly off `structure`'s own "edge",
    /// "go_group" and "go_pair_id" scalar fields, rather than trusted from a
    /// caller that "just built" it: the whole point of the split is that the
    /// build and the run are no longer the same flow.
    ///
    /// What it reads about ANTIPOSITION is now informational only: it puts
    /// what the input build actually contains into the substrate summary.
    /// Whether the sampler moves hydroxyl pairs as compound units is a
    /// separate, user-visible checkbox (checked by default) — see
    /// `hydroxylAntipositionBox_`. This method does not set it.
    void setInputBuild(const core::Structure& structure);

    /// Whether MainWindow should redefine each streamed frame's Cast from
    /// its own functional-group classification (the "Redefine Cast on every
    /// accepted move" checkbox) — read after exec() returns Accepted, the
    /// same way pythonExecutable()/calculatorKind()/runCommand() are.
    bool castPerFrame() const { return config_.castPerFrame; }

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
    /// Initial equilibration: MD steps run once before the first cycle, and
    /// the (stronger) thermostat coupling used for them.
    QSpinBox* equilibrationSteps_ = nullptr;
    QDoubleSpinBox* equilibrationFriction_ = nullptr;
    QComboBox* ensemble_ = nullptr;
    QDoubleSpinBox* pressure_ = nullptr;
    QCheckBox* bothFaces_ = nullptr;
    /// Move every bonded, opposite-face hydroxyl pair as one compound unit.
    /// CHECKED by default. Pairing is recovered from the STARTING GEOMETRY,
    /// so on a build that has no such pairs this finds none and every
    /// hydroxyl stays an ordinary single — which is why a control here
    /// cannot corrupt a structure it disagrees with, only fail to find
    /// anything to do. `hydroxylAntiposition_` below still reports what the
    /// input build actually contains, in the substrate summary.
    QCheckBox* hydroxylAntipositionBox_ = nullptr;
    QSpinBox* seed_ = nullptr;
    QSpinBox* snapshotInterval_ = nullptr;
    /// Live-viewport throttle, in MC cycles; 0 is headless. The ONE knob the
    /// live view has: both the dynamics between MC steps and the accepted
    /// configurations are streamed unconditionally at this interval, so
    /// there is no separate "also show the dynamics" checkbox any more.
    QSpinBox* viewportEvery_ = nullptr;
    QCheckBox* castPerFrame_ = nullptr;
    QLabel* costLabel_ = nullptr;
    QLabel* substrateLabel_ = nullptr;

    int groupCount_ = 0;
    int basalCarbons_ = 0;
    int edgeCarbons_ = 0;
    bool periodic_ = true;
    bool hydroxylAntiposition_ = false;

    mutable core::GrapheneOxideMdmcConfig config_;
};

} // namespace calango::gui
