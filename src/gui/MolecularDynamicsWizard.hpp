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

/// Simulation → "Molecular Dynamics…": a four-stage stepper that replaces the
/// single MD dialog.
///   1. Dynamics Settings — ensemble + physical parameters.
///   2. Calculator & Execution Environment — engine + conda/python env +
///      local/remote mode.
///   3. Calculator Settings — knobs specific to the chosen engine.
///   4. ASE Script Review — the editable generated script, with the action
///      bar (Cancel / Export Script / Run Remote / Run Local).
/// The host inspects action() after exec() to decide how to launch.
class MolecularDynamicsWizard : public QDialog {
    Q_OBJECT

public:
    enum class Action { None, RunLocal, RunRemote };

    explicit MolecularDynamicsWizard(QWidget* parent = nullptr);

    Action action() const { return action_; }
    QString script() const;
    QString pythonExecutable() const;

private Q_SLOTS:
    void goNext();
    void goBack();
    void exportScript();
    void refreshPreview();
    void updateEnsembleEnabled();
    void updateCalculatorEnabled();

private:
    QWidget* buildDynamicsPage();
    QWidget* buildEnvironmentPage();
    QWidget* buildCalculatorPage();
    QWidget* buildReviewPage();
    void updateStage();
    core::CalculatorConfig config() const;

    Action action_ = Action::None;
    int stage_ = 0;
    bool manuallyEdited_ = false;
    bool updatingPreview_ = false;

    QStackedWidget* stack_;
    QLabel* headerLabel_;

    // Stage 1 — dynamics
    QComboBox* ensembleCombo_;
    QDoubleSpinBox* temperatureSpin_;
    QDoubleSpinBox* pressureSpin_; // bar (converted to GPa in config())
    QDoubleSpinBox* timestepSpin_;
    QDoubleSpinBox* frictionSpin_;
    QDoubleSpinBox* tautSpin_;
    QDoubleSpinBox* taupSpin_;
    QSpinBox* stepsSpin_;
    QSpinBox* sampleSpin_;

    // Stage 2 — calculator + environment
    QComboBox* calcCombo_;
    QLineEdit* envEdit_;
    QLabel* envStatus_;
    QRadioButton* localRadio_;
    QRadioButton* remoteRadio_;

    // Stage 3 — per-calculator settings
    QGroupBox* dftGroup_;
    QDoubleSpinBox* cutoffSpin_;
    QSpinBox* kptSpins_[3];
    QGroupBox* maceGroup_;
    QComboBox* maceModelCombo_;
    QComboBox* maceSizeCombo_;
    QComboBox* maceDeviceCombo_;
    QGroupBox* orcaGroup_;
    QComboBox* orcaMethodCombo_;
    QComboBox* orcaBasisCombo_;
    QSpinBox* chargeSpin_;
    QSpinBox* multiplicitySpin_;
    QLabel* calcSettingsHint_;

    // Stage 4 — review
    QPlainTextEdit* preview_;

    // Action bar
    QPushButton* backButton_;
    QPushButton* nextButton_;
    QPushButton* exportButton_;
    QPushButton* runRemoteButton_;
    QPushButton* runLocalButton_;
};

} // namespace calango::gui
