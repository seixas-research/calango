#include "gui/MainWindow.hpp"

#include "core/Structure.hpp"
#include "gui/CalculatorDialog.hpp"
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
#include <QImageWriter>
#include <QPainter>
#include <QProgressDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
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
    resize(1280, 840);

    // Central column: 3D viewport with the playback timeline directly
    // below it (the job console dock sits below both).
    viewport_ = new ViewportWidget(this);
    timeline_ = new TimelineWidget(this);
    timeline_->hide(); // appears when a trajectory is loaded
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(viewport_, 1);
    centralLayout->addWidget(timeline_);
    setCentralWidget(central);
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
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Export &Image…"), QKeySequence(tr("Ctrl+E")),
                        this, &MainWindow::exportImage);
    fileMenu->addAction(tr("Export Ani&mation (GIF/MP4)…"),
                        this, &MainWindow::exportAnimation);
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

    QMenu* simulationMenu = menuBar()->addMenu(tr("&Simulation"));
    simulationMenu->addAction(tr("&New Calculation…"), QKeySequence(tr("Ctrl+R")),
                              this, &MainWindow::newCalculation);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("&Frame Structure"), QKeySequence(tr("F")),
                        viewport_, &ViewportWidget::frameStructure);
    QAction* cellAction = viewMenu->addAction(tr("Show Unit &Cell"));
    cellAction->setCheckable(true);
    cellAction->setChecked(true);
    connect(cellAction, &QAction::toggled, viewport_, &ViewportWidget::setShowCell);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About Calango"), this, &MainWindow::about);

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
// Model management
// ---------------------------------------------------------------------------

void MainWindow::setStructure(std::shared_ptr<core::Structure> structure,
                              const QString& sourceName)
{
    structure_ = std::move(structure);
    currentFileName_ = sourceName;
    frames_.clear();
    timeline_->stop();
    timeline_->hide();
    setWindowTitle(sourceName.isEmpty()
                       ? QStringLiteral("Calango")
                       : QStringLiteral("Calango — %1").arg(sourceName));
    notifyStructureChanged();
}

void MainWindow::notifyStructureChanged(bool frameCamera)
{
    viewport_->setStructure(structure_, frameCamera);
    infoWidget_->updateFromStructure(structure_.get());
}

void MainWindow::pushUndo()
{
    undoStack_.push_back(structure_ ? std::make_shared<core::Structure>(*structure_)
                                    : nullptr);
    if (undoStack_.size() > kMaxUndoDepth)
        undoStack_.pop_front();
    redoStack_.clear();
    updateUndoActions();
}

void MainWindow::updateUndoActions()
{
    undoAction_->setEnabled(!undoStack_.empty());
    redoAction_->setEnabled(!redoStack_.empty());
}

void MainWindow::undo()
{
    if (undoStack_.empty())
        return;
    redoStack_.push_back(structure_ ? std::make_shared<core::Structure>(*structure_)
                                    : nullptr);
    structure_ = undoStack_.back();
    undoStack_.pop_back();
    updateUndoActions();
    notifyStructureChanged(false);
    statusBar()->showMessage(tr("Undo"), 2000);
}

void MainWindow::redo()
{
    if (redoStack_.empty())
        return;
    undoStack_.push_back(structure_ ? std::make_shared<core::Structure>(*structure_)
                                    : nullptr);
    structure_ = redoStack_.back();
    redoStack_.pop_back();
    updateUndoActions();
    notifyStructureChanged(false);
    statusBar()->showMessage(tr("Redo"), 2000);
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

void MainWindow::loadFile(const QString& path)
{
    if (!ensureAseAvailable())
        return;
    try {
        auto structure = std::make_shared<core::Structure>(
            pybridge::AseBridge::readStructure(path.toStdString()));
        pushUndo();
        setStructure(std::move(structure), QFileInfo(path).fileName());
        statusBar()->showMessage(tr("Loaded %1 (%2 atoms)")
                                     .arg(path)
                                     .arg(structure_->size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Open Structure"),
                              QString::fromUtf8(e.what()));
    }
}

void MainWindow::openStructure()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Structure"), QString(),
        tr("Structure files (*.xyz *.extxyz *.cif POSCAR CONTCAR *.vasp *.traj);;"
           "All files (*)"));
    if (!path.isEmpty())
        loadFile(path);
}

void MainWindow::openTrajectory()
{
    if (!ensureAseAvailable())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Trajectory"), QString(),
        tr("Trajectories (*.traj *.extxyz *.xyz);;All files (*)"));
    if (path.isEmpty())
        return;

    try {
        const auto rawFrames = pybridge::AseBridge::readTrajectory(path.toStdString());
        if (rawFrames.empty())
            throw std::runtime_error("Trajectory contains no frames");

        pushUndo();
        setStructure(std::make_shared<core::Structure>(rawFrames.front()),
                     QFileInfo(path).fileName());

        frames_.clear();
        frames_.reserve(rawFrames.size());
        for (const auto& frame : rawFrames)
            frames_.push_back(std::make_shared<core::Structure>(frame));

        timeline_->setFrameCount(static_cast<int>(frames_.size()));
        timeline_->show();
        statusBar()->showMessage(tr("Loaded trajectory %1 (%2 frames)")
                                     .arg(path)
                                     .arg(frames_.size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Open Trajectory"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::showFrame(int index)
{
    if (index < 0 || index >= static_cast<int>(frames_.size()))
        return;
    structure_ = frames_[static_cast<std::size_t>(index)];
    notifyStructureChanged(false);
}

void MainWindow::saveStructureAs()
{
    if (!structure_ || !ensureAseAvailable())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Structure As"), QString(),
        tr("XYZ (*.xyz);;Extended XYZ (*.extxyz);;CIF (*.cif);;VASP POSCAR (*.vasp)"));
    if (path.isEmpty())
        return;
    try {
        pybridge::AseBridge::writeStructure(*structure_, path.toStdString());
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
    if (!structure_ || structure_->empty()) {
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
    if (!structure_ || structure_->empty()) {
        statusBar()->showMessage(tr("Open a structure first."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export Animation"));
    auto* form = new QFormLayout(&dialog);

    auto* sourceCombo = new QComboBox(&dialog);
    sourceCombo->addItem(tr("Turntable rotation (360°)"));
    const bool hasTrajectory = frames_.size() > 1;
    if (hasTrajectory)
        sourceCombo->addItem(tr("Trajectory frames (%1)").arg(frames_.size()));
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
        turntable ? framesSpin->value() : static_cast<int>(frames_.size());

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
            viewport_->setStructure(frames_[static_cast<std::size_t>(i)], false);
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
    if (!structure_) {
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
            *structure_, repeats[0]->value(), repeats[1]->value(), repeats[2]->value()));
        pushUndo();
        setStructure(std::move(repeated), tr("%1 (supercell)").arg(currentFileName_));
        statusBar()->showMessage(tr("Supercell created: %1 atoms").arg(structure_->size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Create Supercell"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::cleaveSurface()
{
    if (!structure_ || !structure_->cell().isDefined()) {
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
            *structure_, miller[0]->value(), miller[1]->value(), miller[2]->value(),
            layersSpin->value(), vacuumSpin->value()));
        pushUndo();
        setStructure(std::move(slab),
                     tr("%1 (%2%3%4) slab")
                         .arg(currentFileName_)
                         .arg(miller[0]->value())
                         .arg(miller[1]->value())
                         .arg(miller[2]->value()));
        statusBar()->showMessage(tr("Slab created: %1 atoms").arg(structure_->size()));
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

    pushUndo();
    const bool firstAtom = !structure_ || structure_->empty();
    if (!structure_) {
        structure_ = std::make_shared<core::Structure>();
        currentFileName_ = tr("untitled");
        setWindowTitle(QStringLiteral("Calango — %1").arg(currentFileName_));
    }
    structure_->addAtom(
        {z, {coords[0]->value(), coords[1]->value(), coords[2]->value()}});
    notifyStructureChanged(firstAtom);
    statusBar()->showMessage(tr("Added %1 atom").arg(symbolEdit->text().trimmed()));
}

void MainWindow::changeElementOfSelection()
{
    const auto& selection = viewport_->selection();
    if (!structure_ || selection.empty()) {
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
        structure_->atoms()[static_cast<std::size_t>(index)].atomicNumber = z;
    notifyStructureChanged(false);
}

void MainWindow::translateSelection()
{
    const auto& selection = viewport_->selection();
    if (!structure_ || selection.empty()) {
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
        structure_->atoms()[static_cast<std::size_t>(index)].position += shift;
    notifyStructureChanged(false);
}

void MainWindow::deleteSelectedAtoms()
{
    const auto& selection = viewport_->selection();
    if (!structure_ || selection.empty()) {
        statusBar()->showMessage(tr("Select atoms first (click / Ctrl+click)."));
        return;
    }

    pushUndo();
    // Remove in descending index order so indices stay valid.
    std::vector<int> indices(selection.begin(), selection.end());
    std::sort(indices.rbegin(), indices.rend());
    for (const int index : indices)
        structure_->removeAtom(static_cast<std::size_t>(index));
    notifyStructureChanged(false);
    statusBar()->showMessage(tr("Deleted %n atom(s)", nullptr,
                                static_cast<int>(indices.size())));
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

void MainWindow::newCalculation()
{
    if (!structure_ || structure_->empty()) {
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
        runScript(dialog.script());
}

void MainWindow::runScript(const QString& script)
{
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
            *structure_, (jobDir + QStringLiteral("/structure.extxyz")).toStdString(),
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
        jobRunner_->start(
            QString::fromStdString(pybridge::PythonEngine::instance().executable()),
            QStringLiteral("run.py"), jobDir);
        statusBar()->showMessage(tr("Job running in %1").arg(jobDir));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Run Job"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::onJobFinished(int exitCode, bool crashed)
{
    if (crashed || exitCode != 0 || lastJobDir_.isEmpty())
        return;

    // Offer the relaxed / final structure produced by the job.
    for (const auto* candidate : {"optimized.extxyz", "md_final.extxyz"}) {
        const QString resultPath = lastJobDir_ + QLatin1Char('/')
            + QLatin1String(candidate);
        if (!QFile::exists(resultPath))
            continue;
        const auto answer = QMessageBox::question(
            this, tr("Job Finished"),
            tr("The job produced %1.\nLoad it into the viewport?")
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
