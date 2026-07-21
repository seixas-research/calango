#pragma once

#include "core/CalculatorConfig.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

namespace calango::gui {

/// "ASE input generator": configuration forms on the left, the live
/// generated Python/ASE script on the right. The script pane is a real
/// editor (syntax-highlighted, manually editable): the first manual edit
/// pauses form→script regeneration until "Regenerate" is pressed, so user
/// tweaks are never silently overwritten. Also hosts the execution-
/// environment selector (conda env / interpreter) used by the job runner.
class CalculatorDialog : public QDialog {
    Q_OBJECT

public:
    explicit CalculatorDialog(QWidget* parent = nullptr);

    core::CalculatorConfig config() const;

    /// Current script text — the user's edited version if they typed in
    /// the editor, otherwise the generated one.
    QString script() const;

    /// Interpreter that will run the job: the selected conda environment's
    /// python, or the embedded interpreter when no environment is chosen.
    QString pythonExecutable() const;

    /// Accepts a python executable path, a conda environment root, or its
    /// bin/ directory; returns the interpreter path or empty if invalid.
    /// Shared with the other job-launching dialogs (Phonon Builder).
    static QString resolveEnvironmentPython(const QString& input);

private Q_SLOTS:
    void refreshPreview();
    void regenerateScript();
    void saveScript();
    void browseMaceModel();
    void browseEnvironmentDir();
    void browseEnvironmentPython();

private:
    QComboBox* calculatorCombo_;
    QComboBox* taskCombo_;
    QComboBox* ensembleCombo_;
    QDoubleSpinBox* fmaxSpin_;
    QSpinBox* maxStepsSpin_;
    QDoubleSpinBox* temperatureSpin_;
    QDoubleSpinBox* timestepSpin_;
    QSpinBox* mdStepsSpin_;
    QDoubleSpinBox* cutoffSpin_;
    QSpinBox* kptSpins_[3];
    QComboBox* maceModelCombo_;
    QComboBox* maceSizeCombo_;
    QLineEdit* maceModelPathEdit_;
    QPushButton* maceBrowseButton_;
    QComboBox* maceDeviceCombo_;
    QLineEdit* envPathEdit_;
    QLabel* envStatusLabel_;
    QLabel* editedNotice_;
    QPlainTextEdit* preview_;
    bool updatingPreview_ = false;
    bool manuallyEdited_ = false;
};

} // namespace calango::gui
