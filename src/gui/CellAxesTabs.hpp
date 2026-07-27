#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSlider;

namespace calango::gui {

class ViewportWidget;

/// The two tabs that formerly made up the standalone "Unit Cell & Axes" dock,
/// plus the per-atom vector overlay controls, gathered into the
/// "Cell, Axes & Vectors" dock.
///
/// What unites them is that all three draw something ONTO the scene that is not
/// the atoms themselves — the cell wireframe, the orientation triad and the
/// per-atom arrows. They are separate tabs because the three overlays are
/// independent: a molecule has no cell, and the triad is a screen-space aid
/// that exists regardless.

/// "Unit cell" tab: wireframe visibility, boundary-atom duplication, colour and
/// line width (widths > 1 render lit tubes, since core-profile drivers clamp
/// GL line width).
class UnitCellPanel : public QWidget {
    Q_OBJECT

public:
    explicit UnitCellPanel(ViewportWidget* viewport, QWidget* parent = nullptr);

private:
    ViewportWidget* viewport_;
};

/// "Axes triad" tab: corner orientation triad visibility, arrowheads, the
/// Cartesian vs. lattice-vector labelling, and its on-screen size.
class AxesTriadPanel : public QWidget {
    Q_OBJECT

public:
    explicit AxesTriadPanel(ViewportWidget* viewport, QWidget* parent = nullptr);

private:
    ViewportWidget* viewport_;
};

/// "Vectors" tab: the per-atom arrow overlay — which vector field is drawn, its
/// length scale, arrow style and colour, and the magnitude filter.
///
/// One overlay is shown at a time rather than several: the arrows share a
/// single length scale and would overlap illegibly if two properties were drawn
/// at once. Each property remembers its OWN colour, so switching Force →
/// Velocity restores that property's colour instead of carrying one over.
class VectorsPanel : public QWidget {
    Q_OBJECT

public:
    explicit VectorsPanel(ViewportWidget* viewport, QWidget* parent = nullptr);

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
    QCheckBox* arrowHeadsCheck_ = nullptr;
};

} // namespace calango::gui
