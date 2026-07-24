#pragma once

#include "gui/VolumetricStyle.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSlider;
class QTabWidget;

namespace calango::gui {

/// "Edit Volumetric Render" — the multi-tab styling window opened from the
/// Volumetric Data panel's toolbar. Its three tabs map to the panel's render
/// modes; the active tab selects what the viewport shows:
///   * Isosurfaces  — isovalue, opacity, specular, positive/negative phase
///     colors, colormap.
///   * Color Slice  — plane orientation, offset, colormap, transparency.
///   * Potential Map — color-ramp bounds and the planar-average profile axis
///     for electrostatic / work-function projections.
///
/// Edits apply live: styleChanged(style, mode) fires as controls move and when
/// the active tab changes, so the viewport tracks the dialog immediately.
class EditVolumetricRenderDialog : public QDialog {
    Q_OBJECT

public:
    EditVolumetricRenderDialog(const VolumetricStyle& style,
                               VolumetricRenderMode mode, double fieldMin,
                               double fieldMax, QWidget* parent = nullptr);

    VolumetricStyle style() const { return style_; }
    VolumetricRenderMode mode() const;

    /// Re-map the isovalue slider / ramp defaults to a newly selected field's
    /// value range (no signal emitted).
    void setFieldRange(double fieldMin, double fieldMax);

Q_SIGNALS:
    void styleChanged(const VolumetricStyle& style, VolumetricRenderMode mode);

private:
    QWidget* buildIsosurfaceTab();
    QWidget* buildColorSliceTab();
    QWidget* buildPotentialTab();
    void emitChange();
    double isovalueFromSlider() const;
    void syncIsoSlider();
    void updateColorButton(QPushButton* button, const QColor& color);

    VolumetricStyle style_;
    double fieldMin_ = 0.0;
    double fieldMax_ = 1.0;
    bool updating_ = false;

    QTabWidget* tabs_ = nullptr;

    // Isosurfaces
    QComboBox* isoGradientCombo_ = nullptr;
    QSlider* isoSlider_ = nullptr;
    QDoubleSpinBox* isoSpin_ = nullptr;
    QDoubleSpinBox* isoOpacitySpin_ = nullptr;
    QDoubleSpinBox* specularSpin_ = nullptr;
    QPushButton* posColorButton_ = nullptr;
    QPushButton* negColorButton_ = nullptr;

    // Color slice
    QComboBox* planeCombo_ = nullptr;
    QSlider* sliceOffsetSlider_ = nullptr;
    QComboBox* sliceGradientCombo_ = nullptr;
    QDoubleSpinBox* sliceOpacitySpin_ = nullptr;

    // Potential map
    QCheckBox* potentialBoundsCheck_ = nullptr;
    QDoubleSpinBox* potentialMinSpin_ = nullptr;
    QDoubleSpinBox* potentialMaxSpin_ = nullptr;
    QComboBox* potentialAxisCombo_ = nullptr;
    QComboBox* potentialGradientCombo_ = nullptr;
};

} // namespace calango::gui
