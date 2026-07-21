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
    void loadSelectedLight();
    void applyLightEdits();

private:
    void refreshLightList(int selectRow);

    ViewportWidget* viewport_;

    QListWidget* lightList_;
    QPushButton* addLightButton_;
    QPushButton* removeLightButton_;
    QDoubleSpinBox* directionSpin_[3];
    QPushButton* ambientButton_;
    QPushButton* diffuseButton_;
    QPushButton* specularButton_;
    bool loadingLight_ = false;
};

} // namespace calango::gui
