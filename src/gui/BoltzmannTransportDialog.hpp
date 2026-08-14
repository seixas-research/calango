#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/BoltzmannTransport.hpp"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace calango::gui {

class SpectrumPlotWidget;
class WannierModelSource;

/// Boltzmann Transport: electronic and thermoelectric properties from the
/// Wannier-interpolated band structure, in the constant relaxation-time
/// approximation.
///
/// Follows the other electronic-structure viewers: a control column, an
/// interactive plot, tensor-component selection and data export. The physics
/// is core::BoltzmannTransport, which is native — BoltzWann is a reference for
/// scope and formulas, not a dependency.
class BoltzmannTransportDialog : public QDialog {
    Q_OBJECT

public:
    explicit BoltzmannTransportDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void compute();
    void exportData();

private:
    core::BoltzmannTransport::Options readOptions() const;
    /// Which tensor component the plot shows, as a row-major 3x3 index.
    int componentIndex() const;

    WannierModelSource* source_ = nullptr;
    QSpinBox* kmesh_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* relaxationTime_ = nullptr;
    QDoubleSpinBox* latticeKappa_ = nullptr;
    QDoubleSpinBox* smearing_ = nullptr;
    QDoubleSpinBox* muMin_ = nullptr;
    QDoubleSpinBox* muMax_ = nullptr;
    QDoubleSpinBox* temperature_ = nullptr;
    QComboBox* component_ = nullptr;
    QComboBox* quantity_ = nullptr;
    QPushButton* computeButton_ = nullptr;
    QLabel* summary_ = nullptr;
    SpectrumPlotWidget* plot_ = nullptr;

    std::vector<double> mu_;
    std::vector<core::BoltzmannTransport::Point> points_;
};

} // namespace calango::gui
