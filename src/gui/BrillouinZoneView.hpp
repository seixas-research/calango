#pragma once

#include "core/BrillouinZone.hpp"
#include "render/Camera.hpp"

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

    void setZone(const core::BrillouinZoneData& zone,
                 const std::vector<LabeledPoint>& points);

    /// Ordered indices into the labeled points forming the k-path; -1
    /// entries mark discontinuities ("breaks") between path sections.
    /// Directional arrowheads are drawn along each connected leg.
    void setPath(const std::vector<int>& path);

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
