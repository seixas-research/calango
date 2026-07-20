#pragma once

#include "render/Camera.hpp"
#include "render/StructureRenderer.hpp"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>

#include <memory>

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// The 3D canvas: owns the camera and renderer, translates mouse input
/// into camera motion, and observes (never mutates) the Structure model.
///
/// Interaction: left-drag rotates, middle-drag or Shift+left-drag pans,
/// wheel zooms, double-click reframes the structure.
class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    /// Rebuilds GPU buffers and (on first load) frames the camera.
    void setStructure(std::shared_ptr<const core::Structure> structure);

    /// Call after in-place edits of the current structure.
    void refreshStructure();

    void frameStructure();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void uploadStructure();

    render::OrbitCamera camera_;
    render::StructureRenderer renderer_;
    std::shared_ptr<const core::Structure> structure_;
    bool structureDirty_ = false;
    QPointF lastMousePos_;
};

} // namespace calango::gui
