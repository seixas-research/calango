#pragma once

#include "core/CalculatorConfig.hpp"

#include "gui/OrchestrationProvenance.hpp"
#include "core/WorkflowReport.hpp"
#include "gui/OrchestrationTransforms.hpp"
#include "gui/SimulationWizardBase.hpp"

#include <QDialog>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QComboBox;
class QPushButton;

namespace calango::core {
class Structure;
}
namespace calango::jobs {
class JobRunner;
}

namespace calango::gui {

class ProcessManagerPanel;

class OrchestrationEdgeItem;
class OrchestrationScene;

/// The processes a orchestration node can represent. A superset of
/// core::TaskKind: the phonon orchestration is its own script generator rather
/// than a TaskKind, but on the canvas it is a process like any other.
///
/// APPEND-ONLY, and the list is no longer ordered by family — use
/// orchestrationTaskFamily() and orchestrationTasks() for anything that cares.
/// The values are not persisted, but renumbering would still churn every
/// switch below for no gain.
enum class OrchestrationTask {
    GeometryOptimization,
    SinglePoint,
    MolecularDynamics,
    Phonon,
    // -- Baseline-inheriting analysis modules, one slot ---------------------
    ElectronicBands,
    Optics,
    Workfunction,
    TwoDBands,
    Wannier,
    BornCharges,
    Gw,
    ChargeDensityDifference,
    RamanIr,
    // -- Two slots ----------------------------------------------------------
    ChargedDefects,
    ChargedDefects2d,
    // -- Structure transforms, executed by the canvas itself ----------------
    Container,
    Supercell,
    DefectGenerator,
    // Not a structure edit: it consumes an upstream ensemble's RESULTS and
    // emits a thermodynamic database. It is in this family because it runs on
    // the canvas rather than as a job, which is what the family actually
    // decides.
    TdbGenerator,
    // -- The alloy pipeline -------------------------------------------------
    // Container(parent lattice) → SQS Generator → simulations → ECI Fitter →
    // CVM Entropy. The first is a structure transform in the strict sense; the
    // other two are in this family for the TdbGenerator's reason — they
    // consume RESULTS and run on the canvas rather than as a job.
    SqsGenerator,
    ClusterExpansionFit,
    CvmEntropy,
    /// Thermodynamic integration — the ABSOLUTE Gibbs free energy of a liquid
    /// (or, with the Einstein reference, a solid).
    ///
    /// A Simulation like Molecular Dynamics, and for the same reason: it reads
    /// a structure, runs a job whose forces come from the chosen engine, and
    /// produces results. What it runs is one thermostatted MD per λ window
    /// rather than one trajectory, but that is inside the generated script and
    /// changes nothing about how the canvas drives it.
    LiquidFreeEnergy,
};

/// Which of the three groups a task belongs to. The Add Process list is
/// separated by this, and so is most of the behaviour that used to be decided
/// by "does it have input slots".
enum class OrchestrationFamily {
    /// Reads a structure, launches a job, produces results. Runs on task
    /// defaults if nobody configured it.
    Simulation,
    /// Reads a structure and produces a structure, IN PROCESS — no
    /// interpreter, no calculator, no launch command. Container, Supercell,
    /// Defect Generator.
    Transform,
    /// Reads one or more COMPLETED RUNS (see orchestrationInputSlots) and
    /// launches a job. Cannot run unconfigured: nothing can default a
    /// baseline path.
    Analysis,
};
OrchestrationFamily orchestrationTaskFamily(OrchestrationTask task);

/// One completed run a node consumes from a connected parent.
///
/// The canvas stages each slot into the child's own job directory under a
/// FIXED name, and hands that same name to the setup wizard as the baseline
/// path. That is what makes a wizard configured before its parent has ever run
/// still correct: the script it generates names a relative path, and the
/// runner guarantees a file is there under that name by the time it executes.
struct OrchestrationInputSlot {
    /// Shown on the node and in the wizard's baseline list ("pristine host").
    QString label;
    /// What to take from the parent's job directory. Empty means the WHOLE
    /// directory — Charge Density Difference reads several files out of one
    /// run, so it inherits the folder rather than a file.
    QString sourceName;
    /// Where it lands in the child's job directory, and therefore the path
    /// the generated script must name.
    QString stagedName;
    /// An optional slot may be left UNCONNECTED; leaving a required one
    /// unconnected refuses the run. It does not make the slot's ARTIFACT
    /// optional: a parent linked to an optional slot but holding nothing to
    /// inherit is still a refusal, because the wizard has by then been
    /// configured against a file that will not be there.
    bool optional = false;
};

/// The runs `task` inherits, in the order parents are linked. Empty for the
/// four self-contained tasks.
QList<OrchestrationInputSlot> orchestrationInputSlots(OrchestrationTask task);
/// How many parents a node of this task must have before it can run.
int orchestrationRequiredInputs(OrchestrationTask task);
/// True when an UNCONFIGURED node of this task can still run, from the task's
/// defaults seeded with the per-element suggested cutoff and k-grid. False for
/// every analysis module: a default baseline path does not exist, and running
/// one on a guess would mean computing something nobody asked for. Also false
/// for the two transforms whose identity operation is not a sensible default
/// (an empty Container, an empty defect recipe).
bool orchestrationTaskHasDefaults(OrchestrationTask task);
/// Directory-name slug ("charged_defects_2d"). Also the `task` field of the
/// provenance record and of the exported workflow document, which is why it
/// is public — and why it must be treated as a stable identifier rather than
/// a label to tidy up.
QString orchestrationTaskSlug(OrchestrationTask task);
/// The inverse. Empty for an unrecognised slug, which is how a document
/// written by a newer Calango is refused rather than half-loaded.
std::optional<OrchestrationTask> orchestrationTaskFromSlug(const QString& slug);
/// Menu/label text ("Charged Defects in 2D Materials").
QString orchestrationTaskDisplayName(OrchestrationTask task);
/// A short name for the same task, for places where the full one does not fit.
///
/// Workspace tabs above all: a tab is a few centimetres wide and there may be a
/// dozen of them, so "Orchestration: Charge Density Difference (Si bulk)" is a
/// tab whose visible portion is "Orchestrat…" — every tab identical, none
/// legible. The Processes panel is a wide list and keeps the full name.
QString orchestrationTaskShortName(OrchestrationTask task);
/// Every task, in Add Process list order.
QList<OrchestrationTask> orchestrationTasks();

/// One simulation process on the orchestration canvas: a draggable rounded
/// rectangle showing the process name, the material it runs on and the
/// calculator, with an input port on the left edge and an output port on the
/// right. The status strip along the top edge tracks execution.
///
/// Double-clicking the node opens the process's standard setup wizard in
/// orchestration mode: its Run button becomes "Save process node", and accepting
/// commits the generated script (plus interpreter and launch command) here
/// instead of executing anything. An unconfigured node runs with defaults.
class OrchestrationNodeItem : public QGraphicsRectItem {
public:
    enum class Status { Pending, Waiting, Running, Done, Failed, Skipped };

    OrchestrationNodeItem(int id, const QString& title,
                     OrchestrationTask task, const QString& materialName,
                     std::shared_ptr<const core::Structure> structure,
                     core::CalculatorKind engine);

    int id() const { return id_; }
    OrchestrationTask task() const { return task_; }
    core::CalculatorKind engine() const { return engine_; }

    /// Wizard-committed configuration ("Save process node"). A configured
    /// node runs exactly the script its wizard generated; an unconfigured
    /// one falls back to the task's defaults at send time.
    void setConfiguration(const QString& script, const QString& python,
                          const QString& runCommand,
                          core::CalculatorKind engine);
    bool isConfigured() const { return !configuredScript_.isEmpty(); }
    const QString& configuredScript() const { return configuredScript_; }
    const QString& configuredPython() const { return configuredPython_; }
    const QString& configuredRunCommand() const
    {
        return configuredRunCommand_;
    }
    const QString& materialName() const { return materialName_; }
    const std::shared_ptr<const core::Structure>& structure() const
    {
        return structure_;
    }
    QString title() const { return title_; }

    /// One inherited-run slot as the node paints it.
    struct InputLine {
        QString text;    ///< "pristine host <- Single Point (1)"
        bool satisfied;  ///< false when no parent is linked for this slot
        bool operator==(const InputLine& other) const
        {
            return text == other.text && satisfied == other.satisfied;
        }
    };
    /// Show which parent fills each input slot. The node GROWS to fit them:
    /// which run feeds which slot is the one thing about a multi-input node a
    /// reader cannot recover from the canvas, because both links look alike.
    void setInputSummary(const std::vector<InputLine>& lines);

    // -- Transform payload ---------------------------------------------------
    // Only one of these is meaningful per node, decided by task(). They are
    // plain members rather than a variant because each is a handful of ints
    // and the switch that would replace them would be longer than they are.

    /// One entry of a Container node: a name and the structure it stands for.
    using BatchItem = QPair<QString, std::shared_ptr<const core::Structure>>;
    /// The structures a Container fans the downstream pipeline out over. The
    /// pipeline runs once per item, in this order.
    const QList<BatchItem>& batchItems() const { return batchItems_; }
    void setBatchItems(const QList<BatchItem>& items);

    const SupercellSpec& supercell() const { return supercell_; }
    void setSupercell(const SupercellSpec& spec);

    const DefectSpec& defectSpec() const { return defects_; }
    void setDefectSpec(const DefectSpec& spec);

    const TdbGeneratorSpec& tdbGenerator() const { return tdb_; }
    void setTdbGenerator(const TdbGeneratorSpec& spec);

    const SqsGeneratorSpec& sqsGenerator() const { return sqs_; }
    void setSqsGenerator(const SqsGeneratorSpec& spec);

    const ClusterExpansionFitSpec& clusterExpansionFit() const
    {
        return clusterFit_;
    }
    void setClusterExpansionFit(const ClusterExpansionFitSpec& spec);

    const CvmEntropySpec& cvmEntropy() const { return cvm_; }
    void setCvmEntropy(const CvmEntropySpec& spec);

    /// Whether this node has everything it needs, as a message: empty when it
    /// is ready, otherwise what is missing. Covers "never configured" for the
    /// analysis modules and the empty-payload cases for the transforms.
    QString configurationProblem() const;

    Status status() const { return status_; }
    void setStatus(Status status);
    /// Row id in the global Processes panel for the current send (-1 when
    /// the orchestration runs without a panel, e.g. headless tests).
    int processTaskId() const { return processTaskId_; }
    void setProcessTaskId(int id) { processTaskId_ = id; }
    /// Where this node's finished job lives, once it ran — the LATEST one.
    QString jobDirectory() const { return jobDirectory_; }
    void setJobDirectory(const QString& directory)
    {
        jobDirectory_ = directory;
    }
    /// Every directory this node has run in during the current orchestration,
    /// oldest first: one per batch item, plus one per retry after a Resume.
    /// jobDirectory() is the last of them.
    ///
    /// This is the data-provenance index the canvas keeps in memory — a failed
    /// attempt's directory stays in the list (and on disk) so the run that
    /// produced a given artifact is always findable, including the one that
    /// went wrong.
    const QStringList& jobHistory() const { return jobHistory_; }
    void recordJobDirectory(const QString& directory);
    void clearJobHistory() { jobHistory_.clear(); }
    /// How many times this node has been dispatched in the current
    /// orchestration. Reported as `attempt` in the provenance record.
    int attempts() const { return jobHistory_.size(); }

    /// Port centres in SCENE coordinates — the edges anchor here.
    QPointF inputPortScenePos() const;
    QPointF outputPortScenePos() const;
    /// True when `scenePos` falls on the output port's grab zone.
    bool hitsOutputPort(const QPointF& scenePos) const;

    void registerEdge(OrchestrationEdgeItem* edge) { edges_.push_back(edge); }
    void unregisterEdge(OrchestrationEdgeItem* edge);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;
    /// Wider than the rect: the ports overhang the left/right edges, and an
    /// item that paints outside its bounding rect leaves ghost trails when
    /// dragged — the scene only repaints what the rect declares.
    QRectF boundingRect() const override;
    /// Dragging a node drags its edge anchors with it.
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override;

private:
    int id_;
    QString title_;
    OrchestrationTask task_;
    QString materialName_;
    std::shared_ptr<const core::Structure> structure_;
    core::CalculatorKind engine_;
    Status status_ = Status::Pending;
    int processTaskId_ = -1;
    QString jobDirectory_;
    QStringList jobHistory_;
    QString configuredScript_;
    QString configuredPython_;
    QString configuredRunCommand_;
    QList<BatchItem> batchItems_;
    SupercellSpec supercell_;
    DefectSpec defects_;
    TdbGeneratorSpec tdb_;
    SqsGeneratorSpec sqs_;
    ClusterExpansionFitSpec clusterFit_;
    CvmEntropySpec cvm_;
    std::vector<InputLine> inputLines_;
    std::vector<OrchestrationEdgeItem*> edges_;
};

/// A directed link between two nodes: the child consumes the parent's
/// outputs (relaxed geometry, saved ground state) as its inputs.
class OrchestrationEdgeItem : public QGraphicsPathItem {
public:
    OrchestrationEdgeItem(OrchestrationNodeItem* from, OrchestrationNodeItem* to);
    OrchestrationNodeItem* from() const { return from_; }
    OrchestrationNodeItem* to() const { return to_; }
    void updatePath();

private:
    OrchestrationNodeItem* from_;
    OrchestrationNodeItem* to_;
};

/// Scene that owns the link-drawing gesture: press on a node's output port,
/// drag (a dashed preview follows), release on another node to connect.
class OrchestrationScene : public QGraphicsScene {
    Q_OBJECT

public:
    using QGraphicsScene::QGraphicsScene;

Q_SIGNALS:
    /// The user drew a link from `from`'s output port onto `to`.
    void connectionRequested(OrchestrationNodeItem* from, OrchestrationNodeItem* to);
    /// The user double-clicked a node — open its setup wizard.
    void nodeActivated(OrchestrationNodeItem* node);
    /// The user double-clicked empty canvas — add a new process node there.
    void addNodeRequested(const QPointF& scenePos);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    OrchestrationNodeItem* nodeAt(const QPointF& scenePos) const;

    OrchestrationNodeItem* pendingFrom_ = nullptr;
    QGraphicsPathItem* pendingPreview_ = nullptr;
};

/// The "Orchestration" dock: a node-based editor for automated simulation
/// pipelines. Processes are nodes on a pannable, zoomable canvas; drawing a
/// link from one node's output port to another node makes the second run
/// after the first, consuming its outputs — the relaxed geometry of a
/// finished parent becomes the child's input structure, and a parent's saved
/// ground state (.gpw) is carried into the child's directory.
///
/// Execution is sequential in dependency order (the app runs one local job
/// at a time by design); each node stages its own job directory under a
/// per-orchestration folder in the simulations directory, using the same script
/// generators and launch-command machinery as the wizards.
///
/// A plain QWidget rather than a QDialog: it lives in the bottom dock area
/// beside Results, so it is a persistent workspace panel rather than
/// something opened, used and dismissed. That is also why it carries no
/// Close button — the dock's own title bar and the View menu own its
/// visibility.
class OrchestrationWindow : public QWidget {
    Q_OBJECT

public:
    using MaterialList =
        QList<QPair<QString, std::shared_ptr<const core::Structure>>>;

    /// `materials` are the open documents (name + structure snapshot) a node
    /// can be assigned; `pythonResolver` maps an engine to the interpreter
    /// its jobs run under (Preferences → Python & Environments);
    /// `processPanel` is the global Processes dock — every dispatched node
    /// registers there (Queued → Running → Completed/Failed) so orchestration
    /// jobs are tracked and reloadable like any other run. Null (headless)
    /// simply skips the mirroring.
    OrchestrationWindow(
        const MaterialList& materials,
        std::function<QString(core::CalculatorKind)> pythonResolver,
        ProcessManagerPanel* processPanel = nullptr,
        QWidget* parent = nullptr);

    /// Everything a factory needs to build one node's setup wizard.
    struct WizardRequest {
        OrchestrationTask task;
        std::shared_ptr<const core::Structure> structure;
        core::CalculatorKind engine;
        /// The node's input slots as (label, staged path) pairs, in link
        /// order, ready to hand straight to the wizard's setDensityBaselines()
        /// or equivalent. The label names the parent that will fill the slot
        /// when one is linked, so the wizard's combo reads like the canvas.
        ///
        /// The paths are RELATIVE and the files do not exist yet — they are
        /// what the runner stages before the node executes. A factory must not
        /// check them for existence.
        QList<QPair<QString, QString>> baselines;
    };
    /// Builds the setup wizard for a node.
    ///
    /// The canvas does not construct wizards itself. It owns scheduling and
    /// the handoff between runs; the catalogue of modules belongs to the host,
    /// which already includes every one of them. Keeping it that way is what
    /// lets this panel be linked into a headless test without dragging in
    /// fifteen wizards, their generators and their Brillouin-zone editors.
    ///
    /// Returning null means "no wizard for this task" and is reported as such.
    using WizardFactory =
        std::function<std::unique_ptr<SimulationWizardBase>(const WizardRequest&)>;
    /// Where a refusal goes.
    ///
    /// Defaults to a modal warning box, which is right for a user who just
    /// pressed Run and needs to know why nothing happened. The headless tests
    /// install a collector instead: a refusal is a normal, expected outcome of
    /// a mis-wired graph and is exactly what has to be asserted, but a modal
    /// box in a test with nobody to dismiss it is a hang rather than a
    /// failure.
    using RefusalHandler = std::function<void(const QString& message)>;
    void setRefusalHandler(RefusalHandler handler);

    /// Where a destructive action asks for confirmation.
    ///
    /// Defaults to a modal `QMessageBox::warning` with Yes/No, defaulting to
    /// No — which is the whole point of the guard. Overridden by the headless
    /// tests for the same reason refusals are: a modal box in a test with
    /// nobody to dismiss it is a hang rather than a failure, and "the canvas
    /// is NOT cleared when the user declines" is precisely what has to be
    /// asserted.
    using ConfirmHandler = std::function<bool(const QString& question)>;
    void setConfirmHandler(ConfirmHandler handler);

    /// Install the wizard catalogue. Without one, double-clicking a node says
    /// so instead of silently doing nothing; the programmatic API
    /// (configureNode) is unaffected, which is how the headless tests drive it.
    void setWizardFactory(WizardFactory factory);

    /// Install a callback asked for the open documents each time the Add
    /// Process dialog is raised.
    ///
    /// The constructor's `materials` is a SNAPSHOT, which was right when the
    /// panel was a dialog opened on demand and wrong now that it is a dock
    /// outliving every tab it was built from: without this, a orchestration added
    /// an hour into a session still offers whatever was open when the window
    /// was created. Unset (the headless tests) keeps the snapshot.
    void setMaterialsProvider(std::function<MaterialList()> provider);

    /// Where a Container gets structures that are not open documents.
    ///
    /// The canvas can open files itself, but the database browser is a large
    /// piece of the host (Materials Project, PubChem, the bulk builder and a
    /// 3D preview) and pulling it in here would put all of it into every
    /// headless test that links this panel. The host installs a callback
    /// instead; unset, the Import from Database button says so.
    ///
    /// Returns the structures the user chose, empty if they cancelled.
    using StructureImporter = std::function<
        QList<QPair<QString, std::shared_ptr<const core::Structure>>>(
            QWidget* parent)>;
    void setDatabaseImporter(StructureImporter importer);

    // -- Programmatic pipeline API -------------------------------------------
    // What the buttons and gestures call, exposed so a pipeline can also be
    // built and driven headlessly (the Si validation test, future scripting).
    /// Add a node for `task` with `engine` and NO structure of its own — the
    /// shape every node created from the UI has. It takes its geometry from
    /// whatever is linked into its input port.
    OrchestrationNodeItem* addProcessNode(OrchestrationTask task,
                                          core::CalculatorKind engine);
    /// Add a node seeded with materials()[materialIndex] as its own source
    /// structure.
    ///
    /// Not reachable from the UI any more — structures enter a pipeline
    /// through a Structure Container, which is the one place that has to be
    /// looked at to know what a run computed. Kept for scripting and for the
    /// tests, where a one-node fixture with its own geometry is exactly the
    /// point. Returns null for an invalid material index.
    OrchestrationNodeItem* addProcessNode(OrchestrationTask task, int materialIndex,
                                     core::CalculatorKind engine);
    /// Link two nodes (parent → child). No-op on duplicates; refuses cycles.
    void linkNodes(OrchestrationNodeItem* from, OrchestrationNodeItem* to);
    /// Commit a configuration to a node, exactly as the wizard's "Save
    /// process node" button does.
    ///
    /// Re-configuring a node that has already RUN invalidates it and
    /// everything downstream of it (back to Pending): their results were
    /// computed from the settings that just changed, so leaving them Done
    /// would let a Resume treat stale output as current. Upstream results are
    /// untouched — that is the whole point of resuming.
    void configureNode(OrchestrationNodeItem* node, const QString& script,
                       const QString& python, const QString& runCommand,
                       core::CalculatorKind engine);
    /// Fill a Container node's structure list. Same invalidation rule as
    /// configureNode.
    void setNodeBatchItems(OrchestrationNodeItem* node,
                           const QList<OrchestrationNodeItem::BatchItem>& items);
    /// Set a Supercell node's repetitions. Same invalidation rule.
    void setNodeSupercell(OrchestrationNodeItem* node,
                          const SupercellSpec& spec);
    /// Set a Defect Generator node's recipe. Same invalidation rule.
    void setNodeDefectSpec(OrchestrationNodeItem* node, const DefectSpec& spec);
    /// Set a TDB Generator node's assessment settings. Same invalidation rule.
    void setNodeTdbGenerator(OrchestrationNodeItem* node,
                             const TdbGeneratorSpec& spec);
    /// Set an SQS Generator node's compositions. Same invalidation rule.
    void setNodeSqsGenerator(OrchestrationNodeItem* node,
                             const SqsGeneratorSpec& spec);
    /// Set an ECI Fitter node's cluster basis. Same invalidation rule.
    void setNodeClusterExpansionFit(OrchestrationNodeItem* node,
                                    const ClusterExpansionFitSpec& spec);
    /// Set a CVM Entropy node's lattice and range. Same invalidation rule.
    void setNodeCvmEntropy(OrchestrationNodeItem* node,
                           const CvmEntropySpec& spec);

    /// The folder the current (or last) run staged everything under. Empty
    /// before the first send.
    QString orchestrationRoot() const { return orchestrationRoot_; }
    /// How many passes the pipeline makes, one per Container item (1 with no
    /// Container node). Valid after a send.
    int batchLength() const { return batchLength_; }
    /// The summary of the current (or last) run.
    const core::WorkflowReport& report() const { return report_; }
    /// True when a Resume would do something: a previous run exists and left
    /// at least one node not Done.
    bool canResume() const;
    /// Load a workflow document from `path`, REPLACING whatever is on the
    /// canvas. Asks first when there is something to replace.
    ///
    /// The file is loaded into a scratch canvas before this one is touched,
    /// so a document that turns out to be malformed half way through leaves
    /// the existing pipeline intact rather than replacing it with a fragment.
    /// Returns false when the user declined or the file was rejected, having
    /// reported why through the refusal handler.
    bool openWorkflow(const QString& path);

    /// Every node, in creation order — which is the order they serialize in.
    const std::vector<OrchestrationNodeItem*>& nodes() const { return nodes_; }
    /// The graph's links as (parent, child) pairs, in the order they were
    /// drawn. That order is load-bearing: link order is slot order.
    QList<QPair<OrchestrationNodeItem*, OrchestrationNodeItem*>> links() const;
    /// The smallest rectangle containing every node, in scene coordinates.
    /// Null when the canvas is empty.
    QRectF nodesBoundingRect() const;
    /// What the canvas currently shows, in scene coordinates. Exposed so a
    /// headless test can assert that Fit to Screen actually framed the graph.
    QRectF visibleSceneRect() const;

public Q_SLOTS:
    /// Queue every node (status → Waiting) and start executing the pipeline
    /// in dependency order, once per Container item.
    void sendToProcesses();
    /// Re-run only what has not succeeded, in the SAME orchestration folder:
    /// every Failed, Skipped and never-started node is re-queued, every Done
    /// node keeps its status, its directory and its artifacts.
    ///
    /// This is the loop the panel is built around: a pipeline that dies six
    /// hours in on a bad parameter should cost the bad node and its
    /// descendants, not the six hours.
    void resumeFromFailure();
    /// Stop the running orchestration: kill the job in flight and leave the
    /// queue unrun.
    ///
    /// Everything already finished KEEPS its status, its directory and its
    /// artifacts, so this is not an undo — it is the counterpart of Resume.
    /// A pipeline that is visibly heading somewhere wrong (a mis-set cutoff, a
    /// structure that should have been relaxed first) otherwise has to be
    /// waited out or the application killed, and killing the application
    /// abandons the run directory mid-write.
    ///
    /// The node in flight is marked Failed rather than Done — it produced no
    /// complete result, and its directory holds a partial one. Nodes still
    /// queued are marked Skipped, which is what they are: not run. Neither is
    /// Done, so canResume() is true afterwards and Resume re-queues exactly
    /// them.
    void abortOrchestration();
    /// Zoom and pan so every node is visible, with a margin.
    void fitToScreen();
    /// Rearrange every node into layered columns in execution order.
    ///
    /// Longest-path layering (a node sits one column right of its
    /// last-finishing parent) with barycentre sweeps to reduce link crossings,
    /// then Fit to Screen. Purely cosmetic — it moves nodes, never the graph.
    void autoLayout();
    /// Delete every node and link, after an explicit confirmation.
    ///
    /// Guarded rather than undoable: the canvas is not in the undo stack, and
    /// a pipeline is minutes of wiring that a mis-click would otherwise cost
    /// in full.
    void clearOrchestration();
    /// Write the pipeline to a JSON file the user picks — the document
    /// `calango-cli` runs on a cluster. Structures travel inside it, so the
    /// file is self-contained and can simply be copied across.
    void exportWorkflow();
    /// Ask for a workflow file and load it, REPLACING the canvas.
    void openWorkflow();

Q_SIGNALS:
    /// A node's job started: `processId` is its Processes-panel row id,
    /// `directory` its job directory. The host uses this to register the
    /// run with the Results panel (metric plots, process selector) exactly
    /// like a standalone job.
    /// `label` is the full name, for the Processes panel's wide list;
    /// `tabTitle` is the short one a workspace tab can actually show.
    void nodeStarted(int processId, const QString& label,
                     const QString& tabTitle, const QString& directory);
    /// One live geometry streamed by the running node — the CALANGO_FRAME
    /// blocks a relaxation or an MD run emits as it goes.
    ///
    /// Forwarded rather than left on this panel's private JobRunner so the
    /// host can drive the viewport and the trajectory timeline from a orchestration
    /// node exactly as it does from a standalone run. `processId` says which
    /// run the frame belongs to; the host opens the trajectory tab lazily on
    /// the first one, so a node that streams nothing costs nothing.
    void nodeFrameStreamed(int processId,
                           const std::shared_ptr<core::Structure>& frame);
    /// The started node's job ended (successfully or not).
    void nodeFinished(int processId, bool success);
    /// The whole pipeline reached its end — every node terminal, every batch
    /// pass made — or was stopped. Carries the summary of what happened.
    void runFinished(const core::WorkflowReport& report);
    /// A TRANSFORM node produced a structure — a Container emitting its batch
    /// item, a Supercell Builder its expansion, a Defect Generator its
    /// decorated cell.
    ///
    /// Transforms have no job and no trajectory, so they emit none of the
    /// signals above: they finish inside startNode() in a few hundred
    /// microseconds, and until this existed their output reached the disk and
    /// nothing else. That is the wrong default for the one family of nodes
    /// whose entire result IS a structure — a Supercell Builder you cannot
    /// look at is a node you have to take on faith.
    ///
    /// `nodeId` is the CANVAS node's stable id, not the process id, and that
    /// is what the host should key a tab on: a batch run re-runs the same node
    /// once per container item with a fresh process id each time, and keying
    /// on the process would open one tab per item.
    /// `variant` separates outputs of the same node that are DIFFERENT
    /// materials rather than successive versions of one. A Supercell node
    /// re-run for each container item is the same material each pass and
    /// reuses its tab (variant 0); a Defect Generator in "one material per
    /// defect" mode emits a genuinely different material each pass, and they
    /// have to accumulate side by side — showing a set of dopants one at a
    /// time in a single tab is not showing the set.
    void nodeStructureProduced(int nodeId, int variant, const QString& label,
                               const std::shared_ptr<const core::Structure>& structure);

private Q_SLOTS:
    void addNode();
    /// Double-click on empty canvas: same Add Process dialog, but the new
    /// node lands where the user clicked.
    void addNodeAt(const QPointF& scenePos);
    void removeSelected();
    void onJobFinished(int exitCode, bool crashed);
    /// Double-click: open the node's standard setup wizard in orchestration mode
    /// ("Save process node" instead of Run) and commit the result.
    void openNodeWizard(OrchestrationNodeItem* node);

private:
    /// The Add Process dialog; on accept, creates the node (at `scenePos`
    /// when given, else staggered left-to-right).
    void promptAddNode(const QPointF* scenePos);
    void connectNodes(OrchestrationNodeItem* from, OrchestrationNodeItem* to);
    /// True if linking from→to would close a cycle (child reaches parent).
    bool wouldCreateCycle(OrchestrationNodeItem* from, OrchestrationNodeItem* to) const;
    QList<OrchestrationNodeItem*> parentsOf(OrchestrationNodeItem* node) const;
    /// The next runnable node: pending, every parent Done. Null when the
    /// orchestration is finished or blocked.
    OrchestrationNodeItem* nextRunnable() const;
    /// Start nodes until one is genuinely RUNNING an external job or nothing
    /// is left to start.
    ///
    /// A single loop rather than a call per completion, because the transform
    /// nodes finish inside startNode(): a Container feeding a Supercell
    /// feeding a Defect Generator would otherwise need three round trips
    /// through the job runner's finished signal, which never fires for them.
    void pump();
    /// Stage + launch (or, for a transform, perform) one node's work. False
    /// when it was refused or staging failed; true both when a job is now
    /// running and when a transform already completed.
    bool startNode(OrchestrationNodeItem* node);
    /// Do a transform node's work in process. Fills `record`'s data
    /// provenance and, when `produced` is given, the structure it made — the
    /// host shows that in the viewport, and re-reading it from disk to do so
    /// would be a second answer to a question already answered here.
    bool runTransform(OrchestrationNodeItem* node, const QString& dir,
                      ProvenanceRecord& record, QString* error,
                      std::shared_ptr<const core::Structure>* produced = nullptr);
    /// Delete every node and link and reset the run state, unconditionally.
    /// The shared half of Clear Orchestration and of opening a workflow over
    /// the top of one.
    void clearGraph();
    /// Mark every descendant of a failed node Skipped — their inputs will
    /// never exist.
    void skipDescendants(OrchestrationNodeItem* node);
    /// Reset `node` and everything downstream to Pending, so a Resume
    /// recomputes them. Used when a node's configuration changes after it ran.
    void invalidateFrom(OrchestrationNodeItem* node);
    /// Nodes whose results depend on which Container item is being processed —
    /// every Container plus everything reachable from one. Only these are
    /// re-queued between batch items.
    bool dependsOnContainer(OrchestrationNodeItem* node) const;
    /// Move to the next Container item and re-queue the nodes that depend on
    /// one. False when the last item is done.
    bool advanceBatch();
    /// Queue `node` for execution and give it a fresh Processes-panel row.
    void enqueue(OrchestrationNodeItem* node);
    /// Directory for `node`'s next attempt, created. Empty on failure.
    QString makeJobDirectory(OrchestrationNodeItem* node);
    /// Label of the Container item currently being processed, for job
    /// directories and process rows. Empty when the graph has no Container.
    QString batchLabel() const;
    /// What a workspace tab showing this node's output is called — short, and
    /// decided in one place so a live trajectory tab and a transform tab from
    /// the same pipeline are named the same way.
    QString tabTitleFor(const OrchestrationNodeItem* node) const;
    /// The batch counter decomposed into its two dimensions: which container
    /// item this pass uses, and which defect of a separate-mode generator.
    /// batchIndex_ runs over their product, defects varying fastest.
    int containerBatchIndex() const;
    int defectBatchIndex() const;
    /// Seed a record with everything known before the node runs.
    ProvenanceRecord beginProvenance(OrchestrationNodeItem* node,
                                     const QString& dir) const;
    /// Complete `record` with the node's outputs and final status, and write
    /// it. `excluded` are the files the canvas itself staged.
    void finishProvenance(OrchestrationNodeItem* node, ProvenanceRecord record,
                          int exitCode, const QStringList& excluded);
    /// Rewrite the orchestration-level manifest (graph, batch plan, per-node
    /// state). Cheap and called on every state change, so the folder is
    /// readable even while the run is in flight.
    void writeManifest() const;
    /// Enable/disable Run and Resume for the current state.
    void updateRunControls();
    /// Report why a node was refused, through refusalHandler_ if one is
    /// installed and a modal warning otherwise.
    void refuse(const QString& message);
    /// Ask before doing something destructive; false means "do not".
    bool confirm(const QString& question);
    /// Repaint every node's "which parent fills which slot" summary. Called
    /// after anything that changes the graph, since a link drawn to one node
    /// changes what a DIFFERENT node reads.
    void refreshInputSummaries();
    /// The slot list for `node`, each paired with the parent that fills it
    /// (null when nothing is linked for it).
    QList<QPair<OrchestrationInputSlot, OrchestrationNodeItem*>>
    resolveInputs(OrchestrationNodeItem* node) const;
    /// A structure standing in for what will actually flow into `node` at run
    /// time, for the benefit of a setup wizard that has to offer per-element
    /// controls (cutoff suggestions, a k-path, an atom picker) before
    /// anything has run.
    ///
    /// Resolved by walking UPSTREAM to the first node that owns geometry —
    /// normally a Container, whose first item it returns. Null when nothing
    /// upstream has any, which a wizard must tolerate: the node is
    /// configurable before its Container is filled, and the script it writes
    /// names `structure.extxyz` either way.
    std::shared_ptr<const core::Structure>
    representativeStructure(OrchestrationNodeItem* node) const;

    /// Mirror one node's state onto its Processes-panel row, if any.
    void updateProcessPanel(OrchestrationNodeItem* node);

    /// Record what became of `node` on THIS pass into the run report.
    ///
    /// Called at every terminal transition rather than swept up at the end,
    /// and that is not a style choice: a batch re-queues its nodes for each
    /// container item, so by the time the run finishes the canvas holds only
    /// the LAST pass's statuses. What happened to structure 3 of 12 exists
    /// only if it was written down when it happened.
    void recordOutcome(OrchestrationNodeItem* node, const QString& note = {});
    /// Close the report, write it beside the run, and announce it.
    void finishRun(bool completed);

    MaterialList materials_;
    std::function<MaterialList()> materialsProvider_;
    WizardFactory wizardFactory_;
    StructureImporter databaseImporter_;
    RefusalHandler refusalHandler_;
    ConfirmHandler confirmHandler_;
    std::function<QString(core::CalculatorKind)> pythonResolver_;
    ProcessManagerPanel* processPanel_ = nullptr;

    OrchestrationScene* scene_ = nullptr;
    QGraphicsView* view_ = nullptr;
    /// The three controls the panel keeps a handle on. Run and Resume are
    /// disabled for the duration of a run (and Resume also whenever there is
    /// nothing to resume); Abort is enabled for exactly that duration and
    /// disabled otherwise — between them they are never both live, which is
    /// the whole of the run-state UI. The rest are icon buttons wired and
    /// forgotten.
    QPushButton* runButton_ = nullptr;
    QPushButton* resumeButton_ = nullptr;
    QPushButton* abortButton_ = nullptr;

    /// Set between pressing Abort and the job runner reporting the kill.
    ///
    /// Needed because a terminated job arrives at onJobFinished as an ordinary
    /// nonzero exit, and the ordinary response to that is to fail the node and
    /// PUMP THE NEXT ONE — which would start a fresh job moments after the
    /// user asked for the pipeline to stop. This flag is what distinguishes
    /// "this node failed" from "the user stopped the run".
    bool aborting_ = false;

    std::vector<OrchestrationNodeItem*> nodes_;
    std::vector<OrchestrationEdgeItem*> edges_;
    jobs::JobRunner* jobRunner_ = nullptr;
    OrchestrationNodeItem* runningNode_ = nullptr;
    /// Provenance for the node currently running, opened at launch and closed
    /// when the job finishes — the only place the start timestamp and the
    /// staged-input list survive between the two.
    ProvenanceRecord runningRecord_;
    QStringList runningStagedFiles_;
    QString orchestrationRoot_; ///< per-run folder all node jobs stage under
    int nextNodeId_ = 1;
    int launchedCount_ = 0;
    /// Container fan-out: `batchLength_` passes over the pipeline, currently
    /// on `batchIndex_`. Both are 0/1 for a graph with no Container node.
    int batchIndex_ = 0;
    int batchLength_ = 1;
    /// Materials a separate-mode Defect Generator contributes per container
    /// item — the fast digit's radix. 1 when there is no such node.
    int batchDefectSpan_ = 1;
    QString runStartedUtc_;
    /// Accumulated as the run goes; written to the orchestration folder and
    /// handed to the host when it ends.
    core::WorkflowReport report_;
};

} // namespace calango::gui
