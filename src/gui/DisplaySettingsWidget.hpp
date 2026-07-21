#pragma once

#include "render/StructureRenderer.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

namespace calango::gui {

class ViewportWidget;

/// Dockable panel driving the renderer's Style and Lights:
///   - representation mode (ball-and-stick / space-filling / wireframe)
///   - atom color mode (element CPK / CN / GCN / custom scalar field)
///     with gradient selection and a live legend-range readout
///   - global atom-radius and bond-width scale sliders
///   - Atom Color Editor launcher
///   - up to render::kMaxLights directional lights with per-light
///     direction and ambient/diffuse/specular colors
class DisplaySettingsWidget : public QWidget {
    Q_OBJECT

public:
    explicit DisplaySettingsWidget(ViewportWidget* viewport, QWidget* parent = nullptr);

private Q_SLOTS:
    void addLight();
    void removeLight();
    void loadSelectedLight();
    void applyLightEdits();
    void applyColorMode();
    void refreshPropertyList();
    void syncColoringFromViewport();

private:
    void refreshLightList(int selectRow);
    void setButtonColor(QPushButton* button, const QColor& color);
    QColor pickColor(QPushButton* button, const QColor& current, const QString& title);

    ViewportWidget* viewport_;

    QComboBox* modeCombo_;
    QComboBox* colorModeCombo_;
    QComboBox* gradientCombo_;
    QComboBox* propertyCombo_;
    QLabel* rangeLabel_;
    QSlider* atomScaleSlider_;
    QDoubleSpinBox* atomScaleSpin_;
    QSlider* bondWidthSlider_;
    QDoubleSpinBox* bondWidthSpin_;

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
