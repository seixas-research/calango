#include "gui/OrchestrationWindow.hpp"

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
#include "ui/IconManager.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
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

// -- Canvas palette ---------------------------------------------------------
//
// The canvas is dark REGARDLESS of the application theme, like every other
// data canvas in Calango (the metric plots, the convergence curves, the Random
// Noise histograms all fill this same 28/30/34). The Orchestration dock sits
// immediately beside Results in the bottom row, so anything else would put two
// differently-lit canvases side by side; and a diagram of light nodes on a
// dark field is the convention every node editor uses, because it is the node
// bodies that carry the information and the field that must recede.
//
// These three belong together and MUST move together: the nodes are painted
// white with dark text, so they take care of themselves, but both link colours
// are drawn straight onto the background and were chosen against it. The edge
// pen in particular used to be 0x50 grey — on this background that is a link
// you cannot see, which on a node canvas means a pipeline whose structure has
// become invisible.
const QColor kCanvasBackground(28, 30, 34);
/// Committed links. Light enough to read on the dark field, muted enough to
/// still sit UNDER the nodes as plumbing rather than competing with them.
const QColor kEdgeColor(150, 156, 166);
/// The dashed link being dragged. Deliberately the brightest thing on the
/// canvas while the gesture is live — it is a preview the user is steering.
const QColor kPendingLinkColor(0x66, 0x99, 0xff);

QString taskDisplayName(OrchestrationTask task)
{
    switch (task) {
    case OrchestrationTask::GeometryOptimization:
        return QObject::tr("Geometry Optimization");
    case OrchestrationTask::MolecularDynamics:
        return QObject::tr("Molecular Dynamics");
    case OrchestrationTask::Phonon:
        return QObject::tr("Phonon");
    case OrchestrationTask::SinglePoint:
        break;
    }
    return QObject::tr("Single-Point Calculation");
}

/// Directory-name slug for a task ("node_2_phonon").
QString taskSlug(OrchestrationTask task)
{
    switch (task) {
    case OrchestrationTask::GeometryOptimization:
        return QStringLiteral("geometry_optimization");
    case OrchestrationTask::MolecularDynamics:
        return QStringLiteral("molecular_dynamics");
    case OrchestrationTask::Phonon:
        return QStringLiteral("phonon");
    case OrchestrationTask::SinglePoint:
        break;
    }
    return QStringLiteral("single_point");
}

QColor statusColor(OrchestrationNodeItem::Status status)
{
    switch (status) {
    case OrchestrationNodeItem::Status::Waiting:
        return QColor(0x8e, 0x6f, 0xc9); // queued: distinct from idle blue
    case OrchestrationNodeItem::Status::Running:
        return QColor(0xff, 0x9e, 0x1a);
    case OrchestrationNodeItem::Status::Done:
        return QColor(0x2c, 0xa0, 0x2c);
    case OrchestrationNodeItem::Status::Failed:
        return QColor(0xd6, 0x27, 0x28);
    case OrchestrationNodeItem::Status::Skipped:
        return QColor(0x8a, 0x8a, 0x8a);
    case OrchestrationNodeItem::Status::Pending:
        break;
    }
    return QColor(0x66, 0x99, 0xff);
}

QString statusText(OrchestrationNodeItem::Status status)
{
    switch (status) {
    case OrchestrationNodeItem::Status::Waiting:
        return QObject::tr("waiting");
    case OrchestrationNodeItem::Status::Running:
        return QObject::tr("running");
    case OrchestrationNodeItem::Status::Done:
        return QObject::tr("done");
    case OrchestrationNodeItem::Status::Failed:
        return QObject::tr("failed");
    case OrchestrationNodeItem::Status::Skipped:
        return QObject::tr("skipped");
    case OrchestrationNodeItem::Status::Pending:
        break;
    }
    return QObject::tr("pending");
}

/// The canvas: wheel zooms about the cursor, middle-drag pans, left-drag
/// moves nodes (or rubber-band selects on empty space).
class OrchestrationView : public QGraphicsView {
public:
    explicit OrchestrationView(QGraphicsScene* scene, QWidget* parent = nullptr)
        : QGraphicsView(scene, parent)
    {
        setRenderHint(QPainter::Antialiasing, true);
        setDragMode(QGraphicsView::RubberBandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setBackgroundBrush(kCanvasBackground);
        // The brush paints the SCENE; the viewport widget and the frame around
        // it are still styled by the application theme, which under Light
        // leaves a pale border and pale scroll bars framing a dark rectangle.
        // The style sheet dresses the widget to match what the brush paints.
        //
        // NoFrame rather than a dark border: the dock already draws an edge
        // around this panel, and a second one inside it reads as a box within
        // a box at the ~250 px height the bottom row gives us.
        setFrameShape(QFrame::NoFrame);
        setStyleSheet(QStringLiteral(
                          "QGraphicsView { background: %1; border: none; }"
                          "QScrollBar:vertical, QScrollBar:horizontal {"
                          "  background: %1; border: none; }"
                          "QScrollBar::handle:vertical,"
                          "QScrollBar::handle:horizontal {"
                          "  background: #4a4f57; border-radius: 4px; }"
                          "QScrollBar::add-line, QScrollBar::sub-line {"
                          "  height: 0px; width: 0px; }"
                          "QScrollBar::add-page, QScrollBar::sub-page {"
                          "  background: transparent; }")
                          .arg(kCanvasBackground.name()));
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
// OrchestrationNodeItem
// ---------------------------------------------------------------------------

OrchestrationNodeItem::OrchestrationNodeItem(
    int id, const QString& title, OrchestrationTask task,
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

void OrchestrationNodeItem::setConfiguration(const QString& script,
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

void OrchestrationNodeItem::setStatus(Status status)
{
    status_ = status;
    update();
}

QPointF OrchestrationNodeItem::inputPortScenePos() const
{
    return mapToScene(QPointF(0.0, kNodeHeight / 2.0));
}

QPointF OrchestrationNodeItem::outputPortScenePos() const
{
    return mapToScene(QPointF(kNodeWidth, kNodeHeight / 2.0));
}

bool OrchestrationNodeItem::hitsOutputPort(const QPointF& scenePos) const
{
    // A generous grab zone: the port is 7 px on screen and a link gesture
    // that misses by two pixels should still start.
    return QLineF(scenePos, outputPortScenePos()).length()
        <= kPortRadius * 2.0;
}

void OrchestrationNodeItem::unregisterEdge(OrchestrationEdgeItem* edge)
{
    edges_.erase(std::remove(edges_.begin(), edges_.end(), edge),
                 edges_.end());
}

QRectF OrchestrationNodeItem::boundingRect() const
{
    // The ports stick kPortRadius past the rect's left/right edges (plus
    // their outline), and the selection pen is 2 px. QGraphicsRectItem's own
    // bounding rect covers none of that, so dragging repainted a region
    // narrower than what paint() had touched — the classic ghost-trail bug:
    // the port circles stayed behind as smears wherever the node had been.
    const double margin = kPortRadius + 2.0;
    return rect().adjusted(-margin, -margin, margin, margin);
}

QVariant OrchestrationNodeItem::itemChange(GraphicsItemChange change,
                                      const QVariant& value)
{
    if (change == ItemScenePositionHasChanged)
        for (OrchestrationEdgeItem* edge : edges_)
            edge->updatePath();
    return QGraphicsRectItem::itemChange(change, value);
}

void OrchestrationNodeItem::paint(QPainter* painter,
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
// OrchestrationEdgeItem
// ---------------------------------------------------------------------------

OrchestrationEdgeItem::OrchestrationEdgeItem(OrchestrationNodeItem* from,
                                   OrchestrationNodeItem* to)
    : from_(from)
    , to_(to)
{
    setPen(QPen(kEdgeColor, 2.0));
    setZValue(-1.0); // under the nodes, so it reads as plumbing
    from_->registerEdge(this);
    to_->registerEdge(this);
    updatePath();
}

void OrchestrationEdgeItem::updatePath()
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
// OrchestrationScene — the link-drawing gesture
// ---------------------------------------------------------------------------

OrchestrationNodeItem* OrchestrationScene::nodeAt(const QPointF& scenePos) const
{
    // dynamic_cast, not qgraphicsitem_cast: the nodes define no custom
    // type() id, and the item-cast would happily match ANY rect item.
    for (QGraphicsItem* item : items(scenePos))
        if (auto* node = dynamic_cast<OrchestrationNodeItem*>(item))
            return node;
    return nullptr;
}

void OrchestrationScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Search a little around the cursor: the port sticks out of the
        // node's rect, so items() at the exact point can miss it.
        for (QGraphicsItem* item : items(
                 QRectF(event->scenePos() - QPointF(10, 10), QSizeF(20, 20)),
                 Qt::IntersectsItemBoundingRect)) {
            auto* node = dynamic_cast<OrchestrationNodeItem*>(item);
            if (node && node->hitsOutputPort(event->scenePos())) {
                pendingFrom_ = node;
                pendingPreview_ = addPath(
                    QPainterPath(),
                    QPen(kPendingLinkColor, 2.0, Qt::DashLine));
                event->accept();
                return; // the gesture owns this press — no node drag
            }
        }
    }
    QGraphicsScene::mousePressEvent(event);
}

void OrchestrationScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
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

void OrchestrationScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (OrchestrationNodeItem* node = nodeAt(event->scenePos())) {
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

void OrchestrationScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (pendingFrom_) {
        OrchestrationNodeItem* target = nodeAt(event->scenePos());
        OrchestrationNodeItem* from = pendingFrom_;
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
// OrchestrationWindow
// ---------------------------------------------------------------------------

OrchestrationWindow::OrchestrationWindow(
    const QList<QPair<QString, std::shared_ptr<const core::Structure>>>&
        materials,
    std::function<QString(core::CalculatorKind)> pythonResolver,
    ProcessManagerPanel* processPanel, QWidget* parent)
    : QWidget(parent)
    , materials_(materials)
    , pythonResolver_(std::move(pythonResolver))
    , processPanel_(processPanel)
{
    setWindowTitle(tr("Orchestration"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // No instructional line above the canvas. It was a standing caption
    // explaining a gesture the user needs told once, charged against a dock
    // that is ~250 px tall — and every pixel it took came out of the canvas,
    // which is the panel. The same text is the canvas's tool tip, so it is
    // still one hover away on the day it is wanted.
    scene_ = new OrchestrationScene(this);
    scene_->setSceneRect(-2000, -2000, 4000, 4000);
    connect(scene_, &OrchestrationScene::connectionRequested, this,
            &OrchestrationWindow::connectNodes);
    connect(scene_, &OrchestrationScene::nodeActivated, this,
            &OrchestrationWindow::openNodeWizard);
    connect(scene_, &OrchestrationScene::addNodeRequested, this,
            &OrchestrationWindow::addNodeAt);
    view_ = new OrchestrationView(scene_, this);
    view_->setToolTip(
        tr("Each node is one simulation process; a link drawn from a node's "
           "right-hand port onto another node runs the second after the "
           "first and feeds it the results — a relaxed geometry becomes the "
           "next input structure, a saved ground state (.gpw) rides along. "
           "Double-click empty canvas to add a process there; double-click a "
           "node to configure it in its setup wizard. Wheel zooms, "
           "middle-drag pans."));
    layout->addWidget(view_, 1);

    // Icon-only action bar, built exactly like the Processes panel's: same
    // IconManager binding (so the glyphs re-tint on a theme switch), same
    // 20 px icons, same "Label — what it does" tool tips carrying the text
    // the buttons no longer show. The two panels sit side by side in the
    // bottom row, and matching them makes the row read as one strip of tools
    // rather than two unrelated widgets.
    auto* controls = new QHBoxLayout;
    const auto makeButton = [this, controls](const QString& iconName,
                                             const QString& tip) {
        auto* button = new QPushButton(this);
        ui::IconManager::bind(button, iconName);
        button->setIconSize(QSize(20, 20));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        controls->addWidget(button);
        return button;
    };

    auto* addButton = makeButton(
        QStringLiteral("add-circle-fill"),
        tr("Add Process… — put a new simulation node on the canvas.\n\n"
           "Double-clicking empty canvas does the same and drops the node "
           "where you clicked."));
    auto* removeButton = makeButton(
        QStringLiteral("delete-bin-line"),
        tr("Remove Selected — delete the selected nodes and every link "
           "that touched them."));
    controls->addStretch(1);
    // Trailing and set apart by the stretch: it is the one button that
    // COMMITS — everything to its left edits the pipeline, this one runs it.
    runButton_ = makeButton(
        QStringLiteral("play-circle-fill"),
        tr("Send to Processes — queue every node (they show as \"waiting\") "
           "and execute the pipeline in dependency order, one process at a "
           "time.\n\n"
           "Each node appears in the Processes panel as it is dispatched, and "
           "its metrics stream into Results."));
    // No Close button: as a dock the panel's visibility belongs to its own
    // title bar and to View → Orchestration. A button calling close() here would
    // hide the widget INSIDE the dock and leave an empty frame behind.
    //
    // No status label either. It reported the node/link count, the running
    // node and the "Orchestration finished: …" summary — of which the count is
    // decoration, and the other two are already told properly by the
    // Processes panel (per-node Queued → Running → Completed/Failed) and by
    // Results. What it alone used to carry was the two REFUSAL messages, and
    // those are now message boxes: a pipeline that would not start is not
    // something to whisper in a corner of the toolbar.
    layout->addLayout(controls);

    connect(addButton, &QPushButton::clicked, this, &OrchestrationWindow::addNode);
    connect(removeButton, &QPushButton::clicked, this,
            &OrchestrationWindow::removeSelected);
    connect(runButton_, &QPushButton::clicked, this,
            &OrchestrationWindow::sendToProcesses);
    for (QPushButton* button : findChildren<QPushButton*>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }

    jobRunner_ = new jobs::JobRunner(this);
    connect(jobRunner_, &jobs::JobRunner::finished, this,
            &OrchestrationWindow::onJobFinished);
    // Live geometries. A relaxation or an MD node emits CALANGO_FRAME blocks
    // exactly as the same script does when launched from its own wizard, and
    // this runner parses them — but nothing was listening, so a orchestration run
    // showed a frozen viewport throughout and left the timeline empty at the
    // end. The host cannot subscribe to this runner directly (it is private to
    // the panel), so the frames are forwarded with the process id that
    // identifies which run they belong to.
    connect(jobRunner_, &jobs::JobRunner::frameStreamed, this,
            [this](const std::shared_ptr<core::Structure>& frame) {
                if (runningNode_ && runningNode_->processTaskId() >= 0)
                    Q_EMIT nodeFrameStreamed(runningNode_->processTaskId(),
                                             frame);
            });
}

void OrchestrationWindow::addNode()
{
    promptAddNode(nullptr);
}

void OrchestrationWindow::addNodeAt(const QPointF& scenePos)
{
    promptAddNode(&scenePos);
}

void OrchestrationWindow::setMaterialsProvider(std::function<MaterialList()> provider)
{
    materialsProvider_ = std::move(provider);
}

void OrchestrationWindow::promptAddNode(const QPointF* scenePos)
{
    // Re-read the open documents before offering them: the panel is docked and
    // long-lived, so the list it was constructed with is stale by now.
    if (materialsProvider_)
        materials_ = materialsProvider_();

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
    for (OrchestrationTask task :
         {OrchestrationTask::GeometryOptimization, OrchestrationTask::SinglePoint,
          OrchestrationTask::MolecularDynamics, OrchestrationTask::Phonon})
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

    OrchestrationNodeItem* node = addProcessNode(
        static_cast<OrchestrationTask>(taskCombo->currentData().toInt()),
        materialCombo->currentIndex(),
        static_cast<core::CalculatorKind>(engineCombo->currentData().toInt()));
    if (node && scenePos)
        node->setPos(*scenePos
                     - QPointF(kNodeWidth / 2.0, kNodeHeight / 2.0));
}

OrchestrationNodeItem* OrchestrationWindow::addProcessNode(OrchestrationTask task,
                                                 int materialIndex,
                                                 core::CalculatorKind engine)
{
    if (materialIndex < 0 || materialIndex >= materials_.size())
        return nullptr;
    auto* node = new OrchestrationNodeItem(
        nextNodeId_++, taskDisplayName(task), task,
        materials_[materialIndex].first, materials_[materialIndex].second,
        engine);
    // Stagger new nodes left-to-right so a freshly built pipeline reads in
    // execution order without any manual arranging.
    node->setPos(static_cast<double>(nodes_.size()) * (kNodeWidth + 60.0),
                 0.0);
    scene_->addItem(node);
    nodes_.push_back(node);
    return node;
}

void OrchestrationWindow::linkNodes(OrchestrationNodeItem* from, OrchestrationNodeItem* to)
{
    connectNodes(from, to);
}

void OrchestrationWindow::configureNode(OrchestrationNodeItem* node,
                                   const QString& script,
                                   const QString& python,
                                   const QString& runCommand,
                                   core::CalculatorKind engine)
{
    if (node)
        node->setConfiguration(script, python, runCommand, engine);
}

void OrchestrationWindow::openNodeWizard(OrchestrationNodeItem* node)
{
    if (!node)
        return;
    // The node's standard setup wizard, in orchestration mode: the review stage's
    // Run button reads "Save process node" and accepting commits the
    // generated script here instead of launching anything.
    std::unique_ptr<SimulationWizardBase> wizard;
    switch (node->task()) {
    case OrchestrationTask::GeometryOptimization:
        wizard = std::make_unique<GeometryOptimizationWizard>(
            node->structure(), this);
        break;
    case OrchestrationTask::MolecularDynamics:
        wizard = std::make_unique<MolecularDynamicsWizard>(node->structure(),
                                                           this);
        break;
    case OrchestrationTask::Phonon:
        wizard = std::make_unique<PhononWizard>(/*periodic=*/true,
                                                node->structure(), this);
        break;
    case OrchestrationTask::SinglePoint:
        wizard = std::make_unique<SinglePointWizard>(this);
        break;
    }
    wizard->enterOrchestrationMode();
    if (node->structure())
        wizard->setStructureElements(
            structureElements(node->structure().get()));
    wizard->selectCalculator(node->engine());
    if (wizard->exec() != QDialog::Accepted)
        return;
    node->setConfiguration(wizard->script(), wizard->pythonExecutable(),
                           wizard->runCommand(), wizard->calculatorKind());
}

void OrchestrationWindow::removeSelected()
{
    // Edges first (they reference nodes), then nodes; unregister so a
    // surviving node never repaints a deleted edge.
    for (auto it = edges_.begin(); it != edges_.end();) {
        OrchestrationEdgeItem* edge = *it;
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
        OrchestrationNodeItem* node = *it;
        if (node->isSelected()) {
            scene_->removeItem(node);
            delete node;
            it = nodes_.erase(it);
        } else {
            ++it;
        }
    }
}

void OrchestrationWindow::connectNodes(OrchestrationNodeItem* from,
                                  OrchestrationNodeItem* to)
{
    for (const OrchestrationEdgeItem* edge : edges_)
        if (edge->from() == from && edge->to() == to)
            return; // already linked
    if (wouldCreateCycle(from, to)) {
        QMessageBox::information(
            this, tr("Orchestration"),
            tr("That link would close a cycle — a process cannot run before "
               "its own results exist."));
        return;
    }
    auto* edge = new OrchestrationEdgeItem(from, to);
    scene_->addItem(edge);
    edges_.push_back(edge);
}

bool OrchestrationWindow::wouldCreateCycle(OrchestrationNodeItem* from,
                                      OrchestrationNodeItem* to) const
{
    // Walk downstream from `to`; reaching `from` means the new edge closes
    // a loop.
    std::vector<OrchestrationNodeItem*> stack{to};
    std::vector<OrchestrationNodeItem*> seen;
    while (!stack.empty()) {
        OrchestrationNodeItem* node = stack.back();
        stack.pop_back();
        if (node == from)
            return true;
        if (std::find(seen.begin(), seen.end(), node) != seen.end())
            continue;
        seen.push_back(node);
        for (const OrchestrationEdgeItem* edge : edges_)
            if (edge->from() == node)
                stack.push_back(edge->to());
    }
    return false;
}

QList<OrchestrationNodeItem*>
OrchestrationWindow::parentsOf(OrchestrationNodeItem* node) const
{
    QList<OrchestrationNodeItem*> parents;
    for (const OrchestrationEdgeItem* edge : edges_)
        if (edge->to() == node)
            parents.append(edge->from());
    return parents;
}

OrchestrationNodeItem* OrchestrationWindow::nextRunnable() const
{
    for (OrchestrationNodeItem* node : nodes_) {
        if (node->status() != OrchestrationNodeItem::Status::Waiting)
            continue;
        bool ready = true;
        for (const OrchestrationNodeItem* parent : parentsOf(node))
            ready = ready
                && parent->status() == OrchestrationNodeItem::Status::Done;
        if (ready)
            return node;
    }
    return nullptr;
}

void OrchestrationWindow::sendToProcesses()
{
    if (nodes_.empty()) {
        QMessageBox::information(this, tr("Orchestration"),
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
    for (OrchestrationNodeItem* node : nodes_) {
        node->setStatus(OrchestrationNodeItem::Status::Waiting);
        node->setJobDirectory(QString());
        node->setProcessTaskId(
            processPanel_ ? processPanel_->registerTask(
                tr("Orchestration: %1 (%2)")
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
    orchestrationRoot_ = root + QStringLiteral("/orchestration_")
        + QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd_HHmmss"));
    QDir().mkpath(orchestrationRoot_);

    runButton_->setEnabled(false);
    OrchestrationNodeItem* first = nextRunnable();
    if (!first || !startNode(first)) {
        runButton_->setEnabled(true);
        // A box, not a toolbar caption. The user just pressed Run and NOTHING
        // happened; a line of grey text beside the button is exactly the way
        // to have that read as the button being broken.
        QMessageBox::warning(
            this, tr("Orchestration"),
            tr("Nothing could start — check the first node.\n\n"
               "Every node either has no parent (and runs on its assigned "
               "material) or inherits its parent's results. A node whose "
               "parent produced no usable geometry is refused rather than "
               "run on the wrong structure."));
    }
}

bool OrchestrationWindow::startNode(OrchestrationNodeItem* node)
{
    const QString dir = orchestrationRoot_
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
    const QList<OrchestrationNodeItem*> parents = parentsOf(node);
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
            // The strict-handoff refusal. This is the message that explains
            // why a pipeline stopped rather than silently computing the wrong
            // thing on an un-relaxed structure, so it must be impossible to
            // miss — it was the one thing the removed status label carried
            // that nothing else reports.
            QMessageBox::warning(
                this, tr("Orchestration"),
                tr("%1 was not started: no usable geometry in its parent's "
                   "results (%2).\n\n"
                   "A node with a parent inherits that parent's output "
                   "structure. Running it on its own original material "
                   "instead would quietly compute the wrong thing, so it is "
                   "refused.")
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
        if (node->task() == OrchestrationTask::Phonon) {
            core::PhononConfig phonon;
            phonon.calculator = config;
            script = QString::fromStdString(core::PhononScriptGenerator::
                                                generate(phonon,
                                                         "structure.extxyz"));
        } else {
            config.task = node->task() == OrchestrationTask::GeometryOptimization
                ? core::TaskKind::GeometryOptimization
                : (node->task() == OrchestrationTask::MolecularDynamics
                       ? core::TaskKind::MolecularDynamics
                       : core::TaskKind::SinglePoint);
            script = QString::fromStdString(
                core::AseScriptGenerator::generate(config,
                                                   "structure.extxyz"));
        }
    }
    QString error;
    if (!writeScript(dir + QStringLiteral("/run.py"), script, &error))
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

    node->setStatus(OrchestrationNodeItem::Status::Running);
    node->setJobDirectory(dir);
    updateProcessPanel(node);
    runningNode_ = node;
    jobRunner_->start(resolved.commandLine, context.pythonExecutable, dir,
                      resolved.environment);
    if (node->processTaskId() >= 0)
        Q_EMIT nodeStarted(node->processTaskId(),
                           tr("Orchestration: %1 (%2)")
                               .arg(node->title(), node->materialName()),
                           dir);
    return true;
}

void OrchestrationWindow::skipDescendants(OrchestrationNodeItem* node)
{
    for (OrchestrationEdgeItem* edge : edges_) {
        if (edge->from() == node
            && edge->to()->status() == OrchestrationNodeItem::Status::Waiting) {
            edge->to()->setStatus(OrchestrationNodeItem::Status::Skipped);
            updateProcessPanel(edge->to());
            skipDescendants(edge->to());
        }
    }
}

void OrchestrationWindow::onJobFinished(int exitCode, bool crashed)
{
    if (!runningNode_)
        return;
    OrchestrationNodeItem* finished = runningNode_;
    runningNode_ = nullptr;

    if (exitCode == 0 && !crashed) {
        finished->setStatus(OrchestrationNodeItem::Status::Done);
    } else {
        finished->setStatus(OrchestrationNodeItem::Status::Failed);
        // Children cannot run on inputs that never materialized — say so on
        // the canvas instead of leaving them "pending" forever.
        skipDescendants(finished);
    }
    updateProcessPanel(finished);
    if (finished->processTaskId() >= 0)
        Q_EMIT nodeFinished(finished->processTaskId(),
                            exitCode == 0 && !crashed);

    if (OrchestrationNodeItem* next = nextRunnable()) {
        if (startNode(next))
            return;
        next->setStatus(OrchestrationNodeItem::Status::Failed);
        updateProcessPanel(next);
        skipDescendants(next);
    }

    // The run is over; the button goes live again. No summary line: each
    // node's own status strip says done/failed on the canvas, and the
    // Processes panel holds the same per-node verdict with its directory.
    runButton_->setEnabled(true);
}

void OrchestrationWindow::updateProcessPanel(OrchestrationNodeItem* node)
{
    if (!processPanel_ || node->processTaskId() < 0)
        return;
    if (!node->jobDirectory().isEmpty())
        processPanel_->setTaskDirectory(node->processTaskId(),
                                        node->jobDirectory());
    switch (node->status()) {
    case OrchestrationNodeItem::Status::Waiting:
        processPanel_->setTaskStatus(node->processTaskId(),
                                     ProcessManagerPanel::Status::Queued);
        break;
    case OrchestrationNodeItem::Status::Running:
        processPanel_->setTaskStatus(node->processTaskId(),
                                     ProcessManagerPanel::Status::Running);
        break;
    case OrchestrationNodeItem::Status::Done:
        processPanel_->setTaskStatus(node->processTaskId(),
                                     ProcessManagerPanel::Status::Completed);
        break;
    case OrchestrationNodeItem::Status::Failed:
    case OrchestrationNodeItem::Status::Skipped:
        // The panel has no "skipped": a node whose inputs never existed is
        // reported as failed there, which is what it is from the queue's
        // point of view.
        processPanel_->setTaskStatus(node->processTaskId(),
                                     ProcessManagerPanel::Status::Failed);
        break;
    case OrchestrationNodeItem::Status::Pending:
        break; // never mirrored: pending nodes were not dispatched
    }
}

} // namespace calango::gui
