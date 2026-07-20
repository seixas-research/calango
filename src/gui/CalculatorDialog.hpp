#pragma once

#include "core/CalculatorConfig.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

namespace calango::gui {

/// "ASE input generator": maps a form onto core::CalculatorConfig and
/// previews the generated Python script live. On accept the caller takes
/// script() and hands it to the job runner; Save Script… exports it for
/// editing or cluster submission.
class CalculatorDialog : public QDialog {
    Q_OBJECT

public:
    explicit CalculatorDialog(QWidget* parent = nullptr);

    core::CalculatorConfig config() const;
    QString script() const;

private Q_SLOTS:
    void refreshPreview();
    void saveScript();
    void browseMaceModel();

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
    QPlainTextEdit* preview_;
};

} // namespace calango::gui
