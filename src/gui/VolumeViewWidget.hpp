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

    /// The isosurface's material: Flat/Diffuse/Glossy shading — matching
    /// gui::IsoShading's numbering, so a caller can pass
    /// static_cast<int>(style.shading) directly — plus the ambient floor and
    /// the Glossy specular strength. Defaults reproduce the shader's
    /// original hardcoded formula exactly (Glossy, 0.28 ambient, 0.35
    /// specular), so a caller that never calls this sees no change.
    void setIsoMaterial(int shadingMode, float ambient, float specular);

    /// Replace the slice-plane geometry: a grid of quads with per-vertex
    /// colors already baked (positions + rgb triplets).
    void setSlice(const std::vector<float>& interleavedPosColor);
    void clearSlice();

    /// The wireframe box of the volumetric grid, for orientation.
    void setBox(const core::VolumetricData& field);

    void frameBox();

    // --- View orientation ---------------------------------------------------
    // Exposed so a host dialog can offer explicit rotation controls beside the
    // canvas. The camera is an arcball, so its state is a quaternion; these
    // take the Euler triple because that is what a numeric control can show
    // and what a named preset ("view down z") is naturally written as.
    void setViewOrientation(float yawDeg, float pitchDeg, float rollDeg = 0.0f);
    /// Turn the view by `degrees` about a CAMERA-space axis (0 = right,
    /// 1 = up, 2 = view). Camera-space rather than world, so a nudge button
    /// does what its arrow suggests whatever the current orientation is.
    void nudgeView(int axis, float degrees);
    void setViewRoll(float degrees);
    float viewYaw() const;
    float viewPitch() const;
    float viewRoll() const;
    /// Restore the default orientation and re-frame the content.
    void resetView();

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

    /// The mesh's own triangle edges, drawn as a thin unlit overlay ON TOP
    /// of the solid fill (a `glPolygonOffset` on the fill is what keeps
    /// them from z-fighting with the coincident triangle surface). Same
    /// vertex layout as setMesh() — color is read but not shaded, so pass
    /// whatever the wireframe should look like (typically the surface's own
    /// color, darkened) already baked in.
    void setWireframeOverlay(std::vector<float> interleavedPosNormalColor);
    void clearWireframeOverlay();

    /// A 3D line segment meant to be drawn as a screen-space QPainter
    /// overlay rather than as GL_LINES — core-profile GL clamps
    /// glLineWidth to 1px on macOS, so this is how a line gets an
    /// adjustable width at all (see StructureRenderer's cell edges, which
    /// use instanced tube geometry for the same reason; a handful of
    /// Brillouin-zone edges don't need geometry that heavy).
    struct OverlayLine {
        QVector3D a, b;
    };
    /// Replace the width-adjustable line overlay (e.g. the Brillouin-zone
    /// wireframe). Projected through the live camera and painted after the
    /// GL pass, same as the label overlay. An empty `lines` clears it.
    void setOverlayLines(std::vector<OverlayLine> lines, const QColor& color,
                         float width);

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

Q_SIGNALS:
    /// The camera orientation changed through a pointer gesture. Emitted so a
    /// host dialog showing the orientation numerically can stay honest — a
    /// read-out that only tracked the buttons beside it would go stale the
    /// moment the user dragged the canvas instead.
    void viewChanged();

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
    /// Project overlayLines_ through the live camera and paint them over the
    /// scene, same projection drawLabels() uses.
    void drawOverlayLines(class QPainter& painter);

    render::OrbitCamera camera_;
    QOpenGLShaderProgram program_;
    Buffer isoBuffer_;
    Buffer sliceBuffer_;
    Buffer boxBuffer_;
    Buffer wireOverlayBuffer_;
    QPointF lastMousePos_;
    QVector3D boxCenter_;
    float boxRadius_ = 10.0f;
    /// Slightly translucent by default so a colour slice behind an isosurface
    /// stays readable; setMeshOpacity() overrides it.
    float meshAlpha_ = 0.88f;
    /// Isosurface material — see setIsoMaterial(). Defaults reproduce the
    /// shader's original hardcoded formula (Glossy shading, 0.28 ambient,
    /// 0.35 specular).
    int isoShadingMode_ = 2;
    float isoAmbient_ = 0.28f;
    float isoSpecular_ = 0.35f;
    std::vector<Label> labels_;
    std::vector<OverlayLine> overlayLines_;
    QColor overlayLineColor_{150, 152, 160};
    float overlayLineWidth_ = 1.0f;
    bool initialized_ = false;
};

} // namespace calango::gui
