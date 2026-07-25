#pragma once

#include <QWidget>

namespace calango::gui {

class ViewportWidget;

/// Zone-9 "Visual Effects" dock: a tabbed panel consolidating all scene
/// rendering effects. Tab 1 embeds the existing LightingPanel (light sources
/// + ambient/diffuse/specular colors); Tab 2 hosts distance-fog controls;
/// Tabs 3-5 host Fog, Blur (depth of field) and Occlusion (SSAO). The last
/// two are separate pages despite sharing one offscreen G-buffer pass: they
/// are independent effects, and combining them made the SSAO parameters read
/// as if they tuned the depth-of-field blur.
/// Replaces the old standalone Visual Effects dialog.
class VisualEffectsPanel : public QWidget {
    Q_OBJECT

public:
    explicit VisualEffectsPanel(ViewportWidget* viewport,
                                QWidget* parent = nullptr);

private:
    QWidget* buildFogTab();
    /// "Blur": depth-of-field controls.
    QWidget* buildDepthBlurTab();
    /// "Occlusion": SSAO radius, intensity, kernel samples and noise scale.
    QWidget* buildOcclusionTab();
    QWidget* buildShadowTab();

    ViewportWidget* viewport_;
};

} // namespace calango::gui
