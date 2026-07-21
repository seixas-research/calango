#pragma once

#include "core/MarchingCubes.hpp"
#include "core/VolumetricData.hpp"

#include <QDialog>

#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QSlider;

namespace calango::gui {

class VolumeViewWidget;

/// Analysis → "Volumetric Data": isosurfaces (marching-cubes family,
/// tetrahedral variant) with a live isovalue control, color-mapped slice
/// planes (axis-aligned or custom normal), and dual-field electrostatic
/// potential maps — Field A shapes the surface, Field B colors it.
/// Reads Gaussian .cube, VASP CHGCAR/LOCPOT/PARCHG/ELFCAR and .xsf
/// grids; exports the isosurface as OBJ and slices as CSV.
class VolumetricDialog : public QDialog {
    Q_OBJECT

public:
    explicit VolumetricDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void loadFieldA();
    void loadFieldB();
    void rebuildIso();
    void rebuildSlice();
    void exportIso();
    void exportSlice();

private:
    void loadField(std::optional<core::VolumetricData>& field, QLabel* label,
                   const QString& role);
    double isovalueFromSlider() const;
    const core::VolumetricData* sliceField() const;

    std::optional<core::VolumetricData> fieldA_;
    std::optional<core::VolumetricData> fieldB_;
    core::IsoMesh isoMesh_; ///< last extracted surface (export)
    std::vector<std::array<double, 4>> sliceSamples_; ///< x,y,z,value (export)

    VolumeViewWidget* view_;
    QLabel* fieldALabel_;
    QLabel* fieldBLabel_;
    QGroupBox* isoGroup_;
    QSlider* isoSlider_;
    QDoubleSpinBox* isoSpin_;
    QCheckBox* epmCheck_;
    QComboBox* isoColormapCombo_;
    QLabel* epmRangeLabel_;
    QGroupBox* sliceGroup_;
    QComboBox* planeCombo_;
    QDoubleSpinBox* normalSpin_[3];
    QSlider* offsetSlider_;
    QComboBox* sliceColormapCombo_;
    QComboBox* sliceFieldCombo_;
};

} // namespace calango::gui
