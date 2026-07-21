#pragma once

#include "core/Structure.hpp"

#include <QDialog>

#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

class LinePlotWidget;
class ViewportWidget;

/// Analysis → "Local Entropy Analysis": the per-atom pair-entropy
/// fingerprint s_S^i (Piaggi & Parrinello 2017, units of k_B). Results
/// are stored as the "local_entropy" scalar field of the structure and
/// immediately mapped onto the atoms via the Custom-property color mode;
/// the dialog shows the value distribution as a histogram.
class LocalEntropyDialog : public QDialog {
    Q_OBJECT

public:
    LocalEntropyDialog(std::shared_ptr<core::Structure> structure,
                       ViewportWidget* viewport, QWidget* parent = nullptr);

Q_SIGNALS:
    /// The "local_entropy" field was (re)computed and stored — the owner
    /// should refresh the views of the structure.
    void fieldStored();

private Q_SLOTS:
    void compute();

private:
    std::shared_ptr<core::Structure> structure_;
    ViewportWidget* viewport_;

    QDoubleSpinBox* cutoffSpin_;
    QDoubleSpinBox* sigmaSpin_;
    QSpinBox* gridSpin_;
    QCheckBox* averageCheck_;
    QLabel* summaryLabel_;
    LinePlotWidget* histogram_;
};

} // namespace calango::gui
