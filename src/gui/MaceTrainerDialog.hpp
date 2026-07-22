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
/// weights, batch size, epochs) plus optional Active-Learning / Query-by-
/// Committee settings (ensemble committee size, base seed, uncertainty
/// threshold). The generated YAML can be exported, or a training run launched
/// locally / remotely through the host's job runner.
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

private:
    QString buildYaml() const;

    // Dataset + architecture.
    QLineEdit* trainFileEdit_;
    QComboBox* sizeCombo_;
    QDoubleSpinBox* rMaxSpin_;
    QSpinBox* channelsSpin_;
    QSpinBox* maxLSpin_;
    QComboBox* deviceCombo_;

    // Optimization.
    QDoubleSpinBox* lrSpin_;
    QSpinBox* batchSpin_;
    QSpinBox* epochsSpin_;
    QDoubleSpinBox* energyWeightSpin_;
    QDoubleSpinBox* forcesWeightSpin_;
    QDoubleSpinBox* stressWeightSpin_;
    QDoubleSpinBox* virialsWeightSpin_;
    QSpinBox* seedSpin_;

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
