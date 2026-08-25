#pragma once

#include "gui/MlipTrainerBackend.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QSpinBox;

namespace calango::gui {

/// The MACE trainer: the one framework in the Trainer wizard with a real
/// backend, and the reference implementation of MlipTrainerBackend.
///
/// It owns three parameter pages — Dataset, Model, Training — and turns
/// whatever they hold into `mace_train.yaml` plus a self-contained Python
/// launcher.
///
/// EVERY KEY IT EMITS IS ONE `mace.tools.arg_parser` ACCEPTS. MACE loads its
/// config through configargparse, which ABORTS on a key it does not
/// recognise, so an invented setting is a failed run rather than an ignored
/// line. `mace_trainer_preflight` checks that live, against whatever
/// mace-torch is installed, rather than against a list written here.
///
/// The three failure modes this backend exists to prevent, all of which
/// happen on the training machine minutes-to-hours after the wizard closed:
///
///   * an unrecognised key — see above;
///   * MACE's default reference-data keys are REF_energy / REF_forces, while
///     ASE (and so every dataset Calango exports) puts the energy and forces
///     on a SinglePointCalculator, leaving atoms.info and atoms.arrays empty
///     on read-back. With the defaults MACE finds neither, warns, sets the
///     per-property weight to zero, and trains a model on nothing;
///   * E0s is not optional: without it, and without config_type=IsolatedAtom
///     frames in the training file, MACE raises before the first epoch.
class MaceTrainerBackend : public MlipTrainerBackend {
    Q_OBJECT

public:
    MaceTrainerBackend();

    core::CalculatorKind kind() const override;
    QList<QWizardPage*> createParameterPages(QWidget* parent) override;
    QString buildConfig(const QString& interpreterNote) const override;
    QString runnerScript(const QString& config) const override;
    void prefillFromDatasetManifest(const QString& trainPath,
                                    const QString& validPath,
                                    const QString& energyKey,
                                    const QString& forcesKey) override;
    void applyTorchDevices(bool cuda, bool mps, bool probeSucceeded) override;
    QString selectedDevice() const override;

private:
    /// The `E0s:` value for the config, or an empty string when the user
    /// picked "read from the training set".
    QString e0sValue() const;
    /// Preset the channels / max L for the chosen MACE size.
    void applySizePreset();
    /// Every control that changes the config, wired to settingsChanged().
    void connectSettingSignals();

    // Dataset page.
    QLineEdit* trainFileEdit_ = nullptr;
    QLineEdit* validFileEdit_ = nullptr;
    QComboBox* energyKeyCombo_ = nullptr;
    QComboBox* forcesKeyCombo_ = nullptr;
    QComboBox* e0sModeCombo_ = nullptr;
    QLineEdit* e0sFileEdit_ = nullptr;

    // Model page.
    QComboBox* sizeCombo_ = nullptr;
    QDoubleSpinBox* rMaxSpin_ = nullptr;
    QSpinBox* channelsSpin_ = nullptr;
    QSpinBox* maxLSpin_ = nullptr;
    /// The two architecture constants this dialog used to hard-code into
    /// every config. Exposed on the model page's Advanced group at exactly
    /// the values they were fixed at, so an untouched wizard emits the same
    /// file it always did — and a user who needs a deeper message-passing
    /// network no longer has to hand-edit the YAML to get one.
    QSpinBox* interactionsSpin_ = nullptr;
    QSpinBox* correlationSpin_ = nullptr;

    // Training page.
    QDoubleSpinBox* lrSpin_ = nullptr;
    QSpinBox* batchSpin_ = nullptr;
    QSpinBox* epochsSpin_ = nullptr;
    QDoubleSpinBox* energyWeightSpin_ = nullptr;
    QDoubleSpinBox* forcesWeightSpin_ = nullptr;
    QDoubleSpinBox* stressWeightSpin_ = nullptr;
    QDoubleSpinBox* virialsWeightSpin_ = nullptr;
    QSpinBox* seedSpin_ = nullptr;
    QSpinBox* patienceSpin_ = nullptr;
    QSpinBox* evalIntervalSpin_ = nullptr;
    QComboBox* dtypeCombo_ = nullptr;
    QComboBox* deviceCombo_ = nullptr;
    QGroupBox* swaGroup_ = nullptr;
    QSpinBox* swaStartSpin_ = nullptr;
    QCheckBox* emaCheck_ = nullptr;
    QDoubleSpinBox* emaDecaySpin_ = nullptr;
    QGroupBox* qbcGroup_ = nullptr;
    QSpinBox* committeeSpin_ = nullptr;
    QDoubleSpinBox* uncertaintySpin_ = nullptr;
    /// Set once the user picks a device by hand, after which an environment
    /// probe stops overriding it — testing the cpu path deliberately must
    /// survive a Check Environment that finds a GPU.
    bool deviceChosenByHand_ = false;
};

} // namespace calango::gui
