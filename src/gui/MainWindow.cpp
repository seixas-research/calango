#include "gui/MainWindow.hpp"

#include "core/BrillouinZone.hpp"
#include "core/Noise.hpp"
#include "core/Structure.hpp"
#include "gui/BrillouinZoneDialog.hpp"
#include "gui/CalculatorDialog.hpp"
#include "gui/CoordinationDialog.hpp"
#include "gui/ExamplesDialog.hpp"
#include "gui/NanoBuilderDialog.hpp"
#include "gui/PhononBuilderDialog.hpp"
#include "gui/RayTraceDialog.hpp"
#include "gui/RdfDialog.hpp"
#include "gui/DisplaySettingsWidget.hpp"
#include "gui/EnergyPlotWidget.hpp"
#include "gui/JobLogWidget.hpp"
#include "gui/StructureInfoWidget.hpp"
#include "gui/TimelineWidget.hpp"
#include "gui/ViewportWidget.hpp"
#include "jobs/JobRunner.hpp"
#include "python_bridge/AnimationExporter.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QCheckBox>
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
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProgressDialog>
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
#include <stdexcept>

namespace calango::gui {

namespace {
constexpr std::size_t kMaxUndoDepth = 50;
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , jobRunner_(new jobs::JobRunner(this))
{
    setWindowTitle(QStringLiteral("Calango"));
    resize(1360, 860);

    // Central column: document tab bar on top, then the shared 3D
    // viewport, then the playback timeline (job console dock sits below).
    tabBar_ = new QTabBar(this);
    tabBar_->setDocumentMode(true);
    tabBar_->setTabsClosable(true);
    tabBar_->setExpanding(false);
    viewport_ = new ViewportWidget(this);
    timeline_ = new TimelineWidget(this);
    timeline_->hide(); // appears when the current document has frames

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(tabBar_);
    centralLayout->addWidget(viewport_, 1);
    centralLayout->addWidget(timeline_);
    setCentralWidget(central);

    connect(tabBar_, &QTabBar::currentChanged, this, &MainWindow::onTabChanged);
    connect(tabBar_, &QTabBar::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    connect(timeline_, &TimelineWidget::frameChanged, this, &MainWindow::showFrame);

    createMenusAndDocks();

    connect(jobRunner_, &jobs::JobRunner::started,
            jobLogWidget_, &JobLogWidget::onJobStarted);
    connect(jobRunner_, &jobs::JobRunner::started,
            energyPlot_, &EnergyPlotWidget::clear);
    connect(jobRunner_, &jobs::JobRunner::outputLine,
            jobLogWidget_, &JobLogWidget::onOutputLine);
    connect(jobRunner_, &jobs::JobRunner::errorLine,
            jobLogWidget_, &JobLogWidget::onErrorLine);
    connect(jobRunner_, &jobs::JobRunner::progress,
            jobLogWidget_, &JobLogWidget::onProgress);
    connect(jobRunner_, &jobs::JobRunner::energySample,
            energyPlot_, &EnergyPlotWidget::addSample);
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
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open Structure…"), QKeySequence::Open,
                        this, &MainWindow::openStructure);
    fileMenu->addAction(tr("Open &Trajectory…"), QKeySequence(tr("Ctrl+T")),
                        this, &MainWindow::openTrajectory);
    fileMenu->addAction(tr("Save Structure &As…"), QKeySequence::SaveAs,
                        this, &MainWindow::saveStructureAs);
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
    updateUndoActions();

    QMenu* buildMenu = menuBar()->addMenu(tr("&Build"));
    buildMenu->addAction(tr("Create &Supercell…"), this, &MainWindow::createSupercell);
    buildMenu->addAction(tr("Cleave S&urface (Slab)…"), this, &MainWindow::cleaveSurface);
    buildMenu->addAction(tr("&Nanomaterial Builder…"), this, &MainWindow::openNanoBuilder);
    buildMenu->addAction(tr("&Phonon Builder (Finite Displacements)…"),
                         this, &MainWindow::openPhononBuilder);
    buildMenu->addAction(tr("By &Examples…"), this, &MainWindow::openExamplesBrowser);

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

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("&Frame Structure"), QKeySequence(tr("F")),
                        viewport_, &ViewportWidget::frameStructure);
    QAction* cellAction = viewMenu->addAction(tr("Show Unit &Cell"));
    cellAction->setCheckable(true);
    cellAction->setChecked(true);
    connect(cellAction, &QAction::toggled, viewport_, &ViewportWidget::setShowCell);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About Calango"), this, &MainWindow::about);

    // Viewport toolbar: framing + projection toggle.
    QToolBar* viewToolbar = addToolBar(tr("Viewport"));
    viewToolbar->setObjectName(QStringLiteral("viewportToolbar"));
    viewToolbar->addAction(tr("Frame"), viewport_, &ViewportWidget::frameStructure);
    QAction* orthoAction = viewToolbar->addAction(tr("Orthographic"));
    orthoAction->setCheckable(true);
    orthoAction->setToolTip(tr("Toggle between perspective and orthographic projection"));
    connect(orthoAction, &QAction::toggled,
            viewport_, &ViewportWidget::setOrthographic);
    viewMenu->addAction(orthoAction);

    auto* infoDock = new QDockWidget(tr("Structure"), this);
    infoDock->setObjectName(QStringLiteral("structureDock"));
    infoWidget_ = new StructureInfoWidget(infoDock);
    infoDock->setWidget(infoWidget_);
    addDockWidget(Qt::LeftDockWidgetArea, infoDock);

    auto* displayDock = new QDockWidget(tr("Display"), this);
    displayDock->setObjectName(QStringLiteral("displayDock"));
    displayDock->setWidget(new DisplaySettingsWidget(viewport_, displayDock));
    addDockWidget(Qt::RightDockWidgetArea, displayDock);

    jobDock_ = new QDockWidget(tr("Job"), this);
    jobDock_->setObjectName(QStringLiteral("jobDock"));
    auto* jobTabs = new QTabWidget(jobDock_);
    jobLogWidget_ = new JobLogWidget(jobTabs);
    energyPlot_ = new EnergyPlotWidget(jobTabs);
    jobTabs->addTab(jobLogWidget_, tr("Log"));
    jobTabs->addTab(energyPlot_, tr("Energy"));
    jobDock_->setWidget(jobTabs);
    addDockWidget(Qt::BottomDockWidgetArea, jobDock_);

    viewMenu->addSeparator();
    viewMenu->addAction(infoDock->toggleViewAction());
    viewMenu->addAction(displayDock->toggleViewAction());
    viewMenu->addAction(jobDock_->toggleViewAction());
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

// ---------------------------------------------------------------------------
// Image & animation export
// ---------------------------------------------------------------------------

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
    widthSpin->setRange(64, 2048);
    widthSpin->setSingleStep(2); // H.264 yuv420p wants even dimensions
    widthSpin->setValue(640);
    auto* heightSpin = new QSpinBox(&dialog);
    heightSpin->setRange(64, 2048);
    heightSpin->setSingleStep(2);
    heightSpin->setValue(480);
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
        QMessageBox::information(this, tr("Cleave Surface"),
                                 tr("Open a bulk structure with a unit cell first."));
        return;
    }
    if (!ensureAseAvailable())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Cleave Surface (Slab)"));
    auto* form = new QFormLayout(&dialog);

    QSpinBox* miller[3];
    const char* names[3] = {"h", "k", "l"};
    auto* millerRow = new QWidget(&dialog);
    auto* millerLayout = new QFormLayout(millerRow);
    millerLayout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        miller[i] = new QSpinBox(&dialog);
        miller[i]->setRange(-9, 9);
        miller[i]->setValue(i == 2 ? 1 : 0); // default (0 0 1)
        millerLayout->addRow(QLatin1String(names[i]), miller[i]);
    }
    form->addRow(tr("Miller indices:"), millerRow);

    auto* layersSpin = new QSpinBox(&dialog);
    layersSpin->setRange(1, 40);
    layersSpin->setValue(4);
    form->addRow(tr("Layers:"), layersSpin);

    auto* vacuumSpin = new QDoubleSpinBox(&dialog);
    vacuumSpin->setRange(0.0, 60.0);
    vacuumSpin->setValue(10.0);
    vacuumSpin->setSuffix(tr(" Å"));
    form->addRow(tr("Vacuum (each side):"), vacuumSpin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;
    if (miller[0]->value() == 0 && miller[1]->value() == 0 && miller[2]->value() == 0) {
        QMessageBox::warning(this, tr("Cleave Surface"),
                             tr("Miller indices (0 0 0) are not a valid plane."));
        return;
    }

    try {
        auto slab = std::make_shared<core::Structure>(pybridge::AseBridge::makeSlab(
            *doc->structure, miller[0]->value(), miller[1]->value(), miller[2]->value(),
            layersSpin->value(), vacuumSpin->value()));
        pushUndo();
        replaceCurrentStructure(std::move(slab),
                                tr("%1 (%2%3%4) slab")
                                    .arg(doc->fileName)
                                    .arg(miller[0]->value())
                                    .arg(miller[1]->value())
                                    .arg(miller[2]->value()));
        statusBar()->showMessage(tr("Slab created: %1 atoms").arg(doc->structure->size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Cleave Surface"), QString::fromUtf8(e.what()));
    }
}

// ---------------------------------------------------------------------------
// Editing tools
// ---------------------------------------------------------------------------

void MainWindow::addAtom()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Atom"));
    auto* form = new QFormLayout(&dialog);

    auto* symbolEdit = new QLineEdit(QStringLiteral("C"), &dialog);
    form->addRow(tr("Element symbol:"), symbolEdit);

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

    const int z = core::Elements::atomicNumber(symbolEdit->text().trimmed().toStdString());
    if (z == 0) {
        QMessageBox::warning(this, tr("Add Atom"),
                             tr("Unknown element symbol '%1'.").arg(symbolEdit->text()));
        return;
    }

    Document& doc = ensureDocument();
    pushUndo();
    const bool firstAtom = !doc.structure || doc.structure->empty();
    if (!doc.structure)
        doc.structure = std::make_shared<core::Structure>();
    doc.structure->addAtom(
        {z, {coords[0]->value(), coords[1]->value(), coords[2]->value()}});
    notifyStructureChanged(firstAtom);
    statusBar()->showMessage(tr("Added %1 atom").arg(symbolEdit->text().trimmed()));
}

void MainWindow::changeElementOfSelection()
{
    Document* doc = currentDocument();
    const auto& selection = viewport_->selection();
    if (!doc || !doc->structure || selection.empty()) {
        statusBar()->showMessage(tr("Select atoms first (click / Ctrl+click)."));
        return;
    }

    bool ok = false;
    const QString symbol = QInputDialog::getText(
        this, tr("Change Element"),
        tr("New element symbol for %n atom(s):", nullptr,
           static_cast<int>(selection.size())),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || symbol.trimmed().isEmpty())
        return;

    const int z = core::Elements::atomicNumber(symbol.trimmed().toStdString());
    if (z == 0) {
        QMessageBox::warning(this, tr("Change Element"),
                             tr("Unknown element symbol '%1'.").arg(symbol));
        return;
    }

    pushUndo();
    for (const int index : selection)
        doc->structure->atoms()[static_cast<std::size_t>(index)].atomicNumber = z;
    notifyStructureChanged(false);
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
    RdfDialog dialog(doc->structure, this);
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

void MainWindow::about()
{
    const auto& python = pybridge::PythonEngine::instance();
    QMessageBox::about(
        this, tr("About Calango"),
        tr("<h3>Calango %1</h3>"
           "<p>Atomistic modeling and simulation front-end built on Qt6, "
           "OpenGL and the Atomic Simulation Environment.</p>"
           "<p>Python: %2<br>ASE: %3</p>")
            .arg(QStringLiteral(CALANGO_VERSION),
                 QString::fromStdString(python.pythonVersion()).section('\n', 0, 0),
                 python.aseAvailable()
                     ? QString::fromStdString(python.aseVersion())
                     : tr("not available")));
}

} // namespace calango::gui
