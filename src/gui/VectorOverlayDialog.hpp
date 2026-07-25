#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSlider;

namespace calango::gui {

class ViewportWidget;

/// "Edit Vector Overlay Setup" — the per-atom arrow overlay controls, moved out
/// of the Representation panel so Zone 8 keeps a single compact action row.
///
/// One overlay is shown at a time rather than several: the arrows share a
/// single length scale and would overlap illegibly if two properties were drawn
/// at once. Each property remembers its OWN colour, so switching Force →
/// Velocity restores that property's colour instead of carrying one over.
///
/// Modeless and live: edits apply to the viewport as they are made.
class VectorOverlayDialog : public QDialog {
    Q_OBJECT

public:
    explicit VectorOverlayDialog(ViewportWidget* viewport,
                                 QWidget* parent = nullptr);

private:
    /// Style field holding the arrow colour of the active overlay, or null
    /// when the overlay is None.
    QColor* overlayColor();
    /// Refresh the swatch + enablement after the overlay selection changes.
    void syncColorButton();
    /// Grey out overlays the current frame carries no data for.
    void refreshAvailability();

    ViewportWidget* viewport_;
    QComboBox* overlayCombo_ = nullptr;
    QSlider* scaleSlider_ = nullptr;
    QDoubleSpinBox* scaleSpin_ = nullptr;
    QPushButton* colorButton_ = nullptr;
};

} // namespace calango::gui
