#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace calango::gui {

/// MLIP → Trainer…: an interactive builder for MACE training configuration
/// YAML files (mace_train.yaml). Exposes the common hyperparameters (model
/// size, cutoff radius, channels, max L, learning rate, per-property loss
/// weights, batch size, epochs), the reference-data keys and isolated-atom
/// energies MACE cannot run without, the stage-two (SWA) and EMA settings a
/// production run wants, plus optional Active-Learning / Query-by-Committee
/// settings (ensemble committee size, base seed, uncertainty threshold). The
/// generated YAML can be exported, or a training run launched locally /
/// remotely through the host's job runner.
///
/// Every key emitted here is one `mace.tools.arg_parser` accepts: MACE loads
/// the config through configargparse, which ABORTS on a key it does not
/// recognise, so an invented setting is a failed run rather than an ignored
/// line.
class MaceTrainerDialog : public QDialog {
    Q_OBJECT

public:
    enum class Action { None, RunLocal, RunRemote };

    explicit MaceTrainerDialog(QWidget* parent = nullptr);

    Action action() const { return action_; }
    /// The (possibly hand-edited) YAML config text.
    QString yaml() const;
    /// Restore a previously-saved YAML verbatim (an Orchestration node
    /// being re-opened after "Save process node") — sets the preview text
    /// and marks it hand-edited, so refreshPreview() never overwrites it
    /// with a freshly regenerated config the individual widgets do not
    /// agree with (they are NOT parsed back out of the YAML; only the text
    /// itself is restored, exactly as it was saved).
    void setInitialYaml(const QString& yaml);
    /// Pre-wire the dataset fields from an Orchestration Dataset Manager
    /// node's own manifest — the "typed output edge" hand-off, absent any
    /// actual edge-typing system on the canvas. Empty arguments are simply
    /// left at their existing defaults. Only meaningful before the dialog
    /// is shown; has no effect after setInitialYaml() (a previously-saved
    /// YAML always wins — see its own doc comment).
    void prefillFromDatasetManifest(const QString& trainPath,
                                    const QString& validPath,
                                    const QString& energyKey,
                                    const QString& forcesKey);
    /// A standalone Python launcher that writes mace_train.yaml and runs the
    /// MACE trainer once per committee seed (Query-by-Committee ensemble).
    QString runnerScript() const;
    /// The resolved interpreter (selected conda env, else the embedded one).
    QString pythonExecutable() const;

private Q_SLOTS:
    void applySizePreset();
    void refreshPreview();
    void exportYaml();
    void browseTrainFile();
    void browseValidFile();
    void browseE0sFile();
    /// Probes mace-torch's availability/version and which torch devices are
    /// usable under the currently selected interpreter, and updates
    /// envStatus_ with the result. Synchronous (a subprocess start/import),
    /// so only fired on demand — dialog construction never blocks on it.
    void checkEnvironment();

private:
    QString buildYaml() const;
    /// The `E0s:` value for the config, or an empty string when the user
    /// picked "read from the training set".
    QString e0sValue() const;
    /// Pre-flight: mace-torch importable under the resolved interpreter, and
    /// mace-torch itself is not vendored/hard-depended-on at build time —
    /// this is the ONLY point either Run button checks it. `false` means
    /// the caller must not accept()/launch anything; a message naming the
    /// missing package and install instructions has already been shown.
    bool preflightMaceTorch();

    // Dataset + architecture.
    QLineEdit* trainFileEdit_;
    QLineEdit* validFileEdit_;
    QComboBox* sizeCombo_;
    QDoubleSpinBox* rMaxSpin_;
    QSpinBox* channelsSpin_;
    QSpinBox* maxLSpin_;
    QComboBox* deviceCombo_;

    // How the reference data is named in the training file, and where the
    // isolated-atom energies come from. Both are settings MACE will not run
    // without getting right — see buildYaml().
    QComboBox* energyKeyCombo_;
    QComboBox* forcesKeyCombo_;
    QComboBox* e0sModeCombo_;
    QLineEdit* e0sFileEdit_;

    // Optimization.
    QDoubleSpinBox* lrSpin_;
    QSpinBox* batchSpin_;
    QSpinBox* epochsSpin_;
    QDoubleSpinBox* energyWeightSpin_;
    QDoubleSpinBox* forcesWeightSpin_;
    QDoubleSpinBox* stressWeightSpin_;
    QDoubleSpinBox* virialsWeightSpin_;
    QSpinBox* seedSpin_;
    QSpinBox* patienceSpin_;
    QSpinBox* evalIntervalSpin_;
    QComboBox* dtypeCombo_;

    // Stage two (SWA) and the exponential moving average — the settings that
    // separate a demonstration run from a production one.
    QGroupBox* swaGroup_;
    QSpinBox* swaStartSpin_;
    QCheckBox* emaCheck_;
    QDoubleSpinBox* emaDecaySpin_;

    // Active learning / Query by Committee.
    QGroupBox* qbcGroup_;
    QSpinBox* committeeSpin_;
    QDoubleSpinBox* uncertaintySpin_;

    // Execution environment.
    QLineEdit* envEdit_;
    QPushButton* checkEnvButton_;
    QLabel* envStatus_;

    QPlainTextEdit* preview_;
    bool manuallyEdited_ = false;
    Action action_ = Action::None;
    /// mace-torch's own __version__, set by the LAST successful pre-flight
    /// check (checkEnvironment() or a Run click) — recorded into the
    /// generated YAML as a comment, so a run's config file names the
    /// package version it was actually generated for. Empty until a check
    /// has succeeded at least once, OR if the installed package genuinely
    /// reports no __version__ — lastCheckAvailable_ is the actual
    /// available/not signal, this is metadata only.
    QString detectedMaceVersion_;
    /// Whether the LAST checkEnvironment() call found mace-torch
    /// importable. What preflightMaceTorch() actually gates a Run on.
    bool lastCheckAvailable_ = false;
};

} // namespace calango::gui
