#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QPlainTextEdit;
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

private:
    QString buildYaml() const;
    /// The `E0s:` value for the config, or an empty string when the user
    /// picked "read from the training set".
    QString e0sValue() const;

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

    QPlainTextEdit* preview_;
    bool manuallyEdited_ = false;
    Action action_ = Action::None;
};

} // namespace calango::gui
