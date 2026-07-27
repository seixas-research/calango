#pragma once

#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QWidget>

namespace calango::gui {

class ViewportWidget;

/// "Lighting" dock panel: up to render::kMaxLights directional lights with
/// per-light view-space direction and ambient/diffuse/specular colors
/// (color intensity doubles as light intensity).
class LightingPanel : public QWidget {
    Q_OBJECT

public:
    explicit LightingPanel(ViewportWidget* viewport, QWidget* parent = nullptr);

private Q_SLOTS:
    void addLight();
    void removeLight();
    /// Replace the light set with one saved earlier (JSON). A file with no
    /// lights in it leaves the scene alone rather than rendering it black.
    void loadPresets();
    /// Write the current lights to JSON — direction plus the three colours,
    /// whose intensity lives in the same channels.
    void savePresets();
    /// Restore render::StructureRenderer::defaultLights(), the set a fresh
    /// viewport starts with.
    void resetLights();
    void loadSelectedLight();
    void applyLightEdits();

private:
    void refreshLightList(int selectRow);

    ViewportWidget* viewport_;

    QListWidget* lightList_;
    QPushButton* addLightButton_;
    QPushButton* removeLightButton_;
    /// Whole-light-set actions, icon-only (see the constructor).
    QPushButton* loadPresetButton_ = nullptr;
    QPushButton* savePresetButton_ = nullptr;
    QPushButton* resetLightsButton_ = nullptr;
    QDoubleSpinBox* directionSpin_[3];
    QPushButton* ambientButton_;
    QPushButton* diffuseButton_;
    QPushButton* specularButton_;
    bool loadingLight_ = false;
};

} // namespace calango::gui
