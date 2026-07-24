#pragma once

#include <QMainWindow>

#include <deque>
#include <map>
#include <memory>
#include <utility>
#include <vector>

class QComboBox;
class QDockWidget;
class QMenu;
class QTabBar;
class QTimer;
class QToolButton;

namespace calango::core {
class Structure;
}
namespace calango::jobs {
class JobRunner;
}

namespace calango::gui {

class BrandingPanel;
class JobLogWidget;
class NebDialog;
class SimulationWizardBase;
class MetricPlotWidget;
class ProcessManagerPanel;
class RemoteAccessPanel;
class StructureInfoWidget;
class SystemStatusBar;
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

    /// Show the startup Welcome Screen (banner + recent projects + quick
    /// actions) and act on the user's choice. No-op semantics are the
    /// caller's job (it checks WelcomeDialog::showAtStartupEnabled()).
    void showWelcomeScreen();

    /// (Re)apply the persisted appearance theme (QSettings "appearance/theme"):
    /// palette/stylesheet via ThemeManager plus the Zone-1 logo variant and the
    /// status-bar thread readout. Called at startup and after Preferences edits.
    void applyAppearanceTheme();

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
    /// Record a successfully opened path in the persistent recent-files list
    /// and refresh the "Open Recent" submenu.
    void addRecentFile(const QString& path);
    void updateRecentFilesMenu();
    void saveStructureAs();
    void saveTrajectoryAs();
    void exportImage();
    void exportAnimation();

    void openSupercellBuilder();
    void cleaveSurface();
    void addAtom();
    void changeElementOfSelection();
    void translateSelection();
    void deleteSelectedAtoms();
    void showBondEditor();
    /// Structure panel → "Edit Structure…": unit cell + atomic positions
    /// editor, applied through the document's undo stack.
    void editStructure();
    void assignBondOrderToSelection(int order);
    void showPreferences();
    void undo();
    void redo();

    void singlePointCalculation();
    void geometryOptimization();
    void molecularDynamics();
    void openMonteCarlo();
    void openNudgedElasticBand();
    void onRemoteResultsReady(const QString& localDir);
    void onJobFinished(int exitCode, bool crashed);
    // -- Per-process metric routing (Results panel) ------------------------
    // Live jobRunner samples/lines are buffered into the running process's
    // record and only mirrored to the shared plots/log when that process is
    // the one currently selected, so a new run never overwrites an old run's
    // metrics.
    void onEnergySample(int step, double value);
    void onTemperatureSample(int step, double value);
    void onForceSample(int step, double value);
    void onPressureSample(int step, double value);
    void onTargetTemperature(double value);
    void onTargetPressure(double value);
    void onJobOutputLine(const QString& line);
    void onJobErrorLine(const QString& line);
    void onJobProgress(int step, int total);
    /// Results panel process selector changed — repopulate the tabs.
    void onProcessSelected(int comboIndex);
    /// One live trajectory frame from the running job's stdout stream.
    void onFrameStreamed(const std::shared_ptr<core::Structure>& frame);
    void showBandStructure();
    /// Open the band/PDOS viewer for a finished job directory.
    void openBandResults(const QString& directory);
    /// Open the phonon band structure + PhDOS viewer for a finished job dir.
    void openPhononResults(const QString& directory);
    /// Simulation → "Optics…": linear optical-response wizard (GPAW response).
    void showOptics();
    /// Open the optical-spectra viewer for a finished job directory.
    void openOpticsResults(const QString& directory);
    /// "Load Result" from the Process panel: band data, trajectory or
    /// final structure — whatever the task directory contains.
    void onProcessResultRequested(const QString& directory);
    /// "View ASE Script": open the task directory's run.py in a
    /// syntax-highlighted, copyable viewer.
    void onViewScriptRequested(const QString& directory);
    /// "Delete Process": confirm, stop it if running, purge proc_<id>/, drop
    /// its record + selector entry + panel row.
    void onDeleteProcessRequested(int id);
    void showFrame(int index);
    void showBrillouinZone();
    void showRdf();
    void showCoordination();
    void showDistributions();
    void showStructureFactor();
    void showSymmetry();
    void showXrd();
    void openNanoBuilder();
    void openPhononBuilder();
    void openSqsBuilder();
    void openClusterExpansion();
    /// Simulation → "Cluster Expansion Calculation…": batch-relax the current
    /// document's ensemble and build a formation-energy convex hull.
    void clusterExpansionCalculation();
    /// Simulation → "Effective Bands…": Popescu-Zunger unfolding of the
    /// active supercell onto a reference primitive cell.
    void effectiveBandsCalculation();
    void openNanoparticleBuilder();
    void showAdsorption();
    void showWarrenCowley();
    void showLocalEntropy();
    /// Analysis → "Partial Charge Analysis…": Bader / Voronoi / Hirshfeld
    /// partitioning as a background DFT job, tabulated and colour-mapped.
    void showPartialCharge();
    /// Analysis → "Velocity Autocorrelation Function (VACF)…": VACF, VDOS,
    /// Green-Kubo diffusion and relaxation time from the current trajectory.
    void showVacf();
    void showRamanModes();
    void newProject();
    void showVolumetricData();
    /// Analysis → "Electron Localization Function (ELF)…": compute η(r) via GPAW
    /// (or load an existing ELF grid) and view it as an isosurface / slice.
    void showElf();
    void showDatasetManager();
    /// MLIP → Trainer…: MACE training-config (YAML) builder + launcher.
    void openMaceTrainer();
    void openExamplesBrowser();
    void openRayTraceDialog();
    void addRandomNoise();
    void loadExample(const QString& resourcePath, const QString& recommendation);

    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    /// Keep documents_ in the same order as the (movable) tab bar and re-index
    /// the two-digit sequence numbers after a drag reorder.
    void onTabMoved(int from, int to);
    /// Viewport toolbar / tab context menu → "Duplicate Workspace / Extract
    /// Frame to New Tab". Clones the geometry currently on screen into a new,
    /// independent static workspace tab. For a trajectory this extracts only
    /// the frame the timeline is parked on, leaving the source tab's full
    /// timeline intact.
    void duplicateOrExtractFrame();
    /// Right-click handler for the workspace tab bar: a context menu with the
    /// duplicate/extract action and Close Tab, targeting the clicked tab.
    void showTabContextMenu(const QPoint& pos);

    void about();

private:
    struct Document {
        std::shared_ptr<core::Structure> structure;
        std::vector<std::shared_ptr<core::Structure>> frames; ///< trajectory
        std::deque<std::shared_ptr<core::Structure>> undoStack;
        std::deque<std::shared_ptr<core::Structure>> redoStack;
        QString fileName;
        /// Process / task descriptor shown in the tab title's third field
        /// (e.g. "Single-Point Calculation"); empty for a plain structure.
        QString task;
    };

    struct ProcessRecord; // full definition below

    void createMenusAndDocks();
    Document* currentDocument();
    /// Creates an empty "Untitled" tab when none exists (for Add Atom).
    Document& ensureDocument();
    int addDocument(std::shared_ptr<core::Structure> structure, const QString& name,
                    std::vector<std::shared_ptr<core::Structure>> frames = {},
                    const QString& task = {});
    /// Re-derive every tab's title as "NN - Formula - Task" with a zero-padded
    /// two-digit sequence number, kept in sync as tabs are added, closed or
    /// reordered. Formula is read live from each document's structure.
    void refreshTabTitles();
    /// Push the current document's state into all views.
    void syncViewsToCurrent(bool frameCamera);
    /// Replace the current document's structure (supercell, slab, undo...).
    void replaceCurrentStructure(std::shared_ptr<core::Structure> structure,
                                 const QString& name);
    void notifyStructureChanged(bool frameCamera = true);
    void pushUndo();
    void updateUndoActions();
    /// Write run.py + structure.extxyz into a fresh job directory; returns
    /// the directory ("" on failure). Shared by local runs (JobRunner) and
    /// remote submissions (RemoteAccessPanel). `procId >= 0` names the dir
    /// `proc_<id>` (per-process metric store); -1 falls back to a timestamp.
    QString stageJob(const QString& script, int procId = -1);
    /// Repaint the Results tabs from process `id`'s buffered (or on-disk)
    /// metrics; add a process to the selector; dump/reload a process's
    /// metrics to/from its proc_<id> directory.
    void syncResultsToProcess(int id);
    void addProcessToSelector(int id, const QString& label);
    void writeProcessMetrics(int id);
    void loadProcessMetrics(int id);
    /// Parse `<directory>/metrics.json` (written live by the generated ASE
    /// scripts) into a record's metric series. Returns false if absent/invalid.
    bool readMetricsJson(const QString& directory, ProcessRecord& record) const;
    /// Timer tick during a local run: re-read the running process's
    /// metrics.json and repaint the Results plots if it's the selected process.
    void pollLiveMetrics();
    /// Launch a staged local job. `taskLabel` names it in the Process
    /// panel; `expectFrames` opens a live trajectory tab that streamed
    /// CALANGO_FRAME blocks append to while the job runs.
    void runScript(const QString& script, const QString& pythonExe,
                   const QString& taskLabel = {}, bool expectFrames = true);
    int indexOfDocument(const Document* document) const;
    bool ensureAseAvailable();
    /// Shared preconditions for the dedicated Simulation dialogs: a non-empty
    /// current structure, ASE available, and no job already running. Shows the
    /// appropriate message (titled `title`) and returns false if not ready.
    bool prepareSimulation(const QString& title);
    /// Run a 4-stage simulation wizard: exec it, then launch its script
    /// locally or submit it remotely per the chosen action. `expectFrames`
    /// is false for runs that produce no trajectory (single-point, bands),
    /// so no live trajectory tab is opened.
    void runSimulationWizard(SimulationWizardBase& wizard, const QString& label,
                             bool expectFrames = true);

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

    /// Buffered metrics + console log of one background process, keyed by its
    /// Process Manager id, so the Results panel can show any run's history and
    /// live runs never overwrite an earlier run's series.
    struct ProcessRecord {
        QString label;
        QString directory;
        QString log;
        std::vector<std::pair<int, double>> energy;
        std::vector<std::pair<int, double>> temperature;
        std::vector<std::pair<int, double>> force;
        std::vector<std::pair<int, double>> pressure;
        bool hasTempTarget = false;
        double tempTarget = 0.0;
        bool hasPressTarget = false;
        double pressTarget = 0.0;
        /// Latest progress from metrics.json ("progress":{step,total}); -1 = none.
        int progressStep = -1;
        int progressTotal = -1;
    };
    std::map<int, ProcessRecord> processRecords_;
    int selectedProcessId_ = -1; ///< process whose data the Results tabs show

    QTabBar* tabBar_ = nullptr;
    ViewportWidget* viewport_ = nullptr;
    BrandingPanel* brandingPanel_ = nullptr;      ///< zone 1 (theme-aware logo)
    SystemStatusBar* systemStatusBar_ = nullptr;  ///< permanent status widgets
    QMenu* recentMenu_ = nullptr; ///< File → Open → Open Recent (dynamic)
    QComboBox* processSelector_ = nullptr; ///< Results-panel process dropdown
    StructureInfoWidget* infoWidget_ = nullptr;
    JobLogWidget* jobLogWidget_ = nullptr;
    MetricPlotWidget* energyPlot_ = nullptr;
    MetricPlotWidget* temperaturePlot_ = nullptr;
    MetricPlotWidget* forcePlot_ = nullptr;
    MetricPlotWidget* pressurePlot_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    QDockWidget* jobDock_ = nullptr;
    QDockWidget* visualEffectsDock_ = nullptr; ///< zone 9 (Lighting + effects)
    QDockWidget* remoteDock_ = nullptr;
    RemoteAccessPanel* remotePanel_ = nullptr;
    ProcessManagerPanel* processPanel_ = nullptr;
    /// Process-panel id of the running local job (-1 when idle).
    int currentTaskId_ = -1;
    /// Document receiving live streamed frames (null outside runs;
    /// cleared when its tab is closed mid-run).
    Document* liveDoc_ = nullptr;
    /// Band of images staged as band.extxyz on the next stageJob (NEB);
    /// consumed and cleared by stageJob.
    std::vector<std::shared_ptr<core::Structure>> stagedBandFrames_;
    /// Cluster-expansion ensemble staged as configs.extxyz on the next
    /// stageJob; consumed and cleared there, like stagedBandFrames_.
    std::vector<std::shared_ptr<core::Structure>> stagedEnsembleFrames_;
    /// Primitive reference cell staged as primitive.extxyz on the next
    /// stageJob (band unfolding); consumed and cleared there.
    std::shared_ptr<const core::Structure> stagedPrimitive_;
    /// Non-modal NEB builder window (owned via WA_DeleteOnClose; nulled on close).
    NebDialog* nebDialog_ = nullptr;
    jobs::JobRunner* jobRunner_ = nullptr;
    /// Polls the running process's metrics.json for live Results-graph updates.
    QTimer* metricsTimer_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    /// Element placed by the viewport's Insertion mode (toolbar selector).
    int activeElementZ_ = 6;
    QToolButton* elementButton_ = nullptr;
    /// Shared between View → Orthographic and the frame-panel toolbar.
    QAction* orthoAction_ = nullptr;
};

} // namespace calango::gui
