#pragma once

#include "gui/VolumetricStyle.hpp"

#include <QStringList>

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace calango::gui {

class IsovalueHistogramWidget;

/// "Edit Volumetric Render" — the styling window opened from the Volumetric
/// Data panel's toolbar. A central "Render Mode" dropdown selects one of two
/// configuration panels (shown via a QStackedWidget); the active mode drives
/// what the viewport renders:
///   * Isosurfaces  — isovalue, opacity, specular, positive/negative phase
///     colors, a grid-interpolation scheme, and an optional "Potential Map
///     Color" group that paints the surface with a second scalar field
///     (colormap, invert, explicit range).
///   * Color Slice  — plane orientation as Miller indices (h k l), offset
///     along the plane normal, how many unit cells it spans, colormap
///     (optionally inverted), explicit range, transparency.
///
/// "Potential Map" was a third mode until it stopped being one: it is the same
/// isosurface, extracted the same way at the same isovalue, differing only in
/// what colours it — so it duplicated every isosurface control and made the
/// user re-select a base field they had already chosen.
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

    /// Bin a newly selected field's own values into the isovalue histogram
    /// and cache them (no signal emitted). Call once per loaded volume,
    /// alongside setFieldRange() — never on every isovalue change, which is
    /// exactly the recompute this caches against.
    void setFieldHistogram(const std::vector<double>& values, double fieldMin,
                           double fieldMax);

    /// Adopt `style` / `mode` wholesale, refreshing every control (no signal
    /// emitted — the caller already holds these values).
    ///
    /// Needed because this dialog is modeless and survives a workspace-tab
    /// change: the panel keeps one render style per tab, so when the user
    /// switches tabs the dialog on screen has to start showing the incoming
    /// tab's settings instead of the outgoing tab's.
    void setStyle(const VolumetricStyle& style, VolumetricRenderMode mode);

    /// Populate the Potential-Map base / secondary field selectors from the
    /// panel's dataset labels; `currentIndex` is the tree's current selection
    /// (offered as the default base). No signal emitted.
    void setDatasets(const QStringList& labels, int currentIndex);

    /// Tell the dialog whether the tab it is editing holds a Wannier function.
    /// Periodic continuation applies to those and to nothing else, so this is
    /// what decides whether that control is live.
    void setHasWannier(bool hasWannier);

Q_SIGNALS:
    void styleChanged(const VolumetricStyle& style, VolumetricRenderMode mode);

private:
    QWidget* buildIsosurfacePage();
    QWidget* buildColorSlicePage();
    QWidget* buildDirectVolumePage();
    /// Enable only the controls the selected draw style / shading model reads.
    void syncIsoStyleEnabled();
    void emitChange();
    double isovalueFromSlider() const;
    void syncIsoSlider();
    void updateColorButton(QPushButton* button, const QColor& color);
    /// Refresh every label that reports fieldMin_/fieldMax_ as text.
    void updateRangeLabels();

    VolumetricStyle style_;
    double fieldMin_ = 0.0;
    double fieldMax_ = 1.0;
    bool updating_ = false;

    bool hasWannier_ = false;    ///< see setHasWannier()
    QComboBox* modeCombo_ = nullptr;
    QStackedWidget* stack_ = nullptr;

    // Isosurfaces
    /// The isosurface shader profile, as a checkbox: legacy (baked CPU
    /// shading) vs lit (GPU, scene lights). Application-wide and persisted
    /// through QSettings, unlike every other control here — it lives in this
    /// dialog because it decides whether the Shading row below still reaches
    /// the main viewport.
    QCheckBox* litSurfaceCheck_ = nullptr;
    /// Voxel-count histogram, drawn directly above isoSlider_ and pinned to
    /// its groove — see IsovalueHistogramWidget.
    IsovalueHistogramWidget* isoHistogram_ = nullptr;
    QCheckBox* isoLogScaleCheck_ = nullptr;
    QSlider* isoSlider_ = nullptr;
    QDoubleSpinBox* isoSpin_ = nullptr;
    QComboBox* drawStyleCombo_ = nullptr;  ///< solid / mesh / both / dots
    QDoubleSpinBox* dotSizeSpin_ = nullptr;
    QSpinBox* dotStrideSpin_ = nullptr;
    QDoubleSpinBox* meshShadeSpin_ = nullptr;
    QComboBox* shadingCombo_ = nullptr;    ///< flat / diffuse / glossy
    QDoubleSpinBox* ambientSpin_ = nullptr;
    QSpinBox* smoothingSpin_ = nullptr;
    QDoubleSpinBox* isoOpacitySpin_ = nullptr;
    QDoubleSpinBox* specularSpin_ = nullptr;
    QPushButton* posColorButton_ = nullptr;
    QPushButton* negColorButton_ = nullptr;
    QComboBox* isoInterpCombo_ = nullptr;
    QDoubleSpinBox* continuationSpin_ = nullptr; ///< Wannier periodic halo

    // Color slice
    QSpinBox* millerSpins_[3] = {nullptr, nullptr, nullptr}; ///< h, k, l
    QSlider* sliceOffsetSlider_ = nullptr;
    QComboBox* sliceGradientCombo_ = nullptr;
    QCheckBox* sliceInvertCheck_ = nullptr;
    QCheckBox* sliceBoundsCheck_ = nullptr;
    QDoubleSpinBox* sliceMinSpin_ = nullptr;
    QDoubleSpinBox* sliceMaxSpin_ = nullptr;
    QComboBox* sliceInterpCombo_ = nullptr;
    QDoubleSpinBox* sliceOpacitySpin_ = nullptr;
    QComboBox* sliceExtentCombo_ = nullptr;
    QCheckBox* sliceBorderCheck_ = nullptr;
    /// "Range: [min, max]" — the whole VOLUME's range (see updateRangeLabels()),
    /// which is what "Custom color range" off falls back to.
    QLabel* sliceRangeLabel_ = nullptr;

    // Direct volume rendering
    QSpinBox* volumeStepsSpin_ = nullptr;
    QDoubleSpinBox* volumeDensitySpin_ = nullptr;
    QDoubleSpinBox* volumeThresholdSpin_ = nullptr;
    QCheckBox* volumeLitCheck_ = nullptr;

    // Potential-map colouring (inside the Isosurfaces page)
    QGroupBox* potentialGroup_ = nullptr; ///< checkable: drives potentialColoring
    QComboBox* potentialSecondaryCombo_ = nullptr;
    QCheckBox* potentialInvertCheck_ = nullptr;
    QComboBox* potentialGradientCombo_ = nullptr;
    QCheckBox* potentialBoundsCheck_ = nullptr;
    QDoubleSpinBox* potentialMinSpin_ = nullptr;
    QDoubleSpinBox* potentialMaxSpin_ = nullptr;
};

} // namespace calango::gui
