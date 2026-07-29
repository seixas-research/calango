#include "gui/WorkflowWindow.hpp"

#include "core/AseScriptGenerator.hpp"
#include "core/PhononScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/CalculatorParameters.hpp"
#include "gui/EnginePresets.hpp"
#include "gui/GeometryOptimizationWizard.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/MolecularDynamicsWizard.hpp"
#include "gui/PhononWizard.hpp"
#include "gui/ProcessManagerPanel.hpp"
#include "gui/RunCommands.hpp"
#include "gui/ScriptStaging.hpp"
#include "gui/SettingsManager.hpp"
#include "gui/SinglePointWizard.hpp"
#include "jobs/JobRunner.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace calango::gui {

namespace {

constexpr double kNodeWidth = 200.0;
constexpr double kNodeHeight = 88.0;
constexpr double kPortRadius = 7.0;

QString taskDisplayName(WorkflowTask task)
{
    switch (task) {
    case WorkflowTask::GeometryOptimization:
        return QObject::tr("Geometry Optimization");
    case WorkflowTask::MolecularDynamics:
        return QObject::tr("Molecular Dynamics");
    case WorkflowTask::Phonon:
        return QObject::tr("Phonon");
    case WorkflowTask::SinglePoint:
        break;
    }
    return QObject::tr("Single-Point Calculation");
}

/// Directory-name slug for a task ("node_2_phonon").
QString taskSlug(WorkflowTask task)
{
    switch (task) {
    case WorkflowTask::GeometryOptimization:
        return QStringLiteral("geometry_optimization");
    case WorkflowTask::MolecularDynamics:
        return QStringLiteral("molecular_dynamics");
    case WorkflowTask::Phonon:
        return QStringLiteral("phonon");
    case WorkflowTask::SinglePoint:
        break;
    }
    return QStringLiteral("single_point");
}

QColor statusColor(WorkflowNodeItem::Status status)
{
    switch (status) {
    case WorkflowNodeItem::Status::Waiting:
        return QColor(0x8e, 0x6f, 0xc9); // queued: distinct from idle blue
    case WorkflowNodeItem::Status::Running:
        return QColor(0xff, 0x9e, 0x1a);
    case WorkflowNodeItem::Status::Done:
        return QColor(0x2c, 0xa0, 0x2c);
    case WorkflowNodeItem::Status::Failed:
        return QColor(0xd6, 0x27, 0x28);
    case WorkflowNodeItem::Status::Skipped:
        return QColor(0x8a, 0x8a, 0x8a);
    case WorkflowNodeItem::Status::Pending:
        break;
    }
    return QColor(0x66, 0x99, 0xff);
}

QString statusText(WorkflowNodeItem::Status status)
{
    switch (status) {
    case WorkflowNodeItem::Status::Waiting:
        return QObject::tr("waiting");
    case WorkflowNodeItem::Status::Running:
        return QObject::tr("running");
    case WorkflowNodeItem::Status::Done:
        return QObject::tr("done");
    case WorkflowNodeItem::Status::Failed:
        return QObject::tr("failed");
    case WorkflowNodeItem::Status::Skipped:
        return QObject::tr("skipped");
    case WorkflowNodeItem::Status::Pending:
        break;
    }
    return QObject::tr("pending");
}

/// The canvas: wheel zooms about the cursor, middle-drag pans, left-drag
/// moves nodes (or rubber-band selects on empty space).
class WorkflowView : public QGraphicsView {
public:
    explicit WorkflowView(QGraphicsScene* scene, QWidget* parent = nullptr)
        : QGraphicsView(scene, parent)
    {
        setRenderHint(QPainter::Antialiasing, true);
        setDragMode(QGraphicsView::RubberBandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setBackgroundBrush(QColor(0xf2, 0xf3, 0xf5));
    }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        // Clamp the cumulative zoom so the canvas cannot be lost to a
        // runaway scroll.
        const double current = transform().m11();
        if ((factor > 1.0 && current < 3.0)
            || (factor < 1.0 && current > 0.2))
            scale(factor, factor);
        event->accept();
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::MiddleButton) {
            panning_ = true;
            panStart_ = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (panning_) {
            const QPoint delta = event->pos() - panStart_;
            panStart_ = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                                            - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value()
                                          - delta.y());
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::MiddleButton && panning_) {
            panning_ = false;
            unsetCursor();
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

private:
    bool panning_ = false;
    QPoint panStart_;
};

} // namespace

// ---------------------------------------------------------------------------
// WorkflowNodeItem
// ---------------------------------------------------------------------------

WorkflowNodeItem::WorkflowNodeItem(
    int id, const QString& title, WorkflowTask task,
    const QString& materialName,
    std::shared_ptr<const core::Structure> structure,
    core::CalculatorKind engine)
    : QGraphicsRectItem(0.0, 0.0, kNodeWidth, kNodeHeight)
    , id_(id)
    , title_(title)
    , task_(task)
    , materialName_(materialName)
    , structure_(std::move(structure))
    , engine_(engine)
{
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemSendsScenePositionChanges, true);
    setToolTip(QObject::tr(
        "%1 on %2 with %3.\nDouble-click to open its setup wizard "
        "(\"Save process node\" commits the configuration here). Drag from "
        "the right-hand port onto another node to run that node after this "
        "one, feeding it these results.")
                   .arg(title_, materialName_,
                        EnginePresets::displayName(engine_)));
}

void WorkflowNodeItem::setConfiguration(const QString& script,
                                        const QString& python,
                                        const QString& runCommand,
                                        core::CalculatorKind engine)
{
    configuredScript_ = script;
    configuredPython_ = python;
    configuredRunCommand_ = runCommand;
    engine_ = engine;
    update(); // the calculator line may have changed
}

void WorkflowNodeItem::setStatus(Status status)
{
    status_ = status;
    update();
}

QPointF WorkflowNodeItem::inputPortScenePos() const
{
    return mapToScene(QPointF(0.0, kNodeHeight / 2.0));
}

QPointF WorkflowNodeItem::outputPortScenePos() const
{
    return mapToScene(QPointF(kNodeWidth, kNodeHeight / 2.0));
}

bool WorkflowNodeItem::hitsOutputPort(const QPointF& scenePos) const
{
    // A generous grab zone: the port is 7 px on screen and a link gesture
    // that misses by two pixels should still start.
    return QLineF(scenePos, outputPortScenePos()).length()
        <= kPortRadius * 2.0;
}

void WorkflowNodeItem::unregisterEdge(WorkflowEdgeItem* edge)
{
    edges_.erase(std::remove(edges_.begin(), edges_.end(), edge),
                 edges_.end());
}

QRectF WorkflowNodeItem::boundingRect() const
{
    // The ports stick kPortRadius past the rect's left/right edges (plus
    // their outline), and the selection pen is 2 px. QGraphicsRectItem's own
    // bounding rect covers none of that, so dragging repainted a region
    // narrower than what paint() had touched — the classic ghost-trail bug:
    // the port circles stayed behind as smears wherever the node had been.
    const double margin = kPortRadius + 2.0;
    return rect().adjusted(-margin, -margin, margin, margin);
}

QVariant WorkflowNodeItem::itemChange(GraphicsItemChange change,
                                      const QVariant& value)
{
    if (change == ItemScenePositionHasChanged)
        for (WorkflowEdgeItem* edge : edges_)
            edge->updatePath();
    return QGraphicsRectItem::itemChange(change, value);
}

void WorkflowNodeItem::paint(QPainter* painter,
                             const QStyleOptionGraphicsItem*, QWidget*)
{
    const QRectF box = rect();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Body.
    painter->setPen(QPen(isSelected() ? QColor(0x1f, 0x77, 0xb4)
                                      : QColor(0x60, 0x60, 0x60),
                         isSelected() ? 2.0 : 1.2));
    painter->setBrush(QColor(0xff, 0xff, 0xff));
    painter->drawRoundedRect(box, 6.0, 6.0);

    // Status strip along the top edge.
    painter->setPen(Qt::NoPen);
    painter->setBrush(statusColor(status_));
    painter->drawRoundedRect(QRectF(box.left(), box.top(), box.width(), 6.0),
                             3.0, 3.0);

    // Ports. Filled output (links start here), hollow input.
    painter->setPen(QPen(QColor(0x40, 0x40, 0x40), 1.2));
    painter->setBrush(QColor(0x1f, 0x77, 0xb4));
    painter->drawEllipse(QPointF(box.right(), box.center().y()), kPortRadius,
                         kPortRadius);
    painter->setBrush(QColor(0xff, 0xff, 0xff));
    painter->drawEllipse(QPointF(box.left(), box.center().y()), kPortRadius,
                         kPortRadius);

    // Metadata: process, material, calculator, status.
    painter->setPen(QColor(0x20, 0x20, 0x20));
    QFont bold = painter->font();
    bold.setBold(true);
    painter->setFont(bold);
    painter->drawText(QRectF(box.left() + 12, box.top() + 10, box.width() - 24,
                             18),
                      Qt::AlignLeft | Qt::AlignVCenter, title_);
    QFont normal = painter->font();
    normal.setBold(false);
    painter->setFont(normal);
    painter->setPen(QColor(0x45, 0x45, 0x45));
    painter->drawText(
        QRectF(box.left() + 12, box.top() + 30, box.width() - 24, 16),
        Qt::AlignLeft | Qt::AlignVCenter,
        QObject::tr("Material: %1").arg(materialName_));
    painter->drawText(
        QRectF(box.left() + 12, box.top() + 47, box.width() - 24, 16),
        Qt::AlignLeft | Qt::AlignVCenter,
        QObject::tr("Calculator: %1")
            .arg(EnginePresets::displayName(engine_)));
    painter->setPen(statusColor(status_).darker(120));
    painter->drawText(
        QRectF(box.left() + 12, box.top() + 64, box.width() - 24, 16),
        Qt::AlignLeft | Qt::AlignVCenter, statusText(status_));
}

// ---------------------------------------------------------------------------
// WorkflowEdgeItem
// ---------------------------------------------------------------------------

WorkflowEdgeItem::WorkflowEdgeItem(WorkflowNodeItem* from,
                                   WorkflowNodeItem* to)
    : from_(from)
    , to_(to)
{
    setPen(QPen(QColor(0x50, 0x50, 0x50), 2.0));
    setZValue(-1.0); // under the nodes, so it reads as plumbing
    from_->registerEdge(this);
    to_->registerEdge(this);
    updatePath();
}

void WorkflowEdgeItem::updatePath()
{
    const QPointF a = from_->outputPortScenePos();
    const QPointF b = to_->inputPortScenePos();
    // A horizontal-tangent cubic: the standard node-editor ribbon, readable
    // at any relative placement of the two nodes.
    const double reach = std::max(40.0, std::abs(b.x() - a.x()) * 0.5);
    QPainterPath path(a);
    path.cubicTo(a + QPointF(reach, 0.0), b - QPointF(reach, 0.0), b);
    setPath(path);
}

// ---------------------------------------------------------------------------
// WorkflowScene — the link-drawing gesture
// ---------------------------------------------------------------------------

WorkflowNodeItem* WorkflowScene::nodeAt(const QPointF& scenePos) const
{
    // dynamic_cast, not qgraphicsitem_cast: the nodes define no custom
    // type() id, and the item-cast would happily match ANY rect item.
    for (QGraphicsItem* item : items(scenePos))
        if (auto* node = dynamic_cast<WorkflowNodeItem*>(item))
            return node;
    return nullptr;
}

void WorkflowScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Search a little around the cursor: the port sticks out of the
        // node's rect, so items() at the exact point can miss it.
        for (QGraphicsItem* item : items(
                 QRectF(event->scenePos() - QPointF(10, 10), QSizeF(20, 20)),
                 Qt::IntersectsItemBoundingRect)) {
            auto* node = dynamic_cast<WorkflowNodeItem*>(item);
            if (node && node->hitsOutputPort(event->scenePos())) {
                pendingFrom_ = node;
                pendingPreview_ = addPath(QPainterPath(),
                                          QPen(QColor(0x1f, 0x77, 0xb4), 2.0,
                                               Qt::DashLine));
                event->accept();
                return; // the gesture owns this press — no node drag
            }
        }
    }
    QGraphicsScene::mousePressEvent(event);
}

void WorkflowScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (pendingFrom_ && pendingPreview_) {
        const QPointF a = pendingFrom_->outputPortScenePos();
        const QPointF b = event->scenePos();
        const double reach = std::max(40.0, std::abs(b.x() - a.x()) * 0.5);
        QPainterPath path(a);
        path.cubicTo(a + QPointF(reach, 0.0), b - QPointF(reach, 0.0), b);
        pendingPreview_->setPath(path);
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

void WorkflowScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (WorkflowNodeItem* node = nodeAt(event->scenePos())) {
        Q_EMIT nodeActivated(node);
        event->accept();
        return;
    }
    // Empty canvas: a double-click means "add a process here" — the fastest
    // way to grow a pipeline, mirroring every node editor's convention.
    if (event->button() == Qt::LeftButton) {
        Q_EMIT addNodeRequested(event->scenePos());
        event->accept();
        return;
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}

void WorkflowScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (pendingFrom_) {
        WorkflowNodeItem* target = nodeAt(event->scenePos());
        WorkflowNodeItem* from = pendingFrom_;
        removeItem(pendingPreview_);
        delete pendingPreview_;
        pendingPreview_ = nullptr;
        pendingFrom_ = nullptr;
        if (target && target != from)
            Q_EMIT connectionRequested(from, target);
        event->accept();
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------------------
// WorkflowWindow
// ---------------------------------------------------------------------------

WorkflowWindow::WorkflowWindow(
    const QList<QPair<QString, std::shared_ptr<const core::Structure>>>&
        materials,
    std::function<QString(core::CalculatorKind)> pythonResolver,
    ProcessManagerPanel* processPanel, QWidget* parent)
    : QDialog(parent)
    , materials_(materials)
    , pythonResolver_(std::move(pythonResolver))
    , processPanel_(processPanel)
{
    setWindowTitle(tr("Workflow"));
    resize(1000, 640);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Each node is one simulation process; a link drawn from a node's "
           "right-hand port onto another node runs the second after the "
           "first and feeds it the results — a relaxed geometry becomes the "
           "next input structure, a saved ground state (.gpw) rides along. "
           "Double-click empty canvas to add a process there; double-click a "
           "node to configure it in its setup wizard. Wheel zooms, "
           "middle-drag pans."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    scene_ = new WorkflowScene(this);
    scene_->setSceneRect(-2000, -2000, 4000, 4000);
    connect(scene_, &WorkflowScene::connectionRequested, this,
            &WorkflowWindow::connectNodes);
    connect(scene_, &WorkflowScene::nodeActivated, this,
            &WorkflowWindow::openNodeWizard);
    connect(scene_, &WorkflowScene::addNodeRequested, this,
            &WorkflowWindow::addNodeAt);
    view_ = new WorkflowView(scene_, this);
    layout->addWidget(view_, 1);

    auto* controls = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add Process…"), this);
    auto* removeButton = new QPushButton(tr("Remove Selected"), this);
    runButton_ = new QPushButton(tr("Send to Processes"), this);
    runButton_->setToolTip(
        tr("Queue every node (they show as \"waiting\") and execute the "
           "pipeline in dependency order, one process at a time."));
    auto* closeButton = new QPushButton(tr("Close"), this);
    controls->addWidget(addButton);
    controls->addWidget(removeButton);
    controls->addStretch(1);
    statusLabel_ = new QLabel(this);
    controls->addWidget(statusLabel_);
    controls->addWidget(runButton_);
    controls->addWidget(closeButton);
    layout->addLayout(controls);

    connect(addButton, &QPushButton::clicked, this, &WorkflowWindow::addNode);
    connect(removeButton, &QPushButton::clicked, this,
            &WorkflowWindow::removeSelected);
    connect(runButton_, &QPushButton::clicked, this,
            &WorkflowWindow::sendToProcesses);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    for (QPushButton* button : findChildren<QPushButton*>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }

    jobRunner_ = new jobs::JobRunner(this);
    connect(jobRunner_, &jobs::JobRunner::finished, this,
            &WorkflowWindow::onJobFinished);

    updateStatusLabel();
}

void WorkflowWindow::addNode()
{
    promptAddNode(nullptr);
}

void WorkflowWindow::addNodeAt(const QPointF& scenePos)
{
    promptAddNode(&scenePos);
}

void WorkflowWindow::promptAddNode(const QPointF* scenePos)
{
    if (materials_.isEmpty()) {
        QMessageBox::information(
            this, tr("Add Process"),
            tr("Open at least one structure first — a process node needs a "
               "material."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Process"));
    auto* form = new QFormLayout(&dialog);
    auto* taskCombo = new QComboBox(&dialog);
    for (WorkflowTask task :
         {WorkflowTask::GeometryOptimization, WorkflowTask::SinglePoint,
          WorkflowTask::MolecularDynamics, WorkflowTask::Phonon})
        taskCombo->addItem(taskDisplayName(task), static_cast<int>(task));
    form->addRow(tr("Process:"), taskCombo);
    auto* materialCombo = new QComboBox(&dialog);
    for (const auto& [name, structure] : materials_)
        materialCombo->addItem(name);
    form->addRow(tr("Material:"), materialCombo);
    auto* engineCombo = new QComboBox(&dialog);
    for (core::CalculatorKind kind :
         {core::CalculatorKind::EMT, core::CalculatorKind::Gpaw,
          core::CalculatorKind::Vasp, core::CalculatorKind::Mace})
        engineCombo->addItem(EnginePresets::displayName(kind),
                             static_cast<int>(kind));
    form->addRow(tr("Calculator:"), engineCombo);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    WorkflowNodeItem* node = addProcessNode(
        static_cast<WorkflowTask>(taskCombo->currentData().toInt()),
        materialCombo->currentIndex(),
        static_cast<core::CalculatorKind>(engineCombo->currentData().toInt()));
    if (node && scenePos)
        node->setPos(*scenePos
                     - QPointF(kNodeWidth / 2.0, kNodeHeight / 2.0));
}

WorkflowNodeItem* WorkflowWindow::addProcessNode(WorkflowTask task,
                                                 int materialIndex,
                                                 core::CalculatorKind engine)
{
    if (materialIndex < 0 || materialIndex >= materials_.size())
        return nullptr;
    auto* node = new WorkflowNodeItem(
        nextNodeId_++, taskDisplayName(task), task,
        materials_[materialIndex].first, materials_[materialIndex].second,
        engine);
    // Stagger new nodes left-to-right so a freshly built pipeline reads in
    // execution order without any manual arranging.
    node->setPos(static_cast<double>(nodes_.size()) * (kNodeWidth + 60.0),
                 0.0);
    scene_->addItem(node);
    nodes_.push_back(node);
    updateStatusLabel();
    return node;
}

void WorkflowWindow::linkNodes(WorkflowNodeItem* from, WorkflowNodeItem* to)
{
    connectNodes(from, to);
}

void WorkflowWindow::configureNode(WorkflowNodeItem* node,
                                   const QString& script,
                                   const QString& python,
                                   const QString& runCommand,
                                   core::CalculatorKind engine)
{
    if (node)
        node->setConfiguration(script, python, runCommand, engine);
}

void WorkflowWindow::openNodeWizard(WorkflowNodeItem* node)
{
    if (!node)
        return;
    // The node's standard setup wizard, in workflow mode: the review stage's
    // Run button reads "Save process node" and accepting commits the
    // generated script here instead of launching anything.
    std::unique_ptr<SimulationWizardBase> wizard;
    switch (node->task()) {
    case WorkflowTask::GeometryOptimization:
        wizard = std::make_unique<GeometryOptimizationWizard>(
            node->structure(), this);
        break;
    case WorkflowTask::MolecularDynamics:
        wizard = std::make_unique<MolecularDynamicsWizard>(node->structure(),
                                                           this);
        break;
    case WorkflowTask::Phonon:
        wizard = std::make_unique<PhononWizard>(/*periodic=*/true,
                                                node->structure(), this);
        break;
    case WorkflowTask::SinglePoint:
        wizard = std::make_unique<SinglePointWizard>(this);
        break;
    }
    wizard->enterWorkflowMode();
    if (node->structure())
        wizard->setStructureElements(
            structureElements(node->structure().get()));
    wizard->selectCalculator(node->engine());
    if (wizard->exec() != QDialog::Accepted)
        return;
    node->setConfiguration(wizard->script(), wizard->pythonExecutable(),
                           wizard->runCommand(), wizard->calculatorKind());
    updateStatusLabel(tr("%1 configured.").arg(node->title()));
}

void WorkflowWindow::removeSelected()
{
    // Edges first (they reference nodes), then nodes; unregister so a
    // surviving node never repaints a deleted edge.
    for (auto it = edges_.begin(); it != edges_.end();) {
        WorkflowEdgeItem* edge = *it;
        if (edge->isSelected() || edge->from()->isSelected()
            || edge->to()->isSelected()) {
            edge->from()->unregisterEdge(edge);
            edge->to()->unregisterEdge(edge);
            scene_->removeItem(edge);
            delete edge;
            it = edges_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = nodes_.begin(); it != nodes_.end();) {
        WorkflowNodeItem* node = *it;
        if (node->isSelected()) {
            scene_->removeItem(node);
            delete node;
            it = nodes_.erase(it);
        } else {
            ++it;
        }
    }
    updateStatusLabel();
}

void WorkflowWindow::connectNodes(WorkflowNodeItem* from,
                                  WorkflowNodeItem* to)
{
    for (const WorkflowEdgeItem* edge : edges_)
        if (edge->from() == from && edge->to() == to)
            return; // already linked
    if (wouldCreateCycle(from, to)) {
        QMessageBox::information(
            this, tr("Workflow"),
            tr("That link would close a cycle — a process cannot run before "
               "its own results exist."));
        return;
    }
    auto* edge = new WorkflowEdgeItem(from, to);
    scene_->addItem(edge);
    edges_.push_back(edge);
    updateStatusLabel();
}

bool WorkflowWindow::wouldCreateCycle(WorkflowNodeItem* from,
                                      WorkflowNodeItem* to) const
{
    // Walk downstream from `to`; reaching `from` means the new edge closes
    // a loop.
    std::vector<WorkflowNodeItem*> stack{to};
    std::vector<WorkflowNodeItem*> seen;
    while (!stack.empty()) {
        WorkflowNodeItem* node = stack.back();
        stack.pop_back();
        if (node == from)
            return true;
        if (std::find(seen.begin(), seen.end(), node) != seen.end())
            continue;
        seen.push_back(node);
        for (const WorkflowEdgeItem* edge : edges_)
            if (edge->from() == node)
                stack.push_back(edge->to());
    }
    return false;
}

QList<WorkflowNodeItem*>
WorkflowWindow::parentsOf(WorkflowNodeItem* node) const
{
    QList<WorkflowNodeItem*> parents;
    for (const WorkflowEdgeItem* edge : edges_)
        if (edge->to() == node)
            parents.append(edge->from());
    return parents;
}

WorkflowNodeItem* WorkflowWindow::nextRunnable() const
{
    for (WorkflowNodeItem* node : nodes_) {
        if (node->status() != WorkflowNodeItem::Status::Waiting)
            continue;
        bool ready = true;
        for (const WorkflowNodeItem* parent : parentsOf(node))
            ready = ready
                && parent->status() == WorkflowNodeItem::Status::Done;
        if (ready)
            return node;
    }
    return nullptr;
}

void WorkflowWindow::sendToProcesses()
{
    if (nodes_.empty()) {
        QMessageBox::information(this, tr("Workflow"),
                                 tr("Add at least one process node first."));
        return;
    }
    if (runningNode_)
        return; // already executing

    // Sending queues EVERY node: each shows as "waiting" until its turn, so
    // the canvas reads as a process queue from the moment of submission. A
    // fresh send resets previous results — they are superseded, not
    // silently reused. Each node also registers as a row in the global
    // Processes panel (Queued), so the dispatch is visible and reloadable
    // where every other run lives, not only on this canvas.
    for (WorkflowNodeItem* node : nodes_) {
        node->setStatus(WorkflowNodeItem::Status::Waiting);
        node->setJobDirectory(QString());
        node->setProcessTaskId(
            processPanel_ ? processPanel_->registerTask(
                tr("Workflow: %1 (%2)")
                    .arg(node->title(), node->materialName()),
                QString())
                          : -1);
        updateProcessPanel(node);
    }
    launchedCount_ = 0;

    QString root = QSettings()
                       .value(QLatin1String(SettingsManager::kSimulationsDir))
                       .toString()
                       .trimmed();
    if (root.isEmpty())
        root = SettingsManager::defaultSimulationsDirectory();
    workflowRoot_ = root + QStringLiteral("/workflow_")
        + QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd_HHmmss"));
    QDir().mkpath(workflowRoot_);

    runButton_->setEnabled(false);
    WorkflowNodeItem* first = nextRunnable();
    if (!first || !startNode(first)) {
        runButton_->setEnabled(true);
        updateStatusLabel(tr("Nothing could start — check the first node."));
    }
}

bool WorkflowWindow::startNode(WorkflowNodeItem* node)
{
    const QString dir = workflowRoot_
        + QStringLiteral("/node_%1_%2")
              .arg(++launchedCount_)
              .arg(taskSlug(node->task()));
    if (!QDir().mkpath(dir))
        return false;

    // --- Inputs: the parent's outputs, or the assigned material -----------
    // A parent's relaxed/final geometry (coordinates AND cell — extxyz
    // carries both, which is what makes a variable-cell relaxation hand over
    // its lattice) becomes this node's input structure; its saved ground
    // state rides along for engines that can restart from it. First
    // connected parent wins when there are several — documented in the
    // tooltip, and the common pipelines are chains anyway.
    //
    // A node WITH a parent must inherit from it or not run at all: falling
    // back to the node's original material here would silently execute the
    // child on the UN-relaxed structure — a run that "succeeds" while
    // computing the wrong thing, which is strictly worse than failing.
    const QList<WorkflowNodeItem*> parents = parentsOf(node);
    if (!parents.isEmpty()) {
        const QString parentDir = parents.front()->jobDirectory();
        QString source;
        for (const char* candidate :
             {"optimized.extxyz", "md_final.extxyz", "single_point.extxyz",
              "structure.extxyz"}) {
            const QString path =
                parentDir + QLatin1Char('/') + QLatin1String(candidate);
            if (QFile::exists(path)) {
                source = path;
                break;
            }
        }
        if (source.isEmpty()
            || !QFile::copy(source,
                            dir + QStringLiteral("/structure.extxyz"))) {
            updateStatusLabel(
                tr("%1: no usable geometry found in the parent's results "
                   "(%2) — the node was not started.")
                    .arg(node->title(), parentDir));
            return false;
        }
        const QString gpw =
            parentDir + QStringLiteral("/single_point.gpw");
        if (QFile::exists(gpw))
            QFile::copy(gpw, dir + QStringLiteral("/single_point.gpw"));
    } else {
        if (!node->structure())
            return false;
        try {
            pybridge::AseBridge::writeStructure(
                *node->structure(),
                (dir + QStringLiteral("/structure.extxyz")).toStdString());
        } catch (const std::exception&) {
            return false;
        }
    }

    // --- Script: wizard-committed, else the task's defaults ----------------
    // A node the user configured ("Save process node" in its wizard) runs
    // exactly the script that wizard generated; anything else falls back to
    // the same generators with default settings, seeded from the per-element
    // suggested cutoff/k-grid.
    QString script = node->configuredScript();
    if (script.isEmpty()) {
        core::CalculatorConfig config;
        config.calculator = node->engine();
        const CalculatorParameters::Suggestion suggestion =
            CalculatorParameters::suggestionFor(
                node->engine(), structureElements(node->structure().get()));
        if (suggestion.planeWaveCutoffEv)
            config.planeWaveCutoffEv = *suggestion.planeWaveCutoffEv;
        if (suggestion.kpts)
            for (int axis = 0; axis < 3; ++axis)
                config.kpts[axis] = (*suggestion.kpts)[axis];
        if (node->task() == WorkflowTask::Phonon) {
            core::PhononConfig phonon;
            phonon.calculator = config;
            script = QString::fromStdString(core::PhononScriptGenerator::
                                                generate(phonon,
                                                         "structure.extxyz"));
        } else {
            config.task = node->task() == WorkflowTask::GeometryOptimization
                ? core::TaskKind::GeometryOptimization
                : (node->task() == WorkflowTask::MolecularDynamics
                       ? core::TaskKind::MolecularDynamics
                       : core::TaskKind::SinglePoint);
            script = QString::fromStdString(
                core::AseScriptGenerator::generate(config,
                                                   "structure.extxyz"));
        }
    }
    QString error;
    if (!writeScriptWithLogger(dir + QStringLiteral("/run.py"), script,
                               &error))
        return false;

    // --- Launch through the shared command machinery -----------------------
    RunCommands::Context context;
    context.pythonExecutable = !node->configuredPython().isEmpty()
        ? node->configuredPython()
        : (pythonResolver_ ? pythonResolver_(node->engine()) : QString());
    context.scriptFile = QStringLiteral("run.py");
    context.cores = RunCommands::cores();
    const RunCommands::Resolved resolved = RunCommands::resolve(
        node->engine(), context, node->configuredRunCommand());

    node->setStatus(WorkflowNodeItem::Status::Running);
    node->setJobDirectory(dir);
    updateProcessPanel(node);
    runningNode_ = node;
    updateStatusLabel(tr("Running %1 (%2)…")
                          .arg(node->title(), node->materialName()));
    jobRunner_->start(resolved.commandLine, context.pythonExecutable, dir,
                      resolved.environment);
    if (node->processTaskId() >= 0)
        Q_EMIT nodeStarted(node->processTaskId(),
                           tr("Workflow: %1 (%2)")
                               .arg(node->title(), node->materialName()),
                           dir);
    return true;
}

void WorkflowWindow::skipDescendants(WorkflowNodeItem* node)
{
    for (WorkflowEdgeItem* edge : edges_) {
        if (edge->from() == node
            && edge->to()->status() == WorkflowNodeItem::Status::Waiting) {
            edge->to()->setStatus(WorkflowNodeItem::Status::Skipped);
            updateProcessPanel(edge->to());
            skipDescendants(edge->to());
        }
    }
}

void WorkflowWindow::onJobFinished(int exitCode, bool crashed)
{
    if (!runningNode_)
        return;
    WorkflowNodeItem* finished = runningNode_;
    runningNode_ = nullptr;

    if (exitCode == 0 && !crashed) {
        finished->setStatus(WorkflowNodeItem::Status::Done);
    } else {
        finished->setStatus(WorkflowNodeItem::Status::Failed);
        // Children cannot run on inputs that never materialized — say so on
        // the canvas instead of leaving them "pending" forever.
        skipDescendants(finished);
    }
    updateProcessPanel(finished);
    if (finished->processTaskId() >= 0)
        Q_EMIT nodeFinished(finished->processTaskId(),
                            exitCode == 0 && !crashed);

    if (WorkflowNodeItem* next = nextRunnable()) {
        if (startNode(next))
            return;
        next->setStatus(WorkflowNodeItem::Status::Failed);
        updateProcessPanel(next);
        skipDescendants(next);
    }

    runButton_->setEnabled(true);
    int done = 0;
    int failed = 0;
    for (const WorkflowNodeItem* node : nodes_) {
        done += node->status() == WorkflowNodeItem::Status::Done;
        failed += node->status() == WorkflowNodeItem::Status::Failed;
    }
    updateStatusLabel(failed
                          ? tr("Workflow finished: %1 done, %2 failed — "
                               "results under %3.")
                                .arg(done)
                                .arg(failed)
                                .arg(workflowRoot_)
                          : tr("Workflow finished: %1 process(es) done — "
                               "results under %2.")
                                .arg(done)
                                .arg(workflowRoot_));
}

void WorkflowWindow::updateProcessPanel(WorkflowNodeItem* node)
{
    if (!processPanel_ || node->processTaskId() < 0)
        return;
    if (!node->jobDirectory().isEmpty())
        processPanel_->setTaskDirectory(node->processTaskId(),
                                        node->jobDirectory());
    switch (node->status()) {
    case WorkflowNodeItem::Status::Waiting:
        processPanel_->setTaskStatus(node->processTaskId(),
                                     ProcessManagerPanel::Status::Queued);
        break;
    case WorkflowNodeItem::Status::Running:
        processPanel_->setTaskStatus(node->processTaskId(),
                                     ProcessManagerPanel::Status::Running);
        break;
    case WorkflowNodeItem::Status::Done:
        processPanel_->setTaskStatus(node->processTaskId(),
                                     ProcessManagerPanel::Status::Completed);
        break;
    case WorkflowNodeItem::Status::Failed:
    case WorkflowNodeItem::Status::Skipped:
        // The panel has no "skipped": a node whose inputs never existed is
        // reported as failed there, which is what it is from the queue's
        // point of view.
        processPanel_->setTaskStatus(node->processTaskId(),
                                     ProcessManagerPanel::Status::Failed);
        break;
    case WorkflowNodeItem::Status::Pending:
        break; // never mirrored: pending nodes were not dispatched
    }
}

void WorkflowWindow::updateStatusLabel(const QString& message)
{
    if (!message.isEmpty()) {
        statusLabel_->setText(message);
        return;
    }
    statusLabel_->setText(tr("%n node(s), ", nullptr,
                             static_cast<int>(nodes_.size()))
                          + tr("%n link(s)", nullptr,
                               static_cast<int>(edges_.size())));
}

} // namespace calango::gui
