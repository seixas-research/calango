#pragma once

#include <QWidget>

namespace calango::gui {

class ViewportWidget;

/// Zone-9 "Visual Effects" dock: a tabbed panel consolidating all scene
/// rendering effects. Tab 1 embeds the existing LightingPanel (light sources
/// + ambient/diffuse/specular colors); Tab 2 hosts distance-fog controls;
/// Tab 3 hosts the screen-space post-processing controls — depth of field
/// and ambient occlusion, which share one offscreen G-buffer pass.
/// Replaces the old standalone Visual Effects dialog.
class VisualEffectsPanel : public QWidget {
    Q_OBJECT

public:
    explicit VisualEffectsPanel(ViewportWidget* viewport,
                                QWidget* parent = nullptr);

private:
    QWidget* buildFogTab();
    /// "Depth & Occlusion": depth-of-field + SSAO controls.
    QWidget* buildDepthBlurTab();
    QWidget* buildShadowTab();

    ViewportWidget* viewport_;
};

} // namespace calango::gui
