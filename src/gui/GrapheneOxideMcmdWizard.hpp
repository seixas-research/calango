#pragma once

#include "core/GrapheneOxideMcmdScriptGenerator.hpp"
#include "gui/CellRelaxationControls.hpp"
#include "gui/GoMcmdLiveTabs.hpp"
#include "gui/SimulationWizardBase.hpp"

namespace calango::core {
class Structure;
}

class QCheckBox;
class QComboBox;
class QVBoxLayout;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

/// Modules → Graphene Oxide → "GO/MCMD…": hybrid MD / Monte Carlo annealing
/// of a functional-group ARRANGEMENT.
///
/// INPUT is a "Graphene Oxide Build" — a structure carrying the persisted
/// classification core::GrapheneOxideBuilder::build() writes onto it (see
/// setInputBuild()) — not a structure this wizard produces itself. Formerly
/// this was reached only as an optional third stage of the builder, chained
/// immediately onto whatever it had just built; it is now a standalone
/// module precisely so ONE build can be refined by SEVERAL independent runs
/// (different temperature, seed, cycle count) without repeating the
/// generation step. The host (MainWindow::openGoMcmd()) is responsible for
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
/// Stages: Environment → Calculator Settings → MCMD Settings → Script Review.
/// The task page comes after the calculator because the cost of the run is
/// cycles × MD steps × one energy evaluation, and what that costs is a fact
/// about the engine — the page says so, in seconds, once the engine is known.
class GrapheneOxideMcmdWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    explicit GrapheneOxideMcmdWizard(QWidget* parent = nullptr);

    /// Seed the run from a Graphene Oxide Build: `structure` must satisfy
    /// core::GrapheneOxideBuilder::hasClassification() (the host checks this
    /// BEFORE constructing the wizard — see MainWindow::openGoMcmd() — so a
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
    /// Tag for the SUBCLASS constructor: builds nothing.
    ///
    /// SimulationWizardBase::buildUi() must be called from the constructor of
    /// the MOST DERIVED class, or every virtual hook it reads —
    /// `wizardTitle()`, `settingsHeader()`, `relaxationMode()` — dispatches to
    /// this class's version instead of the subclass's, and GO/MC-Opt silently
    /// builds GO/MCMD's page under GO/MCMD's title. The base class documents
    /// that rule for itself; this is the same rule one level further down.
    struct DeferUi {};
    GrapheneOxideMcmdWizard(DeferUi, QWidget* parent);

    QString wizardTitle() const override;
    QString settingsHeader() const override { return tr("MCMD Settings"); }
    QWidget* buildSettingsPage() override;

    /// Which module this wizard is configuring — the ONE thing GO/MC-Opt
    /// overrides to become itself.
    ///
    /// GO/MCMD and GO/MC-Opt differ in exactly one place: whether a proposed
    /// move is relaxed by a thermostatted burst or by a local optimizer.
    /// Everything else — the substrate summary, the Monte Carlo block, every
    /// Output control, the calculator page, the cost read-out — is the same
    /// question asked the same way, so they are one wizard with one hook
    /// rather than two files that have to be kept in step.
    virtual core::GoMcRelaxation relaxationMode() const
    {
        return core::GoMcRelaxation::MolecularDynamics;
    }
    /// The half of the settings page both modes share (Output, cost, signal
    /// wiring), so the optimization branch can skip the thermostat block and
    /// still get all of it.
    QWidget* finishSettingsPage(QWidget* page, QVBoxLayout* layout);

    /// GO/MC-Opt's own stage: the optimizer, its convergence criterion, and
    /// the variable-cell group. Empty header under GO/MCMD, which has no
    /// second stage — the base drops a stage whose header is empty.
    QString secondSettingsHeader() const override;
    QWidget* buildSecondSettingsPage() override;

    /// Every field the base wizard's controls define. `virtual` so a
    /// subclass can add its own on top rather than reassembling the forty
    /// the base already fills — GO Grand Canonical MC's four reservoir
    /// settings are exactly that case.
    virtual core::GrapheneOxideMcmdConfig collectConfig() const;

public:
    /// Which of the three trajectory files this run should open live tabs
    /// for. Read by MainWindow at launch; persisted per module so the choice
    /// survives the wizard.
    GoMcmdLiveTabSelection liveTabSelection() const;

protected:
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("run_mcmd.py");
    }
    /// The task page follows the calculator page: the cost estimate it shows
    /// is meaningless before the engine is known.
    bool settingsStageFirst() const override { return false; }

private Q_SLOTS:
    void refreshCost();

private:


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
    /// GO/MC-Opt only, and NULL under GO/MCMD — the relaxation group is built
    /// in one branch or the other, never both. Every read of these is guarded.
    QComboBox* optimizer_ = nullptr;
    QDoubleSpinBox* fmax_ = nullptr;
    QSpinBox* optimizerMaxSteps_ = nullptr;
    QDoubleSpinBox* optimizerMaxStep_ = nullptr;
    /// The shared variable-cell group — the same one Geometry Optimization
    /// builds, so the two modules cannot drift apart on what "relax the cell"
    /// means. Built only on the GO/MC-Opt stage; applyTo() is a documented
    /// no-op before build(), which is what makes reading it unconditional in
    /// collectConfig() safe under GO/MCMD.
    CellRelaxationControls cellControls_;
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
    /// Which trajectory files open a live viewport tab. The FILES are always
    /// written; these choose what is watched. See liveTabSelection().
    QCheckBox* liveTabCandidates_ = nullptr;
    QCheckBox* liveTabAccepted_ = nullptr;
    QCheckBox* liveTabAllStructures_ = nullptr;
    QLabel* costLabel_ = nullptr;
    QLabel* substrateLabel_ = nullptr;

    int groupCount_ = 0;
    int basalCarbons_ = 0;
    int edgeCarbons_ = 0;
    bool periodic_ = true;
    bool hydroxylAntiposition_ = false;

    mutable core::GrapheneOxideMcmdConfig config_;
};

} // namespace calango::gui
