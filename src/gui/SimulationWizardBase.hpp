#pragma once

#include "core/CalculatorConfig.hpp"
#include "gui/ExtendedEngineGroups.hpp"

#include <QDialog>

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QVBoxLayout;

namespace calango::gui {

class GpawElectronicRows;

/// Shared multi-stage stepper shell for the simulation wizards (Molecular
/// Dynamics, Geometry Optimization, Phonon Calculator, …). It owns the
/// standardized Calculator Settings stage (engine selection + backend knobs)
/// and the ASE Script Review stage, plus the navigation + action bar
/// (Back / Cancel / Next / Export Script / Run Remote / Run Local). Each
/// concrete wizard supplies its task-settings stage and the script generation
/// via the virtual hooks. The Conda environment is no longer chosen here — it
/// is resolved silently per engine from the Preferences "Python & Environments"
/// mapping (see pythonExecutable()). The host inspects action() after exec().
class SimulationWizardBase : public QDialog {
    Q_OBJECT

public:
    enum class Action { None, RunLocal, RunRemote };

    /// Where a run is actually going, passed into the pre-flight checks.
    ///
    /// Separate from `Action` because a pre-flight runs BEFORE `action_` is
    /// set — it is what decides whether the run happens at all. Without it
    /// the VASP checks closed every failure with advice about HPC cluster
    /// profiles, including on a purely local run, which described a machine
    /// the user was not using.
    enum class RunTarget { Local, Remote };

    Action action() const { return action_; }

    QString script() const;            ///< the (possibly edited) preview text
    /// The engine the run uses — the host needs it to resolve the launch
    /// command template (Preferences → "Run").
    core::CalculatorKind calculatorKind() const { return selectedCalculator(); }
    /// The (possibly hand-edited) "Running:" line from the review stage. Empty
    /// when the wizard was never opened on that stage, in which case the host
    /// falls back to the engine's configured template.
    QString runCommand() const;
    virtual QString pythonExecutable() const; ///< selected env python, else embedded

    /// A compact JSON record of the calculator this run uses — engine, XC,
    /// cutoff, GPAW mode/grid, k-points, the symmetry flag, plus the resolved
    /// interpreter and Conda env preset. The host persists it next to the job
    /// as `calculator.json` so a downstream post-process (the MLWF wizard) can
    /// inherit the calculator from a completed baseline without re-prompting.
    QString calculatorProvenanceJson() const;

    /// The absolute path to a prior run's charge-density baseline (GPAW
    /// .gpw, VASP CHGCAR) this run's generated script reads non-self-
    /// consistently, or empty when the wizard has no such concept or none is
    /// selected. The host (MainWindow::stageJob()) copies this file into the
    /// job directory under a fixed name (baseline.gpw / baseline.CHGCAR) so
    /// a REMOTE job can find it too — the generated script prefers that
    /// staged copy over the absolute path baked in at generation time, which
    /// only ever resolves on the machine the script was generated on.
    /// Overridden by the wizards that carry their OWN baseline combo
    /// (Electronic Structure, 2D Bands); the base implementation covers
    /// every other wizard generically via vaspBaselineCombo_ — the VASP
    /// settings group's own "Restart from a converged CHGCAR" row (the SOC
    /// workflow's second stage; see CalculatorConfig::vaspChgcarBaselinePath)
    /// — returning empty when that combo does not exist or has no
    /// selection.
    virtual QString baselineDensityPathToStage() const;

    /// The absolute path to a prior VASP run's WAVECAR this run restarts from
    /// (ISTART = 1), or empty. The hybrid counterpart of
    /// baselineDensityPathToStage() above, staged the same way by
    /// MainWindow::stageJob() under the fixed name `baseline.WAVECAR`.
    ///
    /// A SEPARATE hook rather than a second use of the density one: a hybrid
    /// second stage restarts from ORBITALS while a bands run restarts from a
    /// DENSITY, the two files are different, and a run can legitimately want
    /// both (a hybrid band structure on a converged density).
    virtual QString baselineWavecarPathToStage() const;

    /// Completed VASP runs that left a usable WAVECAR, for the calculator
    /// group's "Restart from WAVECAR" combo — the hybrid chain VASP itself
    /// recommends (https://vasp.at/wiki/NiO_HSE06). Fed generically by
    /// MainWindow::runSimulationWizard(), so every wizard that shows a VASP
    /// calculator stage gets it without its own plumbing.
    void setVaspWavecarBaselines(
        const QList<QPair<QString, QString>>& baselines);

    /// Whether the calculator group offers that combo at all. False for a
    /// wizard that already selects its parent run somewhere else — offering
    /// two controls for one thing only lets them disagree.
    virtual bool showsVaspWavecarRow() const { return true; }

    /// Populate the VASP group's "Restart from a converged CHGCAR" combo
    /// (completed VASP Single-point runs; label, absolute CHGCAR path) —
    /// the SOC workflow's second stage. A no-op before the VASP group is
    /// built or when it has none (a wizard whose calculatorAllowed() never
    /// admits VASP has no such combo to fill). Always includes a leading
    /// "None — converge here" entry (empty path) so the field is opt-in.
    void setVaspChgcarBaselines(const QList<QPair<QString, QString>>& baselines);

    /// The inheritance-relevant calculator description read back from a job
    /// directory's `calculator.json`. Empty optional when the file is absent or
    /// unreadable (e.g. a baseline produced by an older Calango release).
    struct InheritedCalculator {
        QString engine;            ///< human name, e.g. "GPAW"
        int engineKind = -1;       ///< core::CalculatorKind as int (-1 = unknown)
        QString xc;
        double cutoffEv = 0.0;
        QString mode;              ///< "PW" / "FD" / "LCAO"
        double gridSpacing = 0.0;
        int kpts[3] = {0, 0, 0};
        bool symmetryOff = false;
        QString pythonExecutable;  ///< interpreter the baseline ran under
        QString condaEnv;          ///< engine's env preset string (may be empty)
        /// VASP DFT+U, as vaspHubbardSummary()'s own comparable string —
        /// empty means "no +U". GPAW carries no equivalent field: its own
        /// restart (a full .gpw) cannot diverge from whatever produced it,
        /// so there is nothing here to compare for that engine (Task 2,
        /// 2026-08-22 — see vaspHubbardSummary()'s own doc comment).
        QString vaspHubbard;
        /// One-line, human-readable description for the inheritance note.
        QString summary() const;
    };
    static std::optional<InheritedCalculator> readCalculatorProvenance(
        const QString& jobDir);

    /// A single-line, ORDER-INDEPENDENT string encoding of a VASP DFT+U
    /// configuration ("Dudarev|Ni:l=2,U=6.00,J=0.00") — comparable with
    /// == across two configs built from independently-ordered tables,
    /// which is the whole point: two SimulationWizardBase instances (a
    /// completed parent's saved provenance, and the child currently open)
    /// never share a QTableWidget, only whatever ends up in this string.
    /// A species with no actual correction (U <= 0) is left out entirely,
    /// so "not configured" and "configured at U=0" compare equal — both
    /// mean no correction, per core::VaspHubbardU's own doc comment. Empty
    /// means "+U is off (or nothing survives the U<=0 filter)".
    static QString vaspHubbardSummary(const core::CalculatorConfig& config);
    /// Warns (does not hard-refuse — this is a physics judgment call, not
    /// a guaranteed failure) when the selected VASP baseline's own DFT+U
    /// settings (read from ITS calculator.json) disagree with this run's
    /// own — an Electronic Structure/2D Bands ICHARG=11 child or the SOC
    /// workflow's stage 2, restarting a CHGCAR whose SCF used different
    /// LDAU settings (or none) than what THIS script is about to write,
    /// silently computing inconsistent physics with no error at all (Task
    /// 2's own "chaining consistency" requirement). A no-op (returns true)
    /// for every calculator other than VASP, and when no baseline is
    /// selected (baselineDensityPathToStage() empty) or its provenance is
    /// unreadable — nothing to compare against either way.
    bool preflightVaspHubbardConsistency();
    /// Warns (never refuses) about the two ways a hybrid run goes wrong
    /// before it starts: no converged orbitals to restart from, and a WAVECAR
    /// whose plane-wave cutoff differs from this run's. Both are judgment
    /// calls a user may legitimately overrule; both are silent disasters if
    /// nobody says them out loud. A no-op for every non-VASP engine and for a
    /// semilocal VASP run.
    bool preflightVaspHybrid();

    /// Shared body of a baseline picker's change handler: read the provenance
    /// sidecar next to the selected .gpw (the combo's currentData) and render
    /// the inheritance note into `note` — "Inherited: …", the no-sidecar
    /// explanation, or cleared when nothing is selected. Returns the parsed
    /// provenance for the wizard to keep (pythonExecutable(), script
    /// generation). The three baseline-inheriting wizards used to carry three
    /// verbatim copies of this, translated string included.
    static std::optional<InheritedCalculator>
    applyBaselineProvenance(const QComboBox* baselineCombo, QLabel* note);

    /// One-line summary of a constraints list for a settings page:
    /// `emptyText` when nothing is constrained (each wizard words the free
    /// case for its own task), else "Constrained: N atom(s), M region(s).".
    static QString constraintSummaryText(
        const std::vector<core::GeometryConstraint>& constraints,
        const QString& emptyText);

    /// The configured VASP POTCAR directory (`VASP_PP_PATH`).
    ///
    /// Static because it describes the INSTALLATION, not a job: every VASP run
    /// on this machine wants the same datasets. Read by the wizards and by the
    /// standalone dialogs (NEB) alike.
    ///
    /// There is no setter. The value belongs to Preferences → External Files,
    /// alongside the Quantum ESPRESSO and SIESTA libraries, and is read from
    /// there — a wizard that could also write it would be a second source of
    /// truth for a path whose whole problem is being set in two places and
    /// silently disagreeing.
    static QString vaspPotcarDirectory();
    /// The Quantum ESPRESSO / SIESTA pseudopotential libraries, from the same
    /// Preferences page and read the same read-only way.
    static QString espressoPseudoDirectory();
    static QString siestaPseudoDirectory();

    /// Chemical elements of the structure this wizard will run on, supplied
    /// by the host for wizards that do not hold a structure themselves.
    /// Feeds the ~/.calango/calculator_parameters.json suggestion lookup
    /// (and nothing else): setting it re-resolves the suggested plane-wave
    /// cutoff and k-grid for the selected engine. Wizards that DO hold a
    /// structure keep their calculatorElements() override as the authority.
    void setStructureElements(const QStringList& symbols);
    /// Tell the wizard whether the structure it will run on is periodic.
    ///
    /// Only xTB cares, and it cares absolutely: xtb-python evaluates isolated
    /// systems only, so a periodic cell is not a degraded run but no run at
    /// all. Supplied by the host rather than derived here because the base
    /// wizard is given the structure's ELEMENTS, not the structure.
    void setStructurePeriodic(bool periodic);

    /// Orchestration-canvas mode: the wizard CONFIGURES a process node rather
    /// than launching a job. The review stage's "Run (Local)" button reads
    /// "Save process node" — accepting hands the generated script back to
    /// the canvas, nothing executes — and "Run (Remote)" is withdrawn,
    /// because queueing is the canvas's concern, not the wizard's. Call
    /// after construction, before exec().
    void enterOrchestrationMode();

    /// Preselect the engine combo (no-op if the kind isn't offered). Public
    /// because hosts other than the wizard's own pages legitimately choose
    /// the engine — the Orchestration canvas opens a node's wizard on the engine
    /// the node already shows.
    void selectCalculator(core::CalculatorKind kind);

protected:
    explicit SimulationWizardBase(QWidget* parent = nullptr);

    /// A concrete wizard MUST call this from its constructor (not the base
    /// constructor) so the virtual hooks below dispatch to the overrides.
    void buildUi();

    // ---- Hooks each concrete wizard implements ---------------------------
    virtual QString wizardTitle() const = 0;      ///< window title
    virtual QString settingsHeader() const = 0;   ///< Stage 1 header text
    virtual QWidget* buildSettingsPage() = 0;     ///< Stage 1 content
    virtual QString generateScript() const = 0;   ///< full ASE script
    virtual QString exportFileName() const { return QStringLiteral("run.py"); }

    /// Whether a calculator kind should appear in the Stage-2 engine combo.
    ///
    /// Default allows every kind. Calango's own native DFT and DFTB engines
    /// used to be the two exceptions here (opt-in per module, since one ran
    /// in-process with its own dispatch path and the other's forces were too
    /// expensive for anything but a single point) — both were removed; see
    /// CalculatorConfig.hpp's CalculatorKind comment. The Electronic Bands
    /// wizard overrides this the other way, to expose only DFT-capable
    /// electronic-structure calculators.
    virtual bool calculatorAllowed(core::CalculatorKind) const
    {
        return true;
    }

    /// Where the subclass's own settings page sits in the flow. Most wizards
    /// define the *task* first (what to compute), so their page leads. The
    /// Electronic Structure and Phonon wizards instead define a k-path, which
    /// only makes sense once the engine is chosen — their page therefore comes
    /// after Calculator Settings, giving:
    ///   Environment -> Calculator Settings -> k-Path -> Script Review.
    virtual bool settingsStageFirst() const { return true; }

    /// When false the dedicated task-settings stage is omitted,
    /// producing a 3-stage flow (Environment → Calculator Settings → Review).
    /// A wizard that returns false should merge its task controls into the
    /// calculator-settings page via buildCalculatorExtras().
    virtual bool hasTaskSettingsStage() const { return true; }

    /// Navigationally skip the task-settings stage RIGHT NOW, without
    /// removing it from the stack. Unlike hasTaskSettingsStage() (resolved
    /// once at construction, before an engine is even selected), this is
    /// re-evaluated on every Next/Back click, so it can depend on a live
    /// control — Geometry Optimization uses it for VASP's own internal
    /// relaxation route (IBRION/NSW/ISIF/EDIFFG), where the ASE-side
    /// optimizer/force-criterion/cell-filter stage would otherwise sit
    /// between two pages that do not use it.
    virtual bool skipTaskSettingsStage() const { return false; }

    /// An optional SECOND task stage, inserted between Calculator Settings and
    /// the settings page, giving a 4-stage flow. The Phonon wizard uses it to
    /// separate two genuinely different decisions that were previously crammed
    /// together: how the displacements are generated (supercell, δ, symmetry)
    /// and where the dispersion is sampled (the q-path). Returning a non-empty
    /// header enables the stage; buildSecondSettingsPage() supplies its content.
    ///
    /// Only meaningful with settingsStageFirst() == false, i.e. when the
    /// subclass's own page already follows Calculator Settings.
    virtual QString secondSettingsHeader() const { return QString(); }
    virtual QWidget* buildSecondSettingsPage() { return nullptr; }

    /// Header for the calculator-settings stage. A wizard that folds its
    /// convergence controls in here (Single-point) overrides this to
    /// "Calculator & Convergence Settings".
    virtual QString calculatorSettingsHeader() const
    {
        return tr("Calculator Settings");
    }

    /// Extra widget appended to the calculator-settings page (e.g. the
    /// Single-point convergence group). Null adds nothing.
    virtual QWidget* buildCalculatorExtras() { return nullptr; }

    /// Extra widget placed ABOVE the script preview on the review stage,
    /// sharing it through a splitter. Used by the Effective Bands wizard,
    /// whose k-path is defined on the primitive lattice and so belongs with
    /// the script that consumes it. Null keeps the review stage as-is.
    virtual QWidget* buildReviewExtras() { return nullptr; }
    /// Review-stage header, so a wizard folding another concern in there can
    /// say so ("k-Path & ASE Script Review").
    virtual QString reviewHeader() const { return tr("ASE Script Review"); }

    /// Notify the subclass that the selected engine changed, so it can show /
    /// hide or retune its buildCalculatorExtras() widgets for that engine.
    virtual void updateCalculatorExtras(core::CalculatorKind) {}

    /// When true, the plane-wave cutoff, XC functional and GPAW mode are
    /// inherited from a mandatory baseline SCF (.gpw) and hidden from the GPAW
    /// calculator page — the run restarts from that density, so those knobs are
    /// locked to the baseline. The Electronic Structure wizard overrides this.
    virtual bool inheritsCalculatorFromBaseline() const { return false; }

    /// When false, the whole standard calculator chrome (engine dropdown, DFT
    /// and per-engine backend groups, hint labels) is hidden, leaving only the
    /// subclass's buildCalculatorExtras() content. The Electronic Structure
    /// wizard uses this to present a single streamlined page (baseline + PDOS +
    /// k-path) with every locked SCF knob suppressed.
    virtual bool showsEngineAndDftControls() const { return true; }

    /// When false the Calculator Settings stage is dropped from the flow
    /// entirely (the page is still constructed so the shared config accessors
    /// stay valid, but it never appears as a stage). The MLWF wizard uses this
    /// to inherit the calculator from a completed baseline instead of asking
    /// the user to redefine it, giving a strict 2-stage wizard.
    virtual bool showsCalculatorStage() const { return true; }

    /// When false the shared "Plane-wave cutoff:" row is hidden from the
    /// calculator page. The Cutoff Convergence wizard overrides this: its
    /// sweep stage defines the cutoffs as a range, and a second, single-value
    /// cutoff field would be a knob the generated script ignores.
    virtual bool showsPlaneWaveCutoffRow() const { return true; }

    /// When false the "k-point grid (Monkhorst-Pack):" row is hidden from the
    /// calculator page. The K-points Convergence wizard overrides this for
    /// the same reason the cutoff sweep hides the cutoff row: the mesh is the
    /// sweep variable, defined on the sweep stage.
    virtual bool showsKpointGridRow() const { return true; }

    /// When true a "Symmetry: off" checkbox is shown in the GPAW settings group,
    /// letting the user emit `symmetry="off"` (no point-group k-point
    /// reduction). Only the Single-Point wizard exposes it — a Single-Point run
    /// with symmetry off is the recommended baseline for an MLWF localization.
    virtual bool showsGpawSymmetryToggle() const { return false; }

    /// When true a "van der Waals Correction (DFTD4)" checkbox is shown.
    ///
    /// Offered by the wizards whose result depends on forces or on energy
    /// DIFFERENCES between geometries — Geometry Optimization, Phonon, MD,
    /// Monte Carlo, NEB — since that is where the missing long-range
    /// correlation of a semilocal functional actually changes the answer. A
    /// single-point total energy gains a constant shift, which is why the
    /// default is off.
    virtual bool showsDispersionToggle() const { return false; }

    /// Chemical species present in the structure this wizard is configuring,
    /// used to seed the Hubbard editor's element completer. Empty is fine —
    /// the completer is simply omitted — but a wizard that holds a structure
    /// should override it, since a U on an element the cell does not contain
    /// is silently inert and hard to spot in a generated script.
    virtual QStringList calculatorElements() const { return {}; }

    /// When true a "Export Electron Density (.cube)" checkbox is shown in the
    /// GPAW "Output & Exports" group. Only the Single-Point wizard exposes it.
    virtual bool showsGpawDensityExport() const { return false; }

    /// Hooks letting a subclass inject its own rows into the shared thematic
    /// GPAW group boxes: convergence/smearing rows into "Electronic Convergence
    /// && Smearing" and spin rows (polarization mode, magnetic moments) into
    /// "Spin Configurations". Used by the Single-Point wizard so its
    /// electronic-structure controls sit in the right physical-domain group.
    /// Default: no rows.
    virtual void buildConvergenceRows(QFormLayout*) {}
    virtual void buildSpinRows(QFormLayout*) {}
    /// The GpawElectronicRows instance a DFT wizard holds, so the base can
    /// tell it which engine is selected — the smearing menu is engine-specific
    /// (VASP has no ISMEAR for several of the methods) and has to be refiltered
    /// when the engine combo changes. Null for a wizard that injects no rows.
    ///
    /// A hook rather than a member of this class: the rows are built by the
    /// subclass, into the subclass's own choice of stage, and moving ownership
    /// here would mean the base constructing widgets a non-DFT wizard never
    /// shows.
    virtual GpawElectronicRows* electronicRows() { return nullptr; }
    /// Controls the subclass creates but wants the BASE to position, because
    /// where they belong is decided by the shared GPAW layout rather than by
    /// the subclass: the SCF energy tolerance goes on the "Convergence
    /// tolerances" row beside eigenstates/density, and the SCF step cap beside
    /// the eigensolver. Null (the default) simply places nothing.
    virtual QWidget* gpawEnergyToleranceWidget() { return nullptr; }
    virtual QWidget* gpawScfStepsWidget() { return nullptr; }
    /// Whether the subclass added any rows via the hooks above — drives the
    /// visibility of the two groups for non-GPAW DFT engines (which otherwise
    /// have nothing to show there).
    virtual bool hasConvergenceExtras() const { return false; }
    virtual bool hasSpinExtras() const { return false; }

    /// Whether this wizard's task moves the ions. Drives the VASP ionic
    /// relaxation row (IBRION / ISIF / EDIFFG), which describes nothing for a
    /// single point.
    virtual bool taskHasIonicSteps() const { return false; }

    /// Fired when the DFT k-point grid changes, so a subclass can rescale a
    /// derived mesh default (e.g. the PDOS k-mesh at 2× the SCF grid).
    virtual void calculatorKgridChanged() {}

    /// The SCF k-point grid value along an axis (0..2). Lets a subclass derive
    /// a denser mesh from the (baseline) SCF sampling.
    int calculatorKpoint(int axis) const;

    /// True right now when VASP is selected, the task has ionic steps, and
    /// the "Relaxation driver" combo is set to VASP's own internal route —
    /// the live predicate updateVaspRows() shows/hides IBRION/ISIF/EDIFFG
    /// with, exposed so a subclass (Geometry Optimization's
    /// skipTaskSettingsStage()) can make the same decision without
    /// duplicating the combo/task/calculator check.
    bool vaspInternalRelaxationSelected() const;
    /// NSW under VASP's own internal relaxation route (vaspNswSpin_). Only
    /// meaningful when vaspInternalRelaxationSelected() is true.
    int vaspNsw() const;

    /// Calculator kind + backend knobs (DFT cutoff/k-points, MACE, ORCA) from
    /// Stages 2–3; the subclass adds its task fields to build the final config.
    core::CalculatorConfig baseCalculatorConfig() const;
    core::CalculatorKind selectedCalculator() const;

protected Q_SLOTS:
    void refreshPreview();
    /// "Hubbard parameters…": open the DFT+U editor and keep its result.
    ///
    /// Protected rather than private because a wizard that hides the standard
    /// GPAW chrome (Electronic Structure) still wants the editor, and puts its
    /// own button on its own page — the U itself still travels through
    /// baseCalculatorConfig(), so there is one owner of the state either way.
    void editHubbardParameters();

    /// Advance / retreat one stage. Protected rather than private so a wizard
    /// can put its own button on a page ("Run simulation" on the Random Noise
    /// stage) and have it mean the same thing as Next — two controls that move
    /// the flow must not be two implementations of moving it.
    ///
    /// goNext() is virtual so a wizard can make leaving a stage do something
    /// first (Random Noise generates its ensemble if the user pressed Next
    /// instead of the button). An override must call the base implementation.
    virtual void goNext();
    void goBack();

private Q_SLOTS:
    void exportScript();
    void updateCalculatorEnabled();

private:
    /// Elements for the suggestion lookup: the subclass's own structure when
    /// it has one, else what the host supplied via setStructureElements().
    QStringList suggestionElements() const;
    /// Pull suggested cutoff / k-grid defaults for the selected engine and
    /// elements from ~/.calango/calculator_parameters.json into the shared
    /// spin boxes. No file, no engine entry, no element match — no change:
    /// the hardcoded defaults stand. Skipped entirely when the calculator is
    /// inherited from a baseline (those knobs are locked to the .gpw).
    void applySuggestedParameters();

    QWidget* buildCalculatorPage();
    /// Wrap `page` in a QScrollArea (resizable, frameless — same idiom as
    /// NebDialog's own settings scroll and HpcPanel's Scheduler tab) before
    /// it goes into `stack_`. Applied uniformly to every stage page in
    /// buildUi() rather than only the Calculator Settings page that
    /// prompted this: a QStackedLayout's own sizeHint/minimumSize is the
    /// MAX across every page it holds (a standing Qt quirk, not specific to
    /// this dialog), so an unwrapped tall page would otherwise force the
    /// dialog itself to grow regardless of which page happens to be
    /// showing. Each page gets its OWN scroll area rather than one wrapping
    /// the whole stack, precisely so a short page's viewport stays sized to
    /// ITS OWN content and never inherits a scrollbar from a tall sibling
    /// page's — the QScrollArea this returns reports a content-independent
    /// minimumSizeHint, which is what actually keeps that from happening.
    /// A page whose content already fits shows no scrollbar at all — this
    /// is a no-op wrapper for it, not a restructuring.
    QWidget* wrapInScrollArea(QWidget* page);
    QWidget* buildMaceGroup(QWidget* parent);
    /// The "VASP settings" group — the primary INCAR tags plus the POTCAR
    /// directory. Shown only when VASP is the selected engine.
    QWidget* buildVaspGroup(QWidget* parent);
    /// Show the ionic-relaxation row only for the tasks that have ionic steps.
    void updateVaspRows();
    /// (Re)build vaspHubbardTable_'s rows from suggestionElements() — one
    /// FIXED row per structure species (see the table's own doc comment for
    /// why rows are never user-add/removable here, unlike GPAW's dialog).
    ///
    /// Called from buildVaspGroup() AND from setStructureElements(), and it
    /// reads suggestionElements() rather than calculatorElements(), because
    /// the two together are what a wizard with no structure of its own needs:
    /// its override returns {}, the host supplies the chemistry after
    /// construction, and reading only the override built a table with zero
    /// rows — VASP DFT+U with nowhere to enter U and J at all, which is
    /// exactly what Single-Point shipped with.
    ///
    /// IDEMPOTENT, and it has to be: a rebuild discards every U and J typed
    /// so far. Same element list as the rows already carry -> returns having
    /// touched nothing but the enabled state.
    void populateVaspHubbardTable();
    /// Raise LMAXMIX to 4 (any enabled row's l == 2) or 6 (l == 3), the
    /// LARGER if both are present — the VASP wiki's own DFT+U guidance —
    /// UNLESS vaspLmaxmixEdited_ says the user already set it by hand.
    /// Connected to every control that can change which species carry a
    /// correction: the enable checkbox and each row's own l/U spin boxes
    /// (U specifically, not J: a species with l>=0 but U==0 requests no
    /// actual correction, matching ASE's own ldau_luj default entry, so
    /// LMAXMIX has nothing to answer for it).
    void refreshVaspHubbardLmaxmixAuto();
    /// Local approximation of the check the generated script performs at
    /// runtime (AseScriptGenerator.cpp, emitVasp()) — catches "no POTCAR
    /// library configured" and "the configured library is missing one of
    /// this structure's elements" BEFORE anything is staged, for both
    /// Run (Local) and Run (Remote). A no-op (returns true without a
    /// dialog) for every calculator other than VASP. See
    /// gui/VaspPotcarPreflight.hpp for what it can and cannot see.
    bool preflightVaspPotcar(RunTarget target);
    /// The same shape of check as preflightVaspPotcar(), for vasp_ncl: a
    /// Non-Collinear (spin-orbit) run with no vasp_ncl configured in
    /// Preferences → External Files → VASP executables is refused before
    /// anything is staged — vasp_std cannot run a noncollinear calculation
    /// at all, so this is not an optimization to skip. A no-op (returns
    /// true) for every calculator other than VASP, and for VASP runs that
    /// are not Non-Collinear.
    bool preflightVaspNcl(RunTarget target);
    /// A second, independent preflight check run alongside
    /// preflightVaspPotcar(), for anything the concrete wizard wants
    /// checked before Run (Local)/Run (Remote) that is not the VASP POTCAR
    /// case above. A no-op (returns true) by default — most wizards need
    /// nothing here. The 2D Bands wizard overrides it to warn, before
    /// staging anything, when Quantum ESPRESSO or SIESTA is selected and no
    /// pseudopotential directory is configured: a weaker check than
    /// preflightVaspPotcar()'s (a directory existing, not a per-element
    /// file existing inside it), because unlike VASP, neither engine has an
    /// established per-element preflight anywhere else in this application
    /// to reuse — see FUTURE.md for that as a follow-up.
    virtual bool preflightSecondary() { return true; }
    /// Run (Local)-only preflight (Task 2): GPAW parallelized by running the
    /// SCRIPT under mpirun (RunCommands.hpp), so "Cores" (Preferences →
    /// Run) > 1 needs BOTH an MPI-enabled GPAW build and an mpirun/mpiexec
    /// launcher to actually parallelize — either missing means the run
    /// would silently execute on 1 core, or worse, as N redundant
    /// independent serial copies of the same calculation. A no-op (returns
    /// true) for every calculator other than GPAW and for Cores <= 1. See
    /// gui/GpawMpiPreflight.hpp. Not run for Run (Remote): a remote job's
    /// rank count is the HPC panel's own Nodes x Tasks/node, resolved and
    /// checked entirely inside HpcPanel::specFromUi() against a cluster
    /// this process cannot probe.
    bool preflightGpawMpi();
    /// The "Quantum ESPRESSO settings" group — the dual cutoff (ecutwfc /
    /// ecutrho), input_dft, occupations + smearing, conv_thr.
    QWidget* buildEspressoGroup(QWidget* parent);
    /// Enable the smearing rows only for `occupations = smearing`, refresh the
    /// pseudo-library note and restate the effective ecutrho.
    void updateEspressoRows();
    /// The "SIESTA settings" group — XC, basis type and size, energy shift,
    /// mesh cutoff. There is deliberately no cutoff-like basis control: SIESTA
    /// has none.
    QWidget* buildSiestaGroup(QWidget* parent);
    void updateSiestaRows();
    /// The "LAMMPS settings" group — interface, pair style, coefficients,
    /// potential files and the executable. LAMMPS is the only engine here that
    /// brings no force field of its own, so it needs the most configuration.
    QWidget* buildLammpsGroup(QWidget* parent);
    /// Show only the LAMMPS rows that apply to the selected interface (the
    /// binary is meaningless for the in-process library).
    void updateLammpsRows();
    /// The "xTB settings" group — method, accuracy, electronic temperature,
    /// SCC iteration cap. Small on purpose: xTB's parameterization IS the
    /// method, so there is no basis or functional to configure.
    QWidget* buildXtbGroup(QWidget* parent);
    /// Hide the electronic rows for GFN-FF (which has no electrons), and
    /// show the periodicity refusal when the structure has a cell.
    void updateXtbRows();
    /// The "DFTB+ settings" group — Slater-Koster directory, SCC controls and
    /// the Fermi filling temperature. The k-grid stays on the shared
    /// Brillouin-zone controls.
    QWidget* buildDftbGroup(QWidget* parent);
    /// Hide the SCC tolerance / iteration rows when SCC is off (they describe
    /// a cycle that does not run).
    void updateDftbRows();
    /// The "GROMACS settings" group — force field, water model, gmx binary,
    /// extra .mdp parameters. Led by the engine-not-a-force-field note: the
    /// topology must be typeable by the chosen force field.
    QWidget* buildGromacsGroup(QWidget* parent);
    /// The shared "Machine-Learning Potential" group serving DeepMD, NequIP /
    /// Allegro, CHGNet, MatterSim and FAIRChem (MACE keeps its own group).
    QWidget* buildMlipGroup(QWidget* parent);
    /// Show only the rows that apply to the selected ML potential.
    void updateMlipRows();
    /// Build the four thematic DFT/GPAW group boxes (Mode & Basis Set;
    /// Brillouin Zone & k-Points; Electronic Convergence & Smearing; Output &
    /// Exports) and add them to `layout`. Shared cutoff/k-points live in the
    /// first two so a single set of widgets serves every DFT engine.
    void buildDftGpawGroups(QWidget* parent, QVBoxLayout* layout);
    QWidget* buildReviewPage();
    void updateStage();
    /// Show only the MACE rows that apply to the selected model source.
    void updateMaceRows();
    /// The custom checkpoint the MACE model-file dropdown currently names:
    /// the selected entry's full path, or the hand-typed text.
    QString maceModelFilePath() const;
    /// Show only the GPAW rows that apply to the selected discretization.
    void updateGpawRows();
    /// Re-fill the "Running:" line from the selected engine's template, unless
    /// the user has typed their own command into it.
    void refreshRunCommand();

    Action action_ = Action::None;
    /// Host-supplied element symbols (see setStructureElements()).
    QStringList structureElements_;
    int stage_ = 0;
    bool hasSettingsStage_ = true; ///< resolved from hasTaskSettingsStage()
    bool settingsFirst_ = true;    ///< resolved from settingsStageFirst()
    /// Resolved from secondSettingsHeader(): true when the subclass supplied
    /// the optional extra stage between Calculator Settings and its own page.
    bool hasSecondSettingsStage_ = false;
    bool showsCalculatorStage_ = true; ///< resolved from showsCalculatorStage()
    int reviewStage_ = 3;          ///< index of the final (review) stage
    /// Index of the task-settings stage in `stack_`, or -1 when
    /// hasTaskSettingsStage() is false. goNext()/goBack() step over this
    /// index when skipTaskSettingsStage() is live-true, without touching the
    /// stack itself — see skipTaskSettingsStage()'s own doc comment.
    int taskSettingsStageIndex_ = -1;
    bool manuallyEdited_ = false;
    bool updatingPreview_ = false;

    QStackedWidget* stack_ = nullptr;
    QLabel* headerLabel_ = nullptr;
    /// Shown on the GPAW calculator page when the cutoff/XC/mode are hidden
    /// because they are inherited from a baseline SCF (see
    /// inheritsCalculatorFromBaseline()).
    QLabel* baselineInheritNote_ = nullptr;

    // Calculator Settings — engine selection (env is resolved from Preferences)
    QWidget* engineWidget_ = nullptr; ///< container for the engine-selector row
    QComboBox* calcCombo_ = nullptr;

    // Per-calculator settings. The DFT/GPAW controls are laid out in four
    // thematic group boxes (see buildDftGpawGroups). Shared cutoff/k-points
    // live in the first two, so a single widget set serves every DFT engine.
    QGroupBox* modeBasisGroup_ = nullptr;  ///< Mode & Basis Set (+ cutoff, XC)
    QGroupBox* bzGroup_ = nullptr;         ///< Brillouin Zone & k-Points
    QGroupBox* convGroup_ = nullptr;       ///< Electronic Convergence & Smearing
    QGroupBox* spinGroup_ = nullptr;       ///< Spin Configurations
    QGroupBox* outputGroup_ = nullptr;     ///< Output Files & Density Exports
    /// "XC defaults to PBE (editable in Stage 4)" note — only meaningful for the
    /// script-template DFT backends; hidden for GPAW, which has its own XC combo.
    QLabel* dftXcNote_ = nullptr;
    QDoubleSpinBox* cutoffSpin_ = nullptr;
    /// The plane-wave cutoff and, beside it, VASP's XC functional — one row,
    /// because ENCUT and the functional are chosen together. The container is
    /// what the form layout can resolve, so visibility toggles address it
    /// rather than the spin box nested inside.
    QWidget* cutoffRow_ = nullptr;
    QLabel* vaspXcLabel_ = nullptr;
    /// The form label of `cutoffRow_`, retitled when the cutoff spin box
    /// is hidden and the row carries only the XC functional.
    QLabel* cutoffRowLabel_ = nullptr;
    /// Eigensolver row: GPAW's solver combo or VASP's ALGO combo, plus the
    /// matching SCF step cap (GPAW's, or VASP's NELM). One row serving both,
    /// with the other engine's widgets hidden.
    QWidget* eigensolverRow_ = nullptr;
    QLabel* scfStepsLabel_ = nullptr;
    /// VASP's single SCF tolerance (EDIFF), on the row where GPAW shows its
    /// three.
    QWidget* vaspTolRow_ = nullptr;
    QSpinBox* kptSpins_[3] = {nullptr, nullptr, nullptr};
    QGroupBox* maceGroup_ = nullptr;
    QComboBox* maceModelCombo_ = nullptr;
    QComboBox* maceSizeCombo_ = nullptr;
    QComboBox* maceDeviceCombo_ = nullptr;
    QComboBox* macePrecisionCombo_ = nullptr;
    /// "Custom trained model" only: an editable dropdown listing the model
    /// checkpoints found in the ML potentials directory (Preferences), with
    /// Browse… as the escape hatch for files elsewhere. Item data carries the
    /// absolute path; the edit line accepts a hand-typed one.
    QWidget* maceModelFileRow_ = nullptr;
    QComboBox* maceModelFileCombo_ = nullptr;
    QPushButton* maceBrowseButton_ = nullptr;
    QLabel* maceModelPathHint_ = nullptr;
    /// MACE-MP-0 only: mace_mp(dispersion=True) — the D3(BJ) correction the
    /// foundation model ships. Hidden for MACE-OFF and custom checkpoints.
    QCheckBox* maceDispersionCheck_ = nullptr;

    // Shared MLIP controls (DeepMD / NequIP / Allegro / CHGNet / MatterSim /
    // FAIRChem). One model-file row and one device selector serve them all;
    // the engine-specific rows below are shown per selection.
    QGroupBox* mlipGroup_ = nullptr;
    QWidget* mlipModelRow_ = nullptr;   ///< model path + Browse
    QLabel* mlipModelLabel_ = nullptr;  ///< retitled per engine (.pb/.pth/.pt)
    QLineEdit* mlipModelEdit_ = nullptr;
    QComboBox* mlipDeviceCombo_ = nullptr;
    QWidget* nequipUnitsRow_ = nullptr; ///< deployed-model energy/length units
    QComboBox* nequipEnergyUnitsCombo_ = nullptr;
    QComboBox* nequipLengthUnitsCombo_ = nullptr;
    QComboBox* chgnetWeightsCombo_ = nullptr;
    QComboBox* matterSimModelCombo_ = nullptr;
    QCheckBox* matterSimThermalCheck_ = nullptr;
    QWidget* matterSimStateRow_ = nullptr; ///< T / P spin boxes
    QDoubleSpinBox* matterSimTempSpin_ = nullptr;
    QDoubleSpinBox* matterSimPressureSpin_ = nullptr;
    QComboBox* fairChemModelCombo_ = nullptr;

    // GPAW controls (distributed across the four thematic groups above).
    QComboBox* gpawModeCombo_ = nullptr;
    QDoubleSpinBox* gpawGridSpacingSpin_ = nullptr;
    QComboBox* gpawBasisCombo_ = nullptr;
    QComboBox* gpawXcCombo_ = nullptr;
    QComboBox* gpawEigensolverCombo_ = nullptr;
    QComboBox* gpawMixerCombo_ = nullptr;
    QDoubleSpinBox* gpawBetaSpin_ = nullptr;
    QSpinBox* gpawNmaxoldSpin_ = nullptr;
    QDoubleSpinBox* gpawWeightSpin_ = nullptr;
    /// Composite rows pairing controls side by side to save vertical space.
    /// Each is the widget the form layout can resolve, so visibility toggles
    /// address these rather than the fields nested inside them:
    ///   gpawMixerRow_    — mixer kind + its beta/nmaxold/weight parameters
    ///   gpawTolRow_      — eigenstate tolerance + density tolerance
    ///   gpawBzTogglesRow_— Gamma-centered Grid + Symmetry: off
    QWidget* gpawMixerRow_ = nullptr;
    QWidget* gpawTolRow_ = nullptr;
    QWidget* gpawBzTogglesRow_ = nullptr;
    /// Convergence thresholds are ~1e-8..1e-4, which a QDoubleSpinBox can
    /// only show as "0.000000040000" — plain line edits with a
    /// scientific-notation validator keep them readable and typable.
    QLineEdit* gpawEigenTolEdit_ = nullptr;
    QLineEdit* gpawDensityTolEdit_ = nullptr;
    /// "Symmetry: off" — emits symmetry="off" (no point-group k-point
    /// reduction). Shown only when showsGpawSymmetryToggle() is true.
    QCheckBox* gpawSymmetryOffCheck_ = nullptr;
    /// "Gamma-centered Grid" — emits kpts={'size':…,'gamma':True}. GPAW only.
    QCheckBox* gpawGammaCheck_ = nullptr;
    /// "Hubbard parameters…" + "van der Waals Correction (DFTD4)", paired on
    /// one row of "Mode & Basis Set" directly under the XC functional combo:
    /// both are corrections to the chosen functional, not k-point settings.
    /// GPAW only, so the row hides as a unit for the other DFT engines.
    QWidget* xcCorrectionsRow_ = nullptr;
    /// "Hubbard parameters…" — GPAW only. The edited state lives here rather
    /// than in the dialog, which is constructed on demand and destroyed on
    /// close.
    QPushButton* hubbardButton_ = nullptr;
    bool hubbardEnabled_ = false;
    std::vector<core::HubbardU> hubbardParameters_;
    /// "van der Waals Correction (DFTD4)" — offered by the wizards whose task
    /// involves forces or energy differences between geometries.
    QCheckBox* dispersionD4Check_ = nullptr;
    /// "Export Charge Density (.cube)" + pseudo/all-electron type — shown only
    /// when showsGpawDensityExport() is true (Single-Point).
    QCheckBox* gpawDensityExportCheck_ = nullptr;
    QComboBox* gpawDensityTypeCombo_ = nullptr;
    /// The six selectable volumetric fields, in the order they are laid out
    /// (two columns) and in the order core::GpawDensityExports declares them.
    static constexpr int kDensityFieldCount = 6;
    QCheckBox* densityFieldChecks_[kDensityFieldCount] = {};
    /// "Compress to HDF5" — GPAW side. Both this and vaspHdf5CompressCheck_
    /// below feed the SAME CalculatorConfig::compressDensityToHdf5 (only one
    /// is ever visible, since only one engine is active), rather than one
    /// shared widget reparented between two group boxes.
    QCheckBox* hdf5CompressCheck_ = nullptr;
    // LAMMPS. The engine supplies no force field of its own, so the pair style
    // and its coefficients ARE the physics and every one of these feeds the
    // generated script.
    QGroupBox* lammpsGroup_ = nullptr;
    QComboBox* lammpsInterfaceCombo_ = nullptr; ///< LAMMPSlib vs lammpsrun
    QLineEdit* lammpsPairStyleEdit_ = nullptr;
    QPlainTextEdit* lammpsPairCoeffEdit_ = nullptr;  ///< one pair_coeff per line
    QPlainTextEdit* lammpsPotentialEdit_ = nullptr;  ///< one file path per line
    QPlainTextEdit* lammpsExtraEdit_ = nullptr;      ///< one command per line
    QLineEdit* lammpsCommandEdit_ = nullptr;         ///< `lmp` (Run only)
    QCheckBox* lammpsLogCheck_ = nullptr;

    // -- xTB ----------------------------------------------------------------
    QGroupBox* xtbGroup_ = nullptr;
    QComboBox* xtbMethodCombo_ = nullptr;
    QDoubleSpinBox* xtbAccuracySpin_ = nullptr;
    QDoubleSpinBox* xtbTempSpin_ = nullptr;   ///< electronic temperature (K)
    QSpinBox* xtbMaxIterSpin_ = nullptr;      ///< SCC iteration cap

    // -- DFTB+ --------------------------------------------------------------
    QGroupBox* dftbGroup_ = nullptr;
    /// Slater-Koster directory (line edit + Browse on one row). Editable and
    /// persisted in QSettings, unlike the VASP POTCAR note: there is no
    /// Preferences → External Files entry for it (yet), so this field is the
    /// single home of the value rather than a second one.
    QWidget* dftbSlakoRow_ = nullptr;
    QLineEdit* dftbSlakoEdit_ = nullptr;
    QCheckBox* dftbSccCheck_ = nullptr;
    QLineEdit* dftbSccTolEdit_ = nullptr;     ///< scientific notation (1e-5)
    QSpinBox* dftbMaxSccSpin_ = nullptr;
    QDoubleSpinBox* dftbFillingTempSpin_ = nullptr; ///< K; 0 = no smearing

    // -- ABINIT / FHI-aims / NWChem / OpenMX / FLEUR / CP2K / Amber ---------
    //
    // One class holding all seven groups, rather than seven more sets of
    // widget members here. What they need from this class is only the SHARED
    // rows above (the plane-wave cutoff and the k-grid), and several of them
    // take neither — so the code that is specific to one engine lives with
    // that engine instead of adding another eighty members to this header.
    ExtendedEngineGroups extendedEngines_{this};

    // -- GROMACS ------------------------------------------------------------
    QGroupBox* gromacsGroup_ = nullptr;
    QComboBox* gromacsForceFieldCombo_ = nullptr;   ///< editable (pdb2gmx -ff)
    QComboBox* gromacsWaterCombo_ = nullptr;        ///< pdb2gmx -water
    QLineEdit* gromacsGmxEdit_ = nullptr;           ///< the gmx wrapper binary
    QPlainTextEdit* gromacsMdpEdit_ = nullptr;      ///< key = value per line

    // -- VASP ---------------------------------------------------------------
    // In the shared base rather than in one wizard, so every wizard built on
    // it that offers VASP gets the same INCAR controls without a second copy
    // to keep in step.
    QGroupBox* vaspGroup_ = nullptr;
    /// Reports the VASP_PP_PATH configured in Preferences → External Files,
    /// and says so when it is not. There is no editable field here: the
    /// dataset library is per-installation, and a second place to set it was a
    /// second place for it to be wrong.
    QLabel* vaspPotcarNote_ = nullptr;
    QComboBox* vaspXcCombo_ = nullptr;
    QComboBox* vaspPrecCombo_ = nullptr;
    QComboBox* vaspAlgoCombo_ = nullptr;
    QSpinBox* vaspNelmSpin_ = nullptr;
    QLineEdit* vaspEdiffEdit_ = nullptr;
    QComboBox* vaspLrealCombo_ = nullptr;
    QComboBox* vaspDriverCombo_ = nullptr;
    QComboBox* vaspIbrionCombo_ = nullptr;
    QComboBox* vaspIsifCombo_ = nullptr;
    QDoubleSpinBox* vaspEdiffgSpin_ = nullptr;
    /// NSW under VASP's own internal relaxation route — surfaced here,
    /// beside IBRION/ISIF/EDIFFG, because the ASE-side "Max steps" field
    /// (Geometry Optimization's own Stage 2) is exactly the stage this route
    /// skips (see skipTaskSettingsStage()). Read only when
    /// vaspInternalRelaxationSelected() is true; the ASE route keeps using
    /// its own max-steps field unchanged.
    QSpinBox* vaspNswSpin_ = nullptr;
    QWidget* vaspIonicRow_ = nullptr;
    /// "Restart from a converged CHGCAR" — the SOC two-stage chain's second
    /// stage (an ordinary Single-point Calculation reusing a first,
    /// collinear one's density via ICHARG = 11). Populated by the host
    /// (mirroring ElectronicBandsWizard::setDensityBaselines()); the base
    /// class's own baselineDensityPathToStage() reads it generically. Empty
    /// selection ("None — converge here") means the pre-existing behaviour:
    /// a fresh SCF.
    QComboBox* vaspBaselineCombo_ = nullptr;
    /// The Functional block — see buildVaspGroup(). The mixing rows are
    /// hidden for a semilocal selection and HFSCREEN additionally for an
    /// unscreened hybrid, so the page never shows a control the generated
    /// INCAR will not carry.
    QComboBox* vaspFunctionalCombo_ = nullptr;
    QDoubleSpinBox* vaspAexxSpin_ = nullptr;
    QDoubleSpinBox* vaspHfscreenSpin_ = nullptr;
    QCheckBox* vaspHybridChainCheck_ = nullptr;
    QComboBox* vaspWavecarCombo_ = nullptr;
    /// SAXIS — only written when Spin Configurations above is set to
    /// Non-Collinear; shown whenever the VASP group itself is, with a
    /// tooltip saying so, rather than adding cross-widget visibility wiring
    /// to a group owned by a different helper (GpawElectronicRows).
    QDoubleSpinBox* vaspSaxisSpin_[3] = {nullptr, nullptr, nullptr};
    /// LMAXMIX — 0 means "leave VASP's own default"; see
    /// CalculatorConfig::vaspLmaxmix's own doc comment. Auto-raised to 4/6
    /// when Hubbard U turns on a d/f-shell species (see
    /// refreshVaspLmaxmixAuto()) UNLESS vaspLmaxmixEdited_ says the user has
    /// already set it by hand — the same "auto until the user overrides it"
    /// shape as SimulationWizardBase::runCommandEdited_.
    QSpinBox* vaspLmaxmixSpin_ = nullptr;
    bool vaspLmaxmixEdited_ = false;

    // -- DFT+U (LDAU), VASP (Task 2, 2026-08-22) -----------------------------
    // A separate model and a separate section from GPAW's own hubbardButton_/
    // HubbardParametersDialog above: VASP's LDAUL/LDAUU/LDAUJ arrays and its
    // LDAUTYPE choice have no GPAW equivalent, and the two engines' data
    // shapes (CalculatorConfig::hubbardU vs. ::vaspHubbardU) are kept
    // deliberately separate for the same reason. Inline in the VASP group
    // itself (species/l/U/J is small enough not to need a modal dialog the
    // way GPAW's free-form add/remove table does), one FIXED row per
    // structure species — never user-add/removable, since an LDAUL/LDAUU/
    // LDAUJ entry for an element the cell does not contain is not merely
    // inert (GPAW's own case) but has no POSCAR-species slot to occupy at
    // all.
    QCheckBox* vaspHubbardCheck_ = nullptr;
    QComboBox* vaspHubbardTypeCombo_ = nullptr;
    /// Columns: l (QSpinBox, -1..3), U (QDoubleSpinBox, eV), J (QDoubleSpinBox,
    /// eV) — the vertical header carries the (read-only) species symbol.
    /// One row per calculatorElements() entry, built once and never resized;
    /// see the class's own note on why rows are never added/removed here.
    QTableWidget* vaspHubbardTable_ = nullptr;
    QCheckBox* vaspLwaveCheck_ = nullptr;
    QCheckBox* vaspLchargCheck_ = nullptr;
    QCheckBox* vaspLaechgCheck_ = nullptr;
    QCheckBox* vaspLorbitCheck_ = nullptr;
    /// "Compress to HDF5" — VASP side. See hdf5CompressCheck_ above.
    QCheckBox* vaspHdf5CompressCheck_ = nullptr;
    QSpinBox* vaspNcoreSpin_ = nullptr;
    QSpinBox* vaspKparSpin_ = nullptr;
    QPlainTextEdit* vaspExtraIncarEdit_ = nullptr;

    // -- Quantum ESPRESSO ---------------------------------------------------
    // QE's decisions have no counterpart in the shared rows, which is why they
    // are a group of their own rather than being squeezed onto the plane-wave
    // cutoff row the way VASP's XC functional is. The dual cutoff is the
    // reason: ecutwfc and ecutrho are ONE decision, made together, and the
    // right ratio depends on the pseudopotential family — a fact that has no
    // GPAW or VASP analogue to sit beside.
    QGroupBox* qeGroup_ = nullptr;
    QLabel* qePseudoNote_ = nullptr;
    QDoubleSpinBox* qeEcutwfcSpin_ = nullptr;
    QDoubleSpinBox* qeEcutrhoSpin_ = nullptr;
    QLabel* qeDualNote_ = nullptr;
    QComboBox* qeInputDftCombo_ = nullptr;
    QComboBox* qeOccupationsCombo_ = nullptr;
    QComboBox* qeSmearingCombo_ = nullptr;
    QDoubleSpinBox* qeDegaussSpin_ = nullptr;
    QLineEdit* qeConvThrEdit_ = nullptr;

    // -- SIESTA -------------------------------------------------------------
    // SIESTA has no plane-wave cutoff at all (see core::SiestaBasisType). Its
    // basis quality is three separate numbers, and offering it the shared
    // "plane-wave cutoff" row silently mapped one of them onto the real-space
    // mesh — so raising it to converge "the basis" refined a grid while the
    // basis stayed exactly as small.
    QGroupBox* siestaGroup_ = nullptr;
    QLabel* siestaPseudoNote_ = nullptr;
    /// Shown only when the structure is periodic — see setStructurePeriodic().
    QLabel* xtbPeriodicNote_ = nullptr;
    bool structurePeriodic_ = false;
    /// Which `siesta` binary a run would actually start, and whether it will be
    /// launched under MPI — both resolved from the Conda environments rather
    /// than assumed to be on $PATH. See RunCommands::condaSiestaCommand().
    QLabel* siestaBinaryNote_ = nullptr;
    QComboBox* siestaXcCombo_ = nullptr;
    QComboBox* siestaBasisTypeCombo_ = nullptr;
    QComboBox* siestaBasisSizeCombo_ = nullptr;
    QDoubleSpinBox* siestaEnergyShiftSpin_ = nullptr;
    QDoubleSpinBox* siestaMeshCutoffSpin_ = nullptr;

    QGroupBox* orcaGroup_ = nullptr;
    QComboBox* orcaMethodCombo_ = nullptr;
    QComboBox* orcaBasisCombo_ = nullptr;
    QSpinBox* chargeSpin_ = nullptr;
    QSpinBox* multiplicitySpin_ = nullptr;
    QLabel* calcSettingsHint_ = nullptr;

    // Stage 4 — review
    QPlainTextEdit* preview_ = nullptr;
    /// Editable "Running:" line: the resolved launch command, shown so the
    /// user can adjust rank counts or flags at the last moment without going
    /// back to Preferences. Re-resolved from the engine whenever the review
    /// stage is (re)entered, UNLESS the user has edited it.
    QLineEdit* runCommandEdit_ = nullptr;
    bool runCommandEdited_ = false;

    // Action bar
    QPushButton* backButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* runRemoteButton_ = nullptr;
    QPushButton* runLocalButton_ = nullptr;
};

} // namespace calango::gui
