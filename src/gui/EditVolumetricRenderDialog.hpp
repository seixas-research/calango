#pragma once

#include "gui/VolumetricStyle.hpp"

#include <QStringList>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSlider;
class QStackedWidget;

namespace calango::gui {

/// "Edit Volumetric Render" — the styling window opened from the Volumetric
/// Data panel's toolbar. A central "Render Mode" dropdown selects one of three
/// configuration panels (shown via a QStackedWidget); the active mode drives
/// what the viewport renders:
///   * Isosurfaces  — isovalue, opacity, specular, positive/negative phase
///     colors, and a grid-interpolation scheme (no colormap: isosurfaces are
///     uniform single-color / phase fills).
///   * Color Slice  — plane orientation, offset, colormap, transparency.
///   * Potential Map — a base isosurface colored by a secondary scalar field
///     (colormap + min/max ramp bounds).
///
/// Edits apply live: styleChanged(style, mode) fires as controls move and when
/// the mode changes, so the viewport tracks the dialog immediately.
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

    /// Populate the Potential-Map base / secondary field selectors from the
    /// panel's dataset labels; `currentIndex` is the tree's current selection
    /// (offered as the default base). No signal emitted.
    void setDatasets(const QStringList& labels, int currentIndex);

Q_SIGNALS:
    void styleChanged(const VolumetricStyle& style, VolumetricRenderMode mode);

private:
    QWidget* buildIsosurfacePage();
    QWidget* buildColorSlicePage();
    QWidget* buildPotentialPage();
    void emitChange();
    double isovalueFromSlider() const;
    void syncIsoSlider();
    void updateColorButton(QPushButton* button, const QColor& color);

    VolumetricStyle style_;
    double fieldMin_ = 0.0;
    double fieldMax_ = 1.0;
    bool updating_ = false;

    QComboBox* modeCombo_ = nullptr;
    QStackedWidget* stack_ = nullptr;

    // Isosurfaces
    QSlider* isoSlider_ = nullptr;
    QDoubleSpinBox* isoSpin_ = nullptr;
    QDoubleSpinBox* isoOpacitySpin_ = nullptr;
    QDoubleSpinBox* specularSpin_ = nullptr;
    QPushButton* posColorButton_ = nullptr;
    QPushButton* negColorButton_ = nullptr;
    QComboBox* isoInterpCombo_ = nullptr;

    // Color slice
    QComboBox* planeCombo_ = nullptr;
    QSlider* sliceOffsetSlider_ = nullptr;
    QComboBox* sliceGradientCombo_ = nullptr;
    QDoubleSpinBox* sliceOpacitySpin_ = nullptr;

    // Potential map
    QComboBox* potentialBaseCombo_ = nullptr;
    QComboBox* potentialSecondaryCombo_ = nullptr;
    QComboBox* potentialGradientCombo_ = nullptr;
    QCheckBox* potentialBoundsCheck_ = nullptr;
    QDoubleSpinBox* potentialMinSpin_ = nullptr;
    QDoubleSpinBox* potentialMaxSpin_ = nullptr;
};

} // namespace calango::gui
