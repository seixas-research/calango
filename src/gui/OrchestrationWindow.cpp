#include "gui/OrchestrationWindow.hpp"

#include "core/AseScriptGenerator.hpp"
#include "core/PhononScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/CalculatorParameters.hpp"
#include "gui/OrchestrationDocument.hpp"
#include "gui/EnginePresets.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/ProcessManagerPanel.hpp"
#include "gui/RunCommands.hpp"
#include "gui/ScriptStaging.hpp"
#include "gui/SettingsManager.hpp"
#include "jobs/JobRunner.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/BulkBuilder.hpp"
#include "ui/IconManager.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QGroupBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <optional>

namespace calango::gui {

namespace {

constexpr double kNodeWidth = 200.0;
constexpr double kNodeHeight = 88.0;
constexpr double kPortRadius = 7.0;
/// Height of one inherited-run line. The node grows by this per input slot.
constexpr double kInputLineHeight = 15.0;

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

/// Copy a finished run's directory into `target`, one level deep.
///
/// One level on purpose: a job directory holds result files beside whatever
/// scratch the engine left, and a recursive copy of a GPAW run can be
/// gigabytes of nothing anyone reads. The modules that inherit a directory
/// (Charge Density Difference) read files at its top level.
bool copyDirectory(const QString& source, const QString& target)
{
    QDir from(source);
    if (!from.exists() || !QDir().mkpath(target))
        return false;
    bool copiedAnything = false;
    for (const QFileInfo& entry :
         from.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        if (QFile::copy(entry.absoluteFilePath(),
                        target + QLatin1Char('/') + entry.fileName()))
            copiedAnything = true;
    }
    return copiedAnything;
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

/// Untranslated status name for the on-disk manifest. Deliberately separate
/// from statusText(): a provenance file read six months later by a script
/// must not depend on the locale the run happened to execute under.
QString statusSlug(OrchestrationNodeItem::Status status)
{
    switch (status) {
    case OrchestrationNodeItem::Status::Waiting:
        return QStringLiteral("waiting");
    case OrchestrationNodeItem::Status::Running:
        return QStringLiteral("running");
    case OrchestrationNodeItem::Status::Done:
        return QStringLiteral("done");
    case OrchestrationNodeItem::Status::Failed:
        return QStringLiteral("failed");
    case OrchestrationNodeItem::Status::Skipped:
        return QStringLiteral("skipped");
    case OrchestrationNodeItem::Status::Pending:
        break;
    }
    return QStringLiteral("pending");
}

QString utcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
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

QString orchestrationTaskSlug(OrchestrationTask task)
{
    switch (task) {
    case OrchestrationTask::GeometryOptimization:
        return QStringLiteral("geometry_optimization");
    case OrchestrationTask::MolecularDynamics:
        return QStringLiteral("molecular_dynamics");
    case OrchestrationTask::Phonon:
        return QStringLiteral("phonon");
    case OrchestrationTask::ElectronicBands:
        return QStringLiteral("electronic_bands");
    case OrchestrationTask::Optics:
        return QStringLiteral("optics");
    case OrchestrationTask::Workfunction:
        return QStringLiteral("workfunction");
    case OrchestrationTask::TwoDBands:
        return QStringLiteral("bands_2d");
    case OrchestrationTask::Wannier:
        return QStringLiteral("wannier");
    case OrchestrationTask::BornCharges:
        return QStringLiteral("born_charges");
    case OrchestrationTask::Gw:
        return QStringLiteral("gw");
    case OrchestrationTask::ChargeDensityDifference:
        return QStringLiteral("charge_density_difference");
    case OrchestrationTask::RamanIr:
        return QStringLiteral("raman_ir");
    case OrchestrationTask::ChargedDefects:
        return QStringLiteral("charged_defects");
    case OrchestrationTask::ChargedDefects2d:
        return QStringLiteral("charged_defects_2d");
    case OrchestrationTask::Container:
        return QStringLiteral("container");
    case OrchestrationTask::Supercell:
        return QStringLiteral("supercell");
    case OrchestrationTask::DefectGenerator:
        return QStringLiteral("defect_generator");
    case OrchestrationTask::TdbGenerator:
        return QStringLiteral("tdb_generator");
    case OrchestrationTask::SinglePoint:
        break;
    }
    return QStringLiteral("single_point");
}

std::optional<OrchestrationTask> orchestrationTaskFromSlug(const QString& slug)
{
    for (OrchestrationTask task : orchestrationTasks())
        if (orchestrationTaskSlug(task) == slug)
            return task;
    return std::nullopt;
}

OrchestrationFamily orchestrationTaskFamily(OrchestrationTask task)
{
    switch (task) {
    case OrchestrationTask::GeometryOptimization:
    case OrchestrationTask::SinglePoint:
    case OrchestrationTask::MolecularDynamics:
    case OrchestrationTask::Phonon:
        return OrchestrationFamily::Simulation;
    case OrchestrationTask::Container:
    case OrchestrationTask::Supercell:
    case OrchestrationTask::DefectGenerator:
    case OrchestrationTask::TdbGenerator:
        return OrchestrationFamily::Transform;
    default:
        break;
    }
    return OrchestrationFamily::Analysis;
}

QString orchestrationTaskDisplayName(OrchestrationTask task)
{
    switch (task) {
    case OrchestrationTask::GeometryOptimization:
        return QObject::tr("Geometry Optimization");
    case OrchestrationTask::MolecularDynamics:
        return QObject::tr("Molecular Dynamics");
    case OrchestrationTask::Phonon:
        return QObject::tr("Phonon");
    case OrchestrationTask::ElectronicBands:
        return QObject::tr("Electronic Bands and DOS");
    case OrchestrationTask::Optics:
        return QObject::tr("Optical Properties");
    case OrchestrationTask::Workfunction:
        return QObject::tr("2D Workfunction");
    case OrchestrationTask::TwoDBands:
        return QObject::tr("2D Bands");
    case OrchestrationTask::Wannier:
        return QObject::tr("Wannier Functions");
    case OrchestrationTask::BornCharges:
        return QObject::tr("Born Effective Charges");
    case OrchestrationTask::Gw:
        return QObject::tr("GW Quasiparticles");
    case OrchestrationTask::ChargeDensityDifference:
        return QObject::tr("Charge Density Difference");
    case OrchestrationTask::RamanIr:
        return QObject::tr("Raman and IR Spectroscopy");
    case OrchestrationTask::ChargedDefects:
        return QObject::tr("Charged Defects");
    case OrchestrationTask::ChargedDefects2d:
        return QObject::tr("Charged Defects in 2D Materials");
    case OrchestrationTask::Container:
        return QObject::tr("Structure Container");
    case OrchestrationTask::Supercell:
        return QObject::tr("Supercell Builder");
    case OrchestrationTask::DefectGenerator:
        return QObject::tr("Defect Generator");
    case OrchestrationTask::TdbGenerator:
        return QObject::tr("TDB Generator (CALPHAD)");
    case OrchestrationTask::SinglePoint:
        break;
    }
    return QObject::tr("Single-Point Calculation");
}

QString orchestrationTaskShortName(OrchestrationTask task)
{
    switch (task) {
    case OrchestrationTask::GeometryOptimization: return QObject::tr("Relax");
    case OrchestrationTask::SinglePoint:          return QObject::tr("Energy");
    case OrchestrationTask::MolecularDynamics:    return QObject::tr("MD");
    case OrchestrationTask::Phonon:               return QObject::tr("Phonon");
    case OrchestrationTask::ElectronicBands:      return QObject::tr("Bands");
    case OrchestrationTask::Optics:               return QObject::tr("Optics");
    case OrchestrationTask::Workfunction:         return QObject::tr("Work fn");
    case OrchestrationTask::TwoDBands:            return QObject::tr("2D bands");
    case OrchestrationTask::Wannier:              return QObject::tr("Wannier");
    case OrchestrationTask::BornCharges:          return QObject::tr("Born Z*");
    case OrchestrationTask::Gw:                   return QObject::tr("GW");
    case OrchestrationTask::ChargeDensityDifference: return QObject::tr("CDD");
    case OrchestrationTask::RamanIr:              return QObject::tr("Raman/IR");
    case OrchestrationTask::ChargedDefects:       return QObject::tr("Charged");
    case OrchestrationTask::ChargedDefects2d:     return QObject::tr("Charged 2D");
    case OrchestrationTask::Container:            return QObject::tr("Container");
    case OrchestrationTask::Supercell:            return QObject::tr("Supercell");
    case OrchestrationTask::DefectGenerator:      return QObject::tr("Defect");
    case OrchestrationTask::TdbGenerator:         return QObject::tr("TDB");
    }
    return QObject::tr("Node");
}

QList<OrchestrationTask> orchestrationTasks()
{
    // Add Process order, grouped by family — the dialog draws a separator
    // wherever the family changes, so this list IS the menu layout.
    return {// Simulation
            OrchestrationTask::GeometryOptimization,
            OrchestrationTask::SinglePoint,
            OrchestrationTask::MolecularDynamics,
            OrchestrationTask::Phonon,
            // Transform
            OrchestrationTask::Container,
            OrchestrationTask::Supercell,
            OrchestrationTask::DefectGenerator,
            OrchestrationTask::TdbGenerator,
            // Analysis
            OrchestrationTask::ElectronicBands,
            OrchestrationTask::Optics,
            OrchestrationTask::Workfunction,
            OrchestrationTask::TwoDBands,
            OrchestrationTask::Wannier,
            OrchestrationTask::BornCharges,
            OrchestrationTask::Gw,
            OrchestrationTask::ChargeDensityDifference,
            OrchestrationTask::RamanIr,
            OrchestrationTask::ChargedDefects,
            OrchestrationTask::ChargedDefects2d};
}

bool orchestrationTaskHasDefaults(OrchestrationTask task)
{
    // The four self-contained simulations, plus the one transform whose
    // default is meaningful: a Supercell node starts at 2x2x2, which is a
    // real operation the node paints on its face.
    //
    // A Container with no structures and a Defect Generator with no operations
    // are excluded for the same reason the analysis modules are: their
    // "default" would be to pass the input through untouched, and a pipeline
    // that computes the pristine cell while its author believes it computed a
    // defect is the failure mode this whole panel exists to prevent.
    switch (task) {
    case OrchestrationTask::GeometryOptimization:
    case OrchestrationTask::SinglePoint:
    case OrchestrationTask::MolecularDynamics:
    case OrchestrationTask::Phonon:
    case OrchestrationTask::Supercell:
    // The TDB Generator belongs here for the opposite reason to the other two
    // transforms: its default IS a complete operation. Everything it needs —
    // the two endpoints, the compositions, the energies — comes out of the
    // file its input slot stages, so an unconfigured node fits an order-2
    // Redlich-Kister model to whatever ensemble arrives. There is no "pass the
    // input through untouched" failure mode to guard against, because it does
    // not pass anything through.
    case OrchestrationTask::TdbGenerator:
        return true;
    default:
        return false;
    }
}

QList<OrchestrationInputSlot> orchestrationInputSlots(OrchestrationTask task)
{
    // The staged names are a CONTRACT with the wizard factory: whatever a slot
    // is called here is the relative path the generated script will name, and
    // the path this panel guarantees exists by the time the node runs. The
    // wizard is configured long before the parent has produced anything, so
    // the two ends can only meet on a name agreed in advance.
    const QString groundState = QObject::tr("ground state");
    const QString gpw = QStringLiteral("single_point.gpw");
    switch (task) {
    case OrchestrationTask::GeometryOptimization:
    case OrchestrationTask::SinglePoint:
    case OrchestrationTask::MolecularDynamics:
    case OrchestrationTask::Phonon:
    // The transforms consume a STRUCTURE, not a completed run, and a
    // structure arrives through the ordinary geometry handoff (or, with no
    // parent, from the node's own material). There is nothing to stage under
    // an agreed name, so there are no slots.
    case OrchestrationTask::Container:
    case OrchestrationTask::Supercell:
    case OrchestrationTask::DefectGenerator:
        return {};

    case OrchestrationTask::TdbGenerator:
        // The one transform with a slot. It consumes a completed run's
        // RESULTS, not a structure, and the staged name is the same one the
        // convex-hull viewer reads — so the file that feeds the hull diagram
        // and the file that feeds the assessment are the same file, and they
        // cannot drift apart into two descriptions of one ensemble.
        return {{QObject::tr("formation-energy ensemble"),
                 QStringLiteral("cluster_expansion.json"),
                 QStringLiteral("cluster_expansion.json"), false}};

    case OrchestrationTask::ElectronicBands:
    case OrchestrationTask::Optics:
    case OrchestrationTask::Workfunction:
    case OrchestrationTask::TwoDBands:
    case OrchestrationTask::Wannier:
    case OrchestrationTask::BornCharges:
        return {{groundState, gpw, QStringLiteral("baseline_1.gpw"), false}};

    case OrchestrationTask::Gw:
        // GPAW route only. Yambo's baseline is a Quantum ESPRESSO `.save`
        // directory and no node on this canvas produces one, so offering it
        // would be offering a link that can never be satisfied.
        return {{groundState, gpw, QStringLiteral("baseline_1.gpw"), false}};

    case OrchestrationTask::ChargeDensityDifference:
        // A directory rather than a file: the CDD run reads the parent's
        // geometry and its calculator provenance alongside the density, and
        // rebuilds both fragment calculations from that run's own settings.
        return {{QObject::tr("combined system"), QString(),
                 QStringLiteral("baseline_1"), false}};

    case OrchestrationTask::RamanIr:
        // Born charges and optics are OPTIONAL here for the same reason they
        // are optional in the wizard: without Z* the IR column is reported as
        // zero rather than guessed, and an optics run only contributes the
        // broadening its dielectric response was validated with.
        return {{groundState, gpw, QStringLiteral("baseline_1.gpw"), false},
                {QObject::tr("Born charges"),
                 QStringLiteral("born_charges.json"),
                 QStringLiteral("baseline_2.json"), true},
                {QObject::tr("optics"), QStringLiteral("optics.json"),
                 QStringLiteral("baseline_3.json"), true}};

    case OrchestrationTask::ChargedDefects:
    case OrchestrationTask::ChargedDefects2d:
        // Two parents, and which is which is LINK ORDER. Nothing inside a .gpw
        // says "this one has the vacancy", so the graph has to say it — which
        // is why the node paints the assignment rather than leaving two
        // identical-looking links to be remembered.
        return {{QObject::tr("pristine host"), gpw,
                 QStringLiteral("baseline_1.gpw"), false},
                {QObject::tr("neutral defect"), gpw,
                 QStringLiteral("baseline_2.gpw"), false}};
    }
    return {};
}

int orchestrationRequiredInputs(OrchestrationTask task)
{
    int required = 0;
    for (const OrchestrationInputSlot& slot : orchestrationInputSlots(task))
        if (!slot.optional)
            ++required;
    return required;
}

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
    const bool transform =
        orchestrationTaskFamily(task_) == OrchestrationFamily::Transform;
    QString tip = transform
        ? QObject::tr(
              "%1.\nA structure-to-structure step: it runs on this canvas "
              "rather than as a job, so it has no calculator and no launch "
              "command. Double-click to configure it. Drag from the "
              "right-hand port onto another node to feed that node the "
              "structure this one produces.")
              .arg(title_)
        : QObject::tr(
              "%1 on %2 with %3.\nDouble-click to open its setup wizard "
              "(\"Save process node\" commits the configuration here). Drag "
              "from the right-hand port onto another node to run that node "
              "after this one, feeding it these results.")
              .arg(title_, materialName_,
                   EnginePresets::displayName(engine_));
    // A node that inherits completed runs says which, and in what order. Two
    // links into the same node look identical on the canvas, so the order they
    // were drawn in is the only thing that distinguishes them -- and it is the
    // difference between a defect diagram and its negative.
    const QList<OrchestrationInputSlot> slots = orchestrationInputSlots(task_);
    if (!slots.isEmpty()) {
        QStringList lines;
        for (int i = 0; i < slots.size(); ++i)
            lines << QObject::tr("  %1. %2%3")
                         .arg(i + 1)
                         .arg(slots[i].label,
                              slots[i].optional ? QObject::tr("  (optional)")
                                                : QString());
        tip += QObject::tr("\n\nInherits, in the order parents are linked:\n%1")
                   .arg(lines.join(QLatin1Char('\n')));
    }
    setToolTip(tip);
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

void OrchestrationNodeItem::setBatchItems(const QList<BatchItem>& items)
{
    batchItems_ = items;
    update();
}

void OrchestrationNodeItem::setSupercell(const SupercellSpec& spec)
{
    supercell_ = spec;
    update();
}

void OrchestrationNodeItem::setTdbGenerator(const TdbGeneratorSpec& spec)
{
    tdb_ = spec;
    update();
}

void OrchestrationNodeItem::setDefectSpec(const DefectSpec& spec)
{
    defects_ = spec;
    update();
}

QString OrchestrationNodeItem::configurationProblem() const
{
    switch (task_) {
    case OrchestrationTask::Container:
        if (batchItems_.isEmpty())
            return QObject::tr(
                "%1 holds no structures.\n\nDouble-click it and choose the "
                "materials the pipeline should be run over — one pass per "
                "structure, in the order they are listed.")
                .arg(title_);
        return QString();
    case OrchestrationTask::Supercell:
        if (!supercell_.isValid())
            return QObject::tr("%1 has an invalid repetition (%2).")
                .arg(title_, supercell_.describe());
        return QString();
    case OrchestrationTask::TdbGenerator:
        if (!tdb_.isValid())
            return QObject::tr(
                "%1 has settings it cannot fit (%2).\n\nThe Redlich-Kister "
                "order must be between 0 and 5, and the upper temperature "
                "above the lower one.")
                .arg(title_, tdb_.describe());
        return QString();
    case OrchestrationTask::DefectGenerator:
        if (defects_.isEmpty())
            return QObject::tr(
                "%1 has no operations.\n\nDouble-click it and add at least "
                "one substitution, addition or removal. A defect node that "
                "forwards the pristine cell untouched would make every "
                "formation energy downstream come out as zero.")
                .arg(title_);
        return QString();
    default:
        break;
    }
    if (!isConfigured() && !orchestrationTaskHasDefaults(task_))
        return QObject::tr(
            "%1 was not started: it has not been configured.\n\n"
            "Unlike a relaxation or a single point, this process has no "
            "defaults to fall back on — it reads a completed run, and which "
            "run that is can only come from its setup wizard. Double-click "
            "the node and save it first.")
            .arg(title_);
    return QString();
}

void OrchestrationNodeItem::recordJobDirectory(const QString& directory)
{
    jobDirectory_ = directory;
    if (!directory.isEmpty())
        jobHistory_ << directory;
}

void OrchestrationNodeItem::setStatus(Status status)
{
    status_ = status;
    update();
}

QPointF OrchestrationNodeItem::inputPortScenePos() const
{
    // rect(), not kNodeHeight: a node with input slots is taller, and a port
    // anchored to the constant would sit above the middle of its own box --
    // and, worse, the edges would join it there.
    return mapToScene(QPointF(0.0, rect().height() / 2.0));
}

QPointF OrchestrationNodeItem::outputPortScenePos() const
{
    return mapToScene(QPointF(rect().width(), rect().height() / 2.0));
}

void OrchestrationNodeItem::setInputSummary(const std::vector<InputLine>& lines)
{
    if (lines == inputLines_)
        return;
    inputLines_ = lines;
    prepareGeometryChange();
    setRect(0.0, 0.0, kNodeWidth,
            kNodeHeight + kInputLineHeight * static_cast<double>(lines.size()));
    for (OrchestrationEdgeItem* edge : edges_)
        edge->updatePath();
    update();
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
    // What the two metadata lines say depends on the family. A transform has
    // no calculator to report and its PARAMETERS are the thing a reader needs
    // — a Supercell node that does not say 2x2x2 on its face is a node you
    // have to open to understand.
    // A node with no structure of its own is the normal shape now: it says
    // where its geometry comes from rather than naming a document, because
    // naming one would be a claim about what it computes that is not true.
    QString primary = structure_
        ? QObject::tr("Material: %1").arg(materialName_)
        : QObject::tr("Structure: from input port");
    QString secondary =
        QObject::tr("Calculator: %1").arg(EnginePresets::displayName(engine_));
    switch (task_) {
    case OrchestrationTask::Container: {
        QStringList names;
        for (const BatchItem& item : batchItems_)
            names << item.first;
        primary = names.isEmpty()
            ? QObject::tr("Structures: none yet")
            : QObject::tr("Structures: %1").arg(names.join(QStringLiteral(", ")));
        secondary = QObject::tr("%n pipeline pass(es)", nullptr,
                                static_cast<int>(batchItems_.size()));
        break;
    }
    case OrchestrationTask::Supercell:
        primary = QObject::tr("Repeat: %1").arg(supercell_.describe());
        secondary = supercell_.isIdentity()
            ? QObject::tr("(identity — passes through)")
            : QObject::tr("in process, no calculator");
        break;
    case OrchestrationTask::TdbGenerator:
        primary = tdb_.describe();
        // Named on the face rather than left to the file: an assessment with
        // no vibrational data has zero excess entropy, and that is a property
        // of the pipeline (a cluster expansion carries energies only), not of
        // anything the user chose here.
        secondary = QObject::tr("static assessment, no calculator");
        break;
    case OrchestrationTask::DefectGenerator:
        primary = defects_.isEmpty() ? QObject::tr("No operations")
                                     : defects_.describe();
        // How many MATERIALS, not how many operations. The two are the same
        // number in one mode and unrelated in the other, and the count that
        // decides how long the pipeline runs is the materials one — a node
        // reading "4 operations" that is about to make the whole downstream
        // graph run four times should say so on its face.
        secondary = defects_.isEmpty()
            ? QObject::tr("in process, no calculator")
            : (defects_.mode == DefectSpec::Mode::Separate
                   ? QObject::tr("%n material(s), one per defect", nullptr,
                                 defects_.variantCount())
                   : QObject::tr("1 material, %n defect(s)", nullptr,
                                 static_cast<int>(defects_.operations.size())));
        break;
    default:
        break;
    }
    const QFontMetricsF metrics(painter->font());
    const double textWidth = box.width() - 24;
    painter->drawText(
        QRectF(box.left() + 12, box.top() + 30, textWidth, 16),
        Qt::AlignLeft | Qt::AlignVCenter,
        metrics.elidedText(primary, Qt::ElideRight, textWidth));
    painter->drawText(
        QRectF(box.left() + 12, box.top() + 47, textWidth, 16),
        Qt::AlignLeft | Qt::AlignVCenter,
        metrics.elidedText(secondary, Qt::ElideRight, textWidth));
    // Inherited runs, one line per slot. An unsatisfied slot is drawn in the
    // failure colour rather than merely left blank: a two-input node wired to
    // one parent looks entirely normal on the canvas, and the run will refuse
    // -- the node should say so before the pipeline is sent, not after.
    double y = box.top() + 64;
    for (const InputLine& line : inputLines_) {
        painter->setPen(line.satisfied ? QColor(0x45, 0x45, 0x45)
                                       : QColor(0xd6, 0x27, 0x28));
        painter->drawText(
            QRectF(box.left() + 12, y, box.width() - 24, kInputLineHeight),
            Qt::AlignLeft | Qt::AlignVCenter, line.text);
        y += kInputLineHeight;
    }

    painter->setPen(statusColor(status_).darker(120));
    painter->drawText(QRectF(box.left() + 12, y, box.width() - 24, 16),
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
// Transform configuration dialogs
// ---------------------------------------------------------------------------
//
// A transform node has no wizard: there is no engine to pick, no convergence
// to set and nothing to preview, so the whole configuration is three spin
// boxes or a table. These are deliberately local to this file — routing them
// through the host's wizard factory would mean the factory has to exist for a
// Supercell node to be editable, and the headless tests install none.

namespace {

/// Read every structure out of `path`. A multi-frame file (a trajectory, a
/// multi-image extxyz) contributes one container entry per frame, named
/// `stem #n` — importing a 20-frame trajectory into a container is one of the
/// obvious ways to build a sweep, and taking only its first frame would be a
/// silent loss.
QList<OrchestrationNodeItem::BatchItem> readStructuresFrom(const QString& path,
                                                           QString* error)
{
    QList<OrchestrationNodeItem::BatchItem> items;
    const QString stem = QFileInfo(path).completeBaseName();
    try {
        std::vector<core::Structure> frames =
            pybridge::AseBridge::readTrajectory(path.toStdString());
        if (frames.empty()) {
            *error = QObject::tr("%1 holds no structures.").arg(stem);
            return items;
        }
        const bool many = frames.size() > 1;
        for (std::size_t i = 0; i < frames.size(); ++i)
            items.append({many ? QStringLiteral("%1 #%2").arg(stem).arg(i + 1)
                               : stem,
                          std::make_shared<const core::Structure>(
                              std::move(frames[i]))});
    } catch (const std::exception& e) {
        *error = QObject::tr("%1 could not be read: %2")
                     .arg(stem, QString::fromUtf8(e.what()));
    }
    return items;
}

/// Bulk crystals straight from `ase.build.bulk`, one container entry per
/// element or formula typed.
///
/// The fastest way there is to build a sweep: "Cu, Au, Pt" and a structure of
/// "ground state" gives three correctly-parameterised fcc cells without
/// opening a file or a browser. The lattice constants come from ASE's own
/// `reference_states` table when left blank, which is both more accurate and
/// less error-prone than typing three numbers from memory.
bool addBulkCrystals(QWidget* parent,
                     QList<OrchestrationNodeItem::BatchItem>* items)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Add Bulk Crystals"));
    auto* form = new QFormLayout(&dialog);

    auto* formulaEdit = new QLineEdit(&dialog);
    formulaEdit->setPlaceholderText(QStringLiteral("Cu, Au, Pt"));
    formulaEdit->setToolTip(QObject::tr(
        "One element or formula per entry, separated by commas or spaces. "
        "Each becomes its own structure in the container, so the pipeline "
        "runs once per material."));
    form->addRow(QObject::tr("Elements / formulas:"), formulaEdit);

    auto* structureCombo = new QComboBox(&dialog);
    // "Ground state" first and default: for a list of elements it is both the
    // right answer and the only one that can differ per entry.
    structureCombo->addItem(QObject::tr("Ground state (ASE reference)"),
                            QString());
    for (const std::string& name : pybridge::BulkBuilder::prototypes())
        structureCombo->addItem(QString::fromStdString(name),
                                QString::fromStdString(name));
    form->addRow(QObject::tr("Crystal structure:"), structureCombo);

    auto* latticeSpin = new QDoubleSpinBox(&dialog);
    latticeSpin->setRange(0.0, 100.0);
    latticeSpin->setDecimals(4);
    latticeSpin->setSingleStep(0.05);
    latticeSpin->setSpecialValueText(QObject::tr("automatic"));
    latticeSpin->setSuffix(QStringLiteral(" Å"));
    latticeSpin->setToolTip(QObject::tr(
        "Lattice constant a. Left at \"automatic\" every entry gets its own "
        "tabulated value, which is what you want for a list of different "
        "elements; a fixed number applies to all of them."));
    form->addRow(QObject::tr("Lattice constant:"), latticeSpin);

    auto* cubicCheck = new QCheckBox(
        QObject::tr("Conventional cubic cell"), &dialog);
    cubicCheck->setToolTip(QObject::tr(
        "Build the conventional cell (4 atoms for fcc) rather than the "
        "primitive one (1 atom). Only meaningful for cubic structures."));
    form->addRow(cubicCheck);

    auto* status = new QLabel(&dialog);
    status->setWordWrap(true);
    form->addRow(status);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Add"));
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    const QStringList names = formulaEdit->text().split(
        QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    if (names.isEmpty())
        return false;

    QStringList problems;
    int added = 0;
    for (const QString& name : names) {
        pybridge::BulkBuilder::PrototypeSpec spec;
        spec.name = name.toStdString();
        spec.cubic = cubicCheck->isChecked();
        QString chosen = structureCombo->currentData().toString();
        if (chosen.isEmpty()) {
            // Per entry, not once for the list: Cu is fcc and Fe is bcc, and a
            // single structure applied to both would silently build one of
            // them wrong.
            const auto reference =
                pybridge::BulkBuilder::referenceState(spec.name);
            if (!reference.found || reference.crystalStructure.empty()) {
                problems << QObject::tr(
                                "%1: ASE has no ground-state reference — pick "
                                "a crystal structure explicitly.")
                                .arg(name);
                continue;
            }
            chosen = QString::fromStdString(reference.crystalStructure);
            spec.crystalStructure = reference.crystalStructure;
            if (latticeSpin->value() <= 0.0 && reference.a > 0.0)
                spec.a = reference.a;
            if (reference.hasCovera) {
                spec.covera = reference.covera;
                spec.hasCovera = true;
            }
        } else {
            spec.crystalStructure = chosen.toStdString();
        }
        if (latticeSpin->value() > 0.0)
            spec.a = latticeSpin->value();

        try {
            auto structure = std::make_shared<const core::Structure>(
                pybridge::BulkBuilder::buildPrototype(spec));
            items->append({QStringLiteral("%1 (%2)").arg(name, chosen),
                           structure});
            ++added;
        } catch (const std::exception& e) {
            problems << QStringLiteral("%1: %2").arg(
                name,
                QString::fromUtf8(e.what()).section(QLatin1Char('\n'), 0, 0));
        }
    }
    if (!problems.isEmpty())
        QMessageBox::warning(parent, QObject::tr("Add Bulk Crystals"),
                             problems.join(QLatin1Char('\n')));
    return added > 0;
}

/// Container contents: the structures the pipeline runs over, and in what
/// order.
///
/// Deliberately NOT a checklist of the open documents any more. A container
/// is the one place a pipeline's inputs are named, and requiring every one of
/// them to be open in a tab first made a twenty-structure sweep into twenty
/// tabs — so the list is now owned by the node, and filled from open
/// documents, from files on disk, or from the database browser.
bool editContainer(QWidget* parent, const OrchestrationWindow::MaterialList& open,
                   const OrchestrationWindow::StructureImporter& fromDatabase,
                   QList<OrchestrationNodeItem::BatchItem>* items)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Container Contents"));
    dialog.resize(520, 380);
    auto* layout = new QVBoxLayout(&dialog);
    auto* caption = new QLabel(
        QObject::tr("Everything downstream of this node runs once per "
                    "structure listed here, top to bottom."),
        &dialog);
    caption->setWordWrap(true);
    layout->addWidget(caption);

    // The structures live in `working`, not on the list items: a container
    // holds things that are not open anywhere — a file, a database hit, one
    // frame of a trajectory — so there is no document to look them up in, and
    // the widget is only ever a view of this list.
    QList<OrchestrationNodeItem::BatchItem> working = *items;
    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    const auto refill = [list, &working] {
        list->clear();
        for (int index = 0; index < working.size(); ++index)
            list->addItem(QStringLiteral("%1. %2")
                              .arg(index + 1)
                              .arg(working[index].first));
    };
    const auto addItem = [&working](const OrchestrationNodeItem::BatchItem& item) {
        working.append(item);
    };
    refill();
    layout->addWidget(list, 1);

    auto* row = new QHBoxLayout;
    auto* fromOpen = new QPushButton(QObject::tr("Add Open Document…"), &dialog);
    auto* fromBulk = new QPushButton(QObject::tr("Add Bulk Crystal…"), &dialog);
    auto* fromFile = new QPushButton(QObject::tr("Import from File…"), &dialog);
    auto* fromDb = new QPushButton(QObject::tr("Import from Database…"), &dialog);
    auto* remove = new QPushButton(QObject::tr("Remove"), &dialog);
    for (QPushButton* button : {fromOpen, fromBulk, fromFile, fromDb, remove})
        row->addWidget(button);
    row->addStretch(1);
    layout->addLayout(row);
    fromOpen->setEnabled(!open.isEmpty());
    fromOpen->setToolTip(open.isEmpty()
                             ? QObject::tr("No structure documents are open.")
                             : QString());

    QObject::connect(fromOpen, &QPushButton::clicked, &dialog, [&] {
        QStringList names;
        for (const auto& [name, structure] : open)
            names << name;
        bool ok = false;
        const QString chosen = QInputDialog::getItem(
            &dialog, QObject::tr("Add Open Document"),
            QObject::tr("Structure:"), names, 0, false, &ok);
        if (!ok)
            return;
        const int index = names.indexOf(chosen);
        if (index >= 0) {
            addItem(open[index]);
            refill();
        }
    });
    QObject::connect(fromBulk, &QPushButton::clicked, &dialog, [&] {
        if (addBulkCrystals(&dialog, &working))
            refill();
    });
    QObject::connect(fromFile, &QPushButton::clicked, &dialog, [&] {
        const QStringList paths = QFileDialog::getOpenFileNames(
            &dialog, QObject::tr("Import Structures"), QString(),
            QObject::tr("Structure files (*.extxyz *.xyz *.cif *.traj *.json "
                        "*.pdb *.poscar POSCAR CONTCAR *.vasp *.gen *.cube);;"
                        "All files (*)"));
        QStringList problems;
        for (const QString& path : paths) {
            QString error;
            const auto imported = readStructuresFrom(path, &error);
            if (!error.isEmpty())
                problems << error;
            for (const auto& item : imported)
                addItem(item);
        }
        refill();
        if (!problems.isEmpty())
            QMessageBox::warning(&dialog, QObject::tr("Import from File"),
                                 problems.join(QLatin1Char('\n')));
    });
    QObject::connect(fromDb, &QPushButton::clicked, &dialog, [&] {
        if (!fromDatabase) {
            QMessageBox::information(
                &dialog, QObject::tr("Import from Database"),
                QObject::tr("The database browser is not available in this "
                            "context."));
            return;
        }
        for (const auto& item : fromDatabase(&dialog))
            addItem(item);
        refill();
    });
    QObject::connect(remove, &QPushButton::clicked, list, [list, &working, &refill] {
        // Highest row first, so each erase leaves the rows still to go at the
        // indices they were found at.
        QList<int> rows;
        for (const QListWidgetItem* entry : list->selectedItems())
            rows << list->row(entry);
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : rows)
            if (row >= 0 && row < working.size())
                working.removeAt(row);
        refill();
    });

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    *items = working;
    return true;
}

/// Supercell Builder: the same integer 3x3 transformation matrix as
/// Build → "Supercell (Transformation Matrix)".
///
/// Three multipliers cannot express the cells that matter most — a rotated
/// orthorhombic cell of a hexagonal lattice, a sqrt(3)xsqrt(3) R30
/// reconstruction, a conventional cell built from a primitive one are all
/// non-diagonal — so the node takes the matrix, and offers the diagonal case
/// as a shortcut rather than as the only option.
bool editSupercell(QWidget* parent, SupercellSpec* spec)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Supercell Builder"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(
        QObject::tr("The supercell's lattice vectors are <b>P · (old cell)</b>, "
                    "row by row. Applied to the structure that reaches this "
                    "node, so a relaxation upstream is expanded after it "
                    "converges — not before."),
        &dialog);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    auto* grid = new QGridLayout;
    QSpinBox* cells[3][3];
    const auto bracket = [&dialog](const QString& glyph) {
        auto* label = new QLabel(glyph, &dialog);
        QFont font = label->font();
        font.setPointSizeF(font.pointSizeF() * 2.4);
        label->setFont(font);
        return label;
    };
    grid->addWidget(bracket(QStringLiteral("[")), 0, 0, 3, 1);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            auto* spin = new QSpinBox(&dialog);
            spin->setRange(-64, 64);
            spin->setValue(spec->p[i][j]);
            spin->setAlignment(Qt::AlignCenter);
            cells[i][j] = spin;
            grid->addWidget(spin, i, j + 1);
        }
    }
    grid->addWidget(bracket(QStringLiteral("]")), 0, 4, 3, 1);
    layout->addLayout(grid);

    auto* status = new QLabel(&dialog);
    status->setWordWrap(true);
    layout->addWidget(status);

    auto* shortcuts = new QHBoxLayout;
    auto* identity = new QPushButton(QObject::tr("Identity"), &dialog);
    auto* diagonal = new QPushButton(QObject::tr("2 × 2 × 2"), &dialog);
    shortcuts->addWidget(identity);
    shortcuts->addWidget(diagonal);
    shortcuts->addStretch(1);
    layout->addLayout(shortcuts);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);

    const auto currentSpec = [&cells] {
        SupercellSpec current;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                current.p[i][j] = cells[i][j]->value();
        return current;
    };
    const auto load = [&cells](const SupercellSpec& value) {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                cells[i][j]->setValue(value.p[i][j]);
    };
    const auto refresh = [&currentSpec, status, buttons] {
        const SupercellSpec current = currentSpec();
        const long det = current.determinant();
        // det P = 0 is not a degenerate case to tolerate: the three vectors are
        // coplanar, so there is no cell. OK is disabled rather than the error
        // being deferred to the run, hours later.
        buttons->button(QDialogButtonBox::Ok)->setEnabled(det != 0);
        if (det == 0) {
            status->setStyleSheet(QStringLiteral("color:#c0392b;"));
            status->setText(QObject::tr(
                "det P = 0 — the three transformed vectors are coplanar; not a "
                "valid supercell."));
            return;
        }
        status->setStyleSheet(QString());
        status->setText(
            current.isIdentity()
                ? QObject::tr("det P = 1, the identity — this node would pass "
                              "the structure through unchanged.")
                : QObject::tr("det P = %1 — the supercell holds %1 times as "
                              "many atoms as the cell that reaches this node.")
                      .arg(std::labs(det)));
    };
    for (auto& row : cells)
        for (QSpinBox* spin : row)
            QObject::connect(spin, &QSpinBox::valueChanged, &dialog,
                             [&refresh] { refresh(); });
    QObject::connect(identity, &QPushButton::clicked, &dialog,
                     [&load] { load(SupercellSpec{}); });
    QObject::connect(diagonal, &QPushButton::clicked, &dialog, [&load] {
        load(SupercellSpec::diagonal(2, 2, 2));
    });
    refresh();

    if (dialog.exec() != QDialog::Accepted)
        return false;
    *spec = currentSpec();
    return true;
}

/// Settings of a TDB Generator node.
///
/// A plain dialog like the other two transforms, not a wizard: there is no
/// calculator to configure, no convergence to choose and no script to
/// generate. The endpoint fields are deliberately optional — the ensemble file
/// names the composition axis and the formulas name the other endpoint, and
/// making the user retype them would be one more place for the node and its
/// input to disagree.
bool editTdbGenerator(QWidget* parent, TdbGeneratorSpec* spec)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("TDB Generator"));
    auto* layout = new QVBoxLayout(&dialog);

    auto* intro = new QLabel(
        QObject::tr(
            "Fit a <b>Redlich-Kister</b> excess Gibbs energy to the formation "
            "energies of the ensemble reaching this node, and write a "
            "<tt>.tdb</tt> beside them.<br><br>"
            "The assessment is <b>static</b>: a cluster-expansion ensemble "
            "carries total energies and no phonons, so the fitted excess "
            "energy is a pure enthalpy and every excess entropy in the "
            "database is exactly zero. Modules → CALPHAD → \"From DFT…\" is "
            "where a vibrational term can be added."),
        &dialog);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    auto* elementA = new QLineEdit(spec->elementA, &dialog);
    elementA->setPlaceholderText(QObject::tr("from the ensemble"));
    form->addRow(QObject::tr("Element at x = 0:"), elementA);
    auto* elementB = new QLineEdit(spec->elementB, &dialog);
    elementB->setPlaceholderText(QObject::tr("from the ensemble"));
    form->addRow(QObject::tr("Element at x = 1:"), elementB);
    auto* phase = new QLineEdit(spec->phaseName, &dialog);
    form->addRow(QObject::tr("Phase name:"), phase);
    auto* order = new QSpinBox(&dialog);
    order->setRange(0, 5);
    order->setValue(spec->order);
    order->setToolTip(QObject::tr(
        "Order 0 is the regular solution — one symmetric interaction. Each "
        "further order adds an asymmetry, and needs compositions able to "
        "resolve it: the fit refuses rather than returning coefficients the "
        "data does not determine."));
    form->addRow(QObject::tr("Redlich-Kister order:"), order);
    auto* low = new QDoubleSpinBox(&dialog);
    low->setRange(1.0, 6000.0);
    low->setValue(spec->lowTemperatureK);
    low->setSuffix(QObject::tr(" K"));
    form->addRow(QObject::tr("Valid from:"), low);
    auto* high = new QDoubleSpinBox(&dialog);
    high->setRange(1.0, 20000.0);
    high->setValue(spec->highTemperatureK);
    high->setSuffix(QObject::tr(" K"));
    form->addRow(QObject::tr("Valid to:"), high);
    layout->addLayout(form);

    auto* status = new QLabel(&dialog);
    status->setWordWrap(true);
    layout->addWidget(status);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);
    layout->addWidget(buttons);

    // Same seam as the supercell dialog: OK is disabled on a spec that cannot
    // be run, so the refusal happens here rather than hours into a pipeline.
    const auto refresh = [&] {
        const bool ok = high->value() > low->value();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
        status->setStyleSheet(ok ? QString()
                                 : QStringLiteral("color:#c0392b;"));
        status->setText(ok
                            ? QObject::tr("Writes assessment.tdb and "
                                          "calphad_assessment.json into this "
                                          "node's results.")
                            : QObject::tr("The upper temperature limit must be "
                                          "above the lower one."));
    };
    QObject::connect(low, &QDoubleSpinBox::valueChanged, &dialog,
                     [&refresh] { refresh(); });
    QObject::connect(high, &QDoubleSpinBox::valueChanged, &dialog,
                     [&refresh] { refresh(); });
    refresh();

    if (dialog.exec() != QDialog::Accepted)
        return false;
    spec->elementA = elementA->text().trimmed().toUpper();
    spec->elementB = elementB->text().trimmed().toUpper();
    spec->phaseName = phase->text().trimmed().toUpper();
    if (spec->phaseName.isEmpty())
        spec->phaseName = QStringLiteral("FCC_A1");
    spec->order = order->value();
    spec->lowTemperatureK = low->value();
    spec->highTemperatureK = high->value();
    return true;
}

enum DefectColumn { kKindColumn, kIndexColumn, kElementColumn,
                    kPositionColumn, kFrameColumn, kDefectColumns };

/// Show only the fields the chosen action actually uses.
///
/// Substitute and Remove address EXISTING atoms, so they want atom indices and
/// a position means nothing to them. Add creates an atom that is not there yet,
/// so it wants coordinates and there is no index to give. Leaving both sets
/// editable invites a recipe that carries a position nothing will read — and,
/// worse, one where a stale index looks like it is still doing something.
///
/// The unused cells are cleared as well as locked: a greyed-out "0, 0, 0" left
/// beside a Remove row is a value the reader has to work out is ignored.
void syncDefectRow(QTableWidget* table, int row)
{
    const auto* kind =
        qobject_cast<QComboBox*>(table->cellWidget(row, kKindColumn));
    if (!kind)
        return;
    const auto action =
        static_cast<DefectOperation::Kind>(kind->currentData().toInt());
    const bool adding = action == DefectOperation::Kind::Add;
    const bool needsElement = action != DefectOperation::Kind::Remove;

    const auto setUsable = [table, row](int column, bool usable,
                                        const QString& placeholder) {
        QTableWidgetItem* item = table->item(row, column);
        if (!item) {
            item = new QTableWidgetItem;
            table->setItem(row, column, item);
        }
        if (usable) {
            item->setFlags(item->flags() | Qt::ItemIsEditable
                           | Qt::ItemIsEnabled);
            item->setForeground(QBrush());
            if (item->text() == placeholder)
                item->setText(QString());
        } else {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setText(placeholder);
            item->setForeground(QColor(0x8a, 0x8a, 0x8a));
        }
    };
    const QString unused = QObject::tr("—");
    setUsable(kIndexColumn, !adding, unused);
    setUsable(kElementColumn, needsElement, unused);
    setUsable(kPositionColumn, adding, unused);
    if (QWidget* frame = table->cellWidget(row, kFrameColumn))
        frame->setEnabled(adding);
}

/// Put the widgets for one recipe row in place and load `operation` into them.
void fillDefectRow(QTableWidget* table, int row, const DefectOperation& operation)
{
    auto* kind = new QComboBox(table);
    kind->addItem(QObject::tr("Substitute"),
                  static_cast<int>(DefectOperation::Kind::Substitute));
    kind->addItem(QObject::tr("Remove"),
                  static_cast<int>(DefectOperation::Kind::Remove));
    kind->addItem(QObject::tr("Add"),
                  static_cast<int>(DefectOperation::Kind::Add));
    kind->setCurrentIndex(kind->findData(static_cast<int>(operation.kind)));
    table->setCellWidget(row, kKindColumn, kind);

    auto* frame = new QComboBox(table);
    frame->addItem(QObject::tr("Cartesian Å"), false);
    frame->addItem(QObject::tr("Fractional"), true);
    frame->setCurrentIndex(operation.fractional ? 1 : 0);
    table->setCellWidget(row, kFrameColumn, frame);

    table->setItem(row, kIndexColumn, new QTableWidgetItem(operation.indices));
    table->setItem(row, kElementColumn, new QTableWidgetItem(operation.element));
    table->setItem(row, kPositionColumn,
                   new QTableWidgetItem(
                       operation.kind == DefectOperation::Kind::Add
                           ? QStringLiteral("%1, %2, %3")
                                 .arg(operation.x, 0, 'g', 6)
                                 .arg(operation.y, 0, 'g', 6)
                                 .arg(operation.z, 0, 'g', 6)
                           : QString()));
    // The row is looked up from the combo rather than captured: removing a row
    // shifts every row below it, and a captured index would then sync somebody
    // else's fields — or one past the end.
    QObject::connect(kind, &QComboBox::currentIndexChanged, table, [table, kind] {
        for (int r = 0; r < table->rowCount(); ++r)
            if (table->cellWidget(r, kKindColumn) == kind) {
                syncDefectRow(table, r);
                return;
            }
    });
    syncDefectRow(table, row);
}

bool editDefects(QWidget* parent, DefectSpec* spec)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Defect Generator"));
    dialog.resize(560, 320);
    auto* layout = new QVBoxLayout(&dialog);
    auto* caption = new QLabel(
        QObject::tr(
            "<b>Substitute</b> and <b>Remove</b> address existing atoms by "
            "index; <b>Add</b> places a new atom at a position. Only the "
            "fields an action uses are editable.<br>"
            "Every index refers to the structure that REACHES this node, "
            "numbered from 0 — removals do not renumber the atoms the other "
            "rows address. Ranges are written \"4-8\"."),
        &dialog);
    caption->setTextFormat(Qt::RichText);
    caption->setWordWrap(true);
    layout->addWidget(caption);

    auto* table = new QTableWidget(0, kDefectColumns, &dialog);
    table->setHorizontalHeaderLabels(
        {QObject::tr("Operation"), QObject::tr("Atoms"),
         QObject::tr("Element"), QObject::tr("Position"),
         QObject::tr("Coordinates")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    // Not a preference: without this, a dead key (´ ` ~ ^) typed at this table
    // recurses through the Cocoa input context until the stack is gone. See
    // disableTypeToEdit().
    disableTypeToEdit(table);
    for (const DefectOperation& operation : spec->operations) {
        const int row = table->rowCount();
        table->insertRow(row);
        fillDefectRow(table, row, operation);
    }
    layout->addWidget(table, 1);

    // The mode: what the rows above MEAN together. Placed under the table
    // because it is a statement about the whole recipe, and worded as an
    // outcome ("one material with...") rather than as a term of art — the
    // difference between a di-vacancy and a set of two vacancies is the entire
    // question, and nobody should have to infer it from the word "combined".
    auto* modeBox = new QGroupBox(QObject::tr("These operations produce"),
                                  &dialog);
    auto* modeLayout = new QVBoxLayout(modeBox);
    auto* combinedRadio = new QRadioButton(
        QObject::tr("One material carrying every operation at once"), modeBox);
    combinedRadio->setToolTip(QObject::tr(
        "Every row is applied to the same cell — a di-vacancy, a substitution "
        "beside an interstitial, a defect complex.\n\n"
        "The node produces ONE structure, and the pipeline downstream runs "
        "once."));
    auto* separateRadio = new QRadioButton(
        QObject::tr("One material per operation, each on the pristine cell"),
        modeBox);
    separateRadio->setToolTip(QObject::tr(
        "Each row is applied on its own to the structure that reaches this "
        "node, giving a SET of singly-defective cells — which is what a "
        "formation-energy or dopant-screening study needs, because the whole "
        "point is that the defects do not see each other.\n\n"
        "The node produces one structure per row, each opens in its own tab, "
        "and the pipeline downstream runs once per defect — exactly as a "
        "Structure Container makes it run once per structure."));
    modeLayout->addWidget(combinedRadio);
    modeLayout->addWidget(separateRadio);
    (spec->mode == DefectSpec::Mode::Separate ? separateRadio : combinedRadio)
        ->setChecked(true);
    layout->addWidget(modeBox);

    auto* rowButtons = new QHBoxLayout;
    auto* addRow = new QPushButton(QObject::tr("Add operation"), &dialog);
    auto* removeRow = new QPushButton(QObject::tr("Remove operation"), &dialog);
    rowButtons->addWidget(addRow);
    rowButtons->addWidget(removeRow);
    rowButtons->addStretch(1);
    layout->addLayout(rowButtons);
    QObject::connect(addRow, &QPushButton::clicked, table, [table] {
        const int row = table->rowCount();
        table->insertRow(row);
        fillDefectRow(table, row, DefectOperation{});
    });
    QObject::connect(removeRow, &QPushButton::clicked, table, [table] {
        if (table->currentRow() >= 0)
            table->removeRow(table->currentRow());
    });

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    DefectSpec edited;
    edited.mode = separateRadio->isChecked() ? DefectSpec::Mode::Separate
                                             : DefectSpec::Mode::Combined;
    for (int row = 0; row < table->rowCount(); ++row) {
        DefectOperation operation;
        if (auto* kind =
                qobject_cast<QComboBox*>(table->cellWidget(row, kKindColumn)))
            operation.kind = static_cast<DefectOperation::Kind>(
                kind->currentData().toInt());
        const bool adding = operation.kind == DefectOperation::Kind::Add;
        // Only the fields the action uses are read back. The others hold the
        // "not used here" placeholder, and reading that as an index list or a
        // position would put nonsense into the recipe.
        if (auto* frame =
                qobject_cast<QComboBox*>(table->cellWidget(row, kFrameColumn)))
            operation.fractional = adding && frame->currentData().toBool();
        if (const QTableWidgetItem* cell = table->item(row, kIndexColumn);
            cell && !adding)
            operation.indices = cell->text().trimmed();
        if (const QTableWidgetItem* cell = table->item(row, kElementColumn);
            cell && operation.kind != DefectOperation::Kind::Remove)
            operation.element = cell->text().trimmed();
        if (const QTableWidgetItem* cell = table->item(row, kPositionColumn);
            cell && adding) {
            const QStringList parts = cell->text().split(
                QRegularExpression(QStringLiteral("[,\\s]+")),
                Qt::SkipEmptyParts);
            if (parts.size() == 3) {
                operation.x = parts[0].toDouble();
                operation.y = parts[1].toDouble();
                operation.z = parts[2].toDouble();
            }
        }
        edited.operations.append(operation);
    }
    *spec = edited;
    return true;
}

} // namespace

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
    auto* clearButton = makeButton(
        QStringLiteral("eraser-line"),
        tr("Clear Orchestration — delete every node and link on the "
           "canvas.\n\nAsks for confirmation first: the canvas is not in the "
           "undo stack."));
    auto* openButton = makeButton(
        QStringLiteral("folder-open-line"),
        tr("Open Workflow… — load a pipeline from a JSON file, replacing "
           "what is on the canvas.\n\n"
           "Asks first when there is something to replace. Reads what Export "
           "Workflow writes, including the workflow.json saved beside every "
           "run's results."));
    auto* exportButton = makeButton(
        QStringLiteral("share-fill"),
        tr("Export Workflow… — write the whole pipeline to a JSON file.\n\n"
           "Structures travel inside the file, so it is self-contained: copy "
           "it to a cluster and run it there with calango-cli, headlessly."));
    auto* layoutButton = makeButton(
        QStringLiteral("node-tree"),
        tr("Auto-Layout — rearrange every node into columns in execution "
           "order, untangling the links.\n\n"
           "Each node lands one column right of its last-finishing parent, and "
           "the rows within a column are ordered to keep links from crossing. "
           "Moves nodes only; the pipeline itself is untouched."));
    auto* fitButton = makeButton(
        QStringLiteral("fullscreen-line"),
        tr("Fit to Screen — zoom and pan so the whole pipeline is visible, "
           "with a margin around it.\n\n"
           "The canvas is 4000 units across and a pipeline built by "
           "double-clicking wanders; this is how you find it again."));
    controls->addStretch(1);
    // Trailing and set apart by the stretch: these are the buttons that
    // COMMIT — everything to their left edits the pipeline, these two run it.
    resumeButton_ = makeButton(
        QStringLiteral("restart-line"),
        tr("Resume — re-run only what has not succeeded, in the same "
           "orchestration folder.\n\n"
           "Every node that finished keeps its status, its directory and its "
           "results; the failed node and everything downstream of it are "
           "queued again. Fix the failed node's parameters first "
           "(double-click it) and this picks up from exactly there instead of "
           "recomputing the pipeline from the start."));
    runButton_ = makeButton(
        QStringLiteral("play-circle-fill"),
        tr("Send to Processes — queue every node (they show as \"waiting\") "
           "and execute the pipeline in dependency order, one process at a "
           "time.\n\n"
           "A Structure Container makes the whole downstream pipeline run "
           "once per structure it holds, in order.\n\n"
           "Each node appears in the Processes panel as it is dispatched, and "
           "its metrics stream into Results. This RESTARTS everything — use "
           "Resume to continue an interrupted run."));
    abortButton_ = makeButton(
        QStringLiteral("stop-circle-fill"),
        tr("Abort — stop the running orchestration.\n\n"
           "The node in flight is killed and marked failed; the nodes still "
           "queued are left unrun. Everything that already finished KEEPS its "
           "status, its directory and its results, so Resume picks up from "
           "exactly there.\n\n"
           "This is the counterpart of Resume, not an undo: nothing already "
           "computed is discarded, and the run folder is left readable rather "
           "than half-written."));

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
    connect(clearButton, &QPushButton::clicked, this,
            &OrchestrationWindow::clearOrchestration);
    // qOverload<>: openWorkflow(QString) is the programmatic form, and the
    // overload set is ambiguous without it.
    connect(openButton, &QPushButton::clicked, this,
            qOverload<>(&OrchestrationWindow::openWorkflow));
    connect(exportButton, &QPushButton::clicked, this,
            &OrchestrationWindow::exportWorkflow);
    connect(layoutButton, &QPushButton::clicked, this,
            &OrchestrationWindow::autoLayout);
    connect(fitButton, &QPushButton::clicked, this,
            &OrchestrationWindow::fitToScreen);
    connect(resumeButton_, &QPushButton::clicked, this,
            &OrchestrationWindow::resumeFromFailure);
    connect(runButton_, &QPushButton::clicked, this,
            &OrchestrationWindow::sendToProcesses);
    connect(abortButton_, &QPushButton::clicked, this,
            &OrchestrationWindow::abortOrchestration);
    updateRunControls();
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
    // No material row, and no "open a structure first" gate. Structures enter
    // a pipeline through a Structure Container and travel down the links; a
    // process node is a process, not a process-plus-a-molecule, and asking
    // for a document at creation time forced every sweep member to be open in
    // a tab before it could be studied.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Process"));
    dialog.resize(620, 460);
    auto* layout = new QVBoxLayout(&dialog);

    // A categorised tree rather than a flat combo: eighteen processes in one
    // drop-down is a list you scroll rather than read, and the family a
    // process belongs to is the single most useful thing to know before
    // picking one — it says whether the node needs a parent at all.
    auto* tree = new QTreeWidget(&dialog);
    tree->setHeaderLabels({tr("Process"), tr("Inputs")});
    tree->setRootIsDecorated(false);
    tree->setIndentation(14);
    tree->setColumnWidth(0, 300);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);

    struct FamilyGroup {
        OrchestrationFamily family;
        QString title;
        QString blurb;
    };
    const QList<FamilyGroup> groups = {
        {OrchestrationFamily::Transform, tr("Structures"),
         tr("Where geometry comes from, and how it is edited on the way "
            "through. These run on the canvas — no calculator, no job.")},
        {OrchestrationFamily::Simulation, tr("Simulations"),
         tr("Read a structure from their input port and compute with it.")},
        {OrchestrationFamily::Analysis, tr("Analysis of a completed run"),
         tr("Read one or more finished runs rather than a structure, so each "
            "needs that many parent nodes linked to it.")},
    };
    QTreeWidgetItem* firstLeaf = nullptr;
    for (const FamilyGroup& group : groups) {
        auto* header = new QTreeWidgetItem(tree, {group.title});
        QFont bold = header->font(0);
        bold.setBold(true);
        header->setFont(0, bold);
        header->setFirstColumnSpanned(true);
        header->setFlags(Qt::ItemIsEnabled); // a heading, not a choice
        header->setToolTip(0, group.blurb);
        for (OrchestrationTask task : orchestrationTasks()) {
            if (orchestrationTaskFamily(task) != group.family)
                continue;
            const int required = orchestrationRequiredInputs(task);
            QString inputs = required > 0
                ? tr("%n run(s)", nullptr, required)
                : (task == OrchestrationTask::Container ? tr("—")
                                                        : tr("structure"));
            auto* leaf = new QTreeWidgetItem(
                header, {orchestrationTaskDisplayName(task), inputs});
            leaf->setData(0, Qt::UserRole, static_cast<int>(task));
            QString hint;
            if (required > 0)
                hint = tr("Inherits %n completed run(s). Link that many "
                          "parent nodes to it — the first link fills the "
                          "first input.",
                          nullptr, required);
            else if (task == OrchestrationTask::Container)
                hint = tr("Holds structures — from open documents, files or "
                          "the database. Everything downstream of it runs "
                          "once per structure, in order.");
            else if (group.family == OrchestrationFamily::Transform)
                hint = tr("Edits the structure that reaches it and passes the "
                          "result on. Runs on this canvas, not as a job.");
            else
                hint = tr("Runs on the structure that reaches its input port. "
                          "Link a Structure Container upstream of it.");
            leaf->setToolTip(0, hint);
            leaf->setToolTip(1, hint);
            if (!firstLeaf)
                firstLeaf = leaf;
        }
        header->setExpanded(true);
    }
    layout->addWidget(tree, 1);

    // The blurb for whatever is selected, so the tool tip is not the only
    // place the distinction between the families is explained.
    auto* description = new QLabel(&dialog);
    description->setWordWrap(true);
    description->setMinimumHeight(44);
    description->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(description);

    auto* engineRow = new QFormLayout;
    auto* engineCombo = new QComboBox(&dialog);
    // MACE first, and therefore pre-selected. A machine-learned potential is
    // the calculator a pipeline should reach for by default: it runs at a cost
    // that makes a batch over a dozen structures finish, which is the shape of
    // work this canvas exists for, while EMT is a toy and GPAW/VASP are a
    // deliberate decision about machine time rather than a default.
    //
    // xTB sits second for the same reason MACE sits first: a semi-empirical
    // tight-binding run is seconds per structure, which is what makes a sweep
    // over a container finish, and unlike MACE it needs no trained model for
    // the elements involved.
    for (core::CalculatorKind kind :
         {core::CalculatorKind::Mace, core::CalculatorKind::Xtb,
          core::CalculatorKind::Gpaw, core::CalculatorKind::Vasp,
          core::CalculatorKind::EMT})
        engineCombo->addItem(EnginePresets::displayName(kind),
                             static_cast<int>(kind));
    engineRow->addRow(tr("Calculator:"), engineCombo);
    layout->addLayout(engineRow);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add Process"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    const auto selectedTask = [tree]() -> std::optional<OrchestrationTask> {
        QTreeWidgetItem* item = tree->currentItem();
        if (!item || item->data(0, Qt::UserRole).isNull())
            return std::nullopt;
        return static_cast<OrchestrationTask>(
            item->data(0, Qt::UserRole).toInt());
    };
    // A transform has no calculator; showing it an engine picker would be
    // offering a setting that does nothing.
    const auto syncControls = [&] {
        const std::optional<OrchestrationTask> task = selectedTask();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(task.has_value());
        const bool needsEngine = task
            && orchestrationTaskFamily(*task) != OrchestrationFamily::Transform;
        engineCombo->setEnabled(needsEngine);
        description->setText(
            task ? tree->currentItem()->toolTip(0)
                 : tr("Pick a process. Structures enter the pipeline through "
                      "a Structure Container and flow along the links."));
    };
    connect(tree, &QTreeWidget::currentItemChanged, &dialog,
            [&syncControls] { syncControls(); });
    connect(tree, &QTreeWidget::itemDoubleClicked, &dialog,
            [&dialog](QTreeWidgetItem* item) {
                if (item && !item->data(0, Qt::UserRole).isNull())
                    dialog.accept();
            });
    if (firstLeaf)
        tree->setCurrentItem(firstLeaf);
    syncControls();

    if (dialog.exec() != QDialog::Accepted)
        return;
    const std::optional<OrchestrationTask> task = selectedTask();
    if (!task)
        return;

    OrchestrationNodeItem* node = addProcessNode(
        *task,
        static_cast<core::CalculatorKind>(engineCombo->currentData().toInt()));
    if (node && scenePos)
        node->setPos(*scenePos
                     - QPointF(kNodeWidth / 2.0, kNodeHeight / 2.0));
    // A brand-new container is empty and cannot run; opening its contents
    // dialog immediately is what the user was going to do next anyway, and it
    // is where the file/database importers live.
    if (node && node->task() == OrchestrationTask::Container)
        openNodeWizard(node);
}

OrchestrationNodeItem* OrchestrationWindow::addProcessNode(
    OrchestrationTask task, core::CalculatorKind engine)
{
    auto* node = new OrchestrationNodeItem(
        nextNodeId_++, orchestrationTaskDisplayName(task), task,
        tr("from input port"), nullptr, engine);
    // Stagger new nodes left-to-right so a freshly built pipeline reads in
    // execution order without any manual arranging.
    node->setPos(static_cast<double>(nodes_.size()) * (kNodeWidth + 60.0),
                 0.0);
    scene_->addItem(node);
    nodes_.push_back(node);
    refreshInputSummaries();
    updateRunControls();
    return node;
}

OrchestrationNodeItem* OrchestrationWindow::addProcessNode(OrchestrationTask task,
                                                 int materialIndex,
                                                 core::CalculatorKind engine)
{
    if (materialIndex < 0 || materialIndex >= materials_.size())
        return nullptr;
    auto* node = new OrchestrationNodeItem(
        nextNodeId_++, orchestrationTaskDisplayName(task), task,
        materials_[materialIndex].first, materials_[materialIndex].second,
        engine);
    // A Container seeded this way starts holding that one material, so it
    // behaves like an ordinary source. (From the UI a container starts empty
    // and its contents dialog opens straight away.)
    if (task == OrchestrationTask::Container)
        node->setBatchItems({materials_[materialIndex]});
    node->setPos(static_cast<double>(nodes_.size()) * (kNodeWidth + 60.0),
                 0.0);
    scene_->addItem(node);
    nodes_.push_back(node);
    refreshInputSummaries();
    updateRunControls();
    return node;
}

std::shared_ptr<const core::Structure>
OrchestrationWindow::representativeStructure(OrchestrationNodeItem* node) const
{
    // Breadth-first upstream, so the nearest source wins: a Supercell node
    // between a Container and a wizard should still offer the Container's
    // elements, and the wizard only needs to know WHICH ELEMENTS it will see.
    std::vector<OrchestrationNodeItem*> frontier{node};
    std::vector<OrchestrationNodeItem*> seen;
    while (!frontier.empty()) {
        std::vector<OrchestrationNodeItem*> next;
        for (OrchestrationNodeItem* current : frontier) {
            if (std::find(seen.begin(), seen.end(), current) != seen.end())
                continue;
            seen.push_back(current);
            if (!current->batchItems().isEmpty())
                return current->batchItems().front().second;
            if (current->structure())
                return current->structure();
            for (OrchestrationEdgeItem* edge : edges_)
                if (edge->to() == current)
                    next.push_back(edge->from());
        }
        frontier = next;
    }
    return nullptr;
}

QList<QPair<OrchestrationNodeItem*, OrchestrationNodeItem*>>
OrchestrationWindow::links() const
{
    QList<QPair<OrchestrationNodeItem*, OrchestrationNodeItem*>> list;
    for (const OrchestrationEdgeItem* edge : edges_)
        list.append(qMakePair(edge->from(), edge->to()));
    return list;
}

void OrchestrationWindow::setDatabaseImporter(StructureImporter importer)
{
    databaseImporter_ = std::move(importer);
}

void OrchestrationWindow::exportWorkflow()
{
    if (nodes_.empty()) {
        QMessageBox::information(this, tr("Export Workflow"),
                                 tr("There is nothing on the canvas to "
                                    "export."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Workflow"), QStringLiteral("workflow.json"),
        tr("Calango workflow (*.json);;All files (*)"));
    if (path.isEmpty())
        return;

    QStringList warnings;
    const QJsonObject document = OrchestrationDocument::build(*this, &warnings);
    QString error;
    if (!OrchestrationDocument::write(document, path, &error)) {
        QMessageBox::warning(this, tr("Export Workflow"),
                             tr("%1 could not be written: %2")
                                 .arg(QFileInfo(path).fileName(), error));
        return;
    }
    // Warnings are shown even on success. Each one is a structure that is NOT
    // in the file the user is about to copy to a cluster, and finding that
    // out there instead of here costs a queue slot and a night.
    if (!warnings.isEmpty())
        QMessageBox::warning(
            this, tr("Export Workflow"),
            tr("The workflow was written, but some structures were left "
               "out:\n\n%1")
                .arg(warnings.join(QLatin1Char('\n'))));
}

void OrchestrationWindow::clearGraph()
{
    // Edges first: they hold raw pointers to the nodes.
    for (OrchestrationEdgeItem* edge : edges_) {
        scene_->removeItem(edge);
        delete edge;
    }
    edges_.clear();
    for (OrchestrationNodeItem* node : nodes_) {
        scene_->removeItem(node);
        delete node;
    }
    nodes_.clear();
    // Node ids restart, and so does the run state: whatever ran belonged to
    // a pipeline that no longer exists, and leaving orchestrationRoot_ set
    // would leave Resume offering to continue it.
    nextNodeId_ = 1;
    orchestrationRoot_.clear();
    batchIndex_ = 0;
    batchLength_ = 1;
    launchedCount_ = 0;
    updateRunControls();
}

void OrchestrationWindow::clearOrchestration()
{
    if (nodes_.empty())
        return;
    if (runningNode_) {
        refuse(tr("The pipeline is running. Wait for it to finish before "
                  "clearing the canvas."));
        return;
    }
    if (!confirm(tr("Are you sure you want to delete all nodes from the "
                    "workflow?")))
        return;
    clearGraph();
}

bool OrchestrationWindow::openWorkflow(const QString& path)
{
    if (runningNode_) {
        refuse(tr("The pipeline is running. Wait for it to finish before "
                  "opening another workflow."));
        return false;
    }
    // Opening REPLACES the canvas, so it asks the same question clearing does
    // — the pipeline about to be discarded is worth as much as the one about
    // to be loaded, and there is no undo for either.
    if (!nodes_.empty()
        && !confirm(tr("Opening a workflow replaces everything on the "
                       "canvas.\n\nAre you sure you want to delete all nodes "
                       "from the workflow?")))
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        refuse(tr("%1 could not be read: %2")
                   .arg(QFileInfo(path).fileName(), file.errorString()));
        return false;
    }
    QJsonParseError parse{};
    const QJsonDocument json =
        QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject()) {
        refuse(tr("%1 is not a valid workflow file: %2")
                   .arg(QFileInfo(path).fileName(),
                        parse.error != QJsonParseError::NoError
                            ? parse.errorString()
                            : tr("the top level is not an object")));
        return false;
    }

    // Loaded into a SCRATCH canvas first, so a document that turns out to be
    // malformed half way through does not leave the user with a pipeline that
    // is neither the old one nor the new one. Only a complete load replaces
    // what is here.
    OrchestrationWindow scratch(materials_, pythonResolver_);
    QString error;
    if (!OrchestrationDocument::load(scratch, json.object(), &error)) {
        refuse(tr("%1 could not be opened.\n\n%2")
                   .arg(QFileInfo(path).fileName(), error));
        return false;
    }

    clearGraph();
    if (!OrchestrationDocument::load(*this, json.object(), &error)) {
        // Unreachable in practice — the scratch load just succeeded on the
        // same bytes — but leaving the canvas half-built would be the one
        // outcome worse than refusing, so it is still reported.
        clearGraph();
        refuse(tr("%1 could not be opened.\n\n%2")
                   .arg(QFileInfo(path).fileName(), error));
        return false;
    }
    fitToScreen();
    return true;
}

void OrchestrationWindow::openWorkflow()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Workflow"), QString(),
        tr("Calango workflow (*.json);;All files (*)"));
    if (!path.isEmpty())
        openWorkflow(path);
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
    if (!node)
        return;
    node->setConfiguration(script, python, runCommand, engine);
    invalidateFrom(node);
}

void OrchestrationWindow::setNodeBatchItems(
    OrchestrationNodeItem* node,
    const QList<OrchestrationNodeItem::BatchItem>& items)
{
    if (!node)
        return;
    node->setBatchItems(items);
    invalidateFrom(node);
    updateRunControls();
}

void OrchestrationWindow::setNodeSupercell(OrchestrationNodeItem* node,
                                           const SupercellSpec& spec)
{
    if (!node)
        return;
    node->setSupercell(spec);
    invalidateFrom(node);
}

void OrchestrationWindow::setNodeTdbGenerator(OrchestrationNodeItem* node,
                                              const TdbGeneratorSpec& spec)
{
    if (!node)
        return;
    node->setTdbGenerator(spec);
    invalidateFrom(node);
}

void OrchestrationWindow::setNodeDefectSpec(OrchestrationNodeItem* node,
                                            const DefectSpec& spec)
{
    if (!node)
        return;
    node->setDefectSpec(spec);
    invalidateFrom(node);
}

void OrchestrationWindow::invalidateFrom(OrchestrationNodeItem* node)
{
    // Only a node that already RAN can be invalidated; a Pending one has
    // nothing stale about it, and re-configuring during a run must not
    // rewrite the status of the node currently executing.
    if (!node || node == runningNode_)
        return;
    if (node->status() == OrchestrationNodeItem::Status::Pending)
        return;
    node->setStatus(OrchestrationNodeItem::Status::Pending);
    updateProcessPanel(node);
    // Its outputs are what every descendant consumed, so they are stale too.
    // The DIRECTORIES are left alone — invalidating a result is not the same
    // as deleting it, and jobHistory() must keep pointing at what was
    // produced, which is exactly the artifact a user compares against after
    // fixing a parameter.
    for (OrchestrationEdgeItem* edge : edges_)
        if (edge->from() == node)
            invalidateFrom(edge->to());
    updateRunControls();
}

void OrchestrationWindow::setWizardFactory(WizardFactory factory)
{
    wizardFactory_ = std::move(factory);
}

void OrchestrationWindow::setRefusalHandler(RefusalHandler handler)
{
    refusalHandler_ = std::move(handler);
}

void OrchestrationWindow::setConfirmHandler(ConfirmHandler handler)
{
    confirmHandler_ = std::move(handler);
}

void OrchestrationWindow::refuse(const QString& message)
{
    if (refusalHandler_)
        refusalHandler_(message);
    else
        QMessageBox::warning(this, tr("Orchestration"), message);
}

bool OrchestrationWindow::confirm(const QString& question)
{
    if (confirmHandler_)
        return confirmHandler_(question);
    // A warning box rather than a question box, defaulting to No: this is one
    // click standing between the user and several minutes of wiring that the
    // undo stack does not cover.
    return QMessageBox::warning(this, tr("Clear Orchestration"), question,
                                QMessageBox::Yes | QMessageBox::No,
                                QMessageBox::No)
        == QMessageBox::Yes;
}

QList<QPair<OrchestrationInputSlot, OrchestrationNodeItem*>>
OrchestrationWindow::resolveInputs(OrchestrationNodeItem* node) const
{
    // Slot i is filled by the i-th parent IN LINK ORDER. parentsOf() walks
    // edges_ in insertion order, so "the first link you drew" and "the first
    // slot" are the same thing -- which is the rule the node paints and the
    // documentation states.
    QList<QPair<OrchestrationInputSlot, OrchestrationNodeItem*>> resolved;
    const QList<OrchestrationNodeItem*> parents = parentsOf(node);
    int index = 0;
    for (const OrchestrationInputSlot& slot : orchestrationInputSlots(node->task())) {
        resolved.append({slot, index < parents.size() ? parents[index] : nullptr});
        ++index;
    }
    return resolved;
}

void OrchestrationWindow::refreshInputSummaries()
{
    for (OrchestrationNodeItem* node : nodes_) {
        std::vector<OrchestrationNodeItem::InputLine> lines;
        for (const auto& [slot, parent] : resolveInputs(node)) {
            if (!parent && slot.optional)
                continue; // an unfilled optional slot is not worth a line
            lines.push_back(
                {parent ? tr("%1 ← %2").arg(slot.label, parent->title())
                        : tr("%1 ← not connected").arg(slot.label),
                 parent != nullptr});
        }
        node->setInputSummary(lines);
    }
}

void OrchestrationWindow::openNodeWizard(OrchestrationNodeItem* node)
{
    if (!node)
        return;

    // The transforms configure themselves: no engine, no convergence, no
    // script to generate, and therefore nothing for the host's wizard
    // catalogue to build.
    if (orchestrationTaskFamily(node->task()) == OrchestrationFamily::Transform) {
        switch (node->task()) {
        case OrchestrationTask::Container: {
            if (materialsProvider_)
                materials_ = materialsProvider_();
            QList<OrchestrationNodeItem::BatchItem> items = node->batchItems();
            if (editContainer(this, materials_, databaseImporter_, &items))
                setNodeBatchItems(node, items);
            break;
        }
        case OrchestrationTask::Supercell: {
            SupercellSpec spec = node->supercell();
            if (editSupercell(this, &spec))
                setNodeSupercell(node, spec);
            break;
        }
        case OrchestrationTask::TdbGenerator: {
            // Explicit, and it must stay above the default: the default arm is
            // the DEFECT editor, so a transform that forgets its case here
            // silently opens the wrong dialog and writes a defect recipe into
            // a node that will never read one.
            TdbGeneratorSpec spec = node->tdbGenerator();
            if (editTdbGenerator(this, &spec))
                setNodeTdbGenerator(node, spec);
            break;
        }
        default: {
            DefectSpec spec = node->defectSpec();
            if (editDefects(this, &spec))
                setNodeDefectSpec(node, spec);
            break;
        }
        }
        return;
    }

    if (!wizardFactory_) {
        // Only reachable with no catalogue installed -- the headless tests,
        // which drive configureNode() directly. Saying so beats a double-click
        // that appears to do nothing.
        QMessageBox::information(
            this, tr("Orchestration"),
            tr("No setup wizards are available in this context. Configure the "
               "node programmatically instead."));
        return;
    }

    WizardRequest request;
    request.task = node->task();
    // The structure the wizard sees is a STAND-IN, resolved upstream — the
    // node has none of its own any more. It exists so per-element controls
    // (cutoff suggestions, a k-path, an atom picker) have something to offer;
    // the script the wizard writes names `structure.extxyz`, which the runner
    // stages from whatever actually flows in.
    request.structure = representativeStructure(node);
    request.engine = node->engine();
    // The baseline paths are what the RUNNER will stage, not what exists now:
    // a node is normally configured before its parents have ever run. Labels
    // name the connected parent so the wizard's combo reads like the canvas.
    for (const auto& [slot, parent] : resolveInputs(node)) {
        request.baselines.append(
            {parent ? tr("%1 — %2").arg(slot.label, parent->title())
                    : tr("%1 — link a parent node").arg(slot.label),
             slot.stagedName});
    }

    // The node's standard setup wizard, in orchestration mode: the review
    // stage's Run button reads "Save process node" and accepting commits the
    // generated script here instead of launching anything.
    std::unique_ptr<SimulationWizardBase> wizard = wizardFactory_(request);
    if (!wizard) {
        QMessageBox::information(
            this, tr("Orchestration"),
            tr("%1 has no setup wizard on this canvas.")
                .arg(orchestrationTaskDisplayName(node->task())));
        return;
    }
    wizard->enterOrchestrationMode();
    if (request.structure) {
        wizard->setStructureElements(
            structureElements(request.structure.get()));
        const auto pbc = request.structure->cell().pbc();
        wizard->setStructurePeriodic(request.structure->cell().isDefined()
                                     && (pbc[0] || pbc[1] || pbc[2]));
    }
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
    refreshInputSummaries();
    updateRunControls();
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
    // A new link may fill a slot on `to` -- and unlinking elsewhere may have
    // emptied one, so every node is re-read rather than just this pair.
    refreshInputSummaries();
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

bool OrchestrationWindow::canResume() const
{
    if (orchestrationRoot_.isEmpty() || runningNode_)
        return false;
    return std::any_of(nodes_.begin(), nodes_.end(),
                       [](const OrchestrationNodeItem* node) {
                           return node->status()
                               != OrchestrationNodeItem::Status::Done;
                       });
}

void OrchestrationWindow::updateRunControls()
{
    if (runButton_)
        runButton_->setEnabled(runningNode_ == nullptr);
    if (resumeButton_)
        resumeButton_->setEnabled(canResume());
    if (abortButton_)
        abortButton_->setEnabled(runningNode_ != nullptr);
}

namespace {

/// How many passes this node makes the pipeline take, or 0 if it makes none.
///
/// A Structure Container is the obvious batch source: one pass per structure.
/// A Defect Generator in "one material per defect" mode is the other one, and
/// for the same reason — it produces N materials, and the pipeline computes
/// each of them. Naming the concept once is what keeps the batch plan, the
/// per-pass re-queue and the labelling from disagreeing about which nodes
/// vary.
int batchDimensionOf(const OrchestrationNodeItem* node)
{
    if (node->task() == OrchestrationTask::Container)
        return static_cast<int>(node->batchItems().size());
    if (node->task() == OrchestrationTask::DefectGenerator)
        return node->defectSpec().mode == DefectSpec::Mode::Separate
            ? node->defectSpec().variantCount()
            : 0;
    return 0;
}

} // namespace

bool OrchestrationWindow::dependsOnContainer(OrchestrationNodeItem* node) const
{
    // Reverse reachability: a node's result depends on which Container item is
    // being processed exactly when a Container is upstream of it. Everything
    // else computed something that does not change between items, and
    // re-running it once per structure would be pure waste.
    std::vector<OrchestrationNodeItem*> stack{node};
    std::vector<OrchestrationNodeItem*> seen;
    while (!stack.empty()) {
        OrchestrationNodeItem* current = stack.back();
        stack.pop_back();
        if (std::find(seen.begin(), seen.end(), current) != seen.end())
            continue;
        seen.push_back(current);
        if (batchDimensionOf(current) > 0)
            return true;
        for (OrchestrationEdgeItem* edge : edges_)
            if (edge->to() == current)
                stack.push_back(edge->from());
    }
    return false;
}

int OrchestrationWindow::containerBatchIndex() const
{
    // The slow digit of the odometer: the container advances once the defect
    // set has been walked through.
    return batchDefectSpan_ > 0 ? batchIndex_ / batchDefectSpan_ : batchIndex_;
}

int OrchestrationWindow::defectBatchIndex() const
{
    return batchDefectSpan_ > 0 ? batchIndex_ % batchDefectSpan_ : 0;
}

QString OrchestrationWindow::tabTitleFor(const OrchestrationNodeItem* node) const
{
    // "Si · Relax", "Si / remove 0 · Defect" — the material first because that
    // is what distinguishes one tab from its neighbours in a batch, then the
    // short task name. Capped so a long container label cannot push the task
    // out of the visible part of the tab.
    constexpr int kMaxLabel = 22;
    QString label = batchLabel();
    if (label.isEmpty())
        label = node->materialName();
    if (label.size() > kMaxLabel)
        label = label.left(kMaxLabel - 1) + QChar(0x2026);
    const QString task = orchestrationTaskShortName(node->task());
    return label.isEmpty() ? task
                           : QStringLiteral("%1 · %2").arg(label, task);
}

QString OrchestrationWindow::batchLabel() const
{
    QStringList parts;
    for (const OrchestrationNodeItem* node : nodes_) {
        if (node->task() != OrchestrationTask::Container
            || node->batchItems().isEmpty())
            continue;
        const int index = std::min(containerBatchIndex(),
                                   static_cast<int>(node->batchItems().size()) - 1);
        parts << node->batchItems()[index].first;
        break;
    }
    // Both dimensions name the pass when both are present: a run folder saying
    // only "Si" for three different defects is three folders you cannot tell
    // apart.
    for (const OrchestrationNodeItem* node : nodes_) {
        if (node->task() != OrchestrationTask::DefectGenerator
            || batchDimensionOf(node) <= 0)
            continue;
        const auto& operations = node->defectSpec().operations;
        const int index =
            std::min(defectBatchIndex(), static_cast<int>(operations.size()) - 1);
        if (index >= 0)
            parts << operations[index].describe();
        break;
    }
    return parts.join(QStringLiteral(" / "));
}

void OrchestrationWindow::enqueue(OrchestrationNodeItem* node)
{
    node->setStatus(OrchestrationNodeItem::Status::Waiting);
    node->setJobDirectory(QString());
    const QString label = batchLabel();
    node->setProcessTaskId(
        processPanel_
            ? processPanel_->registerTask(
                  tr("Orchestration: %1 (%2)")
                      .arg(node->title(),
                           label.isEmpty() ? node->materialName() : label),
                  QString())
            : -1);
    updateProcessPanel(node);
}

QString OrchestrationWindow::makeJobDirectory(OrchestrationNodeItem* node)
{
    QString parent = orchestrationRoot_;
    if (batchLength_ > 1) {
        // One folder per Container item, so a batch of twelve alloys reads as
        // twelve labelled studies rather than one folder of sixty runs whose
        // only distinguishing feature is a counter.
        QString label = batchLabel();
        label.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")),
                      QStringLiteral("_"));
        parent += QStringLiteral("/batch_%1_%2").arg(batchIndex_ + 1).arg(label);
    }
    // launchedCount_ never resets within an orchestration, so a retry after a
    // Resume gets its own directory and the failed attempt's files stay where
    // the provenance record says they are.
    const QString dir = parent
        + QStringLiteral("/node_%1_%2")
              .arg(++launchedCount_)
              .arg(orchestrationTaskSlug(node->task()));
    return QDir().mkpath(dir) ? dir : QString();
}

ProvenanceRecord
OrchestrationWindow::beginProvenance(OrchestrationNodeItem* node,
                                     const QString& dir) const
{
    ProvenanceRecord record;
    record.nodeId = node->id();
    record.task = orchestrationTaskSlug(node->task());
    record.title = node->title();
    record.material = node->materialName();
    record.directory = dir;
    if (orchestrationTaskFamily(node->task()) != OrchestrationFamily::Transform)
        record.engine = EnginePresets::displayName(node->engine());
    record.configured = node->isConfigured();
    if (node->isConfigured())
        record.scriptSha256 = QString::fromLatin1(
            QCryptographicHash::hash(node->configuredScript().toUtf8(),
                                     QCryptographicHash::Sha256)
                .toHex());
    record.runCommand = node->configuredRunCommand();
    record.python = node->configuredPython();
    switch (node->task()) {
    case OrchestrationTask::Container: {
        QStringList names;
        for (const auto& [name, structure] : node->batchItems())
            names << name;
        record.parameters = names.join(QStringLiteral(", "));
        break;
    }
    case OrchestrationTask::Supercell:
        record.parameters = node->supercell().describe();
        break;
    case OrchestrationTask::DefectGenerator:
        record.parameters = node->defectSpec().describe();
        break;
    case OrchestrationTask::TdbGenerator:
        record.parameters = node->tdbGenerator().describe();
        break;
    default:
        break;
    }
    record.batchIndex = batchIndex_;
    record.batchTotal = batchLength_;
    record.batchLabel = batchLabel();
    record.attempt = std::max(1, node->attempts());

    // Logical provenance: which parent fills which named input, in link order.
    // Recorded even for the tasks with no slot table, where the single parent
    // supplies the input geometry.
    const auto inputs = resolveInputs(node);
    const QList<OrchestrationNodeItem*> parents = parentsOf(node);
    for (int index = 0; index < parents.size(); ++index) {
        const QString role = index < inputs.size()
            ? inputs[index].first.label
            : tr("input structure");
        record.parents.append(
            {parents[index]->id(),
             tr("%1 ← %2").arg(role, parents[index]->title())});
    }
    record.status = QStringLiteral("running");
    record.startedUtc = utcNow();
    return record;
}

void OrchestrationWindow::finishProvenance(OrchestrationNodeItem* node,
                                           ProvenanceRecord record,
                                           int exitCode,
                                           const QStringList& excluded)
{
    record.exitCode = exitCode;
    record.status = statusSlug(node->status());
    record.finishedUtc = utcNow();
    record.outputs = describeOutputs(record.directory, excluded);
    writeProvenance(record);
}

void OrchestrationWindow::writeManifest() const
{
    if (orchestrationRoot_.isEmpty())
        return;
    QJsonArray nodeArray;
    for (const OrchestrationNodeItem* node : nodes_) {
        QJsonArray history;
        for (const QString& directory : node->jobHistory())
            history.append(directory);
        nodeArray.append(QJsonObject{
            {QStringLiteral("id"), node->id()},
            {QStringLiteral("task"), orchestrationTaskSlug(node->task())},
            {QStringLiteral("title"), node->title()},
            {QStringLiteral("material"), node->materialName()},
            {QStringLiteral("status"), statusSlug(node->status())},
            {QStringLiteral("configured"), node->isConfigured()},
            {QStringLiteral("directory"), node->jobDirectory()},
            {QStringLiteral("attempts"), node->attempts()},
            {QStringLiteral("directories"), history},
        });
    }
    QJsonArray edgeArray;
    for (const OrchestrationEdgeItem* edge : edges_)
        edgeArray.append(QJsonObject{{QStringLiteral("from"), edge->from()->id()},
                                     {QStringLiteral("to"), edge->to()->id()}});
    const QJsonObject root{
        {QStringLiteral("schema"),
         QStringLiteral("calango.orchestration.manifest/1")},
        {QStringLiteral("started_utc"), runStartedUtc_},
        {QStringLiteral("batch"),
         QJsonObject{{QStringLiteral("index"), batchIndex_},
                     {QStringLiteral("total"), batchLength_},
                     {QStringLiteral("label"), batchLabel()}}},
        {QStringLiteral("nodes"), nodeArray},
        {QStringLiteral("edges"), edgeArray},
    };
    QFile file(orchestrationRoot_ + QStringLiteral("/orchestration.json"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
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

    // --- Batch plan --------------------------------------------------------
    // Every Container must hold the same number of structures, because the
    // pipeline makes ONE pass per item and a pass has to give each container
    // an item. Taking a maximum and clamping would quietly re-use the last
    // structure of the shorter list, which is a study nobody asked for.
    batchIndex_ = 0;
    batchLength_ = 1;
    // Two INDEPENDENT dimensions, and they multiply.
    //
    // Containers are one dimension: several of them supply one structure each
    // per pass, so they have to agree on how many passes there are — a pass
    // that could give one container an item and not another is not a pass.
    //
    // Separate-mode Defect Generators are the other, and they are not the same
    // kind of thing. A Defect Generator is downstream of the geometry, not a
    // source of it: it MULTIPLIES whatever reaches it. One container holding a
    // single structure feeding a generator that makes three defective versions
    // of it is the ordinary case, and requiring the two to agree would refuse
    // exactly the pipeline the feature exists for.
    batchDefectSpan_ = 1;
    int containerSpan = 1;
    const OrchestrationNodeItem* containerRef = nullptr;
    const OrchestrationNodeItem* defectRef = nullptr;
    for (OrchestrationNodeItem* node : nodes_) {
        if (node->task() == OrchestrationTask::Container
            && node->batchItems().isEmpty()) {
            refuse(node->configurationProblem());
            return;
        }
        const int count = batchDimensionOf(node);
        if (count == 0)
            continue;
        const bool isContainer = node->task() == OrchestrationTask::Container;
        const OrchestrationNodeItem*& reference =
            isContainer ? containerRef : defectRef;
        int& span = isContainer ? containerSpan : batchDefectSpan_;
        if (!reference) {
            reference = node;
            span = count;
        } else if (count != span) {
            refuse(isContainer
                       ? tr("%1 holds %2 structures but %3 holds %4.\n\n"
                            "The pipeline makes one pass per structure, so "
                            "every container has to supply one per pass. Give "
                            "them the same length, or put them in separate "
                            "pipelines.")
                             .arg(reference->title())
                             .arg(span)
                             .arg(node->title())
                             .arg(count)
                       : tr("%1 makes %2 materials but %3 makes %4.\n\n"
                            "Two defect generators in one pipeline step "
                            "through their defects together, so they have to "
                            "make the same number. Give them the same number "
                            "of operations, or chain them instead of running "
                            "them side by side.")
                             .arg(reference->title())
                             .arg(span)
                             .arg(node->title())
                             .arg(count));
            return;
        }
    }
    batchLength_ = containerSpan * batchDefectSpan_;

    // Sending queues EVERY node: each shows as "waiting" until its turn, so
    // the canvas reads as a process queue from the moment of submission. A
    // fresh send resets previous results — they are superseded, not
    // silently reused. Each node also registers as a row in the global
    // Processes panel (Queued), so the dispatch is visible and reloadable
    // where every other run lives, not only on this canvas.
    for (OrchestrationNodeItem* node : nodes_) {
        node->clearJobHistory();
        enqueue(node);
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
    runStartedUtc_ = utcNow();
    // A fresh send is a fresh report. Resume deliberately does NOT reset it:
    // the point of resuming is that the earlier passes still happened, and a
    // report that forgot them would describe a run nobody made.
    report_ = core::WorkflowReport();
    report_.startedUtc = runStartedUtc_;
    report_.root = orchestrationRoot_;
    // The pipeline itself, beside its results. Written on every send so the
    // folder is enough to reproduce the run — including on a cluster, where
    // `calango-cli run workflow.json` reads exactly this file. Failures here
    // are not fatal: the science still runs.
    {
        QStringList warnings;
        OrchestrationDocument::write(
            OrchestrationDocument::build(*this, &warnings),
            orchestrationRoot_ + QStringLiteral("/workflow.json"), nullptr);
    }

    updateRunControls();
    if (!nextRunnable()) {
        // A box, not a toolbar caption. The user just pressed Run and NOTHING
        // happened; a line of grey text beside the button is exactly the way
        // to have that read as the button being broken.
        refuse(
            tr("Nothing could start — check the first node.\n\n"
               "Every node either has no parent (and runs on its assigned "
               "material) or inherits its parent's results. A node whose "
               "parent produced no usable geometry is refused rather than "
               "run on the wrong structure."));
        updateRunControls();
        return;
    }
    writeManifest();
    pump();
}

void OrchestrationWindow::resumeFromFailure()
{
    if (runningNode_)
        return;
    if (orchestrationRoot_.isEmpty()) {
        refuse(tr("There is nothing to resume — this pipeline has not been "
                  "run yet. Use Send to Processes."));
        return;
    }
    if (!canResume()) {
        refuse(tr("Every node finished successfully; there is nothing to "
                  "resume. Use Send to Processes to run the pipeline again."));
        return;
    }

    // The whole point: a node that is Done keeps its status, its directory and
    // its artifacts, and is not re-run. Everything else — failed, skipped
    // because an ancestor failed, or invalidated by a configuration change —
    // goes back in the queue.
    for (OrchestrationNodeItem* node : nodes_)
        if (node->status() != OrchestrationNodeItem::Status::Done)
            enqueue(node);

    if (!nextRunnable()) {
        refuse(tr("Nothing can be resumed: every node still to run depends on "
                  "one that has not finished.\n\n"
                  "Check the links into the failed node — a parent that never "
                  "produced a result cannot feed anything."));
        updateRunControls();
        return;
    }
    updateRunControls();
    writeManifest();
    pump();
}

void OrchestrationWindow::abortOrchestration()
{
    if (!runningNode_)
        return; // the button is disabled, but the slot is public API too
    if (!confirm(tr("Abort the running orchestration?\n\n"
                    "\"%1\" is stopped and marked failed, and the nodes still "
                    "queued are left unrun. Everything that already finished "
                    "keeps its results — Resume continues from there.")
                     .arg(runningNode_->title())))
        return;

    aborting_ = true;
    if (jobRunner_->isRunning()) {
        // The kill is asynchronous: terminate() sends SIGTERM and escalates to
        // SIGKILL after three seconds. The pipeline is unwound in
        // onJobFinished, when the process is actually gone — tearing the state
        // down here would race the job runner still writing into the
        // directory it is being evicted from.
        jobRunner_->terminate();
        return;
    }
    // No live process to kill: the job ended between the last event and the
    // click. Unwind directly, or runningNode_ stays set forever and the panel
    // is stuck showing a run that is not happening.
    onJobFinished(-1, true);
}

void OrchestrationWindow::pump()
{
    // One loop rather than one call per completion: the transform nodes finish
    // INSIDE startNode() and never reach the job runner's finished signal, so
    // a Container feeding a Supercell feeding a Defect Generator has to be
    // able to advance three times without an external event in between.
    while (!runningNode_) {
        OrchestrationNodeItem* next = nextRunnable();
        if (!next) {
            if (advanceBatch())
                continue;
            writeManifest();
            // Every pass made and nothing left runnable: this is the end of
            // the run, and the only moment at which the whole of what happened
            // is known.
            finishRun(/*completed=*/true);
            break;
        }
        if (startNode(next))
            continue;
        // Refused or failed to stage. startNode has already said why; the
        // canvas records the verdict and the pipeline carries on with
        // whatever branch does not depend on this node.
        next->setStatus(OrchestrationNodeItem::Status::Failed);
        updateProcessPanel(next);
        // startNode() records the transforms it refuses itself, with the
        // reason; anything reaching here failed to stage or was refused before
        // a directory existed.
        if (orchestrationTaskFamily(next->task()) != OrchestrationFamily::Transform)
            recordOutcome(next, tr("could not be started"));
        skipDescendants(next);
        writeManifest();
    }
    updateRunControls();
}

bool OrchestrationWindow::advanceBatch()
{
    if (batchIndex_ + 1 >= batchLength_)
        return false;
    ++batchIndex_;
    bool queued = false;
    for (OrchestrationNodeItem* node : nodes_) {
        if (!dependsOnContainer(node))
            continue; // its result does not vary with the batch item
        enqueue(node);
        queued = true;
    }
    return queued;
}

bool OrchestrationWindow::runTransform(OrchestrationNodeItem* node,
                                       const QString& dir,
                                       ProvenanceRecord& record,
                                       QString* error,
                                       std::shared_ptr<const core::Structure>* produced)
{
    const QString input = dir + QStringLiteral("/structure.extxyz");
    const QString output = dir + QStringLiteral("/transformed.extxyz");
    core::Structure result;

    if (node->task() == OrchestrationTask::TdbGenerator) {
        // Returns BEFORE the structure read below, and that is the whole
        // reason it is first: this node's input is a results file staged into
        // its slot, and there may be no geometry in the directory at all. It
        // also writes no transformed.extxyz — it produces a database, not a
        // structure, so `produced` stays null and no workspace tab is claimed.
        const QString ensemble = dir + QStringLiteral("/cluster_expansion.json");
        QFile file(ensemble);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            *error = tr("its formation-energy ensemble (%1) could not be read")
                         .arg(QStringLiteral("cluster_expansion.json"));
            return false;
        }
        TdbGeneratorOutput assessed;
        QString problem;
        if (!runTdbAssessment(QString::fromUtf8(file.readAll()),
                              node->tdbGenerator(), &assessed, &problem)) {
            *error = problem;
            return false;
        }
        const auto write = [&dir](const QString& name, const QString& text) {
            QFile out(dir + QLatin1Char('/') + name);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
                return false;
            QTextStream(&out) << text;
            return true;
        };
        if (!write(QStringLiteral("assessment.tdb"), assessed.databaseText)
            || !write(QStringLiteral("calphad_assessment.json"),
                      assessed.summaryJson)) {
            *error = tr("its database could not be written into %1").arg(dir);
            return false;
        }
        // The headline carries the RMS residual, which the .tdb cannot: a
        // badly fitted database is still a valid one, so the quality of the
        // fit has to be recorded where the run is recorded.
        record.parameters = assessed.headline;
        return true;
    }

    if (node->task() == OrchestrationTask::Container) {
        // A source, not an edit: it ignores whatever reached it and emits the
        // batch item for this pass.
        const auto& items = node->batchItems();
        const int index =
            std::min(containerBatchIndex(), static_cast<int>(items.size()) - 1);
        if (index < 0 || !items[index].second) {
            *error = tr("its structure list is empty");
            return false;
        }
        result = *items[index].second;
        record.parameters = items[index].first;
    } else {
        // Everything else edits the structure that reached this node — staged
        // by the ordinary geometry handoff, or written from the node's own
        // material when it has no parent.
        if (!QFile::exists(input)) {
            *error = tr("no input structure reached it");
            return false;
        }
        core::Structure incoming;
        try {
            incoming = pybridge::AseBridge::readStructure(input.toStdString());
        } catch (const std::exception& e) {
            *error = tr("its input structure could not be read (%1)")
                         .arg(QString::fromUtf8(e.what()));
            return false;
        }
        QString problem;
        if (node->task() == OrchestrationTask::Supercell) {
            result = applySupercell(incoming, node->supercell(), &problem);
        } else {
            // Every variant is built, even in separate mode where only one of
            // them is this pass's output: building the whole set is what
            // catches a recipe whose FOURTH operation is impossible on pass
            // one, instead of three passes into a study.
            const QList<DefectVariant> variants =
                applyDefectSet(incoming, node->defectSpec(), &problem);
            if (problem.isEmpty()) {
                const int index =
                    node->defectSpec().mode == DefectSpec::Mode::Separate
                    ? std::min(defectBatchIndex(),
                               static_cast<int>(variants.size()) - 1)
                    : 0;
                result = variants[static_cast<qsizetype>(std::max(0, index))]
                             .structure;
                record.parameters = variants[static_cast<qsizetype>(
                                                 std::max(0, index))]
                                        .label;
            }
        }
        if (!problem.isEmpty()) {
            *error = problem;
            return false;
        }
    }

    try {
        pybridge::AseBridge::writeStructure(result, output.toStdString());
    } catch (const std::exception& e) {
        *error = tr("its result could not be written (%1)")
                     .arg(QString::fromUtf8(e.what()));
        return false;
    }
    if (produced)
        *produced = std::make_shared<const core::Structure>(std::move(result));
    return true;
}

bool OrchestrationWindow::startNode(OrchestrationNodeItem* node)
{
    // --- Refusals that must happen BEFORE a directory is made --------------
    // An analysis module has no defaults to fall back on: its script names a
    // baseline, and no baseline path can be guessed. Running one unconfigured
    // would not produce an approximate answer, it would produce a crash at
    // best and a run against the wrong file at worst. The transforms have
    // their own version of the same question — an empty container, an empty
    // defect recipe — and configurationProblem() answers all of them.
    if (const QString problem = node->configurationProblem();
        !problem.isEmpty()) {
        refuse(problem);
        return false;
    }

    const QList<OrchestrationNodeItem*> parents = parentsOf(node);
    const auto inputs = resolveInputs(node);
    const int required = orchestrationRequiredInputs(node->task());
    if (parents.size() < required) {
        QStringList wanted;
        for (const auto& [slot, parent] : inputs)
            if (!slot.optional)
                wanted << slot.label;
        refuse(
            tr("%1 was not started: it inherits %n completed run(s) (%2) but "
               "only %3 parent node(s) are linked to it.\n\n"
               "Link one parent per input, in that order — the first link "
               "fills the first input.",
               nullptr, required)
                .arg(node->title(), wanted.join(tr(", ")))
                .arg(parents.size()));
        return false;
    }

    const QString dir = makeJobDirectory(node);
    if (dir.isEmpty())
        return false;
    // Recorded before anything is staged, so a node that dies during staging
    // still leaves a findable directory holding the record of what went wrong.
    node->recordJobDirectory(dir);
    ProvenanceRecord record = beginProvenance(node, dir);
    // The files the CANVAS put in the directory. Everything else there when
    // the node finishes is an output.
    QStringList staged{QStringLiteral("run.py"),
                       QStringLiteral("provenance.json")};

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
    //
    // A Container is the exception: it is a SOURCE. It emits the batch item
    // for this pass and has nothing to inherit, so it skips staging entirely
    // even if somebody linked a parent into it.
    if (node->task() == OrchestrationTask::Container) {
        // nothing to stage
    } else if (!parents.isEmpty()) {
        const QString parentDir = parents.front()->jobDirectory();
        QString source;
        for (const char* candidate :
             {"transformed.extxyz", "optimized.extxyz", "md_final.extxyz",
              "single_point.extxyz", "structure.extxyz"}) {
            const QString path =
                parentDir + QLatin1Char('/') + QLatin1String(candidate);
            if (QFile::exists(path)) {
                source = path;
                break;
            }
        }
        // A baseline-inheriting module restarts from the .gpw, which carries
        // its own atoms — so the geometry is staged when there is one but not
        // insisted on. For the self-contained tasks it IS the input, and its
        // absence is the strict-handoff refusal below.
        const bool needsGeometry = inputs.isEmpty();
        if (!source.isEmpty()
            && QFile::copy(source, dir + QStringLiteral("/structure.extxyz"))) {
            staged << QStringLiteral("structure.extxyz");
            record.inputs.append(describeFile(
                dir, QStringLiteral("structure.extxyz"),
                tr("input structure"), source, parents.front()->id()));
        }
        if (needsGeometry
            && !QFile::exists(dir + QStringLiteral("/structure.extxyz"))) {
            // The strict-handoff refusal. This is the message that explains
            // why a pipeline stopped rather than silently computing the wrong
            // thing on an un-relaxed structure, so it must be impossible to
            // miss — it was the one thing the removed status label carried
            // that nothing else reports.
            refuse(
                tr("%1 was not started: no usable geometry in its parent's "
                   "results (%2).\n\n"
                   "A node with a parent inherits that parent's output "
                   "structure. Running it on its own original material "
                   "instead would quietly compute the wrong thing, so it is "
                   "refused.")
                    .arg(node->title(), parentDir));
            return false;
        }
        // Only for the self-contained tasks: a baseline-inheriting node names
        // its own staged slot below, and a .gpw is far too large to copy twice
        // so that one of the copies can go unread.
        if (inputs.isEmpty()) {
            const QString gpw =
                parentDir + QStringLiteral("/single_point.gpw");
            if (QFile::exists(gpw)
                && QFile::copy(gpw,
                               dir + QStringLiteral("/single_point.gpw"))) {
                staged << QStringLiteral("single_point.gpw");
                record.inputs.append(describeFile(
                    dir, QStringLiteral("single_point.gpw"),
                    tr("ground state"), gpw, parents.front()->id()));
            }
        }

        // --- Inherited runs, one staged file per input slot ----------------
        // Each slot lands under the name its wizard was configured against.
        // A missing artifact is refused rather than skipped: the script names
        // that path unconditionally, so letting it through only moves the
        // failure into Python, where it reads as a bug in the module.
        for (const auto& [slot, parent] : inputs) {
            if (!parent) {
                if (slot.optional)
                    continue;
                return false; // already refused above; belt and braces
            }
            const QString target = dir + QLatin1Char('/') + slot.stagedName;
            const QString origin = slot.sourceName.isEmpty()
                ? parent->jobDirectory()
                : parent->jobDirectory() + QLatin1Char('/') + slot.sourceName;
            const bool ok = slot.sourceName.isEmpty()
                ? copyDirectory(parent->jobDirectory(), target)
                : QFile::copy(origin, target);
            if (ok) {
                staged << slot.stagedName;
                record.inputs.append(describeFile(dir, slot.stagedName,
                                                  slot.label, origin,
                                                  parent->id()));
                continue;
            }
            // Note this refuses even for an OPTIONAL slot. Optional governs
            // whether an UNLINKED slot blocks the run; a link the user drew on
            // purpose, to a parent that turns out to hold nothing, is a
            // mis-wired graph either way -- and the wizard has already been
            // configured against the file that is missing.
            refuse(
                tr("%1 was not started: its \"%2\" input expects %3 in the "
                   "results of %4, and there is none.\n\n"
                   "That process did not save what this one reads — a "
                   "Single-Point Calculation has to save its wavefunctions "
                   "(.gpw) for anything downstream to restart from it.")
                    .arg(node->title(), slot.label,
                         slot.sourceName.isEmpty()
                             ? tr("its results directory")
                             : slot.sourceName,
                         parent->title()));
            return false;
        }
    } else {
        // No parent and no structure of its own: nothing feeds this node.
        // Since structures enter a pipeline through a Structure Container,
        // that is what the message names — the alternative, inventing a
        // geometry, is the failure mode this whole panel refuses.
        if (!node->structure()) {
            refuse(tr("%1 was not started: nothing feeds it a structure.\n\n"
                      "A process node takes its geometry from its input port. "
                      "Add a Structure Container, put the structures to study "
                      "in it, and link it to this node.")
                       .arg(node->title()));
            return false;
        }
        try {
            pybridge::AseBridge::writeStructure(
                *node->structure(),
                (dir + QStringLiteral("/structure.extxyz")).toStdString());
        } catch (const std::exception&) {
            return false;
        }
        staged << QStringLiteral("structure.extxyz");
        record.inputs.append(describeFile(dir,
                                          QStringLiteral("structure.extxyz"),
                                          tr("assigned material")));
    }

    // --- Transforms: performed here, not launched --------------------------
    // A structure edit is a few hundred microseconds of array work. Spawning
    // an interpreter for it would spend a thousand times longer starting up
    // than working, and would give the node a calculator and a launch command
    // it has no use for.
    if (orchestrationTaskFamily(node->task()) == OrchestrationFamily::Transform) {
        QString problem;
        std::shared_ptr<const core::Structure> produced;
        if (!runTransform(node, dir, record, &problem, &produced)) {
            node->setStatus(OrchestrationNodeItem::Status::Failed);
            finishProvenance(node, record, -1, staged);
            recordOutcome(node, problem);
            refuse(tr("%1 was not run: %2.").arg(node->title(), problem));
            return false;
        }
        node->setStatus(OrchestrationNodeItem::Status::Done);
        updateProcessPanel(node);
        recordOutcome(node);
        finishProvenance(node, record, 0, staged);
        writeManifest();
        // Publish the structure so the host can show it. A transform runs to
        // completion inside this call and emits none of the job signals, so
        // without this its result exists only as a file nobody was told about.
        if (produced) {
            const QString label = tabTitleFor(node);
            // A separate-mode Defect Generator makes a different material each
            // pass, so each gets its own tab; everything else is one material
            // and reuses tab 0.
            const bool perPassMaterial =
                node->task() == OrchestrationTask::DefectGenerator
                && node->defectSpec().mode == DefectSpec::Mode::Separate;
            Q_EMIT nodeStructureProduced(node->id(),
                                         perPassMaterial ? defectBatchIndex() : 0,
                                         label, produced);
        }
        return true;
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
        // Elements come from the geometry that was just STAGED, not from the
        // node: a node created on the canvas owns no structure, and the
        // per-element cutoff/k-grid suggestion has to describe what will
        // actually be computed. Falling back to the node's own structure
        // keeps the scripted/test path (a node seeded with a material)
        // working unchanged.
        QStringList elements;
        try {
            const core::Structure staged = pybridge::AseBridge::readStructure(
                (dir + QStringLiteral("/structure.extxyz")).toStdString());
            elements = structureElements(&staged);
        } catch (const std::exception&) {
            elements = structureElements(node->structure().get());
        }
        const CalculatorParameters::Suggestion suggestion =
            CalculatorParameters::suggestionFor(node->engine(), elements);
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
    updateProcessPanel(node);
    runningNode_ = node;
    // The record is carried across the launch: the start timestamp and the
    // staged-input list exist only here, and onJobFinished has to close the
    // same record it opened rather than reconstruct one.
    runningRecord_ = record;
    runningStagedFiles_ = staged;
    writeManifest();
    jobRunner_->start(resolved.commandLine, context.pythonExecutable, dir,
                      resolved.environment);
    if (node->processTaskId() >= 0) {
        // The same label the Processes row carries, so a batched pass reads
        // as its own structure in Results rather than as three runs sharing
        // the node's originally assigned material.
        const QString label = batchLabel();
        Q_EMIT nodeStarted(node->processTaskId(),
                           tr("Orchestration: %1 (%2)")
                               .arg(node->title(),
                                    label.isEmpty() ? node->materialName()
                                                    : label),
                           tabTitleFor(node), dir);
    }
    return true;
}

void OrchestrationWindow::recordOutcome(OrchestrationNodeItem* node,
                                       const QString& note)
{
    core::NodeOutcome outcome;
    outcome.nodeId = node->id();
    outcome.task = orchestrationTaskSlug(node->task());
    outcome.title = node->title();
    outcome.engine = orchestrationTaskFamily(node->task())
            == OrchestrationFamily::Transform
        ? QString()
        : EnginePresets::displayName(node->engine());
    outcome.directory = node->jobDirectory();
    outcome.status = statusSlug(node->status());
    outcome.batchIndex = batchIndex_;
    outcome.batchLabel = batchLabel();
    outcome.attempt = node->attempts();
    outcome.note = note;
    // Only a node that actually produced artifacts has physics to extract.
    if (node->status() == OrchestrationNodeItem::Status::Done)
        outcome.metrics = core::extractReportMetrics(outcome.directory,
                                                     outcome.task);
    report_.outcomes.append(outcome);
}

void OrchestrationWindow::finishRun(bool completed)
{
    report_.finishedUtc =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    report_.completed = completed;
    report_.root = orchestrationRoot_;
    report_.batchTotal = batchLength_;
    report_.write();
    Q_EMIT runFinished(report_);
}

void OrchestrationWindow::skipDescendants(OrchestrationNodeItem* node)
{
    for (OrchestrationEdgeItem* edge : edges_) {
        if (edge->from() == node
            && edge->to()->status() == OrchestrationNodeItem::Status::Waiting) {
            edge->to()->setStatus(OrchestrationNodeItem::Status::Skipped);
            updateProcessPanel(edge->to());
            recordOutcome(edge->to(),
                          tr("an upstream node did not produce its input"));
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
    // A killed job reports a nonzero exit like any other failure, so the flag
    // is the only thing that distinguishes "this node failed" from "the user
    // stopped the run" — and the two unwind differently.
    const bool aborted = aborting_;
    aborting_ = false;

    const bool succeeded = exitCode == 0 && !crashed && !aborted;
    if (succeeded) {
        finished->setStatus(OrchestrationNodeItem::Status::Done);
    } else {
        finished->setStatus(OrchestrationNodeItem::Status::Failed);
        // Children cannot run on inputs that never materialized — say so on
        // the canvas instead of leaving them "pending" forever. Their
        // ancestors keep their Done status and their directories: that is
        // what Resume picks up from.
        //
        // Not on an abort: there the whole queue stops, not just this node's
        // branch, and the sweep below covers its descendants along with
        // everything else.
        if (!aborted)
            skipDescendants(finished);
    }
    if (aborted) {
        // Every node still queued — the aborted node's descendants and the
        // unrelated branches alike. Skipped rather than left Waiting so their
        // Processes rows resolve: a row left saying "queued" after the run has
        // stopped reads as a pipeline still going.
        for (OrchestrationNodeItem* node : nodes_) {
            if (node->status() != OrchestrationNodeItem::Status::Waiting)
                continue;
            node->setStatus(OrchestrationNodeItem::Status::Skipped);
            updateProcessPanel(node);
            recordOutcome(node, tr("the run was stopped before it started"));
        }
    }
    updateProcessPanel(finished);
    recordOutcome(finished,
                  succeeded ? QString()
                            : (aborted ? tr("stopped by the user")
                                       : tr("exit code %1").arg(exitCode)));
    finishProvenance(finished, runningRecord_, crashed ? -1 : exitCode,
                     runningStagedFiles_);
    runningRecord_ = ProvenanceRecord();
    runningStagedFiles_.clear();
    if (finished->processTaskId() >= 0)
        Q_EMIT nodeFinished(finished->processTaskId(), succeeded);

    // No summary line when the run ends: each node's own status strip says
    // done/failed on the canvas, and the Processes panel holds the same
    // per-node verdict with its directory.
    if (aborted) {
        // Emphatically NOT pump(): the ordinary response to a failed node is
        // to start the next runnable one, which here would launch a fresh job
        // seconds after the user asked the pipeline to stop.
        writeManifest();
        finishRun(/*completed=*/false);
        updateRunControls();
        return;
    }
    pump();
}

// ---------------------------------------------------------------------------
// Viewport
// ---------------------------------------------------------------------------

QRectF OrchestrationWindow::nodesBoundingRect() const
{
    QRectF box;
    for (const OrchestrationNodeItem* node : nodes_)
        box = box.united(node->sceneBoundingRect());
    return box;
}

QRectF OrchestrationWindow::visibleSceneRect() const
{
    return view_ ? view_->mapToScene(view_->viewport()->rect()).boundingRect()
                 : QRectF();
}

void OrchestrationWindow::autoLayout()
{
    if (nodes_.empty())
        return;

    // --- Layering: longest path from a source -------------------------------
    // Longest path rather than shortest, so a node sits one column to the right
    // of its LAST-finishing parent. With the shortest path a node whose two
    // parents are three columns apart would be drawn beside the near one, with
    // a link running backwards past it — and the canvas reads left to right as
    // execution order, which is the one thing its layout has to be true about.
    std::map<OrchestrationNodeItem*, int> layer;
    for (OrchestrationNodeItem* node : nodes_)
        layer[node] = 0;
    // The graph is acyclic (connectNodes refuses cycles), so relaxing |V| times
    // is enough; the loop stops as soon as nothing moves.
    for (std::size_t pass = 0; pass < nodes_.size(); ++pass) {
        bool changed = false;
        for (OrchestrationEdgeItem* edge : edges_) {
            const int wanted = layer[edge->from()] + 1;
            if (layer[edge->to()] < wanted) {
                layer[edge->to()] = wanted;
                changed = true;
            }
        }
        if (!changed)
            break;
    }

    int columns = 0;
    for (const auto& [node, index] : layer)
        columns = std::max(columns, index + 1);
    std::vector<std::vector<OrchestrationNodeItem*>> byColumn(
        static_cast<std::size_t>(columns));
    // Seeded in creation order, which is the order a pipeline was built in and
    // therefore a sensible tie-break before the crossing pass runs.
    for (OrchestrationNodeItem* node : nodes_)
        byColumn[static_cast<std::size_t>(layer[node])].push_back(node);

    // --- Ordering within a column: barycentre sweeps ------------------------
    // Each node is pulled towards the mean row of its parents, then of its
    // children. Two passes of each direction is the standard cheap
    // approximation to crossing minimisation (which is NP-hard) and is more
    // than enough for the tens of nodes a canvas holds.
    const auto meanRow = [](const QList<OrchestrationNodeItem*>& others,
                            const std::map<OrchestrationNodeItem*, int>& rows,
                            double fallback) {
        double sum = 0.0;
        int count = 0;
        for (OrchestrationNodeItem* other : others) {
            const auto it = rows.find(other);
            if (it == rows.end())
                continue;
            sum += it->second;
            ++count;
        }
        return count > 0 ? sum / count : fallback;
    };
    std::map<OrchestrationNodeItem*, int> row;
    const auto reindex = [&row, &byColumn] {
        for (auto& column : byColumn)
            for (std::size_t i = 0; i < column.size(); ++i)
                row[column[i]] = static_cast<int>(i);
    };
    reindex();
    for (int sweep = 0; sweep < 4; ++sweep) {
        const bool downward = (sweep % 2) == 0;
        for (std::size_t c = 0; c < byColumn.size(); ++c) {
            // Downward sweeps order a column by its parents (already placed),
            // upward sweeps by its children.
            const std::size_t index =
                downward ? c : byColumn.size() - 1 - c;
            std::vector<OrchestrationNodeItem*>& column = byColumn[index];
            std::map<OrchestrationNodeItem*, double> key;
            for (OrchestrationNodeItem* node : column) {
                QList<OrchestrationNodeItem*> neighbours;
                if (downward) {
                    neighbours = parentsOf(node);
                } else {
                    for (OrchestrationEdgeItem* edge : edges_)
                        if (edge->from() == node)
                            neighbours.append(edge->to());
                }
                key[node] = meanRow(neighbours, row, row[node]);
            }
            std::stable_sort(column.begin(), column.end(),
                             [&key](OrchestrationNodeItem* a,
                                    OrchestrationNodeItem* b) {
                                 return key[a] < key[b];
                             });
        }
        reindex();
    }

    // --- Placement -----------------------------------------------------------
    constexpr double kColumnGap = 90.0;
    constexpr double kRowGap = 34.0;
    // Columns are as wide as the widest node in them plus the gap, and rows use
    // each node's OWN height: an input-slot summary makes a node taller, and a
    // fixed pitch would either overlap those or leave the rest adrift.
    double x = 0.0;
    std::vector<double> columnHeights(byColumn.size(), 0.0);
    double tallest = 0.0;
    for (std::size_t c = 0; c < byColumn.size(); ++c) {
        for (OrchestrationNodeItem* node : byColumn[c])
            columnHeights[c] += node->rect().height() + kRowGap;
        if (!byColumn[c].empty())
            columnHeights[c] -= kRowGap;
        tallest = std::max(tallest, columnHeights[c]);
    }
    for (std::size_t c = 0; c < byColumn.size(); ++c) {
        // Each column centred against the tallest, so a pipeline that branches
        // and rejoins reads as symmetric rather than top-aligned and lopsided.
        double y = (tallest - columnHeights[c]) / 2.0;
        double width = kNodeWidth;
        for (OrchestrationNodeItem* node : byColumn[c]) {
            node->setPos(x, y);
            y += node->rect().height() + kRowGap;
            width = std::max(width, node->rect().width());
        }
        x += width + kColumnGap;
    }

    for (OrchestrationEdgeItem* edge : edges_)
        edge->updatePath();
    fitToScreen();
}

void OrchestrationWindow::fitToScreen()
{
    const QRectF box = nodesBoundingRect();
    if (box.isNull() || !view_)
        return;
    // A margin, so the outermost nodes do not sit against the frame — a node
    // whose port is flush with the edge reads as clipped, and the link a user
    // is about to drag from it has nowhere to start.
    const double margin = std::max(24.0, 0.06 * std::max(box.width(),
                                                         box.height()));
    view_->fitInView(box.adjusted(-margin, -margin, margin, margin),
                     Qt::KeepAspectRatio);
    // Same clamp the wheel uses, for the same reason: a single node on the
    // canvas would otherwise be blown up to fill a 250 px dock, and the user
    // would be looking at one enormous rounded rectangle.
    const double scale = view_->transform().m11();
    if (scale > 3.0 || scale < 0.2) {
        const double target = scale > 3.0 ? 3.0 : 0.2;
        view_->scale(target / scale, target / scale);
    }
    view_->centerOn(box.center());
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
