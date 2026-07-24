#pragma once

#include "core/Vacf.hpp"
#include "core/Vec3.hpp"

#include <QDialog>

#include <vector>

class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace calango::gui {

class LinePlotWidget;

/// Analysis → "Velocity Autocorrelation Function (VACF)…": interactive VACF
/// analytics for an MD trajectory. Plots the normalized C_v(t) and the
/// vibrational DOS (FFT of C_v), and reports the Green-Kubo self-diffusion
/// coefficient D and the momentum relaxation time τ, with CSV/image export.
class VacfDialog : public QDialog {
    Q_OBJECT

public:
    /// `velocities[frame][atom]` in Å/fs; `defaultDtFs` seeds the timestep box.
    VacfDialog(std::vector<std::vector<core::Vec3>> velocities,
               double defaultDtFs, QWidget* parent = nullptr);

private Q_SLOTS:
    void recompute();
    void exportCsv();
    void exportImage();

private:
    std::vector<std::vector<core::Vec3>> velocities_;
    core::VacfResult result_;

    QDoubleSpinBox* dtSpin_ = nullptr;
    QSpinBox* maxLagSpin_ = nullptr;
    QSpinBox* startSpin_ = nullptr; ///< first trajectory frame (inclusive)
    QSpinBox* endSpin_ = nullptr;   ///< last trajectory frame (inclusive)
    QSpinBox* stepSpin_ = nullptr;  ///< stride between used frames
    LinePlotWidget* cvPlot_ = nullptr;
    LinePlotWidget* vdosPlot_ = nullptr;
    QLabel* dValueLabel_ = nullptr;   ///< prominent D callout
    QLabel* tauValueLabel_ = nullptr; ///< prominent τ callout
    QLabel* noteLabel_ = nullptr;     ///< units / method note
};

} // namespace calango::gui
