#pragma once

#include "core/CalculatorConfig.hpp"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QWidget;

namespace calango::gui {

/// Shared machinery for the dedicated Simulation-menu dialogs (Single-point,
/// Geometry Optimization, Molecular Dynamics). Each concrete dialog is a
/// focused, self-contained window: the base owns the calculator picker,
/// per-backend knobs (DFT cutoff/k-points, MACE, ORCA), the execution-
/// environment selector, and the live, editable ASE-script preview; each
/// subclass adds only the controls that belong to its task and fills the
/// task-specific config fields. Like CalculatorDialog, the script is the
/// source of truth: the first manual edit pauses form→script regeneration
/// until "Regenerate" is pressed.
class SimulationDialogBase : public QDialog {
    Q_OBJECT

public:
    core::CalculatorConfig config() const;

    /// Current script text — the user's edited version if they typed in the
    /// editor, otherwise the generated one.
    QString script() const;

    /// Interpreter that runs the job (selected environment, else embedded).
    QString pythonExecutable() const;

protected:
    explicit SimulationDialogBase(QWidget* parent = nullptr);

    /// Builds the full UI. A concrete dialog MUST call this from its own
    /// constructor (not the base constructor) so the virtual hooks below
    /// dispatch to the derived overrides.
    void buildUi();

    // ---- Hooks implemented by each concrete simulation dialog ------------

    virtual QString titleText() const = 0;
    virtual QString introText() const = 0;
    virtual QString taskGroupTitle() const = 0;
    virtual core::TaskKind taskKind() const = 0;

    /// Populate the task-settings group. Wire each control to refreshPreview()
    /// via watch().
    virtual void buildTaskControls(QFormLayout* form) = 0;

    /// Fill the task-specific fields (task is already set to taskKind()).
    virtual void applyTaskConfig(core::CalculatorConfig& c) const = 0;

    /// Optional: enable/disable task controls for the current selection
    /// (e.g. MD hides the barostat outside NPT). Default does nothing.
    virtual void updateTaskEnabled(const core::CalculatorConfig& c);

    /// Connect a task control's change signal to refreshPreview(). Handles
    /// QComboBox / QSpinBox / QDoubleSpinBox / QLineEdit.
    void watch(QWidget* widget);

protected Q_SLOTS:
    void refreshPreview();

private Q_SLOTS:
    void regenerateScript();
    void browseEnvironmentDir();
    void browseEnvironmentPython();
    void browseMaceModel();
    void saveScript();

private:
    void buildCalculatorControls(QFormLayout* form);
    void updateCalculatorEnabled(const core::CalculatorConfig& c);

    // Calculator + backend knobs (owned by the base).
    QComboBox* calculatorCombo_ = nullptr;
    QDoubleSpinBox* cutoffSpin_ = nullptr;
    QSpinBox* kptSpins_[3] = {nullptr, nullptr, nullptr};
    QComboBox* maceModelCombo_ = nullptr;
    QComboBox* maceSizeCombo_ = nullptr;
    QLineEdit* maceModelPathEdit_ = nullptr;
    QPushButton* maceBrowseButton_ = nullptr;
    QComboBox* maceDeviceCombo_ = nullptr;
    QComboBox* orcaMethodCombo_ = nullptr;
    QComboBox* orcaBasisCombo_ = nullptr;
    QSpinBox* chargeSpin_ = nullptr;
    QSpinBox* multiplicitySpin_ = nullptr;
    QComboBox* orcaSolvationCombo_ = nullptr;
    QLineEdit* orcaSolventEdit_ = nullptr;

    // Environment + live preview.
    QLineEdit* envPathEdit_ = nullptr;
    QLabel* envStatusLabel_ = nullptr;
    QLabel* editedNotice_ = nullptr;
    QPlainTextEdit* preview_ = nullptr;
    bool updatingPreview_ = false;
    bool manuallyEdited_ = false;
};

/// Simulation → "Single-point Calculation": static energy / forces plus the
/// electronic-convergence knobs (SCF iterations, energy tolerance) alongside
/// the shared k-point mesh and plane-wave cutoff.
class SinglePointDialog : public SimulationDialogBase {
    Q_OBJECT

public:
    explicit SinglePointDialog(QWidget* parent = nullptr);

protected:
    QString titleText() const override;
    QString introText() const override;
    QString taskGroupTitle() const override;
    core::TaskKind taskKind() const override { return core::TaskKind::SinglePoint; }
    void buildTaskControls(QFormLayout* form) override;
    void applyTaskConfig(core::CalculatorConfig& c) const override;

private:
    QSpinBox* scfStepsSpin_ = nullptr;
    QDoubleSpinBox* scfTolSpin_ = nullptr;
};

/// Simulation → "Geometry Optimization": structural relaxation — optimizer
/// algorithm (BFGS / LBFGS / FIRE / …), force convergence, and step cap.
class GeometryOptimizationDialog : public SimulationDialogBase {
    Q_OBJECT

public:
    explicit GeometryOptimizationDialog(QWidget* parent = nullptr);

protected:
    QString titleText() const override;
    QString introText() const override;
    QString taskGroupTitle() const override;
    core::TaskKind taskKind() const override
    {
        return core::TaskKind::GeometryOptimization;
    }
    void buildTaskControls(QFormLayout* form) override;
    void applyTaskConfig(core::CalculatorConfig& c) const override;

private:
    QComboBox* optimizerCombo_ = nullptr;
    QDoubleSpinBox* fmaxSpin_ = nullptr;
    QSpinBox* maxStepsSpin_ = nullptr;
};

/// Simulation → "Molecular Dynamics": ensemble (NVE / NVT / NPT + thermostat),
/// timestep, step count, temperature, thermostat/barostat coupling and
/// external pressure.
class MolecularDynamicsDialog : public SimulationDialogBase {
    Q_OBJECT

public:
    explicit MolecularDynamicsDialog(QWidget* parent = nullptr);

protected:
    QString titleText() const override;
    QString introText() const override;
    QString taskGroupTitle() const override;
    core::TaskKind taskKind() const override
    {
        return core::TaskKind::MolecularDynamics;
    }
    void buildTaskControls(QFormLayout* form) override;
    void applyTaskConfig(core::CalculatorConfig& c) const override;
    void updateTaskEnabled(const core::CalculatorConfig& c) override;

private:
    QComboBox* ensembleCombo_ = nullptr;
    QDoubleSpinBox* temperatureSpin_ = nullptr;
    QDoubleSpinBox* timestepSpin_ = nullptr;
    QSpinBox* mdStepsSpin_ = nullptr;
    QDoubleSpinBox* frictionSpin_ = nullptr;
    QDoubleSpinBox* tautSpin_ = nullptr;
    QDoubleSpinBox* taupSpin_ = nullptr;
    QDoubleSpinBox* pressureSpin_ = nullptr;
};

} // namespace calango::gui
