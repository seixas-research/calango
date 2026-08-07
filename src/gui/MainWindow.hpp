#pragma once

#include "core/CalculatorConfig.hpp"
#include "core/StructureTransforms.hpp"
#include "render/Camera.hpp"
#include "render/Film.hpp"

#include <QMainWindow>
#include <QByteArray>
#include <QImage>
#include <QList>
#include <QPair>
#include <QSize>
#include <QString>

#include <deque>
#include <functional>
#include <map>
#include <set>
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
class PointOfViewDialog;
class SimulationWizardBase;
class MetricPlotWidget;
class OverlayPanel;
class ProcessManagerPanel;
class RemoteAccessPanel;
class RepresentationPanel;
class StructureInfoWidget;
class VolumetricPanel;
class SystemStatusBar;
class TimelineWidget;
class FilmTimelineWidget;
class FilmProductionDialog;
class ViewportWidget;
class OrchestrationWindow;

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
    /// File → Import/Export → "Export to Alembic…": the scene geometry as a
    /// baked .abc cache for the DCC tools (Blender, Houdini, Maya). A loaded
    /// trajectory can be written as animated samples.
    void exportAlembic();

    void openSupercellBuilder();
    /// Build → "Macromolecules…" / "Water & Ice…": the two molecular-system
    /// builders. Each opens its generated cell as a new workspace tab.
    void openMacromoleculeBuilder();
    void openWaterIceBuilder();
    /// Build → "Liquid / Gas Interface…": open a fluid region on the current
    /// structure and pack it with a liquid, a gas, a mixture or an ionic
    /// solution. Opens the result as a new workspace tab, leaving the
    /// substrate's own tab untouched.
    void openLiquidInterfaceBuilder();
    /// Build → "Dislocation…": insert a Volterra line defect into the current
    /// structure by displacing its atoms with a closed-form elastic field.
    /// Opens the result as a new workspace tab.
    void openDislocationBuilder();
    /// Build → "Solid Interface…": stacking faults, twin boundaries,
    /// bicrystals and (multi-phase) polycrystals built from the current
    /// structure as the parent lattice. Also a new tab.
    void openSolidInterfaceBuilder();
    /// Modules → 2D Materials → "Graphene Oxide…": functionalized
    /// graphene at target coverages, opened as a new workspace tab.
    void openGrapheneOxideBuilder();
    void cleaveSurface();
    void addAtom();
    void changeElementOfSelection();
    void translateSelection();
    void deleteSelectedAtoms();
    void showBondEditor();
    /// Representation panel -> "Complete with hydrogens": fill in the
    /// hydrogens each atom's valence implies. Lives here rather than in the
    /// panel because it ADDS atoms, and only the window owns the mutable
    /// document and the undo stack.
    void completeWithHydrogens();
    /// Structure panel → "Edit Structure…": unit cell + atomic positions
    /// editor, applied through the document's undo stack.
    void editStructure();
    // Structure panel action row. These were buttons inside the Edit Structure
    // dialog (acting on its working copy, undone only by cancelling it); here
    // they mutate the document and are undoable like any other edit.
    void centerStructure();
    void addVacuumLayer();
    void wrapStructureIntoCell();
    void showPreferences();
    void undo();
    void redo();

    void singlePointCalculation();
    /// Modules → Parameters Convergence → "Plane-wave Cutoff Convergence…":
    /// single points over an ascending cutoff list, judged against the run at
    /// the highest cutoff.
    void planeWaveCutoffConvergence();
    /// Modules → Parameters Convergence → "K-points Convergence…": single
    /// points over an ascending sequence of Monkhorst-Pack meshes, judged
    /// against the densest one.
    void kPointsConvergence();
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
    /// "Create Mode Trajectory Tab" from the Vibrational Analysis dialog: open
    /// one vibrational period as a scrubbable multi-frame workspace tab.
    void openModeTrajectory(
        const std::vector<std::shared_ptr<core::Structure>>& frames,
        const QString& label);
    /// Simulation → "Optics…" and Modules → 2D Materials → "2D Optics…":
    /// the same linear-response wizard, the second adding the vacuum-axis
    /// question and the 2D observables derived from it.
    void showOptics();
    void show2DOptics();
    /// Electronics → "Nonlinear Optics…": second-harmonic generation, the
    /// shift current and the linear susceptibility tensor through GPAW's
    /// gpaw.nlopt.
    ///
    /// The one response module that converges its OWN ground state rather than
    /// inheriting a Single-Point baseline — gpaw.nlopt requires point-group
    /// symmetry off and a converged empty manifold, which an ordinary baseline
    /// does not have.
    void showNonlinearOptics();
    /// Open the nonlinear-response viewer for a finished job (nlopt.json).
    void openNonlinearOpticsResults(const QString& directory);
    /// Modules → 2D Materials → "2D Bands…": band surfaces E_n(kx, ky) over
    /// the two-dimensional Brillouin zone. Needs a completed single point with
    /// a saved GPAW density, exactly as Electronic Structure does.
    void show2DBands();
    /// Open the 2D band-surface viewer for a finished job directory
    /// (reads its bands_2d.json).
    void open2DBandsResults(const QString& directory);
    /// Modules → 2D Materials → "2D Workfunction…": Φ = E_vac − E_F from the
    /// planar-averaged electrostatic potential of an inherited ground state.
    /// Needs a completed single point with a saved GPAW density, exactly as
    /// 2D Bands does.
    void show2DWorkfunction();
    /// Open the work-function viewer for a finished job directory
    /// (reads its workfunction.json).
    void openWorkfunctionResults(const QString& directory);
    /// Electronics → "Charged defects…": formation energies and transition
    /// levels from a pristine host + neutral defect pair of single points.
    void showChargedDefects();
    void show2DChargedDefects();
    /// Open the charged-defect formation-energy diagram for a finished job.
    void openChargedDefectResults(const QString& directory);
    void open2DChargedDefectResults(const QString& directory);
    /// Open the Wannier-interpolated Fermi-surface viewer for a finished job.
    void openFermiSurfaceResults(const QString& directory);
    /// Open the topological-invariants viewer for a finished job.
    void openTopologyResults(const QString& directory);
    /// Simulation → "GW Calculations…": one-shot G₀W₀ on a completed SCF.
    void showGwCalculations();
    /// Completed Quantum ESPRESSO runs with a saved `.save` directory — the
    /// baselines Yambo converts with p2y.
    QList<QPair<QString, QString>> espressoBaselines() const;
    void openOpticsWizard(bool twoDimensional);
    /// Open the optical-spectra viewer for a finished job directory.
    void openOpticsResults(const QString& directory);
    /// Open the G₀W₀ quasiparticle viewer for a finished job directory
    /// (reads its gw.json summary — same schema for GPAW and Yambo).
    void openGwResults(const QString& directory);
    /// Register every .cube in `directory` in the Volumetric Data dock,
    /// labelled from its filename. Returns how many were added.
    ///
    /// One place, because a single-point run can now write six different
    /// fields (all-electron, pseudo, spin, Hartree, ELF, kinetic energy) and
    /// every caller that used to look for the single hard-coded "density.cube"
    /// silently dropped the other five on the floor.
    int registerDensityCubes(const QString& directory);
    /// The interpreter an ad-hoc post-processing script should run under for
    /// `kind` — the engine's Preferences preset, then the last global env,
    /// then the embedded interpreter.
    ///
    /// The same resolution SimulationWizardBase::pythonExecutable() does. A
    /// post-process imports the same modules the run did (gpaw, here), so
    /// defaulting it to the embedded interpreter guarantees a
    /// ModuleNotFoundError on every machine where GPAW lives in a conda env —
    /// which is all of them.
    QString pythonForEngine(core::CalculatorKind kind) const;
    /// Open the MLWF centres table + orbital viewer for a finished job dir.
    /// Open the dedicated Single-Point Viewer on a finished job directory
    /// (reads its single_point.json summary).
    void openSinglePointResults(const QString& directory);
    /// Copy the converged per-atom results (forces, magnetic moments) from a
    /// finished single point's `single_point.extxyz` onto the structure in the
    /// active tab, so the viewport's vector overlays can draw them.
    ///
    /// Copies the FIELDS rather than swapping the structure: a single point
    /// does not move the atoms, and replacing the object would discard the
    /// selection, the undo stack and any editing the user has done since. A
    /// mismatched atom count (the tab was changed, or another structure is
    /// forward) is a no-op — this must never write one run's moments onto a
    /// different system.
    void adoptSinglePointResults(const QString& directory);
    /// Open the two-panel convergence window (energy per atom and maximum
    /// force vs. the swept parameter) on a finished sweep's directory —
    /// cutoff_convergence.json / kpoints_convergence.json respectively.
    void openCutoffConvergenceResults(const QString& directory);
    void openKpointsConvergenceResults(const QString& directory);
    /// Open the Random Noise Viewer (energy and force distributions, their
    /// standard deviations, and the ensemble export) on a finished run's
    /// directory — reads random_noise.json.
    void openRandomNoiseResults(const QString& directory);
    /// Open the Geometry Optimization Viewer on a finished relaxation's
    /// directory (reads geometry_optimization.json + opt.traj).
    void openGeometryOptimizationResults(const QString& directory);
    /// Open the dedicated MLWF Viewer (viewport orbital overlays + Wannier
    /// interpolation launcher) on a finished job directory.
    void openMlwfResults(const QString& directory);
    /// Register each Wannier orbital cube from a finished MLWF job's
    /// wannier.json into the Volumetric Data dock's "Data" tab.
    void registerWannierOrbitals(const QString& directory);
    /// Single-Point Viewer → "Get Volumetric Data": register an existing
    /// density.cube from `directory` into the Volumetric Data dock, or export
    /// one from the run's saved .gpw as a job when none exists yet.
    void onGetVolumetricData(const QString& directory);
    /// Time series (T, E, P, V), g(r) and the trajectory player for a
    /// finished MD run.
    void openMolecularDynamicsResults(const QString& directory);
    /// Electronics → "X-ray Absorption Spectroscopy (XAS)…": core-hole setup
    /// generation, ground state and spectrum, following the GPAW tutorial.
    void showXas();
    /// Electronics → "Hubbard Parameter Calculation…": U from linear response
    /// (Cococcioni & de Gironcoli) rather than from a literature table.
    void showHubbardParameters();
    /// Open the XAS spectrum viewer for a finished job directory.
    void openXasResults(const QString& directory);
    /// Electronics → "Born Effective Charges…": stage and launch the Z* run.
    void showBornCharges();
    /// Open the Z* tensor read-out for a completed Born-charges process.
    void openBornChargesResults(const QString& directory);
    /// Electronics → "Raman and IR Spectroscopy…": stage and launch the
    /// vibrational-spectroscopy post-process, which consumes a completed Born
    /// Effective Charges run (and, for Raman, an Optics run's settings).
    void showRamanIrSpectroscopy();
    /// Open the spectra + mode table for a completed Raman/IR process.
    void openRamanIrResults(const QString& directory);

    /// Right-click on a process: its viewers, plus the actions the icon bar
    /// already carries (load result, view script, open folder, delete).
    void onProcessContextMenu(const QString& directory, const QPoint& globalPos);

    /// View → "Reset Layout": restore the default dock arrangement captured at
    /// construction, including the branding panel's visibility.
    void resetLayout();
    /// View toolbar → "Set point-of-view…": the modeless camera editor.
    void showPointOfView();
    /// View toolbar → "Reset camera" [F]: apply the default point-of-view the
    /// user stored in ~/.calango/settings.json, or auto-frame the structure
    /// when none has been stored.
    void resetCamera();
    /// View toolbar -> "Film production...": author the current tab's film.
    void showFilmProduction();
    /// Film mode on/off: swaps the trajectory timeline for the film timeline
    /// and takes the camera over.
    void setFilmMode(bool on);
    /// One instant of the film applied to the viewport - camera, fade, cast
    /// opacities and (with a trajectory) the displayed frame.
    void showFilmTime(double seconds);
    /// Re-couple the current tab's film to that tab's trajectory and re-range
    /// the film timeline. Called on tab switch and whenever frames change.
    void refreshFilmTimeline();
    /// Snapshot / restore the Representation panel's cast opacities around a
    /// film preview: the film ANIMATES them but does not own them.
    void rememberCastOpacities();
    void restoreCastOpacities();
    /// Render one side of a dissolve off-screen, with that shot's own cast
    /// opacities applied. Both ends of a film transition are static poses, so
    /// the result is valid for the whole dissolve — see filmCrossfadeKey_.
    QImage renderFilmShot(const render::PointOfView& pov,
                          const std::vector<render::FilmCastOpacity>& casts);
    /// Apply `casts` on top of the film's baseline opacities. Returns true if
    /// anything actually changed, so callers can skip the renderer rebuild.
    bool applyFilmCastOpacities(
        const std::vector<render::FilmCastOpacity>& casts);
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
    /// Analysis → "Magnetic Space Group…": the BNS classification of the
    /// current structure from its coordinates plus its magnetic moments.
    void showMagneticSpaceGroup();
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
    /// Build → "Add adsorbate…": place one atom or one molecule/radical on the
    /// current geometry, opening substrate + adsorbate as a NEW tab.
    void openAddAdsorbate();
    void showAdsorption();
    void showWarrenCowley();
    void showLocalEntropy();
    /// Analysis → "Partial Charge Analysis…": Bader / Voronoi / Hirshfeld
    /// partitioning as a background DFT job, tabulated and colour-mapped.
    void showPartialCharge();
    /// Analysis → "Velocity Autocorrelation Function (VACF)…": VACF, VDOS,
    /// Green-Kubo diffusion and relaxation time from the current trajectory.
    void showVacf();
    void newProject();
    /// Viewport toolbar → "Lattice Plane…": interactive Miller-index plane +
    /// volumetric color-slice overlay in the main 3D viewport.
    /// Viewport toolbar → "Custom overlay…": geometric-primitive overlay manager.
    /// Simulation → "Wannier Functions…": set up +
    /// launch the localization through the standardized wizard (engine
    /// selection + per-engine Conda env). The viewer opens when the job
    /// finishes.
    void showWannier();
    /// Electronics → "Wannier Interpolation…": interpolated band structure +
    /// projected DOS (H(R) → H(k)) from a completed MLWF run.
    void showWannierInterpolation();
    /// Electronics → "Fermi Surface…": E_n(k) = E_F sheets on a dense
    /// interpolated k-grid, from a completed MLWF run.
    void showFermiSurface();
    /// Electronics → "Topological Invariants…": Chern number / Z₂ index from the
    /// hybrid Wannier centre flow, from a completed MLWF run.
    void showTopologicalInvariants();
    /// Completed MLWF processes, as (label, job directory) — the candidates
    /// each Wannier post-process offers in its "Source MLWF process" step.
    /// Keyed on wannier.json, which the MLWF script writes only on success.
    QList<QPair<QString, QString>> completedMlwfRuns() const;

    /// Interpreter a Wannier post-process must run under: the one the MLWF run
    /// in `mlwfDir` itself used (from its calculator.json), else the GPAW
    /// environment from Preferences. NOT the embedded interpreter — it has ASE
    /// but no GPAW, and these scripts all restart GPAW from the localized
    /// wavefunctions.
    QString pythonForMlwfRun(const QString& mlwfDir) const;
    /// True when at least one completed MLWF run exists; otherwise explains
    /// what to run first and returns false. Only this case is refused before
    /// the dialog opens — which run to use, and whether its wavefunctions are
    /// still on disk, is the dialog's own first step.
    bool requireMlwfPrerequisite(const QString& title);
    /// Completed processes that saved GPAW wavefunctions (.gpw), as (label,
    /// directory) pairs — the baselines the MLWF post-process can restart from.
    QList<QPair<QString, QString>> gpawBaselines() const;
    /// Analysis → "Charge Density Difference (CDD)…": pick a completed
    /// single-point, split its atoms into two subsystems, and difference the
    /// densities.
    void showChargeDensityDifference();
    /// Processes whose directory holds `resultFile`, as label -> path. Used to
    /// offer one run's output as another run's input (Born charges and the
    /// dielectric function feeding a phonon dispersion).
    QList<QPair<QString, QString>> processResults(const QString& resultFile) const;
    /// The same set as (label, absolute path to the restart FILE) pairs, for the
    /// wizards whose scripts call `GPAW("<file>")` directly — bands, optics and
    /// the GPAW G₀W₀ path. Handing those a directory produces a script that
    /// only fails once the job is already running.
    QList<QPair<QString, QString>> gpawDensityFiles() const;
    /// Completed processes holding a VASP CHGCAR, as (label, absolute path).
    ///
    /// The VASP analogue of gpawDensityFiles(): an Electronic Structure run
    /// with ICHARG = 11 reads a converged density rather than computing one,
    /// and this is where that density comes from.
    QList<QPair<QString, QString>> vaspChargeDensityFiles() const;
    void showDatasetManager();
    /// MLIP → Trainer…: MACE training-config (YAML) builder + launcher.
    void openMaceTrainer();
    void openExamplesBrowser();
    void openRayTraceDialog();
    void addRandomNoise();

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
    /// What "Export Animation" is rendering. Carried as the source combo's
    /// userData rather than as a row index: which rows exist depends on
    /// whether the workspace has a trajectory and a film.
    enum class AnimationSource { Turntable, Trajectory, Film };
    /// One exported film frame, at export resolution — including the dissolve
    /// mix and the black fade, so the file matches what the preview showed.
    QImage renderFilmFrame(const render::FilmScript& film, int frame,
                           int frameCount, int width, int height,
                           const QColor& background);

    /// One viewer per result file a run can leave behind: which viewers apply
    /// to a process directory is decided by what is actually in it, so the
    /// Processes panel never offers a read-out that has nothing to read.
    struct ViewerEntry {
        const char* resultFile; ///< marker file, "" = "no specific marker"
        QString label;
        void (MainWindow::*open)(const QString&);
    };
    /// The viewers `directory` can offer, in menu order.
    std::vector<ViewerEntry> viewersFor(const QString& directory) const;

    /// One undo step: the displayed structure AND the trajectory it belongs to.
    ///
    /// The frames have to be in here. Several transforms — centring, wrapping,
    /// cell standardization — act on every frame of a trajectory at once, and
    /// an undo stack that remembered only the displayed structure would restore
    /// that one frame and leave the other several hundred transformed, with no
    /// way back. Storing them is nearly free: the frames are shared pointers to
    /// structures that are replaced rather than mutated, so a snapshot is a
    /// pointer copy per frame plus one deep copy of the frame on screen.
    struct Snapshot {
        std::shared_ptr<core::Structure> structure;
        std::vector<std::shared_ptr<core::Structure>> frames;
    };

    struct Document {
        /// Stable identity of this workspace, independent of the tab's
        /// position (tabs are movable and closable, so an index is not an
        /// identity). Records bound to a workspace — currently the Volumetric
        /// Data dock's datasets — key off this.
        int id = -1;
        std::shared_ptr<core::Structure> structure;
        std::vector<std::shared_ptr<core::Structure>> frames; ///< trajectory
        std::deque<Snapshot> undoStack;
        std::deque<Snapshot> redoStack;
        QString fileName;
        /// This tab's camera state. Kept current by the viewport's
        /// cameraChanged signal and re-applied when the tab is shown again, so
        /// switching away and back does not disturb a view the user set up.
        /// Default-constructed (invalid) until the tab has been displayed
        /// once, which is what lets the first display frame normally.
        render::PointOfView pointOfView;
        /// This tab's film. Per workspace for the same reason the camera is:
        /// a film is authored against one structure's geometry and its casts,
        /// and carrying it to another tab would aim it at atoms that are not
        /// there.
        render::FilmScript film;
        /// Process / task descriptor shown in the tab title's third field
        /// (e.g. "Single-Point Calculation"); empty for a plain structure.
        QString task;
    };

    struct ProcessRecord; // full definition below
    struct QueuedJob;     // full definition below

    void createMenusAndDocks();

    /// Build the Orchestration canvas for the bottom-row dock: seeds it with the
    /// open documents, installs the provider that keeps that list current, and
    /// wires each dispatched node's job into the Results panel (process
    /// selector, live metric plots, persistence) exactly as a wizard run is.
    OrchestrationWindow* createOrchestrationPanel(QWidget* parent);
    Document* currentDocument();
    /// Creates an empty "Untitled" tab when none exists (for Add Atom).
    Document& ensureDocument();
    int addDocument(std::shared_ptr<core::Structure> structure, const QString& name,
                    std::vector<std::shared_ptr<core::Structure>> frames = {},
                    const QString& task = {});
    /// Put the whole trajectory into the lattice the Edit Structure dialog just
    /// produced, `edited` being the already-converted displayed frame. Refuses
    /// (changing nothing) when the frames do not all convert to the same atom
    /// count and ordering — see the implementation for why that is the only
    /// safe answer.
    void propagateCellTransform(std::shared_ptr<core::Structure> edited,
                                core::CellTransform transform);

    /// Take the current structure apart into its oxygen functional groups and
    /// put each KIND into its own render cast, named and coloured. Returns how
    /// many casts were created (0 when nothing was recognized, leaving the
    /// scene untouched).
    ///
    /// Derived from connectivity rather than from what a builder remembers
    /// placing, so a graphene oxide loaded from a file is taken apart exactly
    /// like one just generated — see
    /// GrapheneOxideBuilder::findFunctionalGroups().
    int applyFunctionalGroupCasts();

    /// Capture `doc`'s current state as one undo step (see Snapshot).
    static Snapshot snapshotOf(const Document& doc);
    /// Restore a snapshot into `doc`, replacing both the displayed structure
    /// and the trajectory.
    static void restore(Document& doc, Snapshot snapshot);

    /// Apply `transform` to EVERY frame of the active trajectory — or to the
    /// lone structure when the document has no trajectory — as a single undo
    /// step. Returns the number of frames the transform reported changed, 0 if
    /// it changed nothing (in which case the document is left untouched and no
    /// undo step is pushed), or -1 when there is nothing to act on.
    ///
    /// This is what "Center", "Wrap", "Standardize Cell" and "Reduce to
    /// Primitive Cell" go through. Acting on the displayed frame alone is not a
    /// smaller version of these operations, it is a wrong one: a trajectory
    /// whose frames sit in different lattice images or different cells makes
    /// atoms jump as the timeline is scrubbed, and every quantity computed
    /// across frames — an MSD, an RDF, a diffusion coefficient — is then
    /// measuring the transform instead of the physics.
    ///
    /// `transform` returns true when it changed the structure it was handed. It
    /// may throw: nothing is committed until every frame has been transformed,
    /// so a failure part-way leaves the document exactly as it was.
    int applyToAllFrames(const std::function<bool(core::Structure&)>& transform);

    /// Open a trajectory, displaying its first frame. Returns -1 for an empty
    /// list, which is not a document.
    ///
    /// This exists so that no caller writes
    /// `addDocument(frames.front(), name, std::move(frames))`. That is
    /// undefined behaviour, not a style problem: the order in which a call's
    /// arguments are evaluated is unspecified, and GCC picks right-to-left, so
    /// the vector is moved away before `front()` reads it and the new tab opens
    /// on a dangling element. Clang picks left-to-right, which is exactly why
    /// it segfaulted only on Linux. Taking the frames alone and deriving the
    /// displayed structure inside removes the shape of the mistake.
    int addTrajectoryDocument(std::vector<std::shared_ptr<core::Structure>> frames,
                              const QString& name, const QString& task = {});
    /// Re-derive every tab's title as "NN - Formula - Task" with a zero-padded
    /// two-digit sequence number, kept in sync as tabs are added, closed or
    /// reordered. Formula is read live from each document's structure.
    void refreshTabTitles();
    /// Push the current document's state into all views.
    void syncViewsToCurrent(bool frameCamera);
    /// Workspace id of the tab on screen, or -1 when no document is open.
    /// Push undo, swap `edited` in as the current structure (and in the
    /// trajectory frame it stands for), refresh the views and report `message`.
    /// The one path every whole-structure transform goes through.
    void installEditedStructure(std::shared_ptr<core::Structure> edited,
                                const QString& message);
    /// Hand `doc`'s trajectory to the viewport, which needs it so the Custom
    /// Gradient dialog can auto-scale a colour ramp over the whole run instead
    /// of over the one frame on screen. Cheap: it copies shared_ptrs.
    void pushTrajectoryToViewport(const Document* doc);
    /// Park the timeline and the viewport on the last frame of `doc`'s
    /// trajectory. Called when a frame-producing run (geometry optimization,
    /// molecular dynamics, NEB) finishes: the result of a relaxation IS its
    /// final step, and leaving the playhead wherever the run left it showed
    /// the starting geometry to anyone who had scrubbed back — or, for a
    /// trajectory loaded from disk at the end of the job, frame 0.
    /// No-op for a single-structure tab or one that is not on screen.
    void showFinalFrame(const Document* doc);
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
    /// Stage and launch a script as a local job.
    ///
    /// `kind` and `runCommand` decide HOW it is launched: the engine's
    /// template from Preferences → "Run" (or `runCommand`, the possibly
    /// hand-edited "Running:" line from the wizard) is resolved into a shell
    /// command line plus any solver-command environment. Callers that are not
    /// engine-driven (a density export, a Raman post-process) leave both at
    /// their defaults and get a plain `python run.py`.
    /// Stage `script` as a job and either start it or QUEUE it.
    ///
    /// Submitting while something else runs no longer fails — the job is
    /// staged, registered as Queued, and started automatically when the runner
    /// frees up. Callers therefore need no "is a job running?" guard of their
    /// own, and none of them has one any more.
    void runScript(const QString& script, const QString& pythonExe,
                   const QString& taskLabel, bool expectFrames = false,
                   core::CalculatorKind kind = core::CalculatorKind::EMT,
                   const QString& runCommand = QString());
    /// Hand one prepared job to the runner: bind it as the current task, open
    /// its live trajectory tab if it wants one, and start the subprocess.
    void launchJob(const QueuedJob& job);
    /// Start the next queued job if the runner is idle. Connected to
    /// JobRunner::finished AFTER onJobFinished, so the finished run has fully
    /// settled (metrics persisted, viewers opened) before the next one binds
    /// itself as the current task.
    void startNextQueuedJob();
    int indexOfDocument(const Document* document) const;
    /// Append one streamed geometry to `target` and keep the timeline and the
    /// viewport following it. Shared by the main runner's live document and by
    /// an orchestration node's, which differ only in which tab they feed.
    void appendStreamedFrame(Document* target,
                             const std::shared_ptr<core::Structure>& frame);
    /// Turn a finished orchestration node's live tab into a plain trajectory tab —
    /// or, when the node streamed nothing, load whatever trajectory it left in
    /// its job directory so the timeline is populated either way.
    void finalizeOrchestrationTrajectory(int processId, bool success);
    bool ensureAseAvailable();
    /// Shared preconditions for the dedicated Simulation dialogs: a non-empty
    /// current structure and ASE available. It no longer checks whether a job
    /// is running — that is what the queue is for.
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
    /// True while a stored point-of-view is being re-applied, so the
    /// cameraChanged echo is not mistaken for a user camera move.
    bool restoringPointOfView_ = false;
    /// The modeless Set Point-of-View editor; null when it is not open.
    PointOfViewDialog* povDialog_ = nullptr;
    FilmProductionDialog* filmDialog_ = nullptr;
    FilmTimelineWidget* filmTimeline_ = nullptr;
    QAction* filmModeAction_ = nullptr;
    QAction* filmProductionAction_ = nullptr;
    /// Guards showFilmTime() against the cameraChanged echo its own camera
    /// write provokes - without it every film frame would be recorded as a
    /// user camera move and overwrite the tab's saved point-of-view.
    bool applyingFilm_ = false;
    /// The tab's camera as it was when film mode was switched on, restored
    /// when it is switched off: previewing a film must not cost the view the
    /// user had set up.
    render::PointOfView preFilmPov_;
    /// Cast opacities as the Representation panel had them when film mode
    /// started, keyed by cast index. Every previewed frame is applied on top
    /// of these rather than on top of the previous frame.
    std::map<int, float> filmCastBaseline_;
    /// Identity of the dissolve the cached outgoing render belongs to:
    /// the outgoing shot index, the structure it was rendered from, and the
    /// canvas size. Any of the three changing invalidates it — a cached image
    /// of the wrong frame, or at the wrong size, is worse than a re-render.
    struct CrossfadeKey {
        int shot = -1;
        const core::Structure* structure = nullptr;
        QSize size;
        bool operator==(const CrossfadeKey& other) const
        {
            return shot == other.shot && structure == other.structure
                && size == other.size;
        }
    };
    CrossfadeKey filmCrossfadeKey_;
    QImage filmCrossfadeCache_;
    /// Monotonic source of Document::id — never reused, so a closed tab's id
    /// cannot be inherited by a later one (and neither can its bound records).
    int nextWorkspaceId_ = 0;
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
    /// Orchestration node jobs currently executing (each OrchestrationWindow drives
    /// one at a time, but several windows may run concurrently). Their
    /// metrics.json is polled alongside the main window's own job, so the
    /// Results tabs treat orchestration-driven processes like standalone ones.
    std::set<int> orchestrationRunningIds_;

    QTabBar* tabBar_ = nullptr;
    ViewportWidget* viewport_ = nullptr;
    /// Kept because Preferences → Rendering reaches the same shader-profile
    /// setting as the panel's "Shading" row, and that dialog is modal: the row
    /// has to be re-read when it closes or the two go out of step.
    RepresentationPanel* representationPanel_ = nullptr;
    /// Viewport-toolbar "Draw hydrogen atoms". Held because "Complete with
    /// hydrogens" switches it on after building them — hydrogens the user
    /// cannot see are a no-op as far as they can tell.
    QAction* showHydrogensAction_ = nullptr;
    BrandingPanel* brandingPanel_ = nullptr;      ///< zone 1 (theme-aware logo)
    SystemStatusBar* systemStatusBar_ = nullptr;  ///< permanent status widgets
    QMenu* recentMenu_ = nullptr; ///< File → Open → Open Recent (dynamic)
    QComboBox* processSelector_ = nullptr; ///< Results-panel process dropdown
    StructureInfoWidget* infoWidget_ = nullptr;
    VolumetricPanel* volumetricPanel_ = nullptr; ///< zone 13 (Volumetric Data)
    /// The dock holding it, so a run that produces a grid can raise the panel
    /// rather than leave the result sitting in a collapsed tab.
    QDockWidget* volumetricDock_ = nullptr;
    JobLogWidget* jobLogWidget_ = nullptr;
    MetricPlotWidget* energyPlot_ = nullptr;
    MetricPlotWidget* temperaturePlot_ = nullptr;
    MetricPlotWidget* forcePlot_ = nullptr;
    MetricPlotWidget* pressurePlot_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    /// The default dock arrangement, captured once at construction before any
    /// saved state is restored — the only point at which it exists. View →
    /// Reset Layout replays it.
    QByteArray defaultLayoutState_;
    QDockWidget* jobDock_ = nullptr;
    QDockWidget* visualEffectsDock_ = nullptr; ///< zone 9 (Lighting + effects)
    QDockWidget* remoteDock_ = nullptr;
    /// Zone 14 — the node canvas, leading the bottom row. It replaced the
    /// former "Workflow → Add Workflow…" window, so there is exactly one of
    /// them and it outlives the tabs it draws its materials from.
    QDockWidget* orchestrationDock_ = nullptr;
    OrchestrationWindow* orchestrationPanel_ = nullptr;
    RemoteAccessPanel* remotePanel_ = nullptr;
    ProcessManagerPanel* processPanel_ = nullptr;
    /// "Additional overlays" dock — lattice planes, text and primitives.
    OverlayPanel* overlayPanel_ = nullptr;

    /// A job that is staged and registered but not yet started.
    ///
    /// Everything needed to launch it is captured HERE, at submission time,
    /// because none of it can be recovered later: the script came from a
    /// dialog that is about to be destroyed, and `currentDocument()` will be
    /// whatever tab the user has wandered to by the time the queue reaches
    /// this entry.
    struct QueuedJob {
        int processId = -1;
        QString label;
        QString jobDir;         ///< already staged — proc_<processId>/
        QString pythonExecutable;
        QString commandLine;    ///< resolved launch command
        QMap<QString, QString> environment;
        /// Open a live trajectory tab when this one starts (MD/relaxation).
        bool expectFrames = false;
        /// Geometry the live tab is seeded from, captured at submission for
        /// the reason above. Null when `expectFrames` is false.
        std::shared_ptr<core::Structure> liveSeed;
    };
    /// Jobs waiting for the runner, oldest first. Submitting while something
    /// runs appends here instead of being refused; each finish pops one.
    std::deque<QueuedJob> jobQueue_;

    /// Process-panel id of the running local job (-1 when idle).
    int currentTaskId_ = -1;
    /// Document receiving live streamed frames (null outside runs;
    /// cleared when its tab is closed mid-run).
    Document* liveDoc_ = nullptr;
    /// The same thing for orchestration nodes, keyed by process id.
    ///
    /// Separate from liveDoc_ rather than sharing it: the Orchestration panel drives
    /// its own JobRunner, so a node can be streaming while a queued job runs on
    /// the main one, and a single pointer would let whichever started last
    /// steal the other's frames. Entries are created on a node's first frame
    /// and dropped when it finishes or its tab is closed.
    std::map<int, Document*> orchestrationLiveDocs_;
    /// Band of images staged as band.extxyz on the next stageJob (NEB);
    /// consumed and cleared by stageJob.
    std::vector<std::shared_ptr<core::Structure>> stagedBandFrames_;
    /// Cluster-expansion ensemble staged as configs.extxyz on the next
    /// stageJob; consumed and cleared there, like stagedBandFrames_.
    std::vector<std::shared_ptr<core::Structure>> stagedEnsembleFrames_;
    /// Primitive reference cell staged as primitive.extxyz on the next
    /// stageJob (band unfolding); consumed and cleared there.
    std::shared_ptr<const core::Structure> stagedPrimitive_;
    /// Calculator provenance JSON staged as calculator.json on the next
    /// stageJob (set by runSimulationWizard); consumed and cleared there. Lets
    /// the MLWF wizard inherit a completed baseline's engine + parameters.
    QString pendingCalculatorProvenance_;
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
