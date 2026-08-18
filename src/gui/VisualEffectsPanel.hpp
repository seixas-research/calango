#pragma once

#include <QWidget>

namespace calango::gui {

class ViewportWidget;

/// Zone-9 "Visual Effects" dock: a tabbed panel consolidating all scene
/// rendering effects, in the order Light → Shadow → Fog → Blur → SSAO.
/// "Light" embeds the existing LightingPanel (light sources +
/// ambient/diffuse/specular colors); the rest are built here. Blur and SSAO
/// are separate pages despite sharing one offscreen G-buffer pass: they are
/// independent effects, and combining them made the SSAO parameters read as
/// if they tuned the depth-of-field blur.
/// Replaces the old standalone Visual Effects dialog.
///
/// A "Floor" tab lived here too, between Shadow and Fog, until it moved to
/// the "Spatial References" dock (see gui::FloorPanel) — it draws a
/// reference surface IN the scene, the role the cell wireframe and axes
/// triad there also play, rather than a post-process image filter like the
/// tabs left here. Nothing it wrote (`Style::floorOffset` and the rest, the
/// only Visual-Effects settings that ever reached the project file) changed
/// keys in the move.
///
/// Nothing here is keyed by tab: every control reads and writes the live
/// render style through the viewport, and the dock's saved geometry is
/// matched by its objectName — no setting built here reaches a project file
/// at all, so tabs can be renamed, reordered or split without invalidating
/// a saved layout or a saved project.
class VisualEffectsPanel : public QWidget {
    Q_OBJECT

public:
    explicit VisualEffectsPanel(ViewportWidget* viewport,
                                QWidget* parent = nullptr);

private:
    QWidget* buildFogTab();
    /// "Blur": depth-of-field controls.
    QWidget* buildDepthBlurTab();
    /// "SSAO": screen-space ambient-occlusion radius, intensity, kernel
    /// samples and noise scale.
    QWidget* buildOcclusionTab();
    QWidget* buildShadowTab();

    ViewportWidget* viewport_;
};

} // namespace calango::gui
