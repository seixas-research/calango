#pragma once

#include <QWidget>

namespace calango::gui {

class ViewportWidget;

/// Zone-9 "Visual Effects" dock: a tabbed panel consolidating all scene
/// rendering effects, in the order Light → Shadow → Floor → Fog → Blur →
/// SSAO. "Light" embeds the existing LightingPanel (light sources +
/// ambient/diffuse/specular colors); the rest are built here. Blur and SSAO
/// are separate pages despite sharing one offscreen G-buffer pass: they are
/// independent effects, and combining them made the SSAO parameters read as
/// if they tuned the depth-of-field blur.
/// Replaces the old standalone Visual Effects dialog.
///
/// Nothing here is keyed by tab: every control reads and writes the live
/// render style through the viewport, the dock's saved geometry is matched by
/// its objectName, and the only Visual-Effects setting that reaches a file at
/// all — the floor — travels under fixed keys in the project's `viewport`
/// object. Tabs can therefore be renamed, reordered or split without
/// invalidating a saved layout or a saved project.
class VisualEffectsPanel : public QWidget {
    Q_OBJECT

public:
    explicit VisualEffectsPanel(ViewportWidget* viewport,
                                QWidget* parent = nullptr);

Q_SIGNALS:
    /// Re-read every floor control from the viewport's current style, quietly.
    ///
    /// Emitted at the panel from outside when something else moved those
    /// values — which now means one thing: a project restore, which sets them
    /// all at once. Without it the controls keep showing the defaults they
    /// were built with while the viewport draws something else, and a panel
    /// that reports the wrong scene is worse than one with no controls at all.
    void syncFloorFromViewport();

private:
    QWidget* buildFogTab();
    /// "Blur": depth-of-field controls.
    QWidget* buildDepthBlurTab();
    /// "SSAO": screen-space ambient-occlusion radius, intensity, kernel
    /// samples and noise scale.
    QWidget* buildOcclusionTab();
    QWidget* buildShadowTab();
    /// "Floor": the ground plane's height offset, colour, material and
    /// opacity. A page of its own rather than part of the Shadow tab it began
    /// on — see the addTab() sequence for why it sits where it does.
    QWidget* buildFloorTab();

    ViewportWidget* viewport_;
};

} // namespace calango::gui
