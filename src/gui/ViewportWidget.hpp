#pragma once

#include "core/Coordination.hpp"
#include "render/Camera.hpp"
#include "render/StructureRenderer.hpp"

#include <QColor>
#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>

#include <array>
#include <memory>
#include <set>
#include <utility>

class QPainter;

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

    std::shared_ptr<const core::Structure> structure() const { return structure_; }

    void setShowCell(bool show);
    void setRepresentation(render::RepresentationMode mode);

    // -- Scalar color mapping ----------------------------------------------

    render::ColorMode colorMode() const { return renderer_.style().colorMode; }
    /// Switch the atom coloring mode. `customField` names the structure
    /// scalar field to map in CustomScalar mode (ignored otherwise).
    /// CN/GCN scalars are (re)computed here and after every structure
    /// replacement — trajectory playback re-colors frame by frame.
    void setColorMode(render::ColorMode mode, const QString& customField = {});
    void setColorGradient(render::ColorGradient gradient);
    QString customScalarField() const { return customField_; }

    /// Neighbor-cutoff settings the CN/GCN color modes use.
    const core::CoordinationOptions& coordinationOptions() const {
        return coordinationOptions_;
    }
    void setCoordinationOptions(const core::CoordinationOptions& options);

    /// Min/max of the active scalar mapping (legend range); `valid` is
    /// false in Element mode or when no scalars are available.
    struct ScalarRange {
        bool valid = false;
        float min = 0.0f;
        float max = 1.0f;
    };
    ScalarRange scalarRange() const { return scalarRange_; }

    QColor backgroundColor() const { return backgroundColor_; }
    void setBackgroundColor(const QColor& color);

public Q_SLOTS:
    /// Perspective (default) vs. orthographic projection; the transition
    /// preserves apparent scale (see OrbitCamera::projection).
    void setOrthographic(bool orthographic);

    /// Corner coordinate-triad overlay.
    void setShowAxes(bool show);
    /// false = Cartesian X/Y/Z, true = lattice vectors a1/a2/a3
    /// (falls back to Cartesian when the structure has no cell).
    void setAxesLatticeMode(bool lattice);
    /// On-screen edge length of the axes-triad box (logical pixels).
    void setAxesSize(int px);
    int axesSize() const { return axesSizePx_; }

    /// Live style access for UI panels. Call styleChanged() afterwards:
    /// geometry-affecting edits (scales, colors, mode) rebuild the GPU
    /// buffers; light-only edits just repaint.
    render::StructureRenderer::Style& style() { return renderer_.style(); }
    std::vector<render::Light>& lights() { return renderer_.lights(); }
    void styleChanged(bool rebuildGeometry);

    render::OrbitCamera& camera() { return camera_; }

    /// Off-screen high-resolution capture of the current scene into a
    /// QImage (used by the image/GIF export engine). `background` with
    /// alpha 0 produces a transparent image; `extraYawDeg` rotates the
    /// camera around the target (turntable animation frames).
    QImage renderToImage(int width, int height, const QColor& background,
                         float extraYawDeg = 0.0f);

public Q_SLOTS:
    void frameStructure();

    /// Align the view with a Cartesian plane (camera looking along the
    /// remaining axis): 0 = XY (along z), 1 = XZ (along y), 2 = YZ (along x).
    void alignWithXY() { alignToPlane(0); }
    void alignWithXZ() { alignToPlane(1); }
    void alignWithYZ() { alignToPlane(2); }
    void alignToPlane(int plane);

Q_SIGNALS:
    void selectionChanged(int count);
    /// A different Structure is now observed (its scalar fields may differ).
    void structureReplaced();
    /// The scalar color mapping was recomputed (mode, range or data changed).
    void colorMappingChanged();

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

    /// Re-upload instance buffers if dirty (requires a current context).
    void ensureUploaded();

    /// Recompute the per-atom scalars for the active color mode and hand
    /// them to the renderer (CN/GCN analysis or field lookup).
    void updateColorScalars();

    /// Axis directions + labels for the triad (Cartesian or lattice).
    std::array<std::pair<QVector3D, QString>, 3> axesVectors() const;
    /// Corner triad, drawn entirely with QPainter: core-profile GL clamps
    /// glLineWidth to 1 px, so painter strokes are the portable way to get
    /// thick, high-DPI-crisp axis lines.
    void drawAxesOverlay(QPainter& painter);

    render::OrbitCamera camera_;
    render::StructureRenderer renderer_;
    std::shared_ptr<const core::Structure> structure_;
    std::set<int> selection_;
    QString customField_;
    core::CoordinationOptions coordinationOptions_;
    ScalarRange scalarRange_;
    QColor backgroundColor_{26, 28, 33};
    bool structureDirty_ = false;
    bool showAxes_ = true;
    bool axesLatticeMode_ = false;
    int axesSizePx_ = 92;
    QPointF lastMousePos_;
    QPointF pressPos_;
};

} // namespace calango::gui
