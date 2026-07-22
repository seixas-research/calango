#include "gui/MainWindow.hpp"

#include "core/BrillouinZone.hpp"
#include "core/Noise.hpp"
#include "core/Structure.hpp"
#include "gui/BrillouinZoneDialog.hpp"
#include "gui/CalculatorDialog.hpp"
#include "gui/CoordinationDialog.hpp"
#include "gui/DistributionDialog.hpp"
#include "gui/StructureFactorDialog.hpp"
#include "gui/XrdDialog.hpp"
#include "gui/ExamplesDialog.hpp"
#include "gui/NanoBuilderDialog.hpp"
#include "gui/PhononBuilderDialog.hpp"
#include "gui/RayTraceDialog.hpp"
#include "gui/RdfDialog.hpp"
#include "gui/BondEditorDialog.hpp"
#include "gui/CellAxesPanel.hpp"
#include "gui/EnvFile.hpp"
#include "gui/LightingPanel.hpp"
#include "gui/PeriodicTableDialog.hpp"
#include "gui/PreferencesDialog.hpp"
#include "gui/BrandingPanel.hpp"
#include "gui/RemoteAccessPanel.hpp"
#include "gui/RepresentationPanel.hpp"
#include "gui/SlabWizard.hpp"
#include "core/ElectronicScriptGenerator.hpp"
#include "gui/AdsorptionDialog.hpp"
#include "gui/BandPdosWindow.hpp"
#include "gui/ClusterExpansionDialog.hpp"
#include "gui/GeometryOptimizationWizard.hpp"
#include "gui/MolecularDynamicsWizard.hpp"
#include "gui/MonteCarloDialog.hpp"
#include "gui/PhononWizard.hpp"
#include "gui/SimulationWizardBase.hpp"
#include "gui/NanoparticleDialog.hpp"
#include "gui/NebDialog.hpp"
#include "gui/PhononPlotWindow.hpp"
#include "gui/SimulationDialogs.hpp"
#include "gui/SupercellDialog.hpp"
#include "gui/SymmetryDialog.hpp"
#include "gui/DatasetManagerDialog.hpp"
#include "gui/ProcessManagerPanel.hpp"
#include "gui/RamanDialog.hpp"
#include "gui/SqsDialog.hpp"
#include "gui/VisualEffectsDialog.hpp"
#include "gui/VolumetricDialog.hpp"
#include "gui/WarrenCowleyDialog.hpp"
#include "gui/LocalEntropyDialog.hpp"
#include "gui/JobLogWidget.hpp"
#include "gui/MetricPlotWidget.hpp"
#include "gui/ProjectSerializer.hpp"
#include "gui/StructureInfoWidget.hpp"
#include "gui/TimelineWidget.hpp"
#include "gui/ViewportWidget.hpp"
#include "jobs/JobRunner.hpp"
#include "python_bridge/AnimationExporter.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageWriter>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProgressDialog>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace calango::gui {

namespace {
constexpr std::size_t kMaxUndoDepth = 50;
/// Version tag for saveState/restoreState. Bumped when the default dock
/// grid changes so stale saved layouts don't override the new default
/// (v2 = the 8-zone grid workspace, v3 = the 12-zone grid with the
/// branding and Remote Access panels, v4 = the "Job" dock renamed to
/// "Results" with a process selector).
constexpr int kLayoutVersion = 4;

/// Painted icons for the frame-panel camera toolbar (icon-only buttons).
/// Plane icons use the axes-triad colors: x red, y green, z blue.
QIcon cameraToolbarIcon(const QString& kind)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor neutral(155, 160, 170);
    const QColor xColor(235, 100, 90);
    const QColor yColor(110, 210, 130);
    const QColor zColor(90, 148, 250);

    const auto arrow = [&p](const QPointF& from, const QPointF& to,
                            const QColor& color) {
        p.setPen(QPen(color, 3.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(from, to);
        const QLineF line(from, to);
        const QPointF dir = (to - from) / line.length();
        const QPointF perp(-dir.y(), dir.x());
        p.drawLine(to, to - dir * 7.0 + perp * 4.0);
        p.drawLine(to, to - dir * 7.0 - perp * 4.0);
    };

    if (kind == QLatin1String("reset")) {
        p.setPen(QPen(neutral, 2.6));
        p.drawEllipse(QPointF(16, 16), 8.5, 8.5);
        for (const auto& [from, to] :
             {std::pair{QPointF(16, 2), QPointF(16, 8)},
              std::pair{QPointF(16, 24), QPointF(16, 30)},
              std::pair{QPointF(2, 16), QPointF(8, 16)},
              std::pair{QPointF(24, 16), QPointF(30, 16)}})
            p.drawLine(from, to);
        p.setBrush(neutral);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(16, 16), 2.4, 2.4);
    } else if (kind == QLatin1String("ortho")) {
        p.setPen(QPen(neutral, 2.4));
        p.drawRect(QRectF(5, 11, 15, 15));   // front face
        p.drawRect(QRectF(11, 5, 15, 15));   // parallel back face
        p.setPen(QPen(neutral, 1.6));
        for (const auto& [from, to] :
             {std::pair{QPointF(5, 11), QPointF(11, 5)},
              std::pair{QPointF(20, 11), QPointF(26, 5)},
              std::pair{QPointF(5, 26), QPointF(11, 20)},
              std::pair{QPointF(20, 26), QPointF(26, 20)}})
            p.drawLine(from, to);
    } else if (kind == QLatin1String("xy")) {
        arrow(QPointF(7, 25), QPointF(28, 25), xColor);
        arrow(QPointF(7, 25), QPointF(7, 4), yColor);
    } else if (kind == QLatin1String("xz")) {
        arrow(QPointF(7, 25), QPointF(28, 25), xColor);
        arrow(QPointF(7, 25), QPointF(7, 4), zColor);
    } else if (kind == QLatin1String("yz")) {
        arrow(QPointF(7, 25), QPointF(28, 25), yColor);
        arrow(QPointF(7, 25), QPointF(7, 4), zColor);
    } else if (kind == QLatin1String("rotate")) {
        // Orbit arc with an arrowhead.
        p.setPen(QPen(neutral, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(6, 6, 20, 20), 45 * 16, 250 * 16);
        arrow(QPointF(22.2, 24.5), QPointF(24.5, 22.0), neutral);
    } else if (kind == QLatin1String("pan")) {
        // Four-way move cross.
        arrow(QPointF(16, 16), QPointF(16, 3), neutral);
        arrow(QPointF(16, 16), QPointF(16, 29), neutral);
        arrow(QPointF(16, 16), QPointF(3, 16), neutral);
        arrow(QPointF(16, 16), QPointF(29, 16), neutral);
    } else if (kind == QLatin1String("select")) {
        // Dashed selection box around a couple of "atoms".
        QPen dashed(neutral, 2.0);
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        p.drawRect(QRectF(5, 5, 22, 22));
        p.setPen(Qt::NoPen);
        p.setBrush(neutral);
        p.drawEllipse(QPointF(12, 14), 3.2, 3.2);
        p.drawEllipse(QPointF(21, 20), 3.2, 3.2);
    } else if (kind == QLatin1String("insert")) {
        // New atom (circle) with a plus.
        p.setPen(QPen(neutral, 2.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(13, 19), 8.0, 8.0);
        p.setPen(QPen(neutral, 3.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(24, 4), QPointF(24, 14));
        p.drawLine(QPointF(19, 9), QPointF(29, 9));
    } else if (kind == QLatin1String("distance")) {
        // Two atoms joined by a dashed ruler line.
        QPen dashed(neutral, 2.0);
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        p.drawLine(QPointF(8, 24), QPointF(24, 8));
        p.setPen(Qt::NoPen);
        p.setBrush(neutral);
        p.drawEllipse(QPointF(7, 25), 4.0, 4.0);
        p.drawEllipse(QPointF(25, 7), 4.0, 4.0);
    } else if (kind == QLatin1String("angle")) {
        // Two rays from a vertex with the angle arc.
        p.setPen(QPen(neutral, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(6, 26), QPointF(28, 26));
        p.drawLine(QPointF(6, 26), QPointF(22, 6));
        p.setPen(QPen(neutral, 2.0));
        p.drawArc(QRectF(6 - 9, 26 - 9, 18, 18), 0, 52 * 16);
    }
    return QIcon(pixmap);
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , jobRunner_(new jobs::JobRunner(this))
{
    setWindowTitle(QStringLiteral("Calango"));
    resize(1360, 860);

    // Publish MP_API_KEY (Materials Project) from the configured .env file
    // — ~/.env by default, overridable in Edit → Preferences.
    loadEnvironmentFile();

    // Central column: document tab bar on top, then the shared 3D
    // viewport, then the playback timeline (job console dock sits below).
    tabBar_ = new QTabBar(this);
    tabBar_->setDocumentMode(true);
    tabBar_->setTabsClosable(true);
    tabBar_->setExpanding(false);
    viewport_ = new ViewportWidget(this);
    timeline_ = new TimelineWidget(this);
    timeline_->hide(); // appears when the current document has frames

    // Compact icon-only camera toolbar living inside the frame panel
    // (replaces the old top application toolbar). The orthographic action
    // is shared with View → Orthographic so both stay in sync.
    orthoAction_ = new QAction(cameraToolbarIcon(QStringLiteral("ortho")),
                               tr("Orthographic"), this);
    orthoAction_->setCheckable(true);
    orthoAction_->setShortcut(QKeySequence(Qt::Key_O));
    orthoAction_->setToolTip(tr("Toggle perspective / orthographic projection  [O]"));
    connect(orthoAction_, &QAction::toggled,
            viewport_, &ViewportWidget::setOrthographic);

    auto* frameToolbar = new QToolBar(this);
    frameToolbar->setObjectName(QStringLiteral("frameToolbar"));
    frameToolbar->setIconSize(QSize(18, 18));
    frameToolbar->setMovable(false);
    frameToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // --- Mouse interaction modes (exclusive, single-letter hotkeys) --------
    // Plain-letter shortcuts are safe: Qt's ShortcutOverride lets text
    // widgets keep the keys while typing (same as the View menu's "F").
    auto* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    const auto addModeAction =
        [this, frameToolbar, modeGroup](const QString& icon, const QString& text,
                                        ViewportWidget::InteractionMode mode,
                                        const QKeySequence& key) {
            QAction* action = frameToolbar->addAction(
                cameraToolbarIcon(icon),
                tr("%1  [%2]").arg(text, key.toString(QKeySequence::NativeText)));
            action->setCheckable(true);
            action->setShortcut(key);
            modeGroup->addAction(action);
            connect(action, &QAction::triggered, this,
                    [this, mode] { viewport_->setInteractionMode(mode); });
            return action;
        };
    QAction* rotateMode = addModeAction(
        QStringLiteral("rotate"),
        tr("Rotation mode — drag orbits the camera around the structure"),
        ViewportWidget::InteractionMode::Rotate, QKeySequence(Qt::Key_R));
    addModeAction(QStringLiteral("pan"),
                  tr("Translation mode — drag pans the scene"),
                  ViewportWidget::InteractionMode::Pan, QKeySequence(Qt::Key_T));
    addModeAction(QStringLiteral("select"),
                  tr("Selection mode — drag a box to select multiple atoms "
                     "(and their bonds); Delete/Backspace removes them"),
                  ViewportWidget::InteractionMode::Select,
                  QKeySequence(Qt::Key_S));
    addModeAction(QStringLiteral("insert"),
                  tr("Insertion mode — click empty space to add an atom of "
                     "the active element;\ndrag from one atom to another to "
                     "bond them"),
                  ViewportWidget::InteractionMode::Insert,
                  QKeySequence(Qt::Key_I));
    // Visual break: navigation/editing modes | measurement modes.
    frameToolbar->addSeparator();
    addModeAction(QStringLiteral("distance"),
                  tr("Distance measurement — click two atoms to read their "
                     "separation in Å\n(click empty space to reset)"),
                  ViewportWidget::InteractionMode::MeasureDistance,
                  QKeySequence(Qt::Key_D));
    addModeAction(QStringLiteral("angle"),
                  tr("Angle measurement — click three atoms (vertex second) "
                     "to read the angle in degrees\n(click empty space to "
                     "reset)"),
                  ViewportWidget::InteractionMode::MeasureAngle,
                  QKeySequence(Qt::Key_A));
    rotateMode->setChecked(true);

    // Active element for Insertion mode; opens the periodic table.
    elementButton_ = new QToolButton(frameToolbar);
    elementButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    elementButton_->setToolTip(tr("Element inserted by Insertion mode — "
                                  "click to choose from the periodic table"));
    const auto updateElementButton = [this] {
        elementButton_->setText(
            QLatin1String(core::Elements::data(activeElementZ_).symbol));
    };
    updateElementButton();
    connect(elementButton_, &QToolButton::clicked, this,
            [this, updateElementButton] {
                if (const int z =
                        PeriodicTableDialog::pickElement(this, activeElementZ_)) {
                    activeElementZ_ = z;
                    updateElementButton();
                }
            });
    frameToolbar->addWidget(elementButton_);
    frameToolbar->addSeparator();

    QAction* resetAction = frameToolbar->addAction(
        cameraToolbarIcon(QStringLiteral("reset")),
        tr("Reset camera (center and frame the structure)"));
    connect(resetAction, &QAction::triggered,
            viewport_, &ViewportWidget::frameStructure);
    frameToolbar->addAction(orthoAction_);
    frameToolbar->addSeparator();
    QAction* alignXy = frameToolbar->addAction(
        cameraToolbarIcon(QStringLiteral("xy")), tr("Align view with the XY plane"));
    connect(alignXy, &QAction::triggered, viewport_, &ViewportWidget::alignWithXY);
    QAction* alignXz = frameToolbar->addAction(
        cameraToolbarIcon(QStringLiteral("xz")), tr("Align view with the XZ plane"));
    connect(alignXz, &QAction::triggered, viewport_, &ViewportWidget::alignWithXZ);
    QAction* alignYz = frameToolbar->addAction(
        cameraToolbarIcon(QStringLiteral("yz")), tr("Align view with the YZ plane"));
    connect(alignYz, &QAction::triggered, viewport_, &ViewportWidget::alignWithYZ);

    // --- Fixed-angle axis rotations ---------------------------------------
    // X+/X− .. Z+/Z− rotate the scene about a world axis by the editable
    // step; the transform animates smoothly and clicks compose exactly.
    frameToolbar->addSeparator();
    auto* angleStepSpin = new QDoubleSpinBox(frameToolbar);
    angleStepSpin->setRange(1.0, 180.0);
    angleStepSpin->setValue(15.0);
    angleStepSpin->setDecimals(1);
    angleStepSpin->setSingleStep(5.0);
    angleStepSpin->setSuffix(QStringLiteral("°"));
    angleStepSpin->setToolTip(tr("Rotation step of the X/Y/Z axis buttons"));
    frameToolbar->addWidget(angleStepSpin);
    const std::array<QString, 3> axisNames{QStringLiteral("X"),
                                           QStringLiteral("Y"),
                                           QStringLiteral("Z")};
    for (int axis = 0; axis < 3; ++axis) {
        for (const int sign : {+1, -1}) {
            auto* button = new QToolButton(frameToolbar);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setText(QStringLiteral("%1%2").arg(
                axisNames[static_cast<std::size_t>(axis)],
                sign > 0 ? QStringLiteral("+") : QStringLiteral("−")));
            button->setToolTip(sign > 0
                                   ? tr("Rotate counter-clockwise around the "
                                        "%1 axis by the angle step")
                                         .arg(axisNames[static_cast<std::size_t>(axis)])
                                   : tr("Rotate clockwise around the %1 axis "
                                        "by the angle step")
                                         .arg(axisNames[static_cast<std::size_t>(axis)]));
            connect(button, &QToolButton::clicked, this,
                    [this, axis, sign, angleStepSpin] {
                        viewport_->rotateSceneAxis(axis,
                                                   sign * angleStepSpin->value());
                    });
            frameToolbar->addWidget(button);
        }
    }

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(tabBar_);
    centralLayout->addWidget(frameToolbar);
    centralLayout->addWidget(viewport_, 1);
    centralLayout->addWidget(timeline_);
    setCentralWidget(central);

    connect(tabBar_, &QTabBar::currentChanged, this, &MainWindow::onTabChanged);
    connect(tabBar_, &QTabBar::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    connect(timeline_, &TimelineWidget::frameChanged, this, &MainWindow::showFrame);

    createMenusAndDocks();

    // The log widget shows the running job directly (it always clears on
    // start); its output/error/progress are routed through MainWindow so they
    // are buffered per process and only mirrored when that process is
    // selected. Metric samples are likewise routed for per-process isolation.
    connect(jobRunner_, &jobs::JobRunner::started,
            jobLogWidget_, &JobLogWidget::onJobStarted);
    connect(jobRunner_, &jobs::JobRunner::outputLine,
            this, &MainWindow::onJobOutputLine);
    connect(jobRunner_, &jobs::JobRunner::errorLine,
            this, &MainWindow::onJobErrorLine);
    connect(jobRunner_, &jobs::JobRunner::progress,
            this, &MainWindow::onJobProgress);
    connect(jobRunner_, &jobs::JobRunner::energySample,
            this, &MainWindow::onEnergySample);
    connect(jobRunner_, &jobs::JobRunner::temperatureSample,
            this, &MainWindow::onTemperatureSample);
    connect(jobRunner_, &jobs::JobRunner::targetTemperature,
            this, &MainWindow::onTargetTemperature);
    connect(jobRunner_, &jobs::JobRunner::maxForceSample,
            this, &MainWindow::onForceSample);
    connect(jobRunner_, &jobs::JobRunner::pressureSample,
            this, &MainWindow::onPressureSample);
    connect(jobRunner_, &jobs::JobRunner::targetPressure,
            this, &MainWindow::onTargetPressure);
    connect(jobRunner_, &jobs::JobRunner::finished,
            jobLogWidget_, &JobLogWidget::onJobFinished);
    connect(jobRunner_, &jobs::JobRunner::finished,
            this, &MainWindow::onJobFinished);
    connect(jobRunner_, &jobs::JobRunner::frameStreamed,
            this, &MainWindow::onFrameStreamed);
    connect(jobLogWidget_, &JobLogWidget::terminateRequested,
            jobRunner_, &jobs::JobRunner::terminate);

    connect(viewport_, &ViewportWidget::selectionChanged, this, [this](int count) {
        if (count > 0)
            statusBar()->showMessage(tr("%n atom(s) selected", nullptr, count));
    });

    // Insertion mode edits: the viewport only *requests* — the document
    // (with its undo history) is mutated here.
    connect(viewport_, &ViewportWidget::atomInsertRequested, this,
            [this](const core::Vec3& position) {
                Document& doc = ensureDocument();
                pushUndo();
                doc.structure->addAtom({activeElementZ_, position});
                notifyStructureChanged(false);
                statusBar()->showMessage(
                    tr("Added %1 at (%2, %3, %4) Å")
                        .arg(QLatin1String(
                            core::Elements::data(activeElementZ_).symbol))
                        .arg(position.x, 0, 'f', 2)
                        .arg(position.y, 0, 'f', 2)
                        .arg(position.z, 0, 'f', 2));
            });
    // Measurements land in the status console (the viewport overlays the
    // same value on the canvas).
    connect(viewport_, &ViewportWidget::measurementMade, this,
            [this](const QString& text) { statusBar()->showMessage(text); });
    connect(viewport_, &ViewportWidget::deleteSelectionRequested,
            this, &MainWindow::deleteSelectedAtoms);
    connect(viewport_, &ViewportWidget::bondInsertRequested, this,
            [this](int i, int j) {
                Document* doc = currentDocument();
                if (!doc || !doc->structure)
                    return;
                pushUndo();
                doc->structure->addBondOverride(i, j);
                notifyStructureChanged(false);
                statusBar()->showMessage(tr("Bond %1–%2 created").arg(i).arg(j));
            });
    // Insert mode Shift+click: substitute the clicked atom's element.
    connect(viewport_, &ViewportWidget::atomReplaceRequested, this,
            [this](int index) {
                Document* doc = currentDocument();
                if (!doc || !doc->structure || index < 0
                    || index >= static_cast<int>(doc->structure->size()))
                    return;
                pushUndo();
                doc->structure->atoms()[static_cast<std::size_t>(index)]
                    .atomicNumber = activeElementZ_;
                notifyStructureChanged(false);
                statusBar()->showMessage(
                    tr("Replaced atom %1 with %2").arg(index).arg(
                        QLatin1String(core::Elements::data(activeElementZ_).symbol)));
            });
    // Translation (Pan) mode Shift+drag: move a single atom (one undo/drag).
    connect(viewport_, &ViewportWidget::atomTranslateRequested, this,
            [this](int index, const core::Vec3& position, bool begin) {
                Document* doc = currentDocument();
                if (!doc || !doc->structure || index < 0
                    || index >= static_cast<int>(doc->structure->size()))
                    return;
                if (begin)
                    pushUndo();
                doc->structure->atoms()[static_cast<std::size_t>(index)].position =
                    position;
                viewport_->refreshStructure(); // fast, keeps camera + selection
                if (begin)
                    statusBar()->showMessage(tr("Translating atom %1…").arg(index));
            });

    statusBar()->showMessage(tr("Ready — open a structure to begin (File → Open)"));
}

MainWindow::~MainWindow() = default;

void MainWindow::createMenusAndDocks()
{
    // Docks can sit side-by-side (nested), stack as tabs, or float.
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks);

    // Menu bar order is fixed: File, Edit, View, Build, Simulation,
    // Analysis (Help trails as is conventional).
    // ----- File: New | Open | Save | Import/Export | Workspace | Quit ------
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New Workspace"), QKeySequence::New,
                        this, &MainWindow::newProject);
    QMenu* openMenu = fileMenu->addMenu(tr("&Open"));
    openMenu->addAction(tr("&Structure…"), QKeySequence::Open,
                        this, &MainWindow::openStructure);
    openMenu->addAction(tr("&Trajectory…"), QKeySequence(tr("Ctrl+T")),
                        this, &MainWindow::openTrajectory);
    recentMenu_ = openMenu->addMenu(tr("Open &Recent"));
    updateRecentFilesMenu();
    QMenu* saveMenu = fileMenu->addMenu(tr("&Save"));
    saveMenu->addAction(tr("Structure &As…"), QKeySequence::SaveAs,
                        this, &MainWindow::saveStructureAs);
    saveMenu->addAction(tr("Tra&jectory As…"), QKeySequence(tr("Ctrl+Shift+T")),
                        this, &MainWindow::saveTrajectoryAs);
    QMenu* exportMenu = fileMenu->addMenu(tr("&Import / Export"));
    exportMenu->addAction(tr("Export &Image…"), QKeySequence(tr("Ctrl+E")),
                          this, &MainWindow::exportImage);
    exportMenu->addAction(tr("Export Ani&mation (GIF/MP4)…"),
                          this, &MainWindow::exportAnimation);
    exportMenu->addAction(tr("&Ray-Traced Render…"),
                          this, &MainWindow::openRayTraceDialog);
    fileMenu->addSeparator();
    // Project workspace: one .calproj file restores the whole multi-tab
    // session (structures, trajectories, job console + metric series).
    QMenu* workspaceMenu = fileMenu->addMenu(tr("Project &Workspace"));
    workspaceMenu->addAction(tr("Open &Project…"),
                             QKeySequence(tr("Ctrl+Shift+O")),
                             this, &MainWindow::openProject);
    workspaceMenu->addAction(tr("Save P&roject"), QKeySequence::Save,
                             this, &MainWindow::saveProject);
    workspaceMenu->addAction(tr("Save Project As…"),
                             this, &MainWindow::saveProjectAs);
    fileMenu->addAction(tr("&Close Tab"), QKeySequence::Close, this, [this] {
        if (tabBar_->currentIndex() >= 0)
            onTabCloseRequested(tabBar_->currentIndex());
    });
    fileMenu->addSeparator();
    // Quit goes through closeAllWindows so closeEvent persists the window
    // geometry / dock layout (and runs the unsaved-changes guard); the
    // embedded interpreter is finalized by PythonEngine's destructor.
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit,
                        qApp, &QApplication::closeAllWindows);

    // ----- Edit: Selection | Deletion | Bonds | Cell | Preferences ---------
    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    undoAction_ = editMenu->addAction(tr("&Undo"), QKeySequence::Undo,
                                      this, &MainWindow::undo);
    redoAction_ = editMenu->addAction(tr("&Redo"), QKeySequence::Redo,
                                      this, &MainWindow::redo);
    editMenu->addSeparator();
    editMenu->addAction(tr("&Add Atom…"), QKeySequence(tr("Ctrl+Shift+A")),
                        this, &MainWindow::addAtom);
    QMenu* selectionMenu = editMenu->addMenu(tr("&Selection"));
    selectionMenu->addAction(tr("&Change Element of Selection…"),
                             this, &MainWindow::changeElementOfSelection);
    selectionMenu->addAction(tr("&Translate Selection…"),
                             this, &MainWindow::translateSelection);
    editMenu->addAction(tr("&Delete Selected Atoms"), QKeySequence::Delete,
                        this, &MainWindow::deleteSelectedAtoms);
    editMenu->addSeparator();
    editMenu->addAction(tr("&Bond Editor…"), QKeySequence(tr("Ctrl+B")),
                        this, &MainWindow::showBondEditor);
    QMenu* cellMenu = editMenu->addMenu(tr("Unit &Cell"));
    cellMenu->addAction(tr("Create &Supercell…"),
                        this, &MainWindow::createSupercell);
    editMenu->addSeparator();
    editMenu->addAction(tr("&Preferences…"), QKeySequence::Preferences,
                        this, &MainWindow::showPreferences);
    updateUndoActions();

    // ----- View: projection | overlays | alignment | effects | panels ------
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(orthoAction_); // shared with the frame-panel toolbar
    QMenu* overlaysMenu = viewMenu->addMenu(tr("&Overlays"));
    QAction* cellAction = overlaysMenu->addAction(tr("Show Unit &Cell"));
    cellAction->setCheckable(true);
    cellAction->setChecked(true);
    connect(cellAction, &QAction::toggled, viewport_, &ViewportWidget::setShowCell);
    QMenu* alignMenu = viewMenu->addMenu(tr("&Alignment"));
    alignMenu->addAction(tr("&Frame Structure"), QKeySequence(tr("F")),
                         viewport_, &ViewportWidget::frameStructure);
    alignMenu->addAction(tr("Align with &XY Plane"),
                         viewport_, &ViewportWidget::alignWithXY);
    alignMenu->addAction(tr("Align with X&Z Plane"),
                         viewport_, &ViewportWidget::alignWithXZ);
    alignMenu->addAction(tr("Align with &YZ Plane"),
                         viewport_, &ViewportWidget::alignWithYZ);
    viewMenu->addAction(tr("Visual &Effects…"),
                        this, &MainWindow::showVisualEffects);

    // ----- Build: generators and structure sources -------------------------
    QMenu* buildMenu = menuBar()->addMenu(tr("&Build"));
    buildMenu->addAction(tr("&Nanomaterial Builder…"), this, &MainWindow::openNanoBuilder);
    buildMenu->addAction(tr("&Supercell (Transformation Matrix)…"),
                         this, &MainWindow::openSupercellBuilder);
    buildMenu->addAction(tr("&Surface Slab…"), this, &MainWindow::cleaveSurface);
    buildMenu->addAction(tr("Nano&particle && Cluster Builder…"),
                         this, &MainWindow::openNanoparticleBuilder);
    buildMenu->addAction(tr("Special &Quasirandom Structure (SQS)…"),
                         this, &MainWindow::openSqsBuilder);
    buildMenu->addAction(tr("Cluster &Expansion…"),
                         this, &MainWindow::openClusterExpansion);
    buildMenu->addAction(tr("From &Database…"), this, &MainWindow::openExamplesBrowser);
    buildMenu->addAction(tr("Structure Perturbation / &Noise…"),
                         this, &MainWindow::addRandomNoise);

    // ----- Simulation: local/remote jobs and ML datasets -------------------
    QMenu* simulationMenu = menuBar()->addMenu(tr("&Simulation"));
    simulationMenu->addAction(tr("&Single-point Calculation…"),
                              QKeySequence(tr("Ctrl+R")),
                              this, &MainWindow::singlePointCalculation);
    simulationMenu->addAction(tr("&Geometry Optimization…"),
                              this, &MainWindow::geometryOptimization);
    simulationMenu->addAction(tr("&Molecular Dynamics…"),
                              this, &MainWindow::molecularDynamics);
    simulationMenu->addAction(tr("&Phonon Calculator…"),
                              this, &MainWindow::openPhononBuilder);
    simulationMenu->addAction(tr("&Monte Carlo Simulation…"),
                              this, &MainWindow::openMonteCarlo);
    simulationMenu->addAction(tr("&Nudged Elastic Band (NEB)…"),
                              this, &MainWindow::openNudgedElasticBand);
    simulationMenu->addSeparator();
    simulationMenu->addAction(tr("New &Remote Calculation…"),
                              QKeySequence(tr("Ctrl+Shift+R")),
                              this, &MainWindow::newRemoteCalculation);
    simulationMenu->addAction(tr("Electronic &Bands / PDOS…"),
                              this, &MainWindow::showBandStructure);
    simulationMenu->addSeparator();
    simulationMenu->addAction(tr("&Dataset Manager (MLIP)…"),
                              this, &MainWindow::showDatasetManager);

    // ----- Analysis: spec order, reciprocal-space tools at the end ---------
    QMenu* analysisMenu = menuBar()->addMenu(tr("&Analysis"));
    analysisMenu->addAction(tr("Detect &Symmetry…"),
                            this, &MainWindow::showSymmetry);
    analysisMenu->addAction(tr("Structure &Factor S(q)…"),
                            this, &MainWindow::showStructureFactor);
    analysisMenu->addAction(tr("&X-Ray Diffraction (XRD)…"),
                            this, &MainWindow::showXrd);
    analysisMenu->addAction(tr("&Radial Distribution Function…"),
                            this, &MainWindow::showRdf);
    analysisMenu->addAction(tr("Bond &Length / Angle Distributions…"),
                            this, &MainWindow::showDistributions);
    analysisMenu->addAction(tr("&Coordination Numbers (CN / GCN)…"),
                            this, &MainWindow::showCoordination);
    analysisMenu->addAction(tr("&Warren-Cowley analysis"),
                            this, &MainWindow::showWarrenCowley);
    analysisMenu->addAction(tr("Local &Entropy Analysis…"),
                            this, &MainWindow::showLocalEntropy);
    analysisMenu->addAction(tr("Ra&man Modes…"),
                            this, &MainWindow::showRamanModes);
    analysisMenu->addAction(tr("&Volumetric Data…"),
                            this, &MainWindow::showVolumetricData);
    analysisMenu->addAction(tr("Adsorption && Catal&ysis…"),
                            this, &MainWindow::showAdsorption);
    analysisMenu->addSeparator();
    analysisMenu->addAction(tr("&Brillouin Zone / k-Path…"),
                            this, &MainWindow::showBrillouinZone);

    // Help trails the menu bar: online resources first, About last (as is
    // conventional). New documentation/support links belong in kHelpLinks.
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    struct HelpLink {
        const char* title;
        const char* url;
    };
    static constexpr HelpLink kHelpLinks[] = {
        {QT_TR_NOOP("&Documentation"),
         "https://github.com/seixas-research/calango#readme"},
        {QT_TR_NOOP("&GitHub Repository"),
         "https://github.com/seixas-research/calango"},
    };
    for (const auto& link : kHelpLinks) {
        const QUrl url = QUrl(QLatin1String(link.url));
        helpMenu->addAction(tr(link.title),
                            [url] { QDesktopServices::openUrl(url); });
    }
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&About Calango"), this, &MainWindow::about);

    // ----- 12-zone grid workspace (4 columns × 3 rows) ---------------------
    //
    //   | 1 Logo & Name | 2-3   Viewport | 4  Representation  |
    //   | 5 Structure   | 6-7   Viewport | 8  (Representation)|
    //   | 9 Lighting    | 10 Job | 11 Remote | 12 Cell & Axes |
    //
    // The bottom dock area owns both corners, so the third row spans the
    // full window width with four zones side by side. The left column
    // (branding over structure) and the right column (Representation
    // spanning two rows) flank the central widget (tab bar + viewport +
    // timeline) that fills zones 2-3/6-7. Every zone stays resizable via
    // the dock splitters, and panels remain re-dockable/floatable.
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    auto* brandingDock = new QDockWidget(tr("Calango"), this); // zone 1
    brandingDock->setObjectName(QStringLiteral("brandingDock"));
    brandingDock->setWidget(new BrandingPanel(brandingDock));
    // No title bar: zone 1 shows only the centered logo. (The dock title
    // still names the View-menu toggle; an empty title widget removes
    // the header without disabling the dock.)
    brandingDock->setTitleBarWidget(new QWidget(brandingDock));
    addDockWidget(Qt::LeftDockWidgetArea, brandingDock);

    // Compact Process Manager between the branding card and Structure.
    auto* processDock = new QDockWidget(tr("Processes"), this);
    processDock->setObjectName(QStringLiteral("processDock"));
    processPanel_ = new ProcessManagerPanel(processDock);
    processDock->setWidget(processPanel_);
    splitDockWidget(brandingDock, processDock, Qt::Vertical);
    connect(processPanel_, &ProcessManagerPanel::loadResultRequested,
            this, &MainWindow::onProcessResultRequested);
    connect(processPanel_, &ProcessManagerPanel::deleteRequested,
            this, &MainWindow::onDeleteProcessRequested);

    auto* infoDock = new QDockWidget(tr("Structure"), this); // zone 5
    infoDock->setObjectName(QStringLiteral("structureDock"));
    infoWidget_ = new StructureInfoWidget(infoDock);
    infoDock->setWidget(infoWidget_);
    splitDockWidget(processDock, infoDock, Qt::Vertical);

    auto* reprDock = new QDockWidget(tr("Representation"), this); // zones 4 & 8
    reprDock->setObjectName(QStringLiteral("representationDock"));
    auto* reprPanel = new RepresentationPanel(viewport_, reprDock);
    reprDock->setWidget(reprPanel);
    addDockWidget(Qt::RightDockWidgetArea, reprDock);
    connect(reprPanel, &RepresentationPanel::bondOrderAssignRequested,
            this, &MainWindow::assignBondOrderToSelection);

    auto* lightingDock = new QDockWidget(tr("Lighting"), this); // zone 9
    lightingDock->setObjectName(QStringLiteral("lightingDock"));
    lightingDock->setWidget(new LightingPanel(viewport_, lightingDock));
    addDockWidget(Qt::BottomDockWidgetArea, lightingDock);

    jobDock_ = new QDockWidget(tr("Results"), this); // zone 10
    jobDock_->setObjectName(QStringLiteral("resultsDock"));
    auto* jobTabs = new QTabWidget(jobDock_);
    jobLogWidget_ = new JobLogWidget(jobTabs);

    // One MetricPlotWidget per job observable; the specs carry all
    // labeling/units so the tabs stay behaviorally identical.
    MetricPlotWidget::MetricSpec energySpec;
    energySpec.quantity = tr("Energy");
    energySpec.yAxisLabel = tr("Total Energy (eV)");
    energySpec.xAxisLabel = tr("MD/optimization step");
    energySpec.valueSymbol = QStringLiteral("E");
    energySpec.unit = tr("eV");
    energySpec.placeholder = tr("Energy vs. step will appear here during a run");
    energySpec.marker = QStringLiteral("CALANGO_ENERGY");
    energySpec.csvColumn = QStringLiteral("total_energy_eV");
    energySpec.exportBaseName = QStringLiteral("energy.csv");
    energySpec.lineColor = QColor(102, 153, 255);
    energySpec.decimals = 3;

    MetricPlotWidget::MetricSpec temperatureSpec;
    temperatureSpec.quantity = tr("Temperature");
    temperatureSpec.yAxisLabel = tr("Temperature (K)");
    temperatureSpec.xAxisLabel = tr("MD step");
    temperatureSpec.valueSymbol = QStringLiteral("T");
    temperatureSpec.unit = tr("K");
    temperatureSpec.placeholder =
        tr("Temperature vs. step will appear here during an MD run");
    temperatureSpec.marker = QStringLiteral("CALANGO_TEMP");
    temperatureSpec.csvColumn = QStringLiteral("temperature_K");
    temperatureSpec.csvTargetColumn = QStringLiteral("target_K");
    temperatureSpec.exportBaseName = QStringLiteral("temperature.csv");
    temperatureSpec.lineColor = QColor(235, 110, 80);
    temperatureSpec.decimals = 1;
    temperatureSpec.exportDecimals = 2;
    temperatureSpec.flatPadding = 5.0;

    MetricPlotWidget::MetricSpec forceSpec;
    forceSpec.quantity = tr("Force");
    forceSpec.yAxisLabel = tr("Max |F| (eV/Å)");
    forceSpec.xAxisLabel = tr("MD/optimization step");
    forceSpec.valueSymbol = tr("max |F|");
    forceSpec.unit = tr("eV/Å");
    forceSpec.placeholder = tr("Maximum atomic force vs. step will appear here "
                               "during an optimization or MD run");
    forceSpec.marker = QStringLiteral("CALANGO_FMAX");
    forceSpec.csvColumn = QStringLiteral("max_force_eV_per_A");
    forceSpec.exportBaseName = QStringLiteral("max_force.csv");
    forceSpec.lineColor = QColor(110, 210, 130);
    forceSpec.decimals = 3;
    forceSpec.flatPadding = 0.05;

    MetricPlotWidget::MetricSpec pressureSpec;
    pressureSpec.quantity = tr("Pressure");
    pressureSpec.yAxisLabel = tr("Pressure (GPa)");
    pressureSpec.xAxisLabel = tr("MD step");
    pressureSpec.valueSymbol = QStringLiteral("P");
    pressureSpec.unit = tr("GPa");
    pressureSpec.placeholder =
        tr("Pressure vs. step will appear here during a constant-pressure "
           "(NPT) MD run");
    pressureSpec.marker = QStringLiteral("CALANGO_PRESSURE");
    pressureSpec.csvColumn = QStringLiteral("pressure_GPa");
    pressureSpec.csvTargetColumn = QStringLiteral("target_GPa");
    pressureSpec.exportBaseName = QStringLiteral("pressure.csv");
    pressureSpec.lineColor = QColor(188, 140, 255);
    pressureSpec.decimals = 3;
    pressureSpec.flatPadding = 0.1;

    energyPlot_ = new MetricPlotWidget(energySpec, jobTabs);
    temperaturePlot_ = new MetricPlotWidget(temperatureSpec, jobTabs);
    forcePlot_ = new MetricPlotWidget(forceSpec, jobTabs);
    pressurePlot_ = new MetricPlotWidget(pressureSpec, jobTabs);
    jobTabs->addTab(jobLogWidget_, tr("Log"));

    // Plot tabs carry their own Export Data… action for external analysis.
    const auto plotPage = [jobTabs](MetricPlotWidget* plot) {
        auto* page = new QWidget(jobTabs);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 2);
        layout->setSpacing(2);
        layout->addWidget(plot, 1);
        auto* row = new QHBoxLayout;
        row->addStretch(1);
        auto* exportButton = new QPushButton(QObject::tr("Export Data…"), page);
        row->addWidget(exportButton);
        layout->addLayout(row);
        QObject::connect(exportButton, &QPushButton::clicked,
                         plot, &MetricPlotWidget::exportData);
        return page;
    };
    jobTabs->addTab(plotPage(energyPlot_), tr("Energy"));
    jobTabs->addTab(plotPage(temperaturePlot_), tr("Temperature"));
    jobTabs->addTab(plotPage(forcePlot_), tr("Force"));
    jobTabs->addTab(plotPage(pressurePlot_), tr("Pressure"));
    // Wrap the tab widget with a small top margin: without it the tab
    // titles (Log/Energy/...) render flush against the dock's title bar
    // and visually collide with it (zone-10 overflow).
    auto* jobContainer = new QWidget(jobDock_);
    auto* jobLayout = new QVBoxLayout(jobContainer);
    jobLayout->setContentsMargins(4, 8, 4, 4);
    jobLayout->setSpacing(4);
    // Process selector: each background run keeps its own metric history, so
    // switching processes here repopulates every tab with that run's data.
    auto* selectorRow = new QHBoxLayout;
    selectorRow->setContentsMargins(0, 0, 0, 0);
    selectorRow->addWidget(new QLabel(tr("Process:"), jobContainer));
    processSelector_ = new QComboBox(jobContainer);
    processSelector_->setToolTip(
        tr("Select a background run to view its logged metrics."));
    selectorRow->addWidget(processSelector_, 1);
    jobLayout->addLayout(selectorRow);
    connect(processSelector_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onProcessSelected);
    jobLayout->addWidget(jobTabs);
    jobTabs->setDocumentMode(true); // flat tab bar, no frame to overlap
    jobDock_->setWidget(jobContainer);
    splitDockWidget(lightingDock, jobDock_, Qt::Horizontal);

    remoteDock_ = new QDockWidget(tr("Remote Access"), this); // zone 11
    remoteDock_->setObjectName(QStringLiteral("remoteDock"));
    remotePanel_ = new RemoteAccessPanel(
        QString::fromStdString(pybridge::PythonEngine::instance().executable()),
        remoteDock_);
    remoteDock_->setWidget(remotePanel_);
    splitDockWidget(jobDock_, remoteDock_, Qt::Horizontal);

    auto* cellAxesDock = new QDockWidget(tr("Unit Cell && Axes"), this); // zone 12
    cellAxesDock->setObjectName(QStringLiteral("cellAxesDock"));
    cellAxesDock->setWidget(new CellAxesPanel(viewport_, cellAxesDock));
    splitDockWidget(remoteDock_, cellAxesDock, Qt::Horizontal);

    connect(remotePanel_, &RemoteAccessPanel::submitCalculationRequested,
            this, &MainWindow::newRemoteCalculation);
    connect(remotePanel_, &RemoteAccessPanel::resultsReady,
            this, &MainWindow::onRemoteResultsReady);

    // Default grid proportions: side columns ~290 px wide with a compact
    // branding card; the full-width bottom row is ~250 px tall, its four
    // zones sized so the Job console keeps the most room.
    resizeDocks({brandingDock, processDock, infoDock}, {290, 290, 290},
                Qt::Horizontal);
    resizeDocks({reprDock}, {290}, Qt::Horizontal);
    resizeDocks({brandingDock, processDock, infoDock}, {150, 150, 430},
                Qt::Vertical);
    resizeDocks({lightingDock, jobDock_, remoteDock_, cellAxesDock},
                {250, 250, 250, 250}, Qt::Vertical);
    resizeDocks({lightingDock, jobDock_, remoteDock_, cellAxesDock},
                {280, 560, 430, 290}, Qt::Horizontal);

    // Dock titles at 1.2× the theme default across all zones (the earlier
    // 1.5× reduced by 0.8×). The font is set on the QDockWidget (whose
    // title bar renders with it) and reset on each content widget, since
    // fonts would otherwise propagate down.
    const QFont contentFont = QApplication::font();
    QFont dockTitleFont = contentFont;
    dockTitleFont.setPointSizeF(contentFont.pointSizeF() * 1.2);
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        dock->setFont(dockTitleFont);
        if (dock->widget())
            dock->widget()->setFont(contentFont);
    }

    viewMenu->addSeparator();
    viewMenu->addAction(brandingDock->toggleViewAction());
    viewMenu->addAction(processDock->toggleViewAction());
    viewMenu->addAction(infoDock->toggleViewAction());
    viewMenu->addAction(reprDock->toggleViewAction());
    viewMenu->addAction(cellAxesDock->toggleViewAction());
    viewMenu->addAction(lightingDock->toggleViewAction());
    viewMenu->addAction(jobDock_->toggleViewAction());
    viewMenu->addAction(remoteDock_->toggleViewAction());

    // Reapply the layout the user left behind last session. The version
    // tag rejects layouts saved before the 8-zone grid existed, so the
    // new default appears once and user rearrangements persist after.
    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray(),
                 kLayoutVersion);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // File -> Quit funnels through closeAllWindows, so this guard covers
    // both the menu action and the window close button.
    if (isDirty_) {
        const auto choice = QMessageBox::warning(
            this, tr("Unsaved Changes"),
            tr("The project has unsaved changes.\n"
               "Do you want to save them before quitting?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Cancel
            || (choice == QMessageBox::Save && !saveProject())) {
            event->ignore(); // stay open (save failed or was cancelled too)
            return;
        }
    }

    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState(kLayoutVersion));
    QMainWindow::closeEvent(event);
}

bool MainWindow::ensureAseAvailable()
{
    const auto& python = pybridge::PythonEngine::instance();
    if (python.aseAvailable())
        return true;
    QMessageBox::warning(
        this, tr("ASE Not Available"),
        tr("The embedded Python interpreter cannot import ASE.\n\n"
           "Point Calango at an interpreter that has ASE, e.g.:\n"
           "    export CALANGO_PYTHON=/path/to/.venv/bin/python\n"
           "then restart. Diagnose with:  calango --probe-python\n\n"
           "Details:\n%1")
            .arg(QString::fromStdString(python.lastError())));
    return false;
}

// ---------------------------------------------------------------------------
// Document / tab management
// ---------------------------------------------------------------------------

MainWindow::Document* MainWindow::currentDocument()
{
    const int index = tabBar_->currentIndex();
    if (index < 0 || index >= static_cast<int>(documents_.size()))
        return nullptr;
    return documents_[static_cast<std::size_t>(index)].get();
}

MainWindow::Document& MainWindow::ensureDocument()
{
    if (Document* doc = currentDocument())
        return *doc;
    const int index =
        addDocument(std::make_shared<core::Structure>(), tr("Untitled"));
    return *documents_[static_cast<std::size_t>(index)];
}

int MainWindow::addDocument(std::shared_ptr<core::Structure> structure,
                            const QString& name,
                            std::vector<std::shared_ptr<core::Structure>> frames)
{
    auto document = std::make_unique<Document>();
    document->structure = std::move(structure);
    document->frames = std::move(frames);
    document->fileName = name;
    documents_.push_back(std::move(document));

    const int index = tabBar_->addTab(name);
    tabBar_->setCurrentIndex(index); // triggers onTabChanged -> sync
    if (tabBar_->currentIndex() == index)
        syncViewsToCurrent(true); // first tab: currentChanged may not fire
    return index;
}

void MainWindow::onTabChanged(int index)
{
    if (index < 0 || index >= static_cast<int>(documents_.size())) {
        viewport_->setStructure(nullptr);
        infoWidget_->updateFromStructure(nullptr);
        timeline_->stop();
        timeline_->hide();
        setWindowTitle(QStringLiteral("Calango"));
        updateUndoActions();
        return;
    }
    syncViewsToCurrent(true);
}

void MainWindow::onTabCloseRequested(int index)
{
    if (index < 0 || index >= static_cast<int>(documents_.size()))
        return;
    if (documents_[static_cast<std::size_t>(index)].get() == liveDoc_)
        liveDoc_ = nullptr; // stream continues, frames just aren't shown
    documents_.erase(documents_.begin() + index);
    tabBar_->removeTab(index); // currentChanged fires and re-syncs
    isDirty_ = true;
}

void MainWindow::syncViewsToCurrent(bool frameCamera)
{
    Document* doc = currentDocument();
    if (!doc)
        return;
    viewport_->setStructure(doc->structure, frameCamera);
    infoWidget_->updateFromStructure(doc->structure.get());
    setWindowTitle(doc->fileName.isEmpty()
                       ? QStringLiteral("Calango")
                       : QStringLiteral("Calango — %1").arg(doc->fileName));
    if (doc->frames.size() > 1) {
        timeline_->setFrameCount(static_cast<int>(doc->frames.size()));
        timeline_->show();
    } else {
        timeline_->stop();
        timeline_->hide();
    }
    updateUndoActions();
}

void MainWindow::replaceCurrentStructure(std::shared_ptr<core::Structure> structure,
                                         const QString& name)
{
    Document* doc = currentDocument();
    if (!doc)
        return;
    doc->structure = std::move(structure);
    doc->fileName = name;
    doc->frames.clear();
    tabBar_->setTabText(tabBar_->currentIndex(), name);
    syncViewsToCurrent(true);
}

void MainWindow::notifyStructureChanged(bool frameCamera)
{
    Document* doc = currentDocument();
    if (!doc)
        return;
    viewport_->setStructure(doc->structure, frameCamera);
    infoWidget_->updateFromStructure(doc->structure.get());
}

void MainWindow::pushUndo()
{
    Document* doc = currentDocument();
    if (!doc)
        return;
    // Every undoable mutation funnels through here — the natural single
    // point to flag the workspace as having unsaved changes.
    isDirty_ = true;
    doc->undoStack.push_back(
        doc->structure ? std::make_shared<core::Structure>(*doc->structure) : nullptr);
    if (doc->undoStack.size() > kMaxUndoDepth)
        doc->undoStack.pop_front();
    doc->redoStack.clear();
    updateUndoActions();
}

void MainWindow::updateUndoActions()
{
    Document* doc = currentDocument();
    undoAction_->setEnabled(doc && !doc->undoStack.empty());
    redoAction_->setEnabled(doc && !doc->redoStack.empty());
}

void MainWindow::undo()
{
    Document* doc = currentDocument();
    if (!doc || doc->undoStack.empty())
        return;
    doc->redoStack.push_back(
        doc->structure ? std::make_shared<core::Structure>(*doc->structure) : nullptr);
    doc->structure = doc->undoStack.back();
    doc->undoStack.pop_back();
    updateUndoActions();
    notifyStructureChanged(false);
    statusBar()->showMessage(tr("Undo"), 2000);
}

void MainWindow::redo()
{
    Document* doc = currentDocument();
    if (!doc || doc->redoStack.empty())
        return;
    doc->undoStack.push_back(
        doc->structure ? std::make_shared<core::Structure>(*doc->structure) : nullptr);
    doc->structure = doc->redoStack.back();
    doc->redoStack.pop_back();
    updateUndoActions();
    notifyStructureChanged(false);
    statusBar()->showMessage(tr("Redo"), 2000);
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

namespace {

/// Explicit ASE format hints for extensions ase.io cannot infer reliably.
QString formatHintFor(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("data"))
        return QStringLiteral("lammps-data");
    if (suffix == QLatin1String("dump") || suffix == QLatin1String("lammpstrj"))
        return QStringLiteral("lammps-dump-text");
    if (suffix == QLatin1String("pwi") || suffix == QLatin1String("in"))
        return QStringLiteral("espresso-in");
    if (suffix == QLatin1String("pwo"))
        return QStringLiteral("espresso-out");
    if (suffix == QLatin1String("gjf") || suffix == QLatin1String("com"))
        return QStringLiteral("gaussian-in");
    if (suffix == QLatin1String("cell"))
        return QStringLiteral("castep-cell");
    if (suffix == QLatin1String("res"))
        return QStringLiteral("res");
    return {}; // .out and others: let ASE sniff the contents
}

} // namespace

namespace {
const auto kRecentFilesKey = QStringLiteral("recent/files");
constexpr int kMaxRecentFiles = 10;
} // namespace

void MainWindow::addRecentFile(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    QSettings settings;
    QStringList recent = settings.value(kRecentFilesKey).toStringList();
    recent.removeAll(absolute); // de-duplicate; the newest goes to the front
    recent.prepend(absolute);
    while (recent.size() > kMaxRecentFiles)
        recent.removeLast();
    settings.setValue(kRecentFilesKey, recent);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu()
{
    if (!recentMenu_)
        return;
    recentMenu_->clear();

    QStringList recent = QSettings().value(kRecentFilesKey).toStringList();
    // Drop entries whose files no longer exist so the list stays trustworthy.
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [](const QString& p) { return !QFileInfo::exists(p); }),
                 recent.end());

    if (recent.isEmpty()) {
        QAction* empty = recentMenu_->addAction(tr("(no recent files)"));
        empty->setEnabled(false);
        return;
    }

    int index = 1;
    for (const QString& path : recent) {
        // "&1 name" gives Alt-number mnemonics for the first nine entries.
        const QString label = index <= 9
            ? tr("&%1  %2").arg(index).arg(QFileInfo(path).fileName())
            : QFileInfo(path).fileName();
        QAction* action = recentMenu_->addAction(label);
        action->setData(path);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this,
                [this, path] { loadFile(path); });
        ++index;
    }
    recentMenu_->addSeparator();
    recentMenu_->addAction(tr("&Clear Recent Files"), this, [this] {
        QSettings().remove(kRecentFilesKey);
        updateRecentFilesMenu();
    });
}

void MainWindow::loadFile(const QString& path)
{
    // Project workspaces (double-click / "Open with" via the installer's
    // MIME association, or a CLI argument) restore the whole session
    // instead of loading a structure through ASE.
    if (path.endsWith(QStringLiteral(".calproj"), Qt::CaseInsensitive)) {
        if (readProject(path)) {
            projectPath_ = path;
            addRecentFile(path);
        }
        return;
    }
    if (!ensureAseAvailable())
        return;
    try {
        // Always read every frame: multi-frame files (trajectories,
        // animated XYZ) get a document with frames, which automatically
        // reveals and activates the timeline panel.
        const auto rawFrames = pybridge::AseBridge::readTrajectory(
            path.toStdString(), formatHintFor(path).toStdString());
        if (rawFrames.empty())
            throw std::runtime_error("File contains no structures");

        if (rawFrames.size() == 1) {
            auto structure = std::make_shared<core::Structure>(rawFrames.front());
            const auto atomCount = structure->size();
            addDocument(std::move(structure), QFileInfo(path).fileName());
            statusBar()->showMessage(tr("Loaded %1 (%2 atoms)").arg(path).arg(atomCount));
            addRecentFile(path);
        } else {
            std::vector<std::shared_ptr<core::Structure>> frames;
            frames.reserve(rawFrames.size());
            for (const auto& frame : rawFrames)
                frames.push_back(std::make_shared<core::Structure>(frame));
            const auto frameCount = frames.size();
            addDocument(frames.front(), QFileInfo(path).fileName(), std::move(frames));
            statusBar()->showMessage(
                tr("Loaded %1 (%2 frames)").arg(path).arg(frameCount));
            addRecentFile(path);
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Open Structure"),
                              QString::fromUtf8(e.what()));
    }
}

void MainWindow::loadExample(const QString& resourcePath, const QString& recommendation)
{
    // Resource files need a real path for ase.io — stage them in temp
    // with their original name so the tab title stays meaningful.
    static QTemporaryDir stagingDir;
    if (!stagingDir.isValid())
        return;
    const QString target = stagingDir.filePath(QFileInfo(resourcePath).fileName());
    if (!QFile::exists(target))
        QFile::copy(resourcePath, target);
    loadFile(target);
    statusBar()->showMessage(
        tr("%1 — recommended potential: %2")
            .arg(QFileInfo(resourcePath).fileName(), recommendation),
        8000);
}

void MainWindow::openStructure()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open Structure(s)"), QString(),
        tr("Structure files (*.xyz *.extxyz *.cif POSCAR CONTCAR *.vasp *.traj "
           "*.in *.pwi *.pwo *.out *.cell *.data *.dump *.lammpstrj *.gjf *.com "
           "*.res);;"
           "Quantum ESPRESSO (*.in *.pwi *.pwo *.out);;"
           "CASTEP (*.cell);;"
           "LAMMPS (*.data *.dump *.lammpstrj);;"
           "Gaussian (*.gjf *.com);;"
           "SHELX (*.res);;"
           "All files (*)"));
    for (const QString& path : paths)
        loadFile(path);
}

void MainWindow::openTrajectory()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Trajectory"), QString(),
        tr("Trajectories (*.traj *.extxyz *.xyz);;All files (*)"));
    if (!path.isEmpty())
        loadFile(path); // multi-frame aware — activates the timeline
}

void MainWindow::showFrame(int index)
{
    Document* doc = currentDocument();
    if (!doc || index < 0 || index >= static_cast<int>(doc->frames.size()))
        return;
    doc->structure = doc->frames[static_cast<std::size_t>(index)];
    notifyStructureChanged(false);
}

void MainWindow::saveStructureAs()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || !ensureAseAvailable())
        return;
    // Filter -> explicit ASE format (empty = infer from extension).
    static const QList<QPair<QString, QString>> kSaveFormats = {
        {tr("XYZ (*.xyz)"), QString()},
        {tr("Extended XYZ (*.extxyz)"), QStringLiteral("extxyz")},
        {tr("CIF (*.cif)"), QStringLiteral("cif")},
        {tr("VASP POSCAR (*.vasp)"), QStringLiteral("vasp")},
        {tr("Quantum ESPRESSO input (*.pwi *.in)"), QStringLiteral("espresso-in")},
        {tr("LAMMPS data (*.data)"), QStringLiteral("lammps-data")},
        {tr("CASTEP cell (*.cell)"), QStringLiteral("castep-cell")},
        {tr("Gaussian input (*.com *.gjf)"), QStringLiteral("gaussian-in")},
        {tr("SHELX (*.res)"), QStringLiteral("res")},
    };
    QStringList filters;
    for (const auto& entry : kSaveFormats)
        filters << entry.first;

    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Structure As"), QString(), filters.join(QStringLiteral(";;")),
        &selectedFilter);
    if (path.isEmpty())
        return;
    QString format;
    for (const auto& entry : kSaveFormats)
        if (entry.first == selectedFilter)
            format = entry.second;
    try {
        pybridge::AseBridge::writeStructure(*doc->structure, path.toStdString(),
                                            format.toStdString());
        statusBar()->showMessage(tr("Saved %1").arg(path));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Save Structure"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::saveTrajectoryAs()
{
    Document* doc = currentDocument();
    if (!doc || doc->frames.size() < 2) {
        QMessageBox::information(
            this, tr("Save Trajectory"),
            tr("The current tab has no multi-frame trajectory.\n"
               "Open a trajectory (or generate one) first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    static const QList<QPair<QString, QString>> kTrajectoryFormats = {
        {tr("Extended XYZ trajectory (*.extxyz)"), QStringLiteral("extxyz")},
        {tr("XYZ multi-frame (*.xyz)"), QStringLiteral("xyz")},
        {tr("ASE trajectory (*.traj)"), QStringLiteral("traj")},
        {tr("PDB multi-model (*.pdb)"), QStringLiteral("proteindatabank")},
    };
    QStringList filters;
    for (const auto& entry : kTrajectoryFormats)
        filters << entry.first;

    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Trajectory As"), QStringLiteral("trajectory.extxyz"),
        filters.join(QStringLiteral(";;")), &selectedFilter);
    if (path.isEmpty())
        return;
    QString format;
    for (const auto& entry : kTrajectoryFormats)
        if (entry.first == selectedFilter)
            format = entry.second;

    try {
        pybridge::AseBridge::writeTrajectory(doc->frames, path.toStdString(),
                                             format.toStdString());
        statusBar()->showMessage(
            tr("Saved %1 (%2 frames)").arg(path).arg(doc->frames.size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Save Trajectory"), QString::fromUtf8(e.what()));
    }
}

// ---------------------------------------------------------------------------
// Project workspace persistence (.calproj)
// ---------------------------------------------------------------------------

namespace {

/// (step, value[, target]) series of one Job-panel metric tab.
QJsonObject metricToJson(const MetricPlotWidget* plot)
{
    QJsonObject metric;
    QJsonArray samples;
    for (const MetricPlotWidget::Sample& sample : plot->samples())
        samples.append(QJsonArray{sample.step, sample.value});
    metric[QStringLiteral("samples")] = samples;
    if (plot->hasTarget())
        metric[QStringLiteral("target")] = plot->targetValue();
    return metric;
}

void metricFromJson(const QJsonObject& metric, MetricPlotWidget* plot)
{
    plot->clear();
    std::vector<MetricPlotWidget::Sample> samples;
    const QJsonArray array = metric[QStringLiteral("samples")].toArray();
    samples.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& entry : array) {
        const QJsonArray pair = entry.toArray();
        if (pair.size() == 2)
            samples.push_back({pair[0].toInt(), pair[1].toDouble()});
    }
    plot->setSamples(std::move(samples));
    if (metric.contains(QStringLiteral("target")))
        plot->setTarget(metric[QStringLiteral("target")].toDouble());
}

} // namespace

bool MainWindow::writeProject(const QString& path)
{
    QJsonObject root;
    root[QStringLiteral("application")] = QStringLiteral("calango");
    root[QStringLiteral("fileType")] = QStringLiteral("project");
    root[QStringLiteral("formatVersion")] = ProjectSerializer::kFormatVersion;
    root[QStringLiteral("calangoVersion")] = QStringLiteral(CALANGO_VERSION);

    QJsonArray docs;
    for (const auto& document : documents_) {
        QJsonObject docJson;
        docJson[QStringLiteral("fileName")] = document->fileName;
        if (!document->frames.empty()) {
            // Trajectory tab: persist every frame; the displayed structure
            // is one of them, recorded by index instead of duplicated.
            QJsonArray frames;
            int currentFrame = -1;
            for (std::size_t i = 0; i < document->frames.size(); ++i) {
                if (document->frames[i] == document->structure)
                    currentFrame = static_cast<int>(i);
                frames.append(
                    ProjectSerializer::structureToJson(*document->frames[i]));
            }
            docJson[QStringLiteral("frames")] = frames;
            docJson[QStringLiteral("currentFrame")] = currentFrame;
            if (currentFrame < 0 && document->structure)
                docJson[QStringLiteral("structure")]
                    = ProjectSerializer::structureToJson(*document->structure);
        } else if (document->structure) {
            docJson[QStringLiteral("structure")]
                = ProjectSerializer::structureToJson(*document->structure);
        }
        docs.append(docJson);
    }
    root[QStringLiteral("documents")] = docs;
    root[QStringLiteral("activeTab")] = tabBar_->currentIndex();

    QJsonObject viewportJson;
    viewportJson[QStringLiteral("colorMode")]
        = static_cast<int>(viewport_->colorMode());
    viewportJson[QStringLiteral("gradient")]
        = static_cast<int>(viewport_->style().gradient);
    viewportJson[QStringLiteral("customField")] = viewport_->customScalarField();
    viewportJson[QStringLiteral("background")]
        = viewport_->backgroundColor().name();
    root[QStringLiteral("viewport")] = viewportJson;

    QJsonObject job;
    job[QStringLiteral("lastJobDir")] = lastJobDir_;
    job[QStringLiteral("log")] = jobLogWidget_->logText();
    job[QStringLiteral("energy")] = metricToJson(energyPlot_);
    job[QStringLiteral("temperature")] = metricToJson(temperaturePlot_);
    job[QStringLiteral("maxForce")] = metricToJson(forcePlot_);
    job[QStringLiteral("pressure")] = metricToJson(pressurePlot_);
    root[QStringLiteral("job")] = job;

    // QSaveFile: the previous project survives a failed / interrupted save.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Save Project"),
                              tr("Could not write %1:\n%2")
                                  .arg(path, file.errorString()));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        QMessageBox::critical(this, tr("Save Project"),
                              tr("Could not write %1:\n%2")
                                  .arg(path, file.errorString()));
        return false;
    }
    statusBar()->showMessage(tr("Project saved to %1 (%2 tab(s))")
                                 .arg(path)
                                 .arg(documents_.size()));
    return true;
}

bool MainWindow::readProject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Open Project"),
                              tr("Could not read %1:\n%2")
                                  .arg(path, file.errorString()));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = json.object();
    if (parseError.error != QJsonParseError::NoError
        || root[QStringLiteral("application")].toString()
            != QLatin1String("calango")
        || root[QStringLiteral("fileType")].toString() != QLatin1String("project")) {
        QMessageBox::critical(this, tr("Open Project"),
                              tr("%1 is not a Calango project file.").arg(path));
        return false;
    }
    if (root[QStringLiteral("formatVersion")].toInt()
        > ProjectSerializer::kFormatVersion) {
        QMessageBox::critical(
            this, tr("Open Project"),
            tr("%1 was saved by a newer Calango (project format %2, this build "
               "reads up to %3). Update Calango to open it.")
                .arg(path)
                .arg(root[QStringLiteral("formatVersion")].toInt())
                .arg(ProjectSerializer::kFormatVersion));
        return false;
    }

    closeAllDocuments();

    for (const auto& entry : root[QStringLiteral("documents")].toArray()) {
        const QJsonObject docJson = entry.toObject();
        std::vector<std::shared_ptr<core::Structure>> frames;
        for (const auto& frameJson : docJson[QStringLiteral("frames")].toArray())
            frames.push_back(
                ProjectSerializer::structureFromJson(frameJson.toObject()));

        const int currentFrame = docJson[QStringLiteral("currentFrame")].toInt(-1);
        std::shared_ptr<core::Structure> structure;
        if (currentFrame >= 0 && currentFrame < static_cast<int>(frames.size()))
            structure = frames[static_cast<std::size_t>(currentFrame)];
        else if (docJson.contains(QStringLiteral("structure")))
            structure = ProjectSerializer::structureFromJson(
                docJson[QStringLiteral("structure")].toObject());
        else if (!frames.empty())
            structure = frames.front();
        else
            structure = std::make_shared<core::Structure>();

        addDocument(std::move(structure),
                    docJson[QStringLiteral("fileName")].toString(tr("Untitled")),
                    std::move(frames));
    }

    const int activeTab = root[QStringLiteral("activeTab")].toInt(0);
    if (activeTab >= 0 && activeTab < tabBar_->count())
        tabBar_->setCurrentIndex(activeTab);

    // Color mapping / viewport state — restore before the panels sync.
    const QJsonObject viewportJson = root[QStringLiteral("viewport")].toObject();
    if (!viewportJson.isEmpty()) {
        if (const QColor background(
                viewportJson[QStringLiteral("background")].toString());
            background.isValid())
            viewport_->setBackgroundColor(background);
        const int gradient = viewportJson[QStringLiteral("gradient")].toInt(0);
        if (gradient >= 0 && gradient <= static_cast<int>(render::ColorGradient::Afmhot))
            viewport_->setColorGradient(static_cast<render::ColorGradient>(gradient));
        const int colorMode = viewportJson[QStringLiteral("colorMode")].toInt(0);
        if (colorMode >= 0
            && colorMode <= static_cast<int>(render::ColorMode::CustomScalar))
            viewport_->setColorMode(
                static_cast<render::ColorMode>(colorMode),
                viewportJson[QStringLiteral("customField")].toString());
    }

    // Job console + metric series of the last (unexported) run.
    const QJsonObject job = root[QStringLiteral("job")].toObject();
    if (!job.isEmpty()) {
        lastJobDir_ = job[QStringLiteral("lastJobDir")].toString();
        jobLogWidget_->restoreLog(job[QStringLiteral("log")].toString());
        metricFromJson(job[QStringLiteral("energy")].toObject(), energyPlot_);
        metricFromJson(job[QStringLiteral("temperature")].toObject(),
                       temperaturePlot_);
        metricFromJson(job[QStringLiteral("maxForce")].toObject(), forcePlot_);
        metricFromJson(job[QStringLiteral("pressure")].toObject(), pressurePlot_);
    }

    statusBar()->showMessage(tr("Project %1 restored (%2 tab(s))")
                                 .arg(path)
                                 .arg(documents_.size()));
    isDirty_ = false; // freshly restored — matches the file on disk
    return true;
}

void MainWindow::closeAllDocuments()
{
    documents_.clear();
    while (tabBar_->count() > 0)
        tabBar_->removeTab(0); // currentChanged fires; views reset gracefully
}

void MainWindow::openProject()
{
    if (jobRunner_->isRunning()) {
        QMessageBox::information(this, tr("Open Project"),
                                 tr("A calculation is running — kill it before "
                                    "switching projects."));
        return;
    }
    if (!documents_.empty()
        && QMessageBox::question(
               this, tr("Open Project"),
               tr("Opening a project replaces the current workspace "
                  "(%n open tab(s)). Continue?",
                  nullptr, static_cast<int>(documents_.size())))
            != QMessageBox::Yes)
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), QString(),
        tr("Calango project (*.calproj);;All files (*)"));
    if (path.isEmpty())
        return;
    if (readProject(path))
        projectPath_ = path;
}

bool MainWindow::saveProject()
{
    if (projectPath_.isEmpty())
        return saveProjectAs();
    const bool ok = writeProject(projectPath_);
    if (ok)
        isDirty_ = false;
    return ok;
}

bool MainWindow::saveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project As"), QStringLiteral("workspace.calproj"),
        tr("Calango project (*.calproj)"));
    if (path.isEmpty())
        return false; // user cancelled
    if (!path.endsWith(QStringLiteral(".calproj"), Qt::CaseInsensitive))
        path += QStringLiteral(".calproj");
    if (!writeProject(path))
        return false;
    projectPath_ = path;
    isDirty_ = false;
    return true;
}

// ---------------------------------------------------------------------------
// Image & animation export
// ---------------------------------------------------------------------------

namespace {

/// Attaches a resolution-preset combo (720p / 1080p / 4K / Custom) to a
/// width/height spinbox pair: picking a preset writes the spins, editing
/// a spin manually flips the combo back to Custom.
QComboBox* makeResolutionPresetCombo(QDialog* dialog, QSpinBox* widthSpin,
                                     QSpinBox* heightSpin)
{
    struct Preset {
        const char* label;
        int width;
        int height;
    };
    static constexpr Preset kPresets[] = {
        {"720p (1280 × 720)", 1280, 720},
        {"1080p (1920 × 1080)", 1920, 1080},
        {"4K UHD (3840 × 2160)", 3840, 2160},
    };

    auto* combo = new QComboBox(dialog);
    for (const Preset& preset : kPresets)
        combo->addItem(QObject::tr(preset.label), QSize(preset.width, preset.height));
    combo->addItem(QObject::tr("Custom"), QSize());
    combo->setCurrentIndex(combo->count() - 1); // dialogs open with custom sizes

    QObject::connect(combo, &QComboBox::currentIndexChanged, dialog,
                     [combo, widthSpin, heightSpin](int index) {
                         const QSize size = combo->itemData(index).toSize();
                         if (!size.isValid() || size.isEmpty())
                             return; // Custom: leave the spins alone
                         const QSignalBlocker blockWidth(widthSpin);
                         const QSignalBlocker blockHeight(heightSpin);
                         widthSpin->setValue(size.width());
                         heightSpin->setValue(size.height());
                     });
    const auto toCustom = [combo] {
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(combo->count() - 1);
    };
    QObject::connect(widthSpin, &QSpinBox::valueChanged, dialog, toCustom);
    QObject::connect(heightSpin, &QSpinBox::valueChanged, dialog, toCustom);
    return combo;
}

} // namespace

void MainWindow::exportImage()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export Image"));
    auto* form = new QFormLayout(&dialog);

    auto* widthSpin = new QSpinBox(&dialog);
    widthSpin->setRange(64, 8192);
    widthSpin->setValue(viewport_->width() * 2); // 2x viewport = crisp default
    auto* heightSpin = new QSpinBox(&dialog);
    heightSpin->setRange(64, 8192);
    heightSpin->setValue(viewport_->height() * 2);
    form->addRow(tr("Resolution preset:"),
                 makeResolutionPresetCombo(&dialog, widthSpin, heightSpin));
    form->addRow(tr("Width (px):"), widthSpin);
    form->addRow(tr("Height (px):"), heightSpin);

    auto* backgroundCombo = new QComboBox(&dialog);
    backgroundCombo->addItems({tr("Transparent (PNG only)"), tr("Solid white"),
                               tr("Viewport color")});
    form->addRow(tr("Background:"), backgroundCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Image"), QStringLiteral("calango.png"),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;

    const bool transparent = backgroundCombo->currentIndex() == 0;
    const QColor background = transparent ? QColor(0, 0, 0, 0)
        : backgroundCombo->currentIndex() == 1 ? QColor(Qt::white)
                                               : viewport_->backgroundColor();

    QImage image =
        viewport_->renderToImage(widthSpin->value(), heightSpin->value(), background);

    const bool isJpeg = path.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
        || path.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive);
    if (isJpeg && transparent) {
        // JPEG has no alpha channel — composite over white instead.
        QImage flattened(image.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
        QPainter painter(&flattened);
        painter.drawImage(0, 0, image);
        painter.end();
        image = flattened;
    }

    QImageWriter writer(path);
    if (!writer.write(image)) {
        QMessageBox::critical(this, tr("Export Image"),
                              tr("Could not write %1:\n%2").arg(path, writer.errorString()));
        return;
    }
    statusBar()->showMessage(tr("Exported %1 (%2×%3)")
                                 .arg(path)
                                 .arg(image.width())
                                 .arg(image.height()));
}

void MainWindow::exportAnimation()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export Animation"));
    auto* form = new QFormLayout(&dialog);

    auto* sourceCombo = new QComboBox(&dialog);
    sourceCombo->addItem(tr("Turntable rotation (360°)"));
    const bool hasTrajectory = doc->frames.size() > 1;
    if (hasTrajectory)
        sourceCombo->addItem(tr("Trajectory frames (%1)").arg(doc->frames.size()));
    form->addRow(tr("Source:"), sourceCombo);

    auto* framesSpin = new QSpinBox(&dialog);
    framesSpin->setRange(8, 360);
    framesSpin->setValue(72);
    form->addRow(tr("Rotation frames:"), framesSpin);
    connect(sourceCombo, &QComboBox::currentIndexChanged, framesSpin,
            [framesSpin](int index) { framesSpin->setEnabled(index == 0); });

    auto* widthSpin = new QSpinBox(&dialog);
    widthSpin->setRange(64, 4096); // up to 4K UHD
    widthSpin->setSingleStep(2); // H.264 yuv420p wants even dimensions
    widthSpin->setValue(640);
    auto* heightSpin = new QSpinBox(&dialog);
    heightSpin->setRange(64, 4096);
    heightSpin->setSingleStep(2);
    heightSpin->setValue(480);
    form->addRow(tr("Resolution preset:"),
                 makeResolutionPresetCombo(&dialog, widthSpin, heightSpin));
    form->addRow(tr("Width (px):"), widthSpin);
    form->addRow(tr("Height (px):"), heightSpin);

    auto* fpsSpin = new QSpinBox(&dialog);
    fpsSpin->setRange(1, 60);
    fpsSpin->setValue(24);
    form->addRow(tr("Frames per second:"), fpsSpin);

    auto* backgroundCombo = new QComboBox(&dialog);
    backgroundCombo->addItems({tr("Solid white"), tr("Viewport color"),
                               tr("Custom color…"), tr("Transparent (GIF only)")});
    form->addRow(tr("Background:"), backgroundCombo);
    QColor customBackground = Qt::white;
    connect(backgroundCombo, &QComboBox::currentIndexChanged, &dialog,
            [this, &customBackground](int index) {
                if (index != 2)
                    return;
                const QColor chosen = QColorDialog::getColor(
                    customBackground, this, tr("Animation Background Color"));
                if (chosen.isValid())
                    customBackground = chosen;
            });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Animation"), QStringLiteral("calango.gif"),
        tr("GIF animation (*.gif);;MP4 video (*.mp4)"));
    if (path.isEmpty())
        return;
    const bool isMp4 = path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive);

    bool transparent = backgroundCombo->currentIndex() == 3;
    if (transparent && isMp4) {
        QMessageBox::information(this, tr("Export Animation"),
                                 tr("MP4 has no alpha channel — using a solid white "
                                    "background instead."));
        transparent = false;
        backgroundCombo->setCurrentIndex(0);
    }
    const QColor background = transparent ? QColor(0, 0, 0, 0)
        : backgroundCombo->currentIndex() == 1  ? viewport_->backgroundColor()
        : backgroundCombo->currentIndex() == 2  ? customBackground
                                                : QColor(Qt::white);

    const int width = widthSpin->value() & ~1;
    const int height = heightSpin->value() & ~1;
    const bool turntable = sourceCombo->currentIndex() == 0;
    const int frameCount =
        turntable ? framesSpin->value() : static_cast<int>(doc->frames.size());

    QProgressDialog progress(tr("Rendering frames…"), tr("Cancel"), 0, frameCount, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    std::vector<QImage> images;
    images.reserve(static_cast<std::size_t>(frameCount));
    const int restoreFrame = hasTrajectory ? timeline_->currentFrame() : 0;

    for (int i = 0; i < frameCount; ++i) {
        progress.setValue(i);
        QApplication::processEvents();
        if (progress.wasCanceled())
            break;

        if (turntable) {
            images.push_back(viewport_->renderToImage(
                width, height, background,
                360.0f * static_cast<float>(i) / static_cast<float>(frameCount)));
        } else {
            viewport_->setStructure(doc->frames[static_cast<std::size_t>(i)], false);
            images.push_back(viewport_->renderToImage(width, height, background));
        }
    }

    if (!turntable)
        showFrame(restoreFrame); // put the live view back where it was
    if (progress.wasCanceled())
        return;
    progress.setValue(frameCount);

    try {
        if (isMp4)
            pybridge::AnimationExporter::exportMp4(images, path, fpsSpin->value());
        else
            pybridge::AnimationExporter::exportGif(images, path, fpsSpin->value(),
                                                   transparent);
        statusBar()->showMessage(tr("Exported %1 (%2 frames)").arg(path).arg(images.size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Export Animation"), QString::fromUtf8(e.what()));
    }
}

// ---------------------------------------------------------------------------
// Builder tools
// ---------------------------------------------------------------------------

void MainWindow::createSupercell()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Create Supercell"));
    auto* form = new QFormLayout(&dialog);
    QSpinBox* repeats[3];
    const char* axes[3] = {"a", "b", "c"};
    for (int i = 0; i < 3; ++i) {
        repeats[i] = new QSpinBox(&dialog);
        repeats[i]->setRange(1, 20);
        repeats[i]->setValue(i == 0 ? 2 : 1);
        form->addRow(tr("Repeat along %1:").arg(QLatin1String(axes[i])), repeats[i]);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    try {
        auto repeated = std::make_shared<core::Structure>(pybridge::AseBridge::makeSupercell(
            *doc->structure, repeats[0]->value(), repeats[1]->value(),
            repeats[2]->value()));
        pushUndo();
        replaceCurrentStructure(std::move(repeated),
                                tr("%1 (supercell)").arg(doc->fileName));
        statusBar()->showMessage(
            tr("Supercell created: %1 atoms").arg(doc->structure->size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Create Supercell"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::openSupercellBuilder()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Supercell"),
            tr("Open a periodic structure with a defined unit cell first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    SupercellDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    int p[3][3];
    dialog.matrix(p);
    try {
        auto transformed = std::make_shared<core::Structure>(
            pybridge::AseBridge::makeSupercellMatrix(*doc->structure, p));
        pushUndo();
        replaceCurrentStructure(std::move(transformed),
                                tr("%1 (supercell)").arg(doc->fileName));
        statusBar()->showMessage(
            tr("Supercell created: %1 atoms").arg(doc->structure->size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Supercell"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::cleaveSurface()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || !doc->structure->cell().isDefined()) {
        QMessageBox::information(this, tr("Surface Slab"),
                                 tr("Open a bulk structure with a unit cell first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    // Three-stage wizard: orientation (draggable in-plane vectors) →
    // cut / terminations (cross-section) → vacuum + 3D preview.
    SlabWizard wizard(doc->structure, this);
    if (wizard.exec() != QDialog::Accepted || !wizard.result())
        return;

    // The slab opens in its own tab — the bulk structure the wizard was
    // fed stays untouched in the original tab (with its undo history).
    const auto atomCount = wizard.result()->size();
    addDocument(wizard.result(),
                tr("%1 %2").arg(doc->fileName, wizard.resultLabel()));
    statusBar()->showMessage(tr("Slab created in a new tab: %1 atoms").arg(atomCount));
}

// ---------------------------------------------------------------------------
// Editing tools
// ---------------------------------------------------------------------------

void MainWindow::addAtom()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Atom"));
    auto* form = new QFormLayout(&dialog);

    // Element choice via the graphical periodic table.
    int selectedZ = 6; // carbon default
    auto* elementButton = new QPushButton(&dialog);
    const auto updateElementButton = [&selectedZ, elementButton] {
        elementButton->setText(
            QStringLiteral("%1  (Z = %2)")
                .arg(QLatin1String(core::Elements::data(selectedZ).symbol))
                .arg(selectedZ));
    };
    updateElementButton();
    form->addRow(tr("Element:"), elementButton);
    connect(elementButton, &QPushButton::clicked, &dialog,
            [&dialog, &selectedZ, updateElementButton] {
                if (const int z = PeriodicTableDialog::pickElement(&dialog, selectedZ)) {
                    selectedZ = z;
                    updateElementButton();
                }
            });

    QDoubleSpinBox* coords[3];
    const char* names[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        coords[i] = new QDoubleSpinBox(&dialog);
        coords[i]->setRange(-1000.0, 1000.0);
        coords[i]->setDecimals(4);
        coords[i]->setSuffix(tr(" Å"));
        form->addRow(QStringLiteral("%1:").arg(QLatin1String(names[i])), coords[i]);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    Document& doc = ensureDocument();
    pushUndo();
    const bool firstAtom = !doc.structure || doc.structure->empty();
    if (!doc.structure)
        doc.structure = std::make_shared<core::Structure>();
    doc.structure->addAtom(
        {selectedZ, {coords[0]->value(), coords[1]->value(), coords[2]->value()}});
    notifyStructureChanged(firstAtom);
    statusBar()->showMessage(
        tr("Added %1 atom").arg(QLatin1String(core::Elements::data(selectedZ).symbol)));
}

void MainWindow::changeElementOfSelection()
{
    Document* doc = currentDocument();
    const auto& selection = viewport_->selection();
    if (!doc || !doc->structure || selection.empty()) {
        statusBar()->showMessage(tr("Select atoms first (click / Ctrl+click)."));
        return;
    }

    // Highlight the current element when the selection is homogeneous.
    const int firstZ = doc->structure
                           ->atoms()[static_cast<std::size_t>(*selection.begin())]
                           .atomicNumber;
    const int z = PeriodicTableDialog::pickElement(this, firstZ);
    if (z == 0)
        return;

    pushUndo();
    for (const int index : selection)
        doc->structure->atoms()[static_cast<std::size_t>(index)].atomicNumber = z;
    notifyStructureChanged(false);
    statusBar()->showMessage(
        tr("Changed %n atom(s) to %1", nullptr, static_cast<int>(selection.size()))
            .arg(QLatin1String(core::Elements::data(z).symbol)));
}

void MainWindow::translateSelection()
{
    Document* doc = currentDocument();
    const auto& selection = viewport_->selection();
    if (!doc || !doc->structure || selection.empty()) {
        statusBar()->showMessage(tr("Select atoms first (click / Ctrl+click)."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Translate Selection"));
    auto* form = new QFormLayout(&dialog);
    QDoubleSpinBox* delta[3];
    const char* names[3] = {"Δx", "Δy", "Δz"};
    for (int i = 0; i < 3; ++i) {
        delta[i] = new QDoubleSpinBox(&dialog);
        delta[i]->setRange(-100.0, 100.0);
        delta[i]->setDecimals(4);
        delta[i]->setSuffix(tr(" Å"));
        form->addRow(QString::fromUtf8(names[i]) + QStringLiteral(":"), delta[i]);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    pushUndo();
    const core::Vec3 shift{delta[0]->value(), delta[1]->value(), delta[2]->value()};
    for (const int index : selection)
        doc->structure->atoms()[static_cast<std::size_t>(index)].position += shift;
    notifyStructureChanged(false);
}

void MainWindow::deleteSelectedAtoms()
{
    Document* doc = currentDocument();
    const auto& selection = viewport_->selection();
    if (!doc || !doc->structure || selection.empty()) {
        statusBar()->showMessage(tr("Select atoms first (click / Ctrl+click)."));
        return;
    }

    pushUndo();
    // Remove in descending index order so indices stay valid.
    std::vector<int> indices(selection.begin(), selection.end());
    std::sort(indices.rbegin(), indices.rend());
    for (const int index : indices)
        doc->structure->removeAtom(static_cast<std::size_t>(index));
    notifyStructureChanged(false);
    statusBar()->showMessage(tr("Deleted %n atom(s)", nullptr,
                                static_cast<int>(indices.size())));
}

void MainWindow::assignBondOrderToSelection(int order)
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure)
        return;
    const auto& selection = viewport_->selection();
    if (selection.size() != 2)
        return; // the panel's buttons are disabled otherwise

    pushUndo();
    const int i = *selection.begin();
    const int j = *std::next(selection.begin());
    doc->structure->setBondOrder(i, j, order);
    // A multiple bond the user asked for should be visible even when the
    // pair sits outside the auto-detection cutoff.
    if (order > 1)
        doc->structure->addBondOverride(i, j);
    notifyStructureChanged(false);
    statusBar()->showMessage(tr("Bond %1–%2 set to order %3")
                                 .arg(i)
                                 .arg(j)
                                 .arg(order));
}

void MainWindow::showBondEditor()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    // One undo snapshot per editing session (the dialog applies live).
    pushUndo();
    BondEditorDialog dialog(doc->structure, viewport_, this);
    connect(&dialog, &BondEditorDialog::bondsEdited, this,
            [this] { notifyStructureChanged(false); });
    dialog.exec();
}

void MainWindow::showPreferences()
{
    PreferencesDialog dialog(this);
    dialog.exec();
}

// ---------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------

void MainWindow::showBrillouinZone()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || !doc->structure->cell().isDefined()) {
        QMessageBox::information(this, tr("Brillouin Zone"),
                                 tr("Open a periodic structure with a unit cell first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    try {
        const auto zone = core::computeBrillouinZone(doc->structure->cell());
        const auto bandPath = pybridge::AseBridge::bandPathInfo(*doc->structure);
        BrillouinZoneDialog dialog(zone, bandPath, this);
        dialog.exec();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Brillouin Zone"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::showRdf()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Radial Distribution Function"),
                                 tr("Open a structure first."));
        return;
    }
    RdfDialog dialog(doc->structure, doc->frames, this);
    dialog.exec();
}

void MainWindow::showDistributions()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Bond Distributions"),
                                 tr("Open a structure first."));
        return;
    }
    DistributionDialog dialog(doc->structure, this);
    dialog.exec();
}

void MainWindow::showStructureFactor()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Structure Factor"),
                                 tr("Open a structure first."));
        return;
    }
    StructureFactorDialog dialog(doc->structure, doc->frames, this);
    dialog.exec();
}

void MainWindow::showXrd()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("X-Ray Diffraction"),
                                 tr("Open a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    XrdDialog dialog(doc->structure, this);
    dialog.exec();
}

void MainWindow::showBandStructure()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(this, tr("Electronic Bands / PDOS"),
                                 tr("Open a periodic structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    if (jobRunner_->isRunning()) {
        QMessageBox::information(this, tr("Electronic Bands / PDOS"),
                                 tr("A calculation is already running — kill it first."));
        return;
    }

    // Suggested path from ASE's Bravais-lattice detection.
    QString suggestedPath;
    try {
        suggestedPath = QString::fromStdString(
            pybridge::AseBridge::bandPathInfo(*doc->structure).suggestedPath);
    } catch (const std::exception&) {
        suggestedPath.clear();
    }
    const bool gpawAvailable = [] {
        try {
            pybind11::module_::import("gpaw");
            return true;
        } catch (const pybind11::error_already_set&) {
            return false;
        }
    }();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Electronic Bands / PDOS"));
    auto* form = new QFormLayout(&dialog);
    auto* backendCombo = new QComboBox(&dialog);
    backendCombo->addItem(tr("Free electrons (ASE — bands only)"));
    backendCombo->addItem(gpawAvailable
                              ? tr("GPAW (DFT, bands + PDOS)")
                              : tr("GPAW (not installed)"));
    backendCombo->addItem(tr("Quantum ESPRESSO (needs pw.x + pseudos)"));
    if (!gpawAvailable) {
        auto* model = qobject_cast<QStandardItemModel*>(backendCombo->model());
        if (model)
            model->item(1)->setEnabled(false);
    }
    form->addRow(tr("Backend:"), backendCombo);
    auto* pathEdit = new QLineEdit(suggestedPath, &dialog);
    pathEdit->setPlaceholderText(tr("empty = ASE suggestion"));
    form->addRow(tr("k-path:"), pathEdit);
    auto* npointsSpin = new QSpinBox(&dialog);
    npointsSpin->setRange(20, 2000);
    npointsSpin->setValue(80);
    form->addRow(tr("k-points:"), npointsSpin);
    auto* valenceSpin = new QSpinBox(&dialog);
    valenceSpin->setRange(1, 200);
    valenceSpin->setValue(4);
    valenceSpin->setToolTip(tr("Electrons per cell (free-electron backend)"));
    form->addRow(tr("Valence electrons:"), valenceSpin);
    auto* ecutSpin = new QDoubleSpinBox(&dialog);
    ecutSpin->setRange(50.0, 2000.0);
    ecutSpin->setValue(340.0);
    ecutSpin->setSuffix(QStringLiteral(" eV"));
    form->addRow(tr("PW cutoff:"), ecutSpin);
    auto* kgridSpin = new QSpinBox(&dialog);
    kgridSpin->setRange(1, 16);
    kgridSpin->setValue(4);
    form->addRow(tr("SCF k-grid (n³):"), kgridSpin);
    auto* pdosCheck = new QCheckBox(tr("Compute element/orbital PDOS "
                                       "(GPAW backend)"),
                                    &dialog);
    pdosCheck->setChecked(true);
    form->addRow(pdosCheck);

    // Execution environment: solvers like GPAW/QE/SIESTA usually live in
    // dedicated conda envs — same selector as the ASE input generator.
    const auto kBandsEnvKey = QStringLiteral("jobs/bandsEnvironmentPath");
    auto* envRow = new QWidget(&dialog);
    auto* envLayout = new QHBoxLayout(envRow);
    envLayout->setContentsMargins(0, 0, 0, 0);
    auto* envEdit = new QLineEdit(QSettings().value(kBandsEnvKey).toString(),
                                  envRow);
    envEdit->setPlaceholderText(
        tr("conda env folder or python executable (empty = embedded)"));
    auto* envDirButton = new QPushButton(tr("Env Folder…"), envRow);
    auto* envFileButton = new QPushButton(tr("Python…"), envRow);
    envLayout->addWidget(envEdit, 1);
    envLayout->addWidget(envDirButton);
    envLayout->addWidget(envFileButton);
    form->addRow(tr("Environment:"), envRow);
    auto* envStatus = new QLabel(&dialog);
    envStatus->setWordWrap(true);
    form->addRow(QString(), envStatus);
    const auto updateEnvStatus = [envEdit, envStatus] {
        const QString text = envEdit->text().trimmed();
        if (text.isEmpty()) {
            envStatus->setText(
                QObject::tr("Using embedded interpreter: %1")
                    .arg(QString::fromStdString(
                        pybridge::PythonEngine::instance().executable())));
            envStatus->setStyleSheet(QString());
        } else if (const QString python =
                       CalculatorDialog::resolveEnvironmentPython(text);
                   !python.isEmpty()) {
            envStatus->setText(
                QObject::tr("Runs will use: %1").arg(python));
            envStatus->setStyleSheet(QString());
        } else {
            envStatus->setText(
                QObject::tr("No python interpreter found at this path."));
            envStatus->setStyleSheet(QStringLiteral("color: #d9534f;"));
        }
    };
    connect(envEdit, &QLineEdit::textChanged, &dialog, updateEnvStatus);
    connect(envDirButton, &QPushButton::clicked, &dialog, [envEdit, &dialog] {
        const QString dir = QFileDialog::getExistingDirectory(
            &dialog, tr("Select Conda Environment Folder"));
        if (!dir.isEmpty())
            envEdit->setText(dir);
    });
    connect(envFileButton, &QPushButton::clicked, &dialog, [envEdit, &dialog] {
        const QString file = QFileDialog::getOpenFileName(
            &dialog, tr("Select Python Interpreter"));
        if (!file.isEmpty())
            envEdit->setText(file);
    });
    updateEnvStatus();

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QSettings().setValue(kBandsEnvKey, envEdit->text());

    core::ElectronicConfig config;
    config.backend = backendCombo->currentIndex() == 1
        ? core::ElectronicBackend::Gpaw
        : backendCombo->currentIndex() == 2 ? core::ElectronicBackend::Espresso
                                            : core::ElectronicBackend::FreeElectrons;
    config.kpath = pathEdit->text().trimmed().remove(QLatin1Char(',')).toStdString();
    config.npoints = npointsSpin->value();
    config.nvalence = valenceSpin->value();
    config.ecutEv = ecutSpin->value();
    config.scfKpts = kgridSpin->value();
    config.pdos = pdosCheck->isChecked();

    QString python =
        CalculatorDialog::resolveEnvironmentPython(envEdit->text());
    if (python.isEmpty())
        python = QString::fromStdString(
            pybridge::PythonEngine::instance().executable());
    runScript(QString::fromStdString(core::generateElectronicScript(config)),
              python, tr("Band structure"), /*expectFrames=*/false);
}

void MainWindow::openBandResults(const QString& directory)
{
    auto* window = new BandPdosWindow(directory, this);
    if (!window->hasData()) {
        delete window;
        QMessageBox::information(this, tr("Band Structure"),
                                 tr("No bands.json found in %1").arg(directory));
        return;
    }
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

void MainWindow::openPhononResults(const QString& directory)
{
    auto* window = new PhononPlotWindow(directory, this);
    if (!window->hasData()) {
        delete window;
        QMessageBox::information(
            this, tr("Phonon Band Structure"),
            tr("No phonon_band.json found in %1").arg(directory));
        return;
    }
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

void MainWindow::onDeleteProcessRequested(int id)
{
    const auto it = processRecords_.find(id);
    const QString label = it != processRecords_.end() ? it->second.label
                                                       : tr("this process");
    const bool running = id == currentTaskId_;

    const auto choice = QMessageBox::question(
        this, tr("Delete Process"),
        running
            ? tr("Process #%1 (%2) is still running. Stop it and permanently "
                 "delete its data folder?").arg(id).arg(label)
            : tr("Permanently delete process #%1 (%2) and its data folder?")
                  .arg(id).arg(label),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes)
        return;

    // Stop the subprocess first so it releases the directory before we purge.
    if (running) {
        jobRunner_->terminate();
        currentTaskId_ = -1; // its finish must not write metrics back
        liveDoc_ = nullptr;
    }

    // Purge the managed .calango_tmp/proc_<id>/ folder.
    QString directory = it != processRecords_.end() ? it->second.directory
                                                    : QString();
    if (!directory.isEmpty())
        QDir(directory).removeRecursively();

    processRecords_.erase(id);

    // Drop the selector entry; if it was showing, fall back to another process
    // or clear the tabs when none remain.
    if (processSelector_) {
        const int comboIndex = processSelector_->findData(id);
        if (comboIndex >= 0) {
            const QSignalBlocker block(processSelector_);
            processSelector_->removeItem(comboIndex);
        }
        if (processSelector_->count() == 0) {
            selectedProcessId_ = -1;
            energyPlot_->clear();
            temperaturePlot_->clear();
            forcePlot_->clear();
            pressurePlot_->clear();
            jobLogWidget_->restoreLog(QString());
        } else if (selectedProcessId_ == id) {
            processSelector_->setCurrentIndex(processSelector_->count() - 1);
            onProcessSelected(processSelector_->currentIndex());
        }
    }

    processPanel_->removeTask(id);
    statusBar()->showMessage(tr("Deleted process #%1").arg(id));
}

void MainWindow::onProcessResultRequested(const QString& directory)
{
    if (QFile::exists(directory + QStringLiteral("/bands.json"))) {
        openBandResults(directory);
        return;
    }
    if (QFile::exists(directory + QStringLiteral("/phonon_band.json"))) {
        openPhononResults(directory);
        return;
    }
    for (const auto* candidate :
         {"md.traj", "opt.traj", "optimized.extxyz", "md_final.extxyz",
          "perturbed.extxyz"}) {
        const QString path = directory + QLatin1Char('/')
            + QLatin1String(candidate);
        if (QFile::exists(path)) {
            loadFile(path);
            return;
        }
    }
    statusBar()->showMessage(
        tr("No loadable result in %1 yet — try Open Folder").arg(directory));
}

void MainWindow::newProject()
{
    if (isDirty_) {
        const auto choice = QMessageBox::warning(
            this, tr("New Workspace"),
            tr("The project has unsaved changes.\n"
               "Save them before starting a new workspace?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Cancel
            || (choice == QMessageBox::Save && !saveProject()))
            return;
    }
    closeAllDocuments();
    projectPath_.clear();
    isDirty_ = false;
    statusBar()->showMessage(tr("New workspace"));
}

void MainWindow::showVisualEffects()
{
    // Modeless so the scene can be orbited while tuning the sliders.
    auto* dialog = new VisualEffectsDialog(viewport_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::showVolumetricData()
{
    VolumetricDialog dialog(this);
    dialog.exec();
}

void MainWindow::showDatasetManager()
{
    if (!ensureAseAvailable())
        return;
    DatasetManagerDialog dialog(this);
    dialog.exec();
}

void MainWindow::showRamanModes()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Raman Modes"),
                                 tr("Open a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    RamanDialog dialog(doc->structure, this);
    dialog.exec();
}

void MainWindow::showWarrenCowley()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Warren-Cowley analysis"),
                                 tr("Open a structure first."));
        return;
    }
    WarrenCowleyDialog dialog(doc->structure, this);
    dialog.exec();
}

void MainWindow::showLocalEntropy()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Local Entropy Analysis"),
                                 tr("Open a structure first."));
        return;
    }
    LocalEntropyDialog dialog(doc->structure, viewport_, this);
    // The computed field must reach every view (property combo, info
    // panel) — same refresh path as the Bond Editor.
    connect(&dialog, &LocalEntropyDialog::fieldStored, this,
            [this] { notifyStructureChanged(false); });
    dialog.exec();
}

void MainWindow::openNanoparticleBuilder()
{
    if (!ensureAseAvailable())
        return;
    NanoparticleDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
        return;
    const int tab = addDocument(
        std::make_shared<core::Structure>(*dialog.result()),
        dialog.resultName());
    tabBar_->setCurrentIndex(tab);
    statusBar()->showMessage(tr("%1 generated").arg(dialog.resultName()));
}

void MainWindow::showAdsorption()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(
            this, tr("Adsorption & Catalysis"),
            tr("Open a surface first — a periodic slab (e.g. from the Surface "
               "Slab wizard) or a nanoparticle/cluster."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    AdsorptionDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted || dialog.outputs().empty())
        return;
    int lastTab = -1;
    for (const auto& output : dialog.outputs())
        lastTab = addDocument(output.structure, output.name);
    tabBar_->setCurrentIndex(lastTab);
    statusBar()->showMessage(
        tr("%n adsorption structure(s) generated", nullptr,
           static_cast<int>(dialog.outputs().size())));
}

void MainWindow::openSqsBuilder()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Special Quasirandom Structure"),
                                 tr("Open or build a base structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    SqsDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
        return;

    const auto& generated = *dialog.result();
    const int tab = addDocument(
        std::make_shared<core::Structure>(generated.structure),
        tr("SQS (%1)").arg(QString::fromStdString(generated.method)));
    tabBar_->setCurrentIndex(tab);
    statusBar()->showMessage(
        generated.method == "icet"
            ? tr("SQS generated with icet")
            : tr("SQS generated (internal annealer, residual Σα² = %1)")
                  .arg(generated.objective, 0, 'f', 4));
}

void MainWindow::openClusterExpansion()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Cluster Expansion"),
            tr("Open a periodic parent structure with a defined unit cell first."));
        return;
    }

    ClusterExpansionDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
        return;

    const auto& res = *dialog.result();
    // Present the whole inequivalent ensemble as one scrubbable multi-frame
    // document (Save Trajectory As… exports it for the Dataset Manager).
    std::vector<std::shared_ptr<core::Structure>> frames;
    frames.reserve(res.configs.size());
    for (const auto& cfg : res.configs)
        frames.push_back(std::make_shared<core::Structure>(cfg.structure));

    const int tab = addDocument(frames.front(),
                                tr("Cluster Expansion (%1 configs)")
                                    .arg(res.configs.size()),
                                frames);
    tabBar_->setCurrentIndex(tab);

    int pairO = 0, tripO = 0, quadO = 0;
    for (const auto& o : res.orbits) {
        if (o.order == 2) ++pairO;
        else if (o.order == 3) ++tripO;
        else if (o.order == 4) ++quadO;
    }
    statusBar()->showMessage(
        tr("Cluster Expansion: %1 inequivalent configs from %2 decorations "
           "(%3 active sites; orbits %4 pair / %5 triplet / %6 quad)%7")
            .arg(res.configs.size())
            .arg(res.enumerated)
            .arg(res.activeSites)
            .arg(pairO).arg(tripO).arg(quadO)
            .arg(res.sampled ? tr(" — occupation space sub-sampled") : QString()));
}

void MainWindow::showCoordination()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Coordination Numbers"),
                                 tr("Open a structure first."));
        return;
    }
    CoordinationDialog dialog(doc->structure, viewport_, this);
    dialog.exec();
}

void MainWindow::showSymmetry()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()
        || !doc->structure->cell().isDefined()) {
        QMessageBox::information(
            this, tr("Detect Symmetry"),
            tr("Open a periodic structure with a defined unit cell first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    SymmetryDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
        return;
    // A standardize / primitive transform: present the result in a new tab.
    const int tab = addDocument(
        std::make_shared<core::Structure>(*dialog.result()),
        tr("%1 (%2)").arg(doc->fileName, dialog.resultName()));
    tabBar_->setCurrentIndex(tab);
    statusBar()->showMessage(
        tr("%1: %2 atoms").arg(dialog.resultName()).arg(dialog.result()->size()));
}

void MainWindow::openPhononBuilder()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Phonon Calculator"),
                                 tr("Open or build a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    const auto pbc = doc->structure->cell().pbc();
    const bool periodic = doc->structure->cell().isDefined()
        && (pbc[0] || pbc[1] || pbc[2]);
    PhononWizard wizard(periodic, this);
    runSimulationWizard(wizard, tr("Phonon calculation"));
}

void MainWindow::openNanoBuilder()
{
    if (!ensureAseAvailable())
        return;
    NanoBuilderDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
        return;
    const auto atomCount = dialog.result()->size();
    addDocument(dialog.result(), dialog.resultName());
    statusBar()->showMessage(
        tr("Built %1 (%2 atoms)").arg(dialog.resultName()).arg(atomCount));
}

void MainWindow::addRandomNoise()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Random Noise"));
    auto* form = new QFormLayout(&dialog);

    auto* distributionCombo = new QComboBox(&dialog);
    distributionCombo->addItems({tr("Gaussian (normal)"), tr("Uniform")});
    form->addRow(tr("Distribution:"), distributionCombo);

    auto* amplitudeSpin = new QDoubleSpinBox(&dialog);
    amplitudeSpin->setRange(0.001, 5.0);
    amplitudeSpin->setDecimals(3);
    amplitudeSpin->setSingleStep(0.01);
    amplitudeSpin->setValue(0.05);
    amplitudeSpin->setSuffix(tr(" Å"));
    amplitudeSpin->setToolTip(tr("Gaussian: σ per component · Uniform: half-width"));
    form->addRow(tr("Amplitude:"), amplitudeSpin);

    auto* seedSpin = new QSpinBox(&dialog);
    seedSpin->setRange(0, 2147483647);
    seedSpin->setValue(42);
    form->addRow(tr("Random seed:"), seedSpin);

    auto* positionsCheck = new QCheckBox(tr("Perturb atomic positions"), &dialog);
    positionsCheck->setChecked(true);
    auto* cellCheck = new QCheckBox(tr("Perturb unit cell vectors (random strain)"),
                                    &dialog);
    cellCheck->setEnabled(doc->structure->cell().isDefined());
    cellCheck->setToolTip(tr("Atoms follow the cell affinely (fractional "
                             "coordinates preserved)"));
    form->addRow(positionsCheck);
    form->addRow(cellCheck);

    // Stochastic trajectory generation: 1 frame = perturb in place;
    // more frames = a new trajectory tab.
    auto* framesSpin = new QSpinBox(&dialog);
    framesSpin->setRange(1, 1000);
    framesSpin->setValue(1);
    framesSpin->setToolTip(tr("1 perturbs the current structure in place;\n"
                              "more generates a trajectory in a new tab"));
    form->addRow(tr("Frames:"), framesSpin);

    auto* modeCombo = new QComboBox(&dialog);
    modeCombo->addItem(tr("Independent (each frame from the original)"));
    modeCombo->addItem(tr("Cumulative (random walk from previous frame)"));
    modeCombo->setEnabled(false);
    form->addRow(tr("Accumulation:"), modeCombo);
    connect(framesSpin, &QSpinBox::valueChanged, modeCombo,
            [modeCombo](int frames) { modeCombo->setEnabled(frames > 1); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!positionsCheck->isChecked() && !cellCheck->isChecked()) {
        statusBar()->showMessage(tr("Nothing selected to perturb."));
        return;
    }

    core::NoiseOptions options;
    options.distribution = distributionCombo->currentIndex() == 0
        ? core::NoiseOptions::Distribution::Gaussian
        : core::NoiseOptions::Distribution::Uniform;
    options.amplitude = amplitudeSpin->value();
    options.seed = static_cast<unsigned int>(seedSpin->value());
    options.perturbPositions = positionsCheck->isChecked();
    options.perturbCell = cellCheck->isChecked();

    const int frameCount = framesSpin->value();
    if (frameCount == 1) {
        pushUndo();
        core::applyRandomNoise(*doc->structure, options);
        notifyStructureChanged(false);
        statusBar()->showMessage(tr("Applied %1 noise (amplitude %2 Å, seed %3)")
                                     .arg(distributionCombo->currentText())
                                     .arg(options.amplitude)
                                     .arg(options.seed));
        return;
    }

    // Multi-frame stochastic trajectory (frame 0 = unperturbed original).
    const bool cumulative = modeCombo->currentIndex() == 1;
    const core::Structure original = *doc->structure;
    std::vector<std::shared_ptr<core::Structure>> frames;
    frames.reserve(static_cast<std::size_t>(frameCount) + 1);
    frames.push_back(std::make_shared<core::Structure>(original));

    core::Structure walker = original;
    for (int k = 1; k <= frameCount; ++k) {
        core::NoiseOptions frameOptions = options;
        frameOptions.seed = options.seed + static_cast<unsigned int>(k);
        if (cumulative) {
            core::applyRandomNoise(walker, frameOptions); // builds on previous
            frames.push_back(std::make_shared<core::Structure>(walker));
        } else {
            core::Structure fresh = original; // fresh displacement each frame
            core::applyRandomNoise(fresh, frameOptions);
            frames.push_back(std::make_shared<core::Structure>(std::move(fresh)));
        }
    }

    const QString name = tr("%1 (%2 noise ×%3)")
                             .arg(doc->fileName,
                                  cumulative ? tr("cumulative") : tr("independent"))
                             .arg(frameCount);

    // Track the ensemble in the Process panel and checkpoint it into the
    // managed session store, so the perturbed frames stay available for
    // post-processing (RDF, distributions, datasets) without regeneration.
    const QString tasksRoot = projectPath_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/jobs")
        : QFileInfo(projectPath_).absolutePath()
            + QStringLiteral("/.calango_tmp");
    const QString taskDir = tasksRoot + QStringLiteral("/noise_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const int taskId = processPanel_->registerTask(
        tr("Noise trajectory (×%1, seed %2)").arg(frameCount).arg(options.seed),
        taskDir);
    processPanel_->setTaskStatus(taskId, ProcessManagerPanel::Status::Running);
    bool stored = false;
    if (QDir().mkpath(taskDir)) {
        try {
            pybridge::AseBridge::writeTrajectory(
                frames, (taskDir + QStringLiteral("/perturbed.extxyz")).toStdString(),
                "extxyz");
            stored = true;
        } catch (const std::exception&) {
            stored = false; // in-app tab still opens; only the checkpoint failed
        }
    }
    processPanel_->setTaskStatus(taskId,
                                 stored ? ProcessManagerPanel::Status::Completed
                                        : ProcessManagerPanel::Status::Failed);
    isDirty_ = true;

    addDocument(frames.front(), name, std::move(frames));
    statusBar()->showMessage(
        tr("Generated %1-frame noise trajectory (seed %2)%3")
            .arg(frameCount + 1)
            .arg(options.seed)
            .arg(stored ? tr(" — checkpointed to %1").arg(taskDir)
                        : QString()));
}

void MainWindow::openExamplesBrowser()
{
    if (!ensureAseAvailable())
        return;
    ExamplesDialog dialog(this);
    connect(&dialog, &ExamplesDialog::presetChosen,
            this, &MainWindow::loadExample);
    connect(&dialog, &ExamplesDialog::structureFetched, this,
            [this](std::shared_ptr<core::Structure> structure, const QString& name) {
                const auto atomCount = structure->size();
                addDocument(std::move(structure), name);
                statusBar()->showMessage(tr("Fetched %1 (%2 atoms) from the "
                                            "Materials Project")
                                             .arg(name)
                                             .arg(atomCount));
            });
    dialog.exec();
}

void MainWindow::openRayTraceDialog()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }
    RayTraceDialog dialog(viewport_, this);
    dialog.exec();
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

void MainWindow::newCalculation()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("New Calculation"),
                                 tr("Open or build a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    if (jobRunner_->isRunning()) {
        QMessageBox::information(this, tr("New Calculation"),
                                 tr("A calculation is already running — kill it first."));
        return;
    }

    CalculatorDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
        runScript(dialog.script(), dialog.pythonExecutable());
}

bool MainWindow::prepareSimulation(const QString& title)
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, title,
                                 tr("Open or build a structure first."));
        return false;
    }
    if (!ensureAseAvailable())
        return false;
    if (jobRunner_->isRunning()) {
        QMessageBox::information(this, title,
                                 tr("A calculation is already running — kill it first."));
        return false;
    }
    return true;
}

void MainWindow::singlePointCalculation()
{
    if (!prepareSimulation(tr("Single-point Calculation")))
        return;
    SinglePointDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
        runScript(dialog.script(), dialog.pythonExecutable(),
                  tr("Single-point"), /*expectFrames=*/false);
}

void MainWindow::runSimulationWizard(SimulationWizardBase& wizard,
                                     const QString& label)
{
    if (wizard.exec() != QDialog::Accepted)
        return;

    if (wizard.action() == SimulationWizardBase::Action::RunRemote) {
        // Zone-11 Remote Access manager: stage the script and submit it.
        const QString jobDir = stageJob(wizard.script());
        if (jobDir.isEmpty())
            return;
        Document* doc = currentDocument();
        remoteDock_->show();
        remoteDock_->raise();
        const int taskId =
            processPanel_->registerTask(tr("Remote %1").arg(label), jobDir);
        processPanel_->setTaskStatus(taskId, ProcessManagerPanel::Status::Running);
        remotePanel_->submitStagedJob(
            jobDir, doc ? QFileInfo(doc->fileName).completeBaseName() : label);
        statusBar()->showMessage(tr("Submitting %1 run to the cluster…").arg(label));
        return;
    }

    if (jobRunner_->isRunning()) {
        QMessageBox::information(this, label,
                                 tr("A calculation is already running — kill it first."));
        return;
    }
    runScript(wizard.script(), wizard.pythonExecutable(), label,
              /*expectFrames=*/true);
}

void MainWindow::geometryOptimization()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Geometry Optimization"),
                                 tr("Open or build a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    GeometryOptimizationWizard wizard(this);
    runSimulationWizard(wizard, tr("Geometry optimization"));
}

void MainWindow::molecularDynamics()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Molecular Dynamics"),
                                 tr("Open or build a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;
    MolecularDynamicsWizard wizard(this);
    runSimulationWizard(wizard, tr("Molecular dynamics"));
}

void MainWindow::openMonteCarlo()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Monte Carlo Simulation"),
                                 tr("Open or build a structure first."));
        return;
    }

    MonteCarloDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (dialog.method() == MonteCarloDialog::Method::BasinHopping) {
        if (!ensureAseAvailable())
            return;
        if (jobRunner_->isRunning()) {
            QMessageBox::information(this, tr("Monte Carlo Simulation"),
                                     tr("A calculation is already running — kill it first."));
            return;
        }
        runScript(dialog.script(), dialog.pythonExecutable(),
                  tr("Basin Hopping"), /*expectFrames=*/true);
        return;
    }

    // Swap-atoms: the native run already happened in the dialog. Present the
    // trajectory of snapshots and echo the energy trace into the Job panel's
    // Energy plot.
    const auto& res = *dialog.swapResult();
    std::vector<std::shared_ptr<core::Structure>> frames;
    frames.reserve(res.snapshots.size());
    for (const auto& s : res.snapshots)
        frames.push_back(std::make_shared<core::Structure>(s));
    const int tab = addDocument(frames.back(),
                                tr("Swap MC (%1 configs)").arg(frames.size()),
                                frames);
    tabBar_->setCurrentIndex(tab);

    if (energyPlot_) {
        energyPlot_->clear();
        for (std::size_t i = 0; i < res.energyTrace.size(); ++i)
            energyPlot_->addSample(res.stepTrace[i], res.energyTrace[i]);
        jobDock_->show();
        jobDock_->raise();
    }
    statusBar()->showMessage(
        tr("Swap MC: E %1 → %2 eV (best %3), %4 moves accepted, "
           "unlike-bond fraction %5")
            .arg(res.initialEnergy, 0, 'f', 4)
            .arg(res.finalEnergy, 0, 'f', 4)
            .arg(res.bestEnergy, 0, 'f', 4)
            .arg(res.acceptedMoves)
            .arg(res.finalUnlikeFraction, 0, 'f', 3));
}

void MainWindow::openNudgedElasticBand()
{
    if (!ensureAseAvailable())
        return;
    if (documents_.empty()) {
        QMessageBox::information(
            this, tr("Nudged Elastic Band"),
            tr("Open the reactant and product structures first (as tabs), "
               "or load them from files inside the dialog."));
        return;
    }
    if (nebDialog_) { // non-modal: one instance, just resurface it
        nebDialog_->raise();
        nebDialog_->activateWindow();
        return;
    }

    std::vector<NebDialog::NamedStructure> docs;
    for (const auto& d : documents_)
        if (d->structure)
            docs.push_back({d->fileName, d->structure});

    nebDialog_ = new NebDialog(std::move(docs), this);
    connect(nebDialog_, &NebDialog::previewRequested, this,
            [this](const std::vector<std::shared_ptr<core::Structure>>& band) {
                if (band.empty())
                    return;
                std::vector<std::shared_ptr<core::Structure>> frames = band;
                const int tab = addDocument(
                    frames.front(),
                    tr("NEB preview (%1 images)").arg(frames.size()), frames);
                tabBar_->setCurrentIndex(tab);
            });
    connect(nebDialog_, &NebDialog::runRequested, this, [this] {
        if (!nebDialog_)
            return;
        if (jobRunner_->isRunning()) {
            QMessageBox::information(this, tr("Nudged Elastic Band"),
                                     tr("A calculation is already running — kill it first."));
            return;
        }
        stagedBandFrames_ = nebDialog_->band();
        runScript(nebDialog_->script(), nebDialog_->pythonExecutable(),
                  tr("NEB"), /*expectFrames=*/true);
    });
    connect(nebDialog_, &QObject::destroyed, this, [this] { nebDialog_ = nullptr; });
    nebDialog_->show();
}

void MainWindow::addProcessToSelector(int id, const QString& label)
{
    if (!processSelector_)
        return;
    const QSignalBlocker block(processSelector_);
    processSelector_->addItem(QStringLiteral("#%1: %2").arg(id).arg(label), id);
    processSelector_->setCurrentIndex(processSelector_->count() - 1);
    selectedProcessId_ = id;
    syncResultsToProcess(id);
}

void MainWindow::onProcessSelected(int comboIndex)
{
    if (!processSelector_ || comboIndex < 0)
        return;
    selectedProcessId_ = processSelector_->itemData(comboIndex).toInt();
    syncResultsToProcess(selectedProcessId_);
}

void MainWindow::syncResultsToProcess(int id)
{
    auto it = processRecords_.find(id);
    if (it == processRecords_.end())
        return;
    ProcessRecord& r = it->second;
    // Lazily hydrate a record with no in-memory samples from its proc_<id>
    // directory (e.g. after reopening a project).
    if (r.energy.empty() && r.temperature.empty() && r.force.empty()
        && r.pressure.empty() && r.log.isEmpty() && !r.directory.isEmpty())
        loadProcessMetrics(id);

    const auto toSamples = [](const std::vector<std::pair<int, double>>& v) {
        std::vector<MetricPlotWidget::Sample> s;
        s.reserve(v.size());
        for (const auto& [step, value] : v)
            s.push_back({step, value});
        return s;
    };
    energyPlot_->clear();
    energyPlot_->setSamples(toSamples(r.energy));
    temperaturePlot_->clear();
    temperaturePlot_->setSamples(toSamples(r.temperature));
    if (r.hasTempTarget)
        temperaturePlot_->setTarget(r.tempTarget);
    forcePlot_->clear();
    forcePlot_->setSamples(toSamples(r.force));
    pressurePlot_->clear();
    pressurePlot_->setSamples(toSamples(r.pressure));
    if (r.hasPressTarget)
        pressurePlot_->setTarget(r.pressTarget);
    jobLogWidget_->restoreLog(r.log);
}

void MainWindow::onEnergySample(int step, double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.energy.emplace_back(step, value);
    if (currentTaskId_ == selectedProcessId_)
        energyPlot_->addSample(step, value);
}

void MainWindow::onTemperatureSample(int step, double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.temperature.emplace_back(step, value);
    if (currentTaskId_ == selectedProcessId_)
        temperaturePlot_->addSample(step, value);
}

void MainWindow::onForceSample(int step, double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.force.emplace_back(step, value);
    if (currentTaskId_ == selectedProcessId_)
        forcePlot_->addSample(step, value);
}

void MainWindow::onPressureSample(int step, double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.pressure.emplace_back(step, value);
    if (currentTaskId_ == selectedProcessId_)
        pressurePlot_->addSample(step, value);
}

void MainWindow::onTargetTemperature(double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end()) {
        it->second.hasTempTarget = true;
        it->second.tempTarget = value;
    }
    if (currentTaskId_ == selectedProcessId_)
        temperaturePlot_->setTarget(value);
}

void MainWindow::onTargetPressure(double value)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end()) {
        it->second.hasPressTarget = true;
        it->second.pressTarget = value;
    }
    if (currentTaskId_ == selectedProcessId_)
        pressurePlot_->setTarget(value);
}

void MainWindow::onJobOutputLine(const QString& line)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.log += line + QLatin1Char('\n');
    if (currentTaskId_ == selectedProcessId_)
        jobLogWidget_->onOutputLine(line);
}

void MainWindow::onJobErrorLine(const QString& line)
{
    if (auto it = processRecords_.find(currentTaskId_); it != processRecords_.end())
        it->second.log += line + QLatin1Char('\n');
    if (currentTaskId_ == selectedProcessId_)
        jobLogWidget_->onErrorLine(line);
}

void MainWindow::onJobProgress(int step, int total)
{
    if (currentTaskId_ == selectedProcessId_)
        jobLogWidget_->onProgress(step, total);
}

void MainWindow::writeProcessMetrics(int id)
{
    auto it = processRecords_.find(id);
    if (it == processRecords_.end() || it->second.directory.isEmpty())
        return;
    const ProcessRecord& r = it->second;
    const auto writeCsv = [&](const QString& name, const char* column,
                              const std::vector<std::pair<int, double>>& v) {
        if (v.empty())
            return;
        QFile file(r.directory + QLatin1Char('/') + name);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return;
        QTextStream out(&file);
        out << "step," << column << "\n";
        for (const auto& [step, value] : v)
            out << step << ',' << QString::number(value, 'g', 8) << '\n';
    };
    writeCsv(QStringLiteral("energy.csv"), "total_energy_eV", r.energy);
    writeCsv(QStringLiteral("temperature.csv"), "temperature_K", r.temperature);
    writeCsv(QStringLiteral("max_force.csv"), "max_force_eV_per_A", r.force);
    writeCsv(QStringLiteral("pressure.csv"), "pressure_GPa", r.pressure);
    if (!r.log.isEmpty()) {
        QFile file(r.directory + QStringLiteral("/log.txt"));
        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&file) << r.log;
    }
}

void MainWindow::loadProcessMetrics(int id)
{
    auto it = processRecords_.find(id);
    if (it == processRecords_.end() || it->second.directory.isEmpty())
        return;
    ProcessRecord& r = it->second;
    const auto readCsv = [&](const QString& name,
                             std::vector<std::pair<int, double>>& v) {
        QFile file(r.directory + QLatin1Char('/') + name);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        QTextStream in(&file);
        in.readLine(); // header
        while (!in.atEnd()) {
            const QStringList parts = in.readLine().split(QLatin1Char(','));
            if (parts.size() >= 2)
                v.emplace_back(parts[0].toInt(), parts[1].toDouble());
        }
    };
    readCsv(QStringLiteral("energy.csv"), r.energy);
    readCsv(QStringLiteral("temperature.csv"), r.temperature);
    readCsv(QStringLiteral("max_force.csv"), r.force);
    readCsv(QStringLiteral("pressure.csv"), r.pressure);
    QFile logFile(r.directory + QStringLiteral("/log.txt"));
    if (logFile.open(QIODevice::ReadOnly | QIODevice::Text))
        r.log = QString::fromUtf8(logFile.readAll());
}

QString MainWindow::stageJob(const QString& script, int procId)
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure)
        return {};

    // Managed session storage: jobs of a saved project live in a
    // .calango_tmp/ folder next to the .calproj (checkpoints, trajectory
    // dumps and logs stay with the project, reachable from the Process
    // panel); unsaved sessions fall back to the per-user app-data store.
    const QString jobsRoot = projectPath_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/jobs")
        : QFileInfo(projectPath_).absolutePath()
            + QStringLiteral("/.calango_tmp");
    // Per-process metric store: proc_<id> keeps each run's outputs isolated;
    // paths without a process id (e.g. remote submissions) keep a timestamp.
    const QString jobDir = procId >= 0
        ? jobsRoot + QStringLiteral("/proc_%1").arg(procId)
        : jobsRoot + QStringLiteral("/job_")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    if (!QDir().mkpath(jobDir)) {
        QMessageBox::critical(this, tr("Run Calculation"),
                              tr("Could not create run directory %1").arg(jobDir));
        return {};
    }

    try {
        // Stage inputs: structure (extxyz round-trips everything) + script.
        pybridge::AseBridge::writeStructure(
            *doc->structure, (jobDir + QStringLiteral("/structure.extxyz")).toStdString(),
            "extxyz");

        // NEB and other band jobs also stage the full image band as
        // band.extxyz; the member is consumed (cleared) per staging.
        if (!stagedBandFrames_.empty()) {
            pybridge::AseBridge::writeTrajectory(
                stagedBandFrames_,
                (jobDir + QStringLiteral("/band.extxyz")).toStdString(), "extxyz");
            stagedBandFrames_.clear();
        }

        const QString scriptPath = jobDir + QStringLiteral("/run.py");
        QFile scriptFile(scriptPath);
        if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text))
            throw std::runtime_error("Could not write " + scriptPath.toStdString());
        QTextStream(&scriptFile) << script;
        scriptFile.close();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Run Calculation"), QString::fromUtf8(e.what()));
        return {};
    }
    return jobDir;
}

void MainWindow::runScript(const QString& script, const QString& pythonExe,
                           const QString& taskLabel, bool expectFrames)
{
    const QString label = taskLabel.isEmpty() ? tr("Local calculation") : taskLabel;
    // Allocate the process id first so the run stages into proc_<id>/ and its
    // metrics are recorded under that id.
    const int procId = processPanel_->registerTask(label, QString());
    const QString jobDir = stageJob(script, procId);
    if (jobDir.isEmpty()) {
        processPanel_->setTaskStatus(procId, ProcessManagerPanel::Status::Failed);
        return;
    }
    processPanel_->setTaskDirectory(procId, jobDir);

    lastJobDir_ = jobDir;
    isDirty_ = true; // the run console + metric series persist in .calproj
    currentTaskId_ = procId;
    ProcessRecord record;
    record.label = label;
    record.directory = jobDir;
    processRecords_[procId] = std::move(record);
    // Adding + selecting the process repopulates (clears) the tabs for the
    // fresh run; its live samples then flow into the now-selected process.
    addProcessToSelector(procId, label);
    processPanel_->setTaskStatus(procId, ProcessManagerPanel::Status::Running);

    // Live viewport streaming: MD/relaxation scripts emit CALANGO_FRAME
    // blocks — open the trajectory tab NOW and let frames pour in.
    liveDoc_ = nullptr;
    if (expectFrames) {
        Document* doc = currentDocument();
        if (doc && doc->structure) {
            auto first = std::make_shared<core::Structure>(*doc->structure);
            const int tab = addDocument(
                first,
                tr("%1 (live)").arg(taskLabel.isEmpty() ? tr("run") : taskLabel),
                {first});
            liveDoc_ = documents_[static_cast<std::size_t>(tab)].get();
            tabBar_->setCurrentIndex(tab);
        }
    }

    jobDock_->show();
    jobDock_->raise();
    jobRunner_->start(pythonExe, QStringLiteral("run.py"), jobDir);
    statusBar()->showMessage(tr("Running in %1 (%2)").arg(jobDir, pythonExe));
}

int MainWindow::indexOfDocument(const Document* document) const
{
    for (std::size_t i = 0; i < documents_.size(); ++i)
        if (documents_[i].get() == document)
            return static_cast<int>(i);
    return -1;
}

void MainWindow::onFrameStreamed(const std::shared_ptr<core::Structure>& frame)
{
    const int index = liveDoc_ ? indexOfDocument(liveDoc_) : -1;
    if (index < 0 || !frame)
        return;
    Document& doc = *liveDoc_;
    const bool followTail = static_cast<std::size_t>(timeline_->currentFrame())
        + 1 >= doc.frames.size();
    doc.frames.push_back(frame);
    isDirty_ = true;

    if (tabBar_->currentIndex() != index)
        return; // tab exists and accumulates; views update on switch
    timeline_->extendFrameCount(static_cast<int>(doc.frames.size()));
    timeline_->show();
    if (followTail) // keep tracking the newest frame unless the user scrubbed
        timeline_->setCurrentFrame(static_cast<int>(doc.frames.size()) - 1);
}

void MainWindow::newRemoteCalculation()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("New Remote Calculation"),
                                 tr("Open or build a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    CalculatorDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString jobDir = stageJob(dialog.script());
    if (jobDir.isEmpty())
        return;

    remoteDock_->show();
    remoteDock_->raise();
    // Remote tasks are tracked too; the staging dir receives the
    // downloaded results, so "Load Result" works after completion.
    const int taskId = processPanel_->registerTask(tr("Remote calculation"), jobDir);
    processPanel_->setTaskStatus(taskId, ProcessManagerPanel::Status::Running);
    remotePanel_->submitStagedJob(
        jobDir, QFileInfo(doc->fileName).completeBaseName());
    statusBar()->showMessage(tr("Submitting %1 to the cluster…").arg(jobDir));
}

void MainWindow::onRemoteResultsReady(const QString& localDir)
{
    // Same convention as local jobs: trajectories first (they activate
    // the timeline), then a bare optimized structure.
    for (const auto* candidate :
         {"md.traj", "opt.traj", "optimized.extxyz"}) {
        const QString path = localDir + QLatin1Char('/') + QLatin1String(candidate);
        if (QFile::exists(path)) {
            loadFile(path);
            statusBar()->showMessage(
                tr("Remote results loaded from %1").arg(localDir));
            return;
        }
    }
    statusBar()->showMessage(
        tr("Remote run finished — results in %1").arg(localDir));
}

void MainWindow::onJobFinished(int exitCode, bool crashed)
{
    const bool failed = crashed || exitCode != 0;
    if (currentTaskId_ >= 0) {
        processPanel_->setTaskStatus(currentTaskId_,
                                     failed ? ProcessManagerPanel::Status::Failed
                                            : ProcessManagerPanel::Status::Completed);
        // Persist this process's metrics + log to proc_<id>/ so they survive
        // subsequent runs and can be reloaded from the Results selector.
        writeProcessMetrics(currentTaskId_);
        currentTaskId_ = -1;
    }

    // A live-streamed run already owns its trajectory tab — finalize its
    // title and skip the legacy end-of-job trajectory load.
    if (liveDoc_) {
        const int index = indexOfDocument(liveDoc_);
        Document* streamed = liveDoc_;
        liveDoc_ = nullptr;
        if (index >= 0 && streamed->frames.size() > 1) {
            streamed->fileName.replace(tr(" (live)"), QString());
            tabBar_->setTabText(index, streamed->fileName);
            if (tabBar_->currentIndex() == index)
                syncViewsToCurrent(false);
            if (!failed)
                statusBar()->showMessage(
                    tr("Run finished — %n streamed frame(s)", nullptr,
                       static_cast<int>(streamed->frames.size())));
            return;
        }
        // Nothing streamed (e.g. single-point) — drop the placeholder tab.
        if (index >= 0 && streamed->frames.size() <= 1)
            onTabCloseRequested(index);
    }

    if (failed || lastJobDir_.isEmpty())
        return;

    // Electronic-structure runs: open the band/PDOS viewer directly.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/bands.json"))) {
        openBandResults(lastJobDir_);
        return;
    }
    // Phonon runs: open the phonon band structure + PhDOS viewer.
    if (QFile::exists(lastJobDir_ + QStringLiteral("/phonon_band.json"))) {
        openPhononResults(lastJobDir_);
        return;
    }

    // MD / optimization runs: open the trajectory automatically in a new
    // tab — the timeline comes pre-loaded and ready to scrub.
    for (const auto* trajectory : {"opt.traj", "md.traj"}) {
        const QString trajectoryPath = lastJobDir_ + QLatin1Char('/')
            + QLatin1String(trajectory);
        if (!QFile::exists(trajectoryPath))
            continue;
        loadFile(trajectoryPath);
        statusBar()->showMessage(
            tr("Run finished — trajectory %1 opened in a new tab")
                .arg(QLatin1String(trajectory)),
            8000);
        return;
    }

    // Otherwise offer the final structure, if the job produced one.
    for (const auto* candidate : {"optimized.extxyz", "md_final.extxyz"}) {
        const QString resultPath = lastJobDir_ + QLatin1Char('/')
            + QLatin1String(candidate);
        if (!QFile::exists(resultPath))
            continue;
        const auto answer = QMessageBox::question(
            this, tr("Run Finished"),
            tr("The run produced %1.\nLoad it into a new tab?")
                .arg(QLatin1String(candidate)));
        if (answer == QMessageBox::Yes)
            loadFile(resultPath);
        return;
    }
}


void MainWindow::about()
{
    const auto& python = pybridge::PythonEngine::instance();
    QMessageBox box(this);
    box.setWindowTitle(tr("About Calango"));
    // Brand banner: the transparent icon variant, scaled for the dialog.
    box.setIconPixmap(
        QPixmap(QStringLiteral(":/assets/.internal/icon_transparent.png"))
            .scaled(140, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    box.setText(
        tr("<h3>Calango %1</h3>"
           "<p>For visual atomistic modeling</p>"
           "<p>Python: %2<br>ASE: %3</p>")
            .arg(QStringLiteral(CALANGO_VERSION),
                 QString::fromStdString(python.pythonVersion()).section('\n', 0, 0),
                 python.aseAvailable()
                     ? QString::fromStdString(python.aseVersion())
                     : tr("not available")));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

} // namespace calango::gui
