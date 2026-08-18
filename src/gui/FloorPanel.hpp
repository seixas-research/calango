#pragma once

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSlider;

namespace calango::gui {

class ViewportWidget;

/// "Floor" tab: the ground plane's height, colour, material, opacity and
/// orientation — a large plane just under the structure, so an isolated
/// molecule reads as an object resting in a space rather than one floating
/// in a void. It is a shadow RECEIVER: the atoms and bonds cast onto it, and
/// it casts nothing itself. Display only — never part of the structure, not
/// picked by clicks, never in an exported POSCAR/CIF/XYZ.
///
/// Moved here, into the "Spatial References" dock, from Visual Effects
/// (where it sat between Shadow and Fog): the plane is drawn IN the scene as
/// a reference surface, the role the cell wireframe and the axes triad also
/// play, not a post-process image filter like Blur or SSAO. Nothing about
/// how it is stored moved with it — `Style::floorOffset` and the rest still
/// travel under the same fixed keys in the project's `viewport` object (see
/// VisualEffectsPanel's own note on why a tab can be relocated without
/// invalidating a saved layout or project: the dock's saved geometry is
/// matched by objectName, never by which tab holds what).
class FloorPanel : public QWidget {
    Q_OBJECT

public:
    explicit FloorPanel(ViewportWidget* viewport, QWidget* parent = nullptr);

Q_SIGNALS:
    /// Re-read every control from the viewport's current style, quietly.
    /// Emitted from outside when something else moved those values — which
    /// today means one thing, a project restore, which sets them all at
    /// once. See VisualEffectsPanel::syncFloorFromViewport(), which this
    /// replaces.
    void syncFromViewport();

private:
    /// Re-derive the height slider's own range from the current structure's
    /// scale (StructureRenderer::floorBase()'s `reach`) rather than a fixed
    /// absolute span — a single atom and a slab call for different notions
    /// of "reasonable" height range. The spin box beside it is unaffected:
    /// it keeps its own fixed, wide range regardless, so a value further out
    /// than the slider currently reaches is still just a matter of typing it.
    void refreshHeightRange();

    ViewportWidget* viewport_;
    QGroupBox* floorGroup_ = nullptr;
    QSlider* heightSlider_ = nullptr;
    QDoubleSpinBox* heightSpin_ = nullptr;
};

} // namespace calango::gui
