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

/// Shared four-stage stepper shell for the simulation wizards (Molecular
/// Dynamics, Geometry Optimization, Phonon Calculator). It owns the
/// standardized Stages 2–4 (Calculator & Execution Environment, Calculator
/// Settings, ASE Script Review) plus the navigation + action bar
/// (Back / Cancel / Next / Export Script / Run Remote / Run Local). Each
/// concrete wizard supplies Stage 1 (its task settings) and the script
/// generation via the virtual hooks. The host inspects action() after exec().
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

    /// Calculator kind + backend knobs (DFT cutoff/k-points, MACE, ORCA) from
    /// Stages 2–3; the subclass adds its task fields to build the final config.
    core::CalculatorConfig baseCalculatorConfig() const;
    core::CalculatorKind selectedCalculator() const;

protected Q_SLOTS:
    void refreshPreview();

private Q_SLOTS:
    void goNext();
    void goBack();
    void exportScript();
    void updateCalculatorEnabled();

private:
    QWidget* buildEnvironmentPage();
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

    // Stage 2 — calculator + environment
    QComboBox* calcCombo_ = nullptr;
    QLineEdit* envEdit_ = nullptr;
    QLabel* envStatus_ = nullptr;
    /// Execution mode chosen on the Script Review stage's "Execution Settings…"
    /// launcher; biases which Run button is the default (both remain available).
    bool preferRemote_ = false;
    /// Guards the env-preset auto-fill from re-saving during programmatic edits.
    bool loadingEnvPreset_ = false;

    // Stage 3 — per-calculator settings
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
    QPushButton* execSettingsButton_ = nullptr; ///< review-stage exec launcher
    QPushButton* runRemoteButton_ = nullptr;
    QPushButton* runLocalButton_ = nullptr;

    /// Review-stage "Execution Settings…" popup: choose Local/Remote and edit
    /// the environment, replacing the removed Execution Mode stage controls.
    void showExecutionSettings();
    /// Auto-fill the environment field from the selected calculator's saved
    /// preset (`~/.calango/settings.json`), and persist edits back per engine.
    void applyEnvPresetForCalculator();
};

} // namespace calango::gui
