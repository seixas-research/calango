#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

namespace calango::gui {

class ViewportWidget;

/// "Representation" dock panel: representation mode, atom color mapping
/// (element / CN / GCN / custom scalar with gradient + legend range),
/// global atom-radius and bond-width scales, gradient bond shading,
/// per-element settings, and the viewport background color.
class RepresentationPanel : public QWidget {
    Q_OBJECT

public:
    explicit RepresentationPanel(ViewportWidget* viewport, QWidget* parent = nullptr);

Q_SIGNALS:
    /// "Bond Editor…": the host opens the dialog against the current mutable
    /// document. The panel only observes the viewport, so it cannot own an
    /// editor that mutates the structure (and pushes undo).
    void bondEditorRequested();

private Q_SLOTS:
    void applyColorMode();
    void refreshPropertyList();
    void syncColoringFromViewport();

private:
    /// Push the Min/Max fields to the viewport as the scalar color window
    /// (or restore auto-scaling when "Auto-scale to data" is ticked).
    void applyColorRange();
    /// Style field holding the arrow color of the active vector overlay, or
    /// null when the overlay is None.
    QColor* vectorOverlayColor();
    /// Refresh the swatch + enablement after the overlay selection changes.
    void syncVectorColorButton();

    ViewportWidget* viewport_;

    QComboBox* modeCombo_;
    QComboBox* colorModeCombo_;
    QComboBox* gradientCombo_;
    QCheckBox* invertGradientCheck_;
    QComboBox* propertyCombo_;
    /// Editable color-scale bounds + the auto-scale toggle they override.
    QDoubleSpinBox* rangeMinSpin_;
    QDoubleSpinBox* rangeMaxSpin_;
    QCheckBox* autoRangeCheck_;
    /// True while the bounds fields are being repopulated from the data, so
    /// their valueChanged does not read as a user override.
    bool syncingRange_ = false;
    QSlider* atomScaleSlider_;
    QDoubleSpinBox* atomScaleSpin_;
    QSlider* bondWidthSlider_;
    QDoubleSpinBox* bondWidthSpin_;
    QCheckBox* gradientBondsCheck_;
    QComboBox* vectorOverlayCombo_;
    QComboBox* surfaceFinishCombo_;
    QPushButton* vectorColorButton_;
    QSlider* vectorScaleSlider_;
    QDoubleSpinBox* vectorScaleSpin_;
};

} // namespace calango::gui
