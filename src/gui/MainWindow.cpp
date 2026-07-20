#include "gui/MainWindow.hpp"

#include "core/Structure.hpp"
#include "gui/CalculatorDialog.hpp"
#include "gui/JobLogWidget.hpp"
#include "gui/StructureInfoWidget.hpp"
#include "gui/ViewportWidget.hpp"
#include "jobs/JobRunner.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>
#include <QVBoxLayout>

#include <stdexcept>

namespace calango::gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , jobRunner_(new jobs::JobRunner(this))
{
    setWindowTitle(QStringLiteral("Calango"));
    resize(1280, 840);

    viewport_ = new ViewportWidget(this);
    setCentralWidget(viewport_);

    createMenusAndDocks();

    connect(jobRunner_, &jobs::JobRunner::started,
            jobLogWidget_, &JobLogWidget::onJobStarted);
    connect(jobRunner_, &jobs::JobRunner::outputLine,
            jobLogWidget_, &JobLogWidget::onOutputLine);
    connect(jobRunner_, &jobs::JobRunner::errorLine,
            jobLogWidget_, &JobLogWidget::onErrorLine);
    connect(jobRunner_, &jobs::JobRunner::progress,
            jobLogWidget_, &JobLogWidget::onProgress);
    connect(jobRunner_, &jobs::JobRunner::finished,
            jobLogWidget_, &JobLogWidget::onJobFinished);
    connect(jobLogWidget_, &JobLogWidget::terminateRequested,
            jobRunner_, &jobs::JobRunner::terminate);

    statusBar()->showMessage(tr("Ready — open a structure to begin (File → Open)"));
}

MainWindow::~MainWindow() = default;

void MainWindow::createMenusAndDocks()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open Structure…"), QKeySequence::Open,
                        this, &MainWindow::openStructure);
    fileMenu->addAction(tr("Save Structure &As…"), QKeySequence::SaveAs,
                        this, &MainWindow::saveStructureAs);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit,
                        qApp, &QApplication::closeAllWindows);

    QMenu* buildMenu = menuBar()->addMenu(tr("&Build"));
    buildMenu->addAction(tr("Create &Supercell…"), this, &MainWindow::createSupercell);

    QMenu* simulationMenu = menuBar()->addMenu(tr("&Simulation"));
    simulationMenu->addAction(tr("&New Calculation…"), QKeySequence(tr("Ctrl+R")),
                              this, &MainWindow::newCalculation);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("&Frame Structure"), QKeySequence(tr("F")),
                        viewport_, &ViewportWidget::frameStructure);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About Calango"), this, &MainWindow::about);

    auto* infoDock = new QDockWidget(tr("Structure"), this);
    infoDock->setObjectName(QStringLiteral("structureDock"));
    infoWidget_ = new StructureInfoWidget(infoDock);
    infoDock->setWidget(infoWidget_);
    addDockWidget(Qt::LeftDockWidgetArea, infoDock);

    jobDock_ = new QDockWidget(tr("Job Output"), this);
    jobDock_->setObjectName(QStringLiteral("jobDock"));
    jobLogWidget_ = new JobLogWidget(jobDock_);
    jobDock_->setWidget(jobLogWidget_);
    addDockWidget(Qt::BottomDockWidgetArea, jobDock_);

    viewMenu->addSeparator();
    viewMenu->addAction(infoDock->toggleViewAction());
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
           "Install it into the interpreter Calango was built against:\n"
           "    pip install ase\n\n"
           "Details:\n%1")
            .arg(QString::fromStdString(python.lastError())));
    return false;
}

void MainWindow::loadFile(const QString& path)
{
    if (!ensureAseAvailable())
        return;
    try {
        auto structure = std::make_shared<core::Structure>(
            pybridge::AseBridge::readStructure(path.toStdString()));
        setStructure(std::move(structure), QFileInfo(path).fileName());
        statusBar()->showMessage(tr("Loaded %1 (%2 atoms)")
                                     .arg(path)
                                     .arg(structure_->size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Open Structure"),
                              QString::fromUtf8(e.what()));
    }
}

void MainWindow::setStructure(std::shared_ptr<core::Structure> structure,
                              const QString& sourceName)
{
    structure_ = std::move(structure);
    currentFileName_ = sourceName;
    setWindowTitle(sourceName.isEmpty()
                       ? QStringLiteral("Calango")
                       : QStringLiteral("Calango — %1").arg(sourceName));
    notifyStructureChanged();
}

void MainWindow::notifyStructureChanged()
{
    viewport_->setStructure(structure_);
    infoWidget_->updateFromStructure(structure_.get());
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
        setStructure(std::move(repeated),
                     tr("%1 (supercell)").arg(currentFileName_));
        statusBar()->showMessage(tr("Supercell created: %1 atoms").arg(structure_->size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Create Supercell"), QString::fromUtf8(e.what()));
    }
}

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
