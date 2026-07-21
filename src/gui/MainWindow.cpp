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
#include "gui/RepresentationPanel.hpp"
#include "gui/SlabWizard.hpp"
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

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
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
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>

namespace calango::gui {

namespace {
constexpr std::size_t kMaxUndoDepth = 50;
/// Version tag for saveState/restoreState. Bumped when the default dock
/// grid changes so stale saved layouts don't override the new default
/// (v2 = the 8-zone grid workspace).
constexpr int kLayoutVersion = 2;

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
    orthoAction_->setToolTip(tr("Toggle perspective / orthographic projection"));
    connect(orthoAction_, &QAction::toggled,
            viewport_, &ViewportWidget::setOrthographic);

    auto* frameToolbar = new QToolBar(this);
    frameToolbar->setObjectName(QStringLiteral("frameToolbar"));
    frameToolbar->setIconSize(QSize(18, 18));
    frameToolbar->setMovable(false);
    frameToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
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

    connect(jobRunner_, &jobs::JobRunner::started,
            jobLogWidget_, &JobLogWidget::onJobStarted);
    for (MetricPlotWidget* plot :
         {energyPlot_, temperaturePlot_, forcePlot_, pressurePlot_})
        connect(jobRunner_, &jobs::JobRunner::started,
                plot, &MetricPlotWidget::clear);
    connect(jobRunner_, &jobs::JobRunner::outputLine,
            jobLogWidget_, &JobLogWidget::onOutputLine);
    connect(jobRunner_, &jobs::JobRunner::errorLine,
            jobLogWidget_, &JobLogWidget::onErrorLine);
    connect(jobRunner_, &jobs::JobRunner::progress,
            jobLogWidget_, &JobLogWidget::onProgress);
    connect(jobRunner_, &jobs::JobRunner::energySample,
            energyPlot_, &MetricPlotWidget::addSample);
    connect(jobRunner_, &jobs::JobRunner::temperatureSample,
            temperaturePlot_, &MetricPlotWidget::addSample);
    connect(jobRunner_, &jobs::JobRunner::targetTemperature,
            temperaturePlot_, &MetricPlotWidget::setTarget);
    connect(jobRunner_, &jobs::JobRunner::maxForceSample,
            forcePlot_, &MetricPlotWidget::addSample);
    connect(jobRunner_, &jobs::JobRunner::pressureSample,
            pressurePlot_, &MetricPlotWidget::addSample);
    connect(jobRunner_, &jobs::JobRunner::targetPressure,
            pressurePlot_, &MetricPlotWidget::setTarget);
    connect(jobRunner_, &jobs::JobRunner::finished,
            jobLogWidget_, &JobLogWidget::onJobFinished);
    connect(jobRunner_, &jobs::JobRunner::finished,
            this, &MainWindow::onJobFinished);
    connect(jobLogWidget_, &JobLogWidget::terminateRequested,
            jobRunner_, &jobs::JobRunner::terminate);

    connect(viewport_, &ViewportWidget::selectionChanged, this, [this](int count) {
        if (count > 0)
            statusBar()->showMessage(tr("%n atom(s) selected", nullptr, count));
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
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    // Project workspace: one .calproj file restores the whole multi-tab
    // session (structures, trajectories, job console + metric series).
    fileMenu->addAction(tr("Open &Project…"), QKeySequence(tr("Ctrl+Shift+O")),
                        this, &MainWindow::openProject);
    fileMenu->addAction(tr("Save P&roject"), QKeySequence::Save,
                        this, &MainWindow::saveProject);
    fileMenu->addAction(tr("Save Project As…"),
                        this, &MainWindow::saveProjectAs);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Open Structure…"), QKeySequence::Open,
                        this, &MainWindow::openStructure);
    fileMenu->addAction(tr("Open &Trajectory…"), QKeySequence(tr("Ctrl+T")),
                        this, &MainWindow::openTrajectory);
    fileMenu->addAction(tr("Save Structure &As…"), QKeySequence::SaveAs,
                        this, &MainWindow::saveStructureAs);
    fileMenu->addAction(tr("Save Tra&jectory As…"), QKeySequence(tr("Ctrl+Shift+T")),
                        this, &MainWindow::saveTrajectoryAs);
    fileMenu->addAction(tr("&Close Tab"), QKeySequence::Close, this, [this] {
        if (tabBar_->currentIndex() >= 0)
            onTabCloseRequested(tabBar_->currentIndex());
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Export &Image…"), QKeySequence(tr("Ctrl+E")),
                        this, &MainWindow::exportImage);
    fileMenu->addAction(tr("Export Ani&mation (GIF/MP4)…"),
                        this, &MainWindow::exportAnimation);
    fileMenu->addAction(tr("&Ray-Traced Render…"),
                        this, &MainWindow::openRayTraceDialog);
    fileMenu->addSeparator();
    // Quit goes through closeAllWindows so closeEvent persists the window
    // geometry / dock layout; the embedded interpreter is finalized by
    // PythonEngine's destructor in main() afterwards.
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit,
                        qApp, &QApplication::closeAllWindows);

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    undoAction_ = editMenu->addAction(tr("&Undo"), QKeySequence::Undo,
                                      this, &MainWindow::undo);
    redoAction_ = editMenu->addAction(tr("&Redo"), QKeySequence::Redo,
                                      this, &MainWindow::redo);
    editMenu->addSeparator();
    editMenu->addAction(tr("&Add Atom…"), QKeySequence(tr("Ctrl+Shift+A")),
                        this, &MainWindow::addAtom);
    editMenu->addAction(tr("&Change Element of Selection…"),
                        this, &MainWindow::changeElementOfSelection);
    editMenu->addAction(tr("&Translate Selection…"),
                        this, &MainWindow::translateSelection);
    editMenu->addAction(tr("&Delete Selected Atoms"), QKeySequence::Delete,
                        this, &MainWindow::deleteSelectedAtoms);
    editMenu->addSeparator();
    editMenu->addAction(tr("&Bond Editor…"), QKeySequence(tr("Ctrl+B")),
                        this, &MainWindow::showBondEditor);
    editMenu->addSeparator();
    editMenu->addAction(tr("&Preferences…"), QKeySequence::Preferences,
                        this, &MainWindow::showPreferences);
    updateUndoActions();

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("&Frame Structure"), QKeySequence(tr("F")),
                        viewport_, &ViewportWidget::frameStructure);
    QAction* cellAction = viewMenu->addAction(tr("Show Unit &Cell"));
    cellAction->setCheckable(true);
    cellAction->setChecked(true);
    connect(cellAction, &QAction::toggled, viewport_, &ViewportWidget::setShowCell);
    viewMenu->addAction(orthoAction_); // shared with the frame-panel toolbar

    QMenu* buildMenu = menuBar()->addMenu(tr("&Build"));
    buildMenu->addAction(tr("Create &Supercell…"), this, &MainWindow::createSupercell);
    buildMenu->addAction(tr("&Surface Slab…"), this, &MainWindow::cleaveSurface);
    buildMenu->addAction(tr("&Nanomaterial Builder…"), this, &MainWindow::openNanoBuilder);
    buildMenu->addAction(tr("&Normal Modes / Phonon Builder…"),
                         this, &MainWindow::openPhononBuilder);
    buildMenu->addAction(tr("From &Database…"), this, &MainWindow::openExamplesBrowser);

    QMenu* simulationMenu = menuBar()->addMenu(tr("&Simulation"));
    simulationMenu->addAction(tr("&New Calculation…"), QKeySequence(tr("Ctrl+R")),
                              this, &MainWindow::newCalculation);
    simulationMenu->addAction(tr("Random &Noise…"), this, &MainWindow::addRandomNoise);

    QMenu* analysisMenu = menuBar()->addMenu(tr("&Analysis"));
    analysisMenu->addAction(tr("&Brillouin Zone / k-Path…"),
                            this, &MainWindow::showBrillouinZone);
    analysisMenu->addAction(tr("&Radial Distribution Function…"),
                            this, &MainWindow::showRdf);
    analysisMenu->addAction(tr("&Coordination Numbers (CN / GCN)…"),
                            this, &MainWindow::showCoordination);
    analysisMenu->addAction(tr("Bond &Length / Angle Distributions…"),
                            this, &MainWindow::showDistributions);
    analysisMenu->addAction(tr("Structure &Factor S(q)…"),
                            this, &MainWindow::showStructureFactor);
    analysisMenu->addAction(tr("&X-Ray Diffraction (XRD)…"),
                            this, &MainWindow::showXrd);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About Calango"), this, &MainWindow::about);

    // ----- 8-zone grid workspace (4 columns × 2 rows) ----------------------
    //
    //   | 1 Structure | 2-3 Viewport | 4 Representation   |
    //   | 5 Lighting  | 6-7 Job      | 8 Unit Cell & Axes |
    //
    // The side columns own their corners, so the bottom dock area (Job)
    // spans only the middle columns — the central widget (tab bar +
    // viewport + timeline) fills zones 2-3. Every zone stays resizable
    // via the dock splitters, and panels remain re-dockable/floatable.
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    auto* infoDock = new QDockWidget(tr("Structure"), this); // zone 1
    infoDock->setObjectName(QStringLiteral("structureDock"));
    infoWidget_ = new StructureInfoWidget(infoDock);
    infoDock->setWidget(infoWidget_);
    addDockWidget(Qt::LeftDockWidgetArea, infoDock);

    auto* lightingDock = new QDockWidget(tr("Lighting"), this); // zone 5
    lightingDock->setObjectName(QStringLiteral("lightingDock"));
    lightingDock->setWidget(new LightingPanel(viewport_, lightingDock));
    splitDockWidget(infoDock, lightingDock, Qt::Vertical);

    auto* reprDock = new QDockWidget(tr("Representation"), this); // zone 4
    reprDock->setObjectName(QStringLiteral("representationDock"));
    reprDock->setWidget(new RepresentationPanel(viewport_, reprDock));
    addDockWidget(Qt::RightDockWidgetArea, reprDock);

    auto* cellAxesDock = new QDockWidget(tr("Unit Cell && Axes"), this); // zone 8
    cellAxesDock->setObjectName(QStringLiteral("cellAxesDock"));
    cellAxesDock->setWidget(new CellAxesPanel(viewport_, cellAxesDock));
    splitDockWidget(reprDock, cellAxesDock, Qt::Vertical);

    jobDock_ = new QDockWidget(tr("Job"), this); // zones 6-7
    jobDock_->setObjectName(QStringLiteral("jobDock"));
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
    energySpec.placeholder = tr("Energy vs. step will appear here during a job");
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
    jobDock_->setWidget(jobTabs);
    addDockWidget(Qt::BottomDockWidgetArea, jobDock_);

    // Default grid proportions: side columns ~290 px, bottom row ~240 px,
    // side columns split evenly between their two zones.
    resizeDocks({infoDock, lightingDock}, {290, 290}, Qt::Horizontal);
    resizeDocks({reprDock, cellAxesDock}, {290, 290}, Qt::Horizontal);
    resizeDocks({infoDock, lightingDock}, {1, 1}, Qt::Vertical);
    resizeDocks({reprDock, cellAxesDock}, {1, 1}, Qt::Vertical);
    resizeDocks({jobDock_}, {240}, Qt::Vertical);

    viewMenu->addSeparator();
    viewMenu->addAction(infoDock->toggleViewAction());
    viewMenu->addAction(reprDock->toggleViewAction());
    viewMenu->addAction(cellAxesDock->toggleViewAction());
    viewMenu->addAction(lightingDock->toggleViewAction());
    viewMenu->addAction(jobDock_->toggleViewAction());

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
    documents_.erase(documents_.begin() + index);
    tabBar_->removeTab(index); // currentChanged fires and re-syncs
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

void MainWindow::loadFile(const QString& path)
{
    // Project workspaces (double-click / "Open with" via the installer's
    // MIME association, or a CLI argument) restore the whole session
    // instead of loading a structure through ASE.
    if (path.endsWith(QStringLiteral(".calproj"), Qt::CaseInsensitive)) {
        if (readProject(path))
            projectPath_ = path;
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
        } else {
            std::vector<std::shared_ptr<core::Structure>> frames;
            frames.reserve(rawFrames.size());
            for (const auto& frame : rawFrames)
                frames.push_back(std::make_shared<core::Structure>(frame));
            const auto frameCount = frames.size();
            addDocument(frames.front(), QFileInfo(path).fileName(), std::move(frames));
            statusBar()->showMessage(
                tr("Loaded %1 (%2 frames)").arg(path).arg(frameCount));
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
                                 tr("A job is running — kill it before "
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

void MainWindow::saveProject()
{
    if (projectPath_.isEmpty()) {
        saveProjectAs();
        return;
    }
    writeProject(projectPath_);
}

void MainWindow::saveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project As"), QStringLiteral("workspace.calproj"),
        tr("Calango project (*.calproj)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".calproj"), Qt::CaseInsensitive))
        path += QStringLiteral(".calproj");
    if (writeProject(path))
        projectPath_ = path;
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

void MainWindow::openPhononBuilder()
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure || doc->structure->empty()) {
        QMessageBox::information(this, tr("Phonon Builder"),
                                 tr("Open or build a structure first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    PhononBuilderDialog dialog(doc->structure, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (dialog.generateDisplacementsOnly()) {
        try {
            auto frames = dialog.buildDisplacedFrames();
            const auto frameCount = frames.size();
            auto reference = frames.front();
            addDocument(std::move(reference),
                        tr("%1 (displacements)").arg(doc->fileName),
                        std::move(frames));
            statusBar()->showMessage(
                tr("Generated %1 displaced structures (δ scrubbed on the timeline; "
                   "save frames for external codes via File → Save Structure As)")
                    .arg(frameCount));
        } catch (const std::exception& e) {
            QMessageBox::critical(this, tr("Phonon Builder"), QString::fromUtf8(e.what()));
        }
        return;
    }

    if (jobRunner_->isRunning()) {
        QMessageBox::information(this, tr("Phonon Builder"),
                                 tr("A job is already running — kill it first."));
        return;
    }
    runScript(dialog.script(), dialog.pythonExecutable());
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
    addDocument(frames.front(), name, std::move(frames));
    statusBar()->showMessage(
        tr("Generated %1-frame noise trajectory (seed %2)")
            .arg(frameCount + 1)
            .arg(options.seed));
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
                                 tr("A job is already running — kill it first."));
        return;
    }

    CalculatorDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
        runScript(dialog.script(), dialog.pythonExecutable());
}

void MainWindow::runScript(const QString& script, const QString& pythonExe)
{
    Document* doc = currentDocument();
    if (!doc || !doc->structure)
        return;

    // Each job gets its own directory under the per-user app-data location.
    const QString jobsRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/jobs");
    const QString jobDir = jobsRoot + QStringLiteral("/job_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    if (!QDir().mkpath(jobDir)) {
        QMessageBox::critical(this, tr("Run Job"),
                              tr("Could not create job directory %1").arg(jobDir));
        return;
    }

    try {
        // Stage inputs: structure (extxyz round-trips everything) + script.
        pybridge::AseBridge::writeStructure(
            *doc->structure, (jobDir + QStringLiteral("/structure.extxyz")).toStdString(),
            "extxyz");

        const QString scriptPath = jobDir + QStringLiteral("/run.py");
        QFile scriptFile(scriptPath);
        if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text))
            throw std::runtime_error("Could not write " + scriptPath.toStdString());
        QTextStream(&scriptFile) << script;
        scriptFile.close();

        lastJobDir_ = jobDir;
        jobDock_->show();
        jobDock_->raise();
        jobRunner_->start(pythonExe, QStringLiteral("run.py"), jobDir);
        statusBar()->showMessage(tr("Job running in %1 (%2)").arg(jobDir, pythonExe));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Run Job"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::onJobFinished(int exitCode, bool crashed)
{
    if (crashed || exitCode != 0 || lastJobDir_.isEmpty())
        return;

    // MD / optimization runs: open the trajectory automatically in a new
    // tab — the timeline comes pre-loaded and ready to scrub.
    for (const auto* trajectory : {"opt.traj", "md.traj"}) {
        const QString trajectoryPath = lastJobDir_ + QLatin1Char('/')
            + QLatin1String(trajectory);
        if (!QFile::exists(trajectoryPath))
            continue;
        loadFile(trajectoryPath);
        statusBar()->showMessage(
            tr("Job finished — trajectory %1 opened in a new tab")
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
            this, tr("Job Finished"),
            tr("The job produced %1.\nLoad it into a new tab?")
                .arg(QLatin1String(candidate)));
        if (answer == QMessageBox::Yes)
            loadFile(resultPath);
        return;
    }
}

namespace {

/// User-facing version string, read at runtime from the plain-text
/// `version` file (standard C++ file I/O). Searched next to the binary,
/// one level up (a `build/` dir inside the repository root), and in the
/// working directory; the compile-time version is only a fallback.
QString runtimeVersion()
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/version"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../version"),
        QStringLiteral("version"),
    };
    for (const QString& path : candidates) {
        std::ifstream file(path.toStdString());
        if (!file.is_open())
            continue;
        std::string line;
        std::getline(file, line);
        const QString version = QString::fromStdString(line).trimmed();
        if (!version.isEmpty())
            return version;
    }
    return QStringLiteral(CALANGO_VERSION);
}

} // namespace

void MainWindow::about()
{
    const auto& python = pybridge::PythonEngine::instance();
    QMessageBox box(this);
    box.setWindowTitle(tr("About Calango"));
    // Brand banner: the transparent icon variant, scaled for the dialog.
    box.setIconPixmap(
        QPixmap(QStringLiteral(":/assets/calango/icon_transparent.png"))
            .scaled(140, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    box.setText(
        tr("<h3>Calango %1</h3>"
           "<p>Atomistic modeling and simulation front-end built on Qt6, "
           "OpenGL and the Atomic Simulation Environment.</p>"
           "<p>Python: %2<br>ASE: %3</p>")
            .arg(runtimeVersion(),
                 QString::fromStdString(python.pythonVersion()).section('\n', 0, 0),
                 python.aseAvailable()
                     ? QString::fromStdString(python.aseVersion())
                     : tr("not available")));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

} // namespace calango::gui
