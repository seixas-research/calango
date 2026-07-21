#pragma once

#include <QMainWindow>

#include <deque>
#include <memory>
#include <vector>

class QDockWidget;
class QTabBar;
class QToolButton;

namespace calango::core {
class Structure;
}
namespace calango::jobs {
class JobRunner;
}

namespace calango::gui {

class JobLogWidget;
class MetricPlotWidget;
class RemoteAccessPanel;
class StructureInfoWidget;
class TimelineWidget;
class ViewportWidget;

/// Application shell and MVC "Controller" with a tabbed multi-document
/// workspace: each tab is a Document (structure + optional trajectory +
/// its own undo history). All documents share the single accelerated
/// viewport — the tab bar switches which document the views observe —
/// so display settings apply consistently and no GL context churn occurs.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Load any ASE-readable structure file into a new tab.
    void loadFile(const QString& path);

protected:
    /// Persists the window geometry and dock layout before shutdown; the
    /// embedded Python interpreter is released by PythonEngine's destructor
    /// in main() after the window is gone.
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void openProject();
    /// Both return false when nothing was written (error or cancel) —
    /// the close-event guard aborts quitting in that case.
    bool saveProject();
    bool saveProjectAs();
    void openStructure();
    void openTrajectory();
    void saveStructureAs();
    void saveTrajectoryAs();
    void exportImage();
    void exportAnimation();

    void createSupercell();
    void cleaveSurface();
    void addAtom();
    void changeElementOfSelection();
    void translateSelection();
    void deleteSelectedAtoms();
    void showBondEditor();
    void assignBondOrderToSelection(int order);
    void showPreferences();
    void undo();
    void redo();

    void newCalculation();
    void newRemoteCalculation();
    void onRemoteResultsReady(const QString& localDir);
    void onJobFinished(int exitCode, bool crashed);
    void showFrame(int index);
    void showBrillouinZone();
    void showRdf();
    void showCoordination();
    void showDistributions();
    void showStructureFactor();
    void showXrd();
    void openNanoBuilder();
    void openPhononBuilder();
    void openSqsBuilder();
    void showWarrenCowley();
    void showLocalEntropy();
    void showRamanModes();
    void newProject();
    void showVisualEffects();
    void showVolumetricData();
    void showDatasetManager();
    void openExamplesBrowser();
    void openRayTraceDialog();
    void addRandomNoise();
    void loadExample(const QString& resourcePath, const QString& recommendation);

    void onTabChanged(int index);
    void onTabCloseRequested(int index);

    void about();

private:
    struct Document {
        std::shared_ptr<core::Structure> structure;
        std::vector<std::shared_ptr<core::Structure>> frames; ///< trajectory
        std::deque<std::shared_ptr<core::Structure>> undoStack;
        std::deque<std::shared_ptr<core::Structure>> redoStack;
        QString fileName;
    };

    void createMenusAndDocks();
    Document* currentDocument();
    /// Creates an empty "Untitled" tab when none exists (for Add Atom).
    Document& ensureDocument();
    int addDocument(std::shared_ptr<core::Structure> structure, const QString& name,
                    std::vector<std::shared_ptr<core::Structure>> frames = {});
    /// Push the current document's state into all views.
    void syncViewsToCurrent(bool frameCamera);
    /// Replace the current document's structure (supercell, slab, undo...).
    void replaceCurrentStructure(std::shared_ptr<core::Structure> structure,
                                 const QString& name);
    void notifyStructureChanged(bool frameCamera = true);
    void pushUndo();
    void updateUndoActions();
    /// Write run.py + structure.extxyz into a fresh job directory under
    /// app-data; returns the directory ("" on failure). Shared by local
    /// runs (JobRunner) and remote submissions (RemoteAccessPanel).
    QString stageJob(const QString& script);
    void runScript(const QString& script, const QString& pythonExe);
    bool ensureAseAvailable();

    // -- .calproj project workspace persistence ----------------------------
    /// Serialize the whole session (documents, tabs, viewport color
    /// mapping, job console + metric series) into `path`. Reports errors
    /// itself; returns false on failure.
    bool writeProject(const QString& path);
    /// Replace the session with the one stored in `path`.
    bool readProject(const QString& path);
    void closeAllDocuments();

    std::vector<std::unique_ptr<Document>> documents_;
    QString lastJobDir_;
    /// Unsaved-changes flag: set by every workspace mutation (undoable
    /// edits, document add/close, job runs), cleared by project
    /// save/load. Drives the quit confirmation in closeEvent().
    bool isDirty_ = false;
    QString projectPath_; ///< current .calproj file ("" until saved/opened)

    QTabBar* tabBar_ = nullptr;
    ViewportWidget* viewport_ = nullptr;
    StructureInfoWidget* infoWidget_ = nullptr;
    JobLogWidget* jobLogWidget_ = nullptr;
    MetricPlotWidget* energyPlot_ = nullptr;
    MetricPlotWidget* temperaturePlot_ = nullptr;
    MetricPlotWidget* forcePlot_ = nullptr;
    MetricPlotWidget* pressurePlot_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    QDockWidget* jobDock_ = nullptr;
    QDockWidget* remoteDock_ = nullptr;
    RemoteAccessPanel* remotePanel_ = nullptr;
    jobs::JobRunner* jobRunner_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    /// Element placed by the viewport's Insertion mode (toolbar selector).
    int activeElementZ_ = 6;
    QToolButton* elementButton_ = nullptr;
    /// Shared between View → Orthographic and the frame-panel toolbar.
    QAction* orthoAction_ = nullptr;
};

} // namespace calango::gui
