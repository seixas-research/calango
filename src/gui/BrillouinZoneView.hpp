#pragma once

#include "core/BrillouinZone.hpp"
#include "render/Camera.hpp"

#include <QColor>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>

#include <vector>

namespace calango::gui {

/// Interactive 3D view of the first Brillouin zone: semi-transparent
/// Wigner-Seitz polyhedron, zone edges, high-symmetry points with QPainter
/// text labels, and the user's k-path drawn as connected segments.
///
/// Interaction: left-drag rotates, Shift+drag pans, wheel zooms; a left
/// *click* on a high-symmetry point emits pointPicked(index) so the dialog
/// can append it to the path sequence.
class BrillouinZoneView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit BrillouinZoneView(QWidget* parent = nullptr);
    ~BrillouinZoneView() override;

    struct LabeledPoint {
        QString label;     ///< display label ("Γ", "X", ...)
        QVector3D cartesian; ///< Å⁻¹
    };

    /// User-controllable appearance of the zone and k-path (driven by the
    /// "Customize Appearance…" dialog). Defaults reproduce the built-in look.
    struct Style {
        QColor surfaceColor{93, 140, 235};   ///< Wigner-Seitz face fill
        float surfaceAlpha = 0.16f;          ///< face transparency (0..1)
        QColor edgeColor{150, 156, 166};     ///< zone border wireframe
        QColor pathColor{240, 150, 30};      ///< k-path line + on-path points
        float pathThickness = 2.4f;          ///< k-path line width (px)
        bool showLabels = true;              ///< high-symmetry point labels
        bool showOrderNumbers = false;       ///< sequential badges along path
        bool showPathArrows = true;          ///< directional arrows along legs
    };

    void setStyle(const Style& style);
    const Style& style() const { return style_; }

    void setZone(const core::BrillouinZoneData& zone,
                 const std::vector<LabeledPoint>& points);

    /// Ordered indices into the labeled points forming the k-path; -1
    /// entries mark discontinuities ("breaks") between path sections.
    /// Directional arrowheads are drawn along each connected leg.
    void setPath(const std::vector<int>& path);

    /// Perspective (default) vs. orthographic projection — the toggle
    /// preserves apparent scale (see OrbitCamera::projection). Figure
    /// exports follow the active mode.
    void setOrthographic(bool orthographic);

    /// Publication-style figure (white background) of the current zone,
    /// high-symmetry labels and k-path, rendered with QPainter primitives
    /// only — suitable for both raster (QImage) and vector (QSvgGenerator)
    /// paint devices at any resolution.
    void paintFigure(QPainter& painter, const QSize& size) const;

Q_SIGNALS:
    void pointPicked(int index);

protected:
    void initializeGL() override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QMatrix4x4 mvp() const;
    bool project(const QVector3D& point, const QMatrix4x4& mvp, QPointF& screen) const;
    static bool projectTo(const QVector3D& point, const QMatrix4x4& mvp,
                          const QSizeF& size, QPointF& screen);
    void uploadZone();
    void uploadPath();

    render::OrbitCamera camera_;
    core::BrillouinZoneData zone_;
    Style style_;
    std::vector<LabeledPoint> points_;
    std::vector<int> path_;
    float zoneRadius_ = 1.0f;
    bool zoneDirty_ = false;
    bool pathDirty_ = false;

    QOpenGLShaderProgram flatProgram_;
    QOpenGLShaderProgram pointProgram_;
    QOpenGLVertexArrayObject faceVao_, edgeVao_, pathVao_, pointVao_;
    QOpenGLBuffer faceVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer edgeVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer pathVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer pointVbo_{QOpenGLBuffer::VertexBuffer};
    int faceVertexCount_ = 0;
    int edgeVertexCount_ = 0;
    int pathVertexCount_ = 0;

    QPointF lastMousePos_;
    QPointF pressPos_;
};

} // namespace calango::gui
