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

    render::OrbitCamera camera_;
    QOpenGLShaderProgram program_;
    Buffer isoBuffer_;
    Buffer sliceBuffer_;
    Buffer boxBuffer_;
    QPointF lastMousePos_;
    QVector3D boxCenter_;
    float boxRadius_ = 10.0f;
    bool initialized_ = false;
};

} // namespace calango::gui
