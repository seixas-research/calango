#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

namespace calango::gui {

class ViewportWidget;

/// "Custom Gradient Coloring" — the scalar colour mapping: which per-atom
/// property is mapped, through which gradient, over which value range.
///
/// It lives in a dialog rather than in the Representation panel because none of
/// it does anything until "Color by" leaves Element, so as permanent rows it
/// was five inert controls in the app's tallest dock.
///
/// The Min/Max bounds matter more than they look. Auto-scaling spans the whole
/// trajectory — every frame, not the displayed one — because a ramp
/// renormalized per frame flickers through playback and means a different value
/// at every step. Typing bounds pins the scale further, so one colour means one
/// value across separate structures and exported images too.
///
/// Modeless and live: edits apply to the viewport as they are made.
class CustomGradientColoringDialog : public QDialog {
    Q_OBJECT

public:
    explicit CustomGradientColoringDialog(ViewportWidget* viewport,
                                          QWidget* parent = nullptr);

private Q_SLOTS:
    /// Repopulate the property list after a structure change.
    void refreshPropertyList();
    /// Mirror the viewport's mapping state without re-triggering it.
    void syncFromViewport();

private:
    /// Push the Min/Max fields to the viewport as the scalar colour window
    /// (or restore auto-scaling when "Auto-scale to data" is ticked).
    void applyColorRange();
    /// Switch the viewport to the selected property.
    void applyProperty();

    ViewportWidget* viewport_;
    QComboBox* gradientCombo_ = nullptr;
    QCheckBox* invertGradientCheck_ = nullptr;
    QComboBox* propertyCombo_ = nullptr;
    QDoubleSpinBox* rangeMinSpin_ = nullptr;
    QDoubleSpinBox* rangeMaxSpin_ = nullptr;
    QCheckBox* autoRangeCheck_ = nullptr;
    /// True while the bounds fields are being repopulated from the data, so
    /// their valueChanged does not read as a user override.
    bool syncingRange_ = false;
    /// True when the auto-scaled bounds came from the whole trajectory rather
    /// than from the displayed frame. Those bounds are pushed to the renderer
    /// as an explicit window — otherwise it would re-normalize per frame and
    /// the trajectory-wide scale would never actually apply.
    bool trajectoryScaled_ = false;
};

} // namespace calango::gui
