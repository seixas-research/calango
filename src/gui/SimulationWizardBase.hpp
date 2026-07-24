#pragma once

#include "core/CalculatorConfig.hpp"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;

namespace calango::gui {

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

    Action action() const { return action_; }
    QString script() const;            ///< the (possibly edited) preview text
    QString pythonExecutable() const;  ///< selected env python, else embedded

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
    /// Default allows every kind; the Electronic Bands wizard overrides this
    /// to expose only DFT-capable electronic-structure calculators.
    virtual bool calculatorAllowed(core::CalculatorKind) const { return true; }

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

    /// Fired when the DFT k-point grid changes, so a subclass can rescale a
    /// derived mesh default (e.g. the PDOS k-mesh at 2× the SCF grid).
    virtual void calculatorKgridChanged() {}

    /// The SCF k-point grid value along an axis (0..2). Lets a subclass derive
    /// a denser mesh from the (baseline) SCF sampling.
    int calculatorKpoint(int axis) const;

    /// Calculator kind + backend knobs (DFT cutoff/k-points, MACE, ORCA) from
    /// Stages 2–3; the subclass adds its task fields to build the final config.
    core::CalculatorConfig baseCalculatorConfig() const;
    core::CalculatorKind selectedCalculator() const;
    /// Preselect the engine combo (no-op if the kind isn't offered). Call after
    /// buildUi() to open a wizard on a specific default engine.
    void selectCalculator(core::CalculatorKind kind);

protected Q_SLOTS:
    void refreshPreview();

private Q_SLOTS:
    void goNext();
    void goBack();
    void exportScript();
    void updateCalculatorEnabled();

private:
    QWidget* buildCalculatorPage();
    QWidget* buildMaceGroup(QWidget* parent);
    QWidget* buildGpawGroup(QWidget* parent);
    QWidget* buildReviewPage();
    void updateStage();
    /// Show only the MACE rows that apply to the selected model source.
    void updateMaceRows();
    /// Show only the GPAW rows that apply to the selected discretization.
    void updateGpawRows();

    Action action_ = Action::None;
    int stage_ = 0;
    bool hasSettingsStage_ = true; ///< resolved from hasTaskSettingsStage()
    bool settingsFirst_ = true;    ///< resolved from settingsStageFirst()
    int reviewStage_ = 3;          ///< index of the final (review) stage
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

    // Per-calculator settings
    QGroupBox* dftGroup_ = nullptr;
    /// "XC defaults to PBE (editable in Stage 4)" note — only meaningful for the
    /// script-template DFT backends; hidden for GPAW, which has its own XC combo.
    QLabel* dftXcNote_ = nullptr;
    QDoubleSpinBox* cutoffSpin_ = nullptr;
    QSpinBox* kptSpins_[3] = {nullptr, nullptr, nullptr};
    QGroupBox* maceGroup_ = nullptr;
    QComboBox* maceModelCombo_ = nullptr;
    QComboBox* maceSizeCombo_ = nullptr;
    QComboBox* maceDeviceCombo_ = nullptr;
    QComboBox* macePrecisionCombo_ = nullptr;
    QLineEdit* maceModelPathEdit_ = nullptr;
    QPushButton* maceBrowseButton_ = nullptr;
    QLabel* maceModelPathHint_ = nullptr;

    // Stage 3 — GPAW (shares cutoff/k-points with dftGroup_ above)
    QGroupBox* gpawGroup_ = nullptr;
    QComboBox* gpawModeCombo_ = nullptr;
    QDoubleSpinBox* gpawGridSpacingSpin_ = nullptr;
    QComboBox* gpawBasisCombo_ = nullptr;
    QComboBox* gpawXcCombo_ = nullptr;
    QComboBox* gpawEigensolverCombo_ = nullptr;
    QComboBox* gpawMixerCombo_ = nullptr;
    QDoubleSpinBox* gpawBetaSpin_ = nullptr;
    QSpinBox* gpawNmaxoldSpin_ = nullptr;
    QDoubleSpinBox* gpawWeightSpin_ = nullptr;
    /// Convergence thresholds are ~1e-8..1e-4, which a QDoubleSpinBox can
    /// only show as "0.000000040000" — plain line edits with a
    /// scientific-notation validator keep them readable and typable.
    QLineEdit* gpawEigenTolEdit_ = nullptr;
    QLineEdit* gpawDensityTolEdit_ = nullptr;
    QGroupBox* orcaGroup_ = nullptr;
    QComboBox* orcaMethodCombo_ = nullptr;
    QComboBox* orcaBasisCombo_ = nullptr;
    QSpinBox* chargeSpin_ = nullptr;
    QSpinBox* multiplicitySpin_ = nullptr;
    QLabel* calcSettingsHint_ = nullptr;

    // Stage 4 — review
    QPlainTextEdit* preview_ = nullptr;

    // Action bar
    QPushButton* backButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* runRemoteButton_ = nullptr;
    QPushButton* runLocalButton_ = nullptr;
};

} // namespace calango::gui
