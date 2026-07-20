#pragma once

#include "render/Camera.hpp"
#include "render/StructureRenderer.hpp"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>

#include <memory>
#include <set>

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// The 3D canvas: owns the camera and renderer, translates mouse input
/// into camera motion and atom picking, and observes (never mutates) the
/// Structure model. Selection is view state; editing it emits
/// selectionChanged so the controller can act on it.
///
/// Interaction: left-drag rotates, middle-drag or Shift+left-drag pans,
/// wheel zooms, double-click reframes. A left *click* (no drag) picks the
/// atom under the cursor (Ctrl/Cmd+click toggles; empty click clears).
class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    /// Rebuilds GPU buffers; clears the selection. `frameCamera` recenters
    /// the view (disable during trajectory playback).
    void setStructure(std::shared_ptr<const core::Structure> structure,
                      bool frameCamera = true);

    /// Call after in-place edits of the current structure (keeps camera).
    void refreshStructure();

    const std::set<int>& selection() const { return selection_; }
    void clearSelection();

    void setShowCell(bool show);

public Q_SLOTS:
    void frameStructure();

Q_SIGNALS:
    void selectionChanged(int count);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    /// Ray-cast from a screen position against atom display spheres.
    /// Returns the atom index of the nearest hit, or -1.
    int pickAtom(const QPointF& screenPos) const;

    render::OrbitCamera camera_;
    render::StructureRenderer renderer_;
    std::shared_ptr<const core::Structure> structure_;
    std::set<int> selection_;
    bool structureDirty_ = false;
    QPointF lastMousePos_;
    QPointF pressPos_;
};

} // namespace calango::gui
