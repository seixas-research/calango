#pragma once

#include "core/CalculatorConfig.hpp"

#include <QWizard>

#include <memory>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QWizardPage;

namespace calango::gui {

class MlipTrainerBackend;

/// MLIP → Trainer…: a multi-step builder for a machine-learning-potential
/// training run.
///
/// THE SHAPE, and why it changed. This was one dialog with every MACE
/// parameter on it at once — two columns, five group boxes, a YAML preview
/// beside them — and it had grown past the point where it could be read, let
/// alone adjusted, without scrolling past the control you came for. It is
/// now a wizard, one decision per page:
///
///   1. **Framework** — which model type to train. Every MLIP engine the
///      calculator library knows how to RUN is listed; exactly one (MACE)
///      has an implemented trainer, and the others say what theirs would
///      need rather than being hidden. See MlipTrainerBackend.
///   2..N. **The framework's own parameters** — for MACE: Dataset, Model,
///      Training. Supplied by the BACKEND, not by this class, which is what
///      makes a second framework a subclass rather than an edit here.
///   N+1. **Config** — the generated config file, editable, plus the
///      interpreter, the pre-flight and the Run buttons.
///
/// THE CONFIG PAGE IS THE LAST WORD. Whatever text is in that editor is what
/// gets written and run — verbatim, including hand edits. It replaces the
/// free-form "extra keys" override the old dialog had, and subsumes it: a
/// whole editable file is strictly more powerful than an append-only
/// override box, and it has the property the override box never did, which
/// is that what you SEE is what runs. "Regenerate from settings" rebuilds it
/// from the pages and says so first, because that discards the edits.
///
/// The class name still says MACE, and the Orchestration node it backs is
/// still OrchestrationTask::MaceTrainer with its own persisted slug — both
/// are append-only identifiers, neither is user-visible, and the menu entry
/// has read "Trainer…" since before this change. Renaming them is a job for
/// the commit that adds the second backend, not for the one that makes the
/// second backend possible.
class MaceTrainerDialog : public QWizard {
    Q_OBJECT

public:
    enum class Action { None, RunLocal, RunRemote };

    explicit MaceTrainerDialog(QWidget* parent = nullptr);
    ~MaceTrainerDialog() override;

    Action action() const { return action_; }
    /// The (possibly hand-edited) config text — what will actually run.
    QString yaml() const;
    /// Restore a previously-saved config verbatim (an Orchestration node
    /// being re-opened after "Save process node") — sets the editor text
    /// and marks it hand-edited, so nothing ever overwrites it with a
    /// freshly regenerated config the individual widgets do not agree with
    /// (they are NOT parsed back out of it; only the text itself is
    /// restored, exactly as it was saved).
    void setInitialYaml(const QString& yaml);
    /// Pre-wire the dataset fields from an Orchestration Dataset Manager
    /// node's own manifest — the "typed output edge" hand-off, absent any
    /// actual edge-typing system on the canvas. Empty arguments are simply
    /// left at their existing defaults. Only meaningful before the wizard
    /// is shown; has no effect after setInitialYaml() (a previously-saved
    /// config always wins — see its own doc comment).
    void prefillFromDatasetManifest(const QString& trainPath,
                                    const QString& validPath,
                                    const QString& energyKey,
                                    const QString& forcesKey);
    /// A standalone Python launcher that writes the config and runs the
    /// training (once per committee seed, for a Query-by-Committee run).
    QString runnerScript() const;
    /// The resolved interpreter (selected conda env, else the embedded one).
    QString pythonExecutable() const;

    /// The framework currently selected on page 1.
    core::CalculatorKind framework() const { return framework_; }

private Q_SLOTS:
    void refreshPreview();
    void exportYaml();
    /// Probes the framework's package availability/version and which torch
    /// devices are usable under the currently selected interpreter, and
    /// updates envStatus_ with the result. Synchronous (a subprocess
    /// start/import), so only fired on demand — construction never blocks
    /// on it.
    void checkEnvironment();

private:
    QWizardPage* buildFrameworkPage();
    QWizardPage* buildConfigPage();
    /// Swap in the backend for `kind`, replacing whatever parameter pages
    /// the previous one contributed. A no-op when it is already installed.
    ///
    /// Only ever called with MACE today, because it is the only implemented
    /// backend — but written as a swap rather than as a one-time build so
    /// that the second backend needs no change HERE, which is the whole
    /// claim the framework page makes.
    void installBackend(core::CalculatorKind kind);
    /// Pre-flight: the framework's Python package importable under the
    /// resolved interpreter. `false` means the caller must not
    /// accept()/launch anything; a message naming the missing package and
    /// its install command has already been shown.
    bool preflightPackage();
    /// Show the Export / Run buttons on the config page and nowhere else.
    void syncCustomButtons(int pageId);

    std::unique_ptr<MlipTrainerBackend> backend_;
    core::CalculatorKind framework_ = core::CalculatorKind::Mace;
    /// Whether `framework_` has an implemented backend — the framework
    /// page's completeness, read by reference so Next follows the selection
    /// without the page having to know about backends at all.
    bool frameworkImplemented_ = true;
    /// The ids this wizard's own two pages own. Parameter pages take
    /// 1..kConfigPageId-1, so a backend contributing more of them never
    /// collides with the config page and QWizard's default id ordering
    /// still puts it last.
    static constexpr int kFrameworkPageId = 0;
    static constexpr int kConfigPageId = 100;
    QList<int> parameterPageIds_;

    QListWidget* frameworkList_ = nullptr;
    QLabel* frameworkDetails_ = nullptr;

    QLineEdit* envEdit_ = nullptr;
    QPushButton* checkEnvButton_ = nullptr;
    QLabel* envStatus_ = nullptr;
    QPlainTextEdit* preview_ = nullptr;
    QLabel* previewStale_ = nullptr;
    bool manuallyEdited_ = false;
    Action action_ = Action::None;
    /// The framework package's own __version__, set by the LAST successful
    /// pre-flight check (checkEnvironment() or a Run click) — recorded into
    /// the generated config as a comment, so a run's config file names the
    /// package version it was actually generated for. Empty until a check
    /// has succeeded at least once, OR if the installed package genuinely
    /// reports no __version__ — lastCheckAvailable_ is the actual
    /// available/not signal, this is metadata only.
    QString detectedVersion_;
    /// Whether the LAST checkEnvironment() call found the package
    /// importable. What preflightPackage() actually gates a Run on.
    bool lastCheckAvailable_ = false;
};

} // namespace calango::gui
