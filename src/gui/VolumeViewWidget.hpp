#pragma once

#include "core/MarchingCubes.hpp"
#include "core/VolumetricData.hpp"
#include "render/Camera.hpp"
#include "render/ColorMap.hpp"

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>

#include <vector>

namespace calango::gui {

/// 3D canvas of the Volumetric Data viewer: isosurface (optionally
/// colored by a second field — EPM), a color-mapped slice plane, and the
/// grid-box wireframe. Orbit/pan/zoom mirror the main viewport.
class VolumeViewWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit VolumeViewWidget(QWidget* parent = nullptr);
    ~VolumeViewWidget() override;

    /// Replace the isosurface geometry (already extracted; colorRange is
    /// the min/max used for EPM vertex colors, ignored without values).
    void setIsoMesh(const core::IsoMesh& mesh, render::ColorGradient gradient,
                    double colorMin, double colorMax, bool colored);
    void clearIsoMesh();

    /// Replace the slice-plane geometry: a grid of quads with per-vertex
    /// colors already baked (positions + rgb triplets).
    void setSlice(const std::vector<float>& interleavedPosColor);
    void clearSlice();

    /// The wireframe box of the volumetric grid, for orientation.
    void setBox(const core::VolumetricData& field);

    void frameBox();

    // --- Generic geometry -------------------------------------------------
    // The canvas itself is not volumetric: it is an orbit camera over a lit
    // triangle soup and an unlit line soup. These expose that directly, so a
    // second 3D plot (the 2D band surfaces) reuses the camera, the shader and
    // the interaction rather than growing a near-identical widget beside it.
    // Layout matches the internal one: pos(3) + normal(3) + color(3).

    /// Replace the shaded triangle geometry.
    void setMesh(std::vector<float> interleavedPosNormalColor);
    /// Replace the unlit line geometry (axes, boundaries, guide lines).
    void setLines(std::vector<float> interleavedPosNormalColor);
    /// Frame an arbitrary bounding sphere, for content with no grid box.
    void setBounds(const QVector3D& center, float radius);
    /// Opacity of the shaded triangles. Isosurfaces default to slightly
    /// translucent so a slice behind one stays readable; stacked band surfaces
    /// want to be opaque, or every sheet shows through every other.
    void setMeshOpacity(float alpha);

    /// A short caption pinned to a point in the scene.
    struct Label {
        QVector3D position; ///< world space
        QString text;
        QColor color{230, 230, 235};
    };
    /// Replace the text overlay. Drawn with QPainter after the GL pass and
    /// projected through the live camera, so captions track the geometry as it
    /// orbits without needing a glyph atlas or a text shader.
    void setLabels(std::vector<Label> labels);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    struct Buffer {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        int vertexCount = 0;
        bool dirty = false;
        std::vector<float> staging; ///< interleaved pos(3) normal(3) color(3)
    };
    void upload(Buffer& buffer);
    void draw(Buffer& buffer, GLenum mode, bool unlit, float alpha);
    /// Project labels_ through the live camera and paint them over the scene.
    void drawLabels(class QPainter& painter);

    render::OrbitCamera camera_;
    QOpenGLShaderProgram program_;
    Buffer isoBuffer_;
    Buffer sliceBuffer_;
    Buffer boxBuffer_;
    QPointF lastMousePos_;
    QVector3D boxCenter_;
    float boxRadius_ = 10.0f;
    /// Slightly translucent by default so a colour slice behind an isosurface
    /// stays readable; setMeshOpacity() overrides it.
    float meshAlpha_ = 0.88f;
    std::vector<Label> labels_;
    bool initialized_ = false;
};

} // namespace calango::gui
