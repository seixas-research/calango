#pragma once

#include "core/CalculatorConfig.hpp"

#include <QDialog>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QList>
#include <QPair>
#include <QString>

#include <functional>
#include <memory>
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

class WorkflowEdgeItem;
class WorkflowScene;

/// The processes a workflow node can represent. A superset of
/// core::TaskKind: the phonon workflow is its own script generator rather
/// than a TaskKind, but on the canvas it is a process like any other.
enum class WorkflowTask {
    GeometryOptimization,
    SinglePoint,
    MolecularDynamics,
    Phonon,
};

/// One simulation process on the workflow canvas: a draggable rounded
/// rectangle showing the process name, the material it runs on and the
/// calculator, with an input port on the left edge and an output port on the
/// right. The status strip along the top edge tracks execution.
///
/// Double-clicking the node opens the process's standard setup wizard in
/// workflow mode: its Run button becomes "Save process node", and accepting
/// commits the generated script (plus interpreter and launch command) here
/// instead of executing anything. An unconfigured node runs with defaults.
class WorkflowNodeItem : public QGraphicsRectItem {
public:
    enum class Status { Pending, Waiting, Running, Done, Failed, Skipped };

    WorkflowNodeItem(int id, const QString& title,
                     WorkflowTask task, const QString& materialName,
                     std::shared_ptr<const core::Structure> structure,
                     core::CalculatorKind engine);

    int id() const { return id_; }
    WorkflowTask task() const { return task_; }
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

    Status status() const { return status_; }
    void setStatus(Status status);
    /// Row id in the global Processes panel for the current send (-1 when
    /// the workflow runs without a panel, e.g. headless tests).
    int processTaskId() const { return processTaskId_; }
    void setProcessTaskId(int id) { processTaskId_ = id; }
    /// Where this node's finished job lives, once it ran.
    QString jobDirectory() const { return jobDirectory_; }
    void setJobDirectory(const QString& directory)
    {
        jobDirectory_ = directory;
    }

    /// Port centres in SCENE coordinates — the edges anchor here.
    QPointF inputPortScenePos() const;
    QPointF outputPortScenePos() const;
    /// True when `scenePos` falls on the output port's grab zone.
    bool hitsOutputPort(const QPointF& scenePos) const;

    void registerEdge(WorkflowEdgeItem* edge) { edges_.push_back(edge); }
    void unregisterEdge(WorkflowEdgeItem* edge);

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
    WorkflowTask task_;
    QString materialName_;
    std::shared_ptr<const core::Structure> structure_;
    core::CalculatorKind engine_;
    Status status_ = Status::Pending;
    int processTaskId_ = -1;
    QString jobDirectory_;
    QString configuredScript_;
    QString configuredPython_;
    QString configuredRunCommand_;
    std::vector<WorkflowEdgeItem*> edges_;
};

/// A directed link between two nodes: the child consumes the parent's
/// outputs (relaxed geometry, saved ground state) as its inputs.
class WorkflowEdgeItem : public QGraphicsPathItem {
public:
    WorkflowEdgeItem(WorkflowNodeItem* from, WorkflowNodeItem* to);
    WorkflowNodeItem* from() const { return from_; }
    WorkflowNodeItem* to() const { return to_; }
    void updatePath();

private:
    WorkflowNodeItem* from_;
    WorkflowNodeItem* to_;
};

/// Scene that owns the link-drawing gesture: press on a node's output port,
/// drag (a dashed preview follows), release on another node to connect.
class WorkflowScene : public QGraphicsScene {
    Q_OBJECT

public:
    using QGraphicsScene::QGraphicsScene;

Q_SIGNALS:
    /// The user drew a link from `from`'s output port onto `to`.
    void connectionRequested(WorkflowNodeItem* from, WorkflowNodeItem* to);
    /// The user double-clicked a node — open its setup wizard.
    void nodeActivated(WorkflowNodeItem* node);
    /// The user double-clicked empty canvas — add a new process node there.
    void addNodeRequested(const QPointF& scenePos);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    WorkflowNodeItem* nodeAt(const QPointF& scenePos) const;

    WorkflowNodeItem* pendingFrom_ = nullptr;
    QGraphicsPathItem* pendingPreview_ = nullptr;
};

/// The "Workflow" dock: a node-based editor for automated simulation
/// pipelines. Processes are nodes on a pannable, zoomable canvas; drawing a
/// link from one node's output port to another node makes the second run
/// after the first, consuming its outputs — the relaxed geometry of a
/// finished parent becomes the child's input structure, and a parent's saved
/// ground state (.gpw) is carried into the child's directory.
///
/// Execution is sequential in dependency order (the app runs one local job
/// at a time by design); each node stages its own job directory under a
/// per-workflow folder in the simulations directory, using the same script
/// generators and launch-command machinery as the wizards.
///
/// A plain QWidget rather than a QDialog: it lives in the bottom dock area
/// beside Results, so it is a persistent workspace panel rather than
/// something opened, used and dismissed. That is also why it carries no
/// Close button — the dock's own title bar and the View menu own its
/// visibility.
class WorkflowWindow : public QWidget {
    Q_OBJECT

public:
    using MaterialList =
        QList<QPair<QString, std::shared_ptr<const core::Structure>>>;

    /// `materials` are the open documents (name + structure snapshot) a node
    /// can be assigned; `pythonResolver` maps an engine to the interpreter
    /// its jobs run under (Preferences → Python & Environments);
    /// `processPanel` is the global Processes dock — every dispatched node
    /// registers there (Queued → Running → Completed/Failed) so workflow
    /// jobs are tracked and reloadable like any other run. Null (headless)
    /// simply skips the mirroring.
    WorkflowWindow(
        const MaterialList& materials,
        std::function<QString(core::CalculatorKind)> pythonResolver,
        ProcessManagerPanel* processPanel = nullptr,
        QWidget* parent = nullptr);

    /// Install a callback asked for the open documents each time the Add
    /// Process dialog is raised.
    ///
    /// The constructor's `materials` is a SNAPSHOT, which was right when the
    /// panel was a dialog opened on demand and wrong now that it is a dock
    /// outliving every tab it was built from: without this, a workflow added
    /// an hour into a session still offers whatever was open when the window
    /// was created. Unset (the headless tests) keeps the snapshot.
    void setMaterialsProvider(std::function<MaterialList()> provider);

    // -- Programmatic pipeline API -------------------------------------------
    // What the buttons and gestures call, exposed so a pipeline can also be
    // built and driven headlessly (the Si validation test, future scripting).
    /// Add a node for `task` on materials()[materialIndex] with `engine`.
    /// Returns null for an invalid material index.
    WorkflowNodeItem* addProcessNode(WorkflowTask task, int materialIndex,
                                     core::CalculatorKind engine);
    /// Link two nodes (parent → child). No-op on duplicates; refuses cycles.
    void linkNodes(WorkflowNodeItem* from, WorkflowNodeItem* to);
    /// Commit a configuration to a node, exactly as the wizard's "Save
    /// process node" button does.
    void configureNode(WorkflowNodeItem* node, const QString& script,
                       const QString& python, const QString& runCommand,
                       core::CalculatorKind engine);
    const std::vector<WorkflowNodeItem*>& processNodes() const
    {
        return nodes_;
    }

public Q_SLOTS:
    /// Queue every node (status → Waiting) and start executing the pipeline
    /// in dependency order.
    void sendToProcesses();

Q_SIGNALS:
    /// A node's job started: `processId` is its Processes-panel row id,
    /// `directory` its job directory. The host uses this to register the
    /// run with the Results panel (metric plots, process selector) exactly
    /// like a standalone job.
    void nodeStarted(int processId, const QString& label,
                     const QString& directory);
    /// One live geometry streamed by the running node — the CALANGO_FRAME
    /// blocks a relaxation or an MD run emits as it goes.
    ///
    /// Forwarded rather than left on this panel's private JobRunner so the
    /// host can drive the viewport and the trajectory timeline from a workflow
    /// node exactly as it does from a standalone run. `processId` says which
    /// run the frame belongs to; the host opens the trajectory tab lazily on
    /// the first one, so a node that streams nothing costs nothing.
    void nodeFrameStreamed(int processId,
                           const std::shared_ptr<core::Structure>& frame);
    /// The started node's job ended (successfully or not).
    void nodeFinished(int processId, bool success);

private Q_SLOTS:
    void addNode();
    /// Double-click on empty canvas: same Add Process dialog, but the new
    /// node lands where the user clicked.
    void addNodeAt(const QPointF& scenePos);
    void removeSelected();
    void onJobFinished(int exitCode, bool crashed);
    /// Double-click: open the node's standard setup wizard in workflow mode
    /// ("Save process node" instead of Run) and commit the result.
    void openNodeWizard(WorkflowNodeItem* node);

private:
    /// The Add Process dialog; on accept, creates the node (at `scenePos`
    /// when given, else staggered left-to-right).
    void promptAddNode(const QPointF* scenePos);
    void connectNodes(WorkflowNodeItem* from, WorkflowNodeItem* to);
    /// True if linking from→to would close a cycle (child reaches parent).
    bool wouldCreateCycle(WorkflowNodeItem* from, WorkflowNodeItem* to) const;
    QList<WorkflowNodeItem*> parentsOf(WorkflowNodeItem* node) const;
    /// The next runnable node: pending, every parent Done. Null when the
    /// workflow is finished or blocked.
    WorkflowNodeItem* nextRunnable() const;
    /// Stage + launch one node's job; false if staging failed.
    bool startNode(WorkflowNodeItem* node);
    /// Mark every descendant of a failed node Skipped — their inputs will
    /// never exist.
    void skipDescendants(WorkflowNodeItem* node);

    /// Mirror one node's state onto its Processes-panel row, if any.
    void updateProcessPanel(WorkflowNodeItem* node);

    MaterialList materials_;
    std::function<MaterialList()> materialsProvider_;
    std::function<QString(core::CalculatorKind)> pythonResolver_;
    ProcessManagerPanel* processPanel_ = nullptr;

    WorkflowScene* scene_ = nullptr;
    QGraphicsView* view_ = nullptr;
    /// The only control the panel keeps a handle on: it is disabled for the
    /// duration of a run. The other two are icon buttons wired and forgotten.
    QPushButton* runButton_ = nullptr;

    std::vector<WorkflowNodeItem*> nodes_;
    std::vector<WorkflowEdgeItem*> edges_;
    jobs::JobRunner* jobRunner_ = nullptr;
    WorkflowNodeItem* runningNode_ = nullptr;
    QString workflowRoot_; ///< per-run folder all node jobs stage under
    int nextNodeId_ = 1;
    int launchedCount_ = 0;
};

} // namespace calango::gui
