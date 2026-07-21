#pragma once

#include "core/Structure.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include <memory>

namespace calango::gui {

class LinePlotWidget;

/// Simulated powder X-ray diffraction (Debye scattering equation via
/// ase.utils.xrdebye): wavelength presets (Cu/Co/Mo/Cr Kα or custom),
/// 2θ range, and a supercell repeat that sharpens Bragg peaks for
/// periodic structures. Runs on the GUI thread (embedded Python) with a
/// busy cursor; exports the 2θ–intensity curve and the detected peak
/// list (2θ, intensity, d-spacing) as .csv/.dat.
class XrdDialog : public QDialog {
    Q_OBJECT

public:
    XrdDialog(std::shared_ptr<const core::Structure> structure,
              QWidget* parent = nullptr);

private Q_SLOTS:
    void compute();
    void exportCurve();
    void exportPeaks();

private:
    double wavelength() const;

    std::shared_ptr<const core::Structure> structure_;
    pybridge::AseBridge::XrdResult lastResult_;

    QComboBox* wavelengthCombo_;
    QDoubleSpinBox* wavelengthSpin_;
    QDoubleSpinBox* thetaMinSpin_;
    QDoubleSpinBox* thetaMaxSpin_;
    QSpinBox* pointsSpin_;
    QSpinBox* repeatSpin_;
    QLabel* statusLabel_;
    QPushButton* computeButton_;
    QPushButton* exportCurveButton_;
    QPushButton* exportPeaksButton_;
    LinePlotWidget* plot_;
};

} // namespace calango::gui
