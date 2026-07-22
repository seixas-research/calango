#include "gui/BrillouinZoneView.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <set>

namespace calango::gui {

namespace {

const char* kFlatVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main() { gl_Position = uMvp * vec4(aPos, 1.0); }
)";

const char* kFlatFragmentShader = R"(#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() { fragColor = uColor; }
)";

const char* kPointVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
uniform float uPointSize;
void main() { gl_Position = uMvp * vec4(aPos, 1.0); gl_PointSize = uPointSize; }
)";

const char* kPointFragmentShader = R"(#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    if (length(gl_PointCoord - vec2(0.5)) > 0.5)
        discard;
    fragColor = uColor;
}
)";

QVector3D toQt(const calango::core::Vec3& v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

QVector4D colorVec(const QColor& c, float alpha = 1.0f)
{
    return {static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
            static_cast<float>(c.blueF()), alpha};
}

void setupPositionVao(QOpenGLVertexArrayObject& vao, QOpenGLBuffer& vbo,
                      QOpenGLFunctions_3_3_Core* gl)
{
    vao.create();
    vao.bind();
    vbo.create();
    vbo.bind();
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    vao.release();
}

} // namespace

BrillouinZoneView::BrillouinZoneView(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(460, 420);
}

BrillouinZoneView::~BrillouinZoneView()
{
    makeCurrent();
    doneCurrent();
}

void BrillouinZoneView::setZone(const core::BrillouinZoneData& zone,
                                const std::vector<LabeledPoint>& points)
{
    zone_ = zone;
    points_ = points;
    path_.clear();
    zoneDirty_ = true;
    pathDirty_ = true;

    float radius = 0.5f;
    for (const auto& vertex : zone_.vertices)
        radius = std::max(radius, toQt(vertex).length());
    zoneRadius_ = radius;
    camera_.frame({0.0f, 0.0f, 0.0f}, radius);
    update();
}

void BrillouinZoneView::setPath(const std::vector<int>& path)
{
    path_ = path;
    pathDirty_ = true;
    update();
}

void BrillouinZoneView::setStyle(const Style& style)
{
    style_ = style;
    update();
}

void BrillouinZoneView::setOrthographic(bool orthographic)
{
    camera_.setProjectionMode(orthographic
                                  ? render::CameraProjection::Orthographic
                                  : render::CameraProjection::Perspective);
    update();
}

void BrillouinZoneView::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_PROGRAM_POINT_SIZE);

    flatProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kFlatVertexShader);
    flatProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFlatFragmentShader);
    flatProgram_.link();
    pointProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kPointVertexShader);
    pointProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kPointFragmentShader);
    pointProgram_.link();

    setupPositionVao(faceVao_, faceVbo_, this);
    setupPositionVao(edgeVao_, edgeVbo_, this);
    setupPositionVao(pathVao_, pathVbo_, this);
    setupPositionVao(pointVao_, pointVbo_, this);

    zoneDirty_ = true;
    pathDirty_ = true;
}

void BrillouinZoneView::uploadZone()
{
    std::vector<float> faceVertices;
    std::vector<float> edgeVertices;
    std::vector<float> pointVertices;

    const auto push = [](std::vector<float>& data, const QVector3D& p) {
        data.insert(data.end(), {p.x(), p.y(), p.z()});
    };

    std::set<std::pair<int, int>> edges;
    for (const auto& face : zone_.faces) {
        const QVector3D v0 = toQt(zone_.vertices[static_cast<std::size_t>(face[0])]);
        for (std::size_t t = 1; t + 1 < face.size(); ++t) { // triangle fan
            push(faceVertices, v0);
            push(faceVertices, toQt(zone_.vertices[static_cast<std::size_t>(face[t])]));
            push(faceVertices, toQt(zone_.vertices[static_cast<std::size_t>(face[t + 1])]));
        }
        for (std::size_t i = 0; i < face.size(); ++i) {
            const int a = face[i];
            const int b = face[(i + 1) % face.size()];
            edges.insert({std::min(a, b), std::max(a, b)});
        }
    }
    for (const auto& [a, b] : edges) {
        push(edgeVertices, toQt(zone_.vertices[static_cast<std::size_t>(a)]));
        push(edgeVertices, toQt(zone_.vertices[static_cast<std::size_t>(b)]));
    }
    for (const auto& point : points_)
        push(pointVertices, point.cartesian);

    faceVertexCount_ = static_cast<int>(faceVertices.size()) / 3;
    faceVbo_.bind();
    faceVbo_.allocate(faceVertices.data(),
                      static_cast<int>(faceVertices.size() * sizeof(float)));
    edgeVertexCount_ = static_cast<int>(edgeVertices.size()) / 3;
    edgeVbo_.bind();
    edgeVbo_.allocate(edgeVertices.data(),
                      static_cast<int>(edgeVertices.size() * sizeof(float)));
    pointVbo_.bind();
    pointVbo_.allocate(pointVertices.data(),
                       static_cast<int>(pointVertices.size() * sizeof(float)));
}

void BrillouinZoneView::uploadPath()
{
    std::vector<float> vertices;
    const auto push = [&vertices](const QVector3D& p) {
        vertices.insert(vertices.end(), {p.x(), p.y(), p.z()});
    };

    for (std::size_t i = 0; i + 1 < path_.size(); ++i) {
        // -1 marks a section break: no leg is drawn across it.
        if (path_[i] < 0 || path_[i + 1] < 0)
            continue;
        const QVector3D a = points_[static_cast<std::size_t>(path_[i])].cartesian;
        const QVector3D b = points_[static_cast<std::size_t>(path_[i + 1])].cartesian;
        push(a);
        push(b);

        // Directional arrowhead: four short wings meeting at ~60% along
        // the leg, visible from any viewing angle.
        const QVector3D dir = (b - a).normalized();
        const float length = (b - a).length();
        if (length < 1e-6f)
            continue;
        const float wing = std::min(0.14f * length, 0.07f * zoneRadius_);
        const QVector3D reference = std::abs(dir.z()) < 0.9f
            ? QVector3D(0.0f, 0.0f, 1.0f)
            : QVector3D(1.0f, 0.0f, 0.0f);
        const QVector3D perp1 = QVector3D::crossProduct(dir, reference).normalized();
        const QVector3D perp2 = QVector3D::crossProduct(dir, perp1).normalized();
        const QVector3D tip = a + (b - a) * 0.6f;
        for (const QVector3D& perp : {perp1, -perp1, perp2, -perp2}) {
            push(tip);
            push(tip - dir * wing + perp * (wing * 0.55f));
        }
    }
    pathVertexCount_ = static_cast<int>(vertices.size()) / 3;
    pathVbo_.bind();
    pathVbo_.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));
}

QMatrix4x4 BrillouinZoneView::mvp() const
{
    const float aspect = height() > 0
        ? static_cast<float>(width()) / static_cast<float>(height())
        : 1.0f;
    return camera_.projection(aspect) * camera_.view();
}

bool BrillouinZoneView::project(const QVector3D& point, const QMatrix4x4& mvp,
                                QPointF& screen) const
{
    return projectTo(point, mvp, QSizeF(width(), height()), screen);
}

bool BrillouinZoneView::projectTo(const QVector3D& point, const QMatrix4x4& mvp,
                                  const QSizeF& size, QPointF& screen)
{
    const QVector4D clip = mvp * QVector4D(point, 1.0f);
    if (clip.w() <= 0.0f)
        return false;
    const QVector3D ndc = clip.toVector3D() / clip.w();
    screen = {(ndc.x() * 0.5 + 0.5) * size.width(),
              (0.5 - ndc.y() * 0.5) * size.height()};
    return true;
}

void BrillouinZoneView::paintGL()
{
    if (zoneDirty_) {
        uploadZone();
        zoneDirty_ = false;
    }
    if (pathDirty_) {
        uploadPath();
        pathDirty_ = false;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const QMatrix4x4 transform = mvp();

    flatProgram_.bind();
    flatProgram_.setUniformValue("uMvp", transform);

    if (edgeVertexCount_ > 0) {
        flatProgram_.setUniformValue("uColor", colorVec(style_.edgeColor));
        edgeVao_.bind();
        glDrawArrays(GL_LINES, 0, edgeVertexCount_);
        edgeVao_.release();
    }
    // NB: the k-path lines are NOT drawn here. glLineWidth > 1 is unreliable
    // in a 3.3 core profile (macOS clamps it to 1.0), so k-path legs and their
    // direction arrows are drawn in the QPainter overlay below, where the pen
    // width honors style_.pathThickness exactly.
    flatProgram_.release();

    // High-symmetry points: path members highlighted in the k-path color.
    if (!points_.empty()) {
        pointProgram_.bind();
        pointProgram_.setUniformValue("uMvp", transform);
        pointVao_.bind();
        for (int i = 0; i < static_cast<int>(points_.size()); ++i) {
            const bool onPath = std::find(path_.begin(), path_.end(), i) != path_.end();
            pointProgram_.setUniformValue("uPointSize", onPath ? 17.0f : 12.0f);
            pointProgram_.setUniformValue(
                "uColor", onPath ? colorVec(style_.pathColor)
                                 : QVector4D(0.35f, 0.75f, 1.0f, 1.0f));
            glDrawArrays(GL_POINTS, i, 1);
        }
        pointVao_.release();
        pointProgram_.release();
    }

    // Translucent zone faces last (depth-tested, but not depth-written).
    if (faceVertexCount_ > 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        flatProgram_.bind();
        flatProgram_.setUniformValue("uMvp", transform);
        flatProgram_.setUniformValue(
            "uColor", colorVec(style_.surfaceColor, style_.surfaceAlpha));
        faceVao_.bind();
        glDrawArrays(GL_TRIANGLES, 0, faceVertexCount_);
        faceVao_.release();
        flatProgram_.release();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // Overlay (QPainter): k-path legs + direction arrows, then labels and
    // path order numbers. Drawing the path here (rather than in GL) lets the
    // pen width follow style_.pathThickness on every platform.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const double penWidth = std::max(0.5, static_cast<double>(style_.pathThickness));
    painter.setPen(QPen(style_.pathColor, penWidth, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    for (std::size_t i = 0; i + 1 < path_.size(); ++i) {
        if (path_[i] < 0 || path_[i + 1] < 0)
            continue; // discontinuous section break
        QPointF from, to;
        if (!project(points_[static_cast<std::size_t>(path_[i])].cartesian,
                     transform, from)
            || !project(points_[static_cast<std::size_t>(path_[i + 1])].cartesian,
                        transform, to))
            continue;
        painter.setPen(QPen(style_.pathColor, penWidth, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(from, to);

        if (!style_.showPathArrows)
            continue;
        // Directional arrowhead at 60% along the leg, showing the navigation
        // order between high-symmetry points.
        const QPointF delta = to - from;
        const double length = std::hypot(delta.x(), delta.y());
        if (length < 8.0)
            continue;
        const QPointF dir = delta / length;
        const QPointF normal(-dir.y(), dir.x());
        const QPointF tip = from + delta * 0.6;
        const double wing = std::min(9.0 + penWidth * 2.0, length * 0.3);
        QPolygonF arrow;
        arrow << tip << tip - dir * wing + normal * (wing * 0.5)
              << tip - dir * wing - normal * (wing * 0.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(style_.pathColor);
        painter.drawPolygon(arrow);
    }
    painter.setBrush(Qt::NoBrush);

    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() + 1.5);
    font.setBold(true);
    painter.setFont(font);
    for (std::size_t i = 0; i < points_.size(); ++i) {
        QPointF screen;
        if (!project(points_[i].cartesian, transform, screen))
            continue;
        if (style_.showLabels) {
            painter.setPen(QColor(235, 238, 245));
            painter.drawText(screen + QPointF(9, -7), points_[i].label);
        }

        if (!style_.showOrderNumbers)
            continue;
        // Order badge(s) for path membership, e.g. "1,4" — breaks (-1) do
        // not consume an order number.
        QStringList orders;
        int order = 0;
        for (const int entry : path_) {
            if (entry < 0)
                continue;
            ++order;
            if (entry == static_cast<int>(i))
                orders << QString::number(order);
        }
        if (!orders.isEmpty()) {
            painter.setPen(style_.pathColor);
            painter.drawText(screen + QPointF(9, 14), orders.join(QLatin1Char(',')));
        }
    }
}

void BrillouinZoneView::paintFigure(QPainter& painter, const QSize& size) const
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(QRect(QPoint(0, 0), size), Qt::white);

    const float aspect = size.height() > 0
        ? static_cast<float>(size.width()) / static_cast<float>(size.height())
        : 1.0f;
    const QMatrix4x4 view = camera_.view();
    const QMatrix4x4 transform = camera_.projection(aspect) * view;
    const QSizeF sizeF(size);
    const double scale = std::min(size.width(), size.height()) / 560.0;

    // Zone faces, painter's algorithm: farthest centroid first.
    std::vector<std::pair<float, const std::vector<int>*>> ordered;
    for (const auto& face : zone_.faces) {
        QVector3D centroid;
        for (const int v : face)
            centroid += toQt(zone_.vertices[static_cast<std::size_t>(v)]);
        centroid /= static_cast<float>(face.size());
        ordered.emplace_back((view.map(centroid)).z(), &face);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    QColor surfaceFill = style_.surfaceColor;
    surfaceFill.setAlphaF(style_.surfaceAlpha);
    painter.setPen(Qt::NoPen);
    painter.setBrush(surfaceFill);
    for (const auto& [depth, face] : ordered) {
        (void)depth;
        QPolygonF polygon;
        bool visible = true;
        for (const int v : *face) {
            QPointF screen;
            if (!projectTo(toQt(zone_.vertices[static_cast<std::size_t>(v)]),
                           transform, sizeF, screen)) {
                visible = false;
                break;
            }
            polygon << screen;
        }
        if (visible)
            painter.drawPolygon(polygon);
    }

    // Zone edges (deduplicated).
    std::set<std::pair<int, int>> edges;
    for (const auto& face : zone_.faces)
        for (std::size_t i = 0; i < face.size(); ++i) {
            const int a = face[i];
            const int b = face[(i + 1) % face.size()];
            edges.insert({std::min(a, b), std::max(a, b)});
        }
    painter.setPen(QPen(style_.edgeColor, 1.4 * scale));
    painter.setBrush(Qt::NoBrush);
    for (const auto& [a, b] : edges) {
        QPointF from, to;
        if (projectTo(toQt(zone_.vertices[static_cast<std::size_t>(a)]), transform,
                      sizeF, from)
            && projectTo(toQt(zone_.vertices[static_cast<std::size_t>(b)]), transform,
                         sizeF, to))
            painter.drawLine(from, to);
    }

    // k-path legs with 2D arrowheads at 60% of each leg.
    const QColor pathColor = style_.pathColor;
    const double pathWidth = style_.pathThickness * scale;
    painter.setPen(QPen(pathColor, pathWidth, Qt::SolidLine, Qt::RoundCap));
    for (std::size_t i = 0; i + 1 < path_.size(); ++i) {
        if (path_[i] < 0 || path_[i + 1] < 0)
            continue;
        QPointF from, to;
        if (!projectTo(points_[static_cast<std::size_t>(path_[i])].cartesian,
                       transform, sizeF, from)
            || !projectTo(points_[static_cast<std::size_t>(path_[i + 1])].cartesian,
                          transform, sizeF, to))
            continue;
        painter.drawLine(from, to);

        if (!style_.showPathArrows)
            continue;
        const QPointF delta = to - from;
        const double length = std::hypot(delta.x(), delta.y());
        if (length < 8.0)
            continue;
        const QPointF dir = delta / length;
        const QPointF normal(-dir.y(), dir.x());
        const QPointF tip = from + delta * 0.6;
        const double wing = std::min(14.0 * scale, length * 0.25);
        QPolygonF arrow;
        arrow << tip << tip - dir * wing + normal * (wing * 0.5)
              << tip - dir * wing - normal * (wing * 0.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(pathColor);
        painter.drawPolygon(arrow);
        painter.setPen(QPen(pathColor, pathWidth, Qt::SolidLine, Qt::RoundCap));
    }

    // High-symmetry points, labels and order badges.
    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() * std::max(1.0, scale) + 2.0);
    font.setBold(true);
    painter.setFont(font);
    for (std::size_t i = 0; i < points_.size(); ++i) {
        QPointF screen;
        if (!projectTo(points_[i].cartesian, transform, sizeF, screen))
            continue;
        const bool onPath =
            std::find(path_.begin(), path_.end(), static_cast<int>(i)) != path_.end();
        painter.setPen(Qt::NoPen);
        painter.setBrush(onPath ? pathColor : QColor(58, 199, 235));
        painter.drawEllipse(screen, (onPath ? 5.5 : 4.0) * scale,
                            (onPath ? 5.5 : 4.0) * scale);

        if (style_.showLabels) {
            painter.setPen(QColor(32, 36, 42));
            painter.drawText(screen + QPointF(8 * scale, -6 * scale),
                             points_[i].label);
        }

        if (!style_.showOrderNumbers)
            continue;
        QStringList orders;
        int order = 0;
        for (const int entry : path_) {
            if (entry < 0)
                continue;
            ++order;
            if (entry == static_cast<int>(i))
                orders << QString::number(order);
        }
        if (!orders.isEmpty()) {
            painter.setPen(pathColor.darker(110));
            painter.drawText(screen + QPointF(8 * scale, 15 * scale),
                             orders.join(QLatin1Char(',')));
        }
    }
}

void BrillouinZoneView::mousePressEvent(QMouseEvent* event)
{
    lastMousePos_ = event->position();
    pressPos_ = event->position();
}

void BrillouinZoneView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF delta = event->position() - lastMousePos_;
    lastMousePos_ = event->position();
    if (event->buttons().testFlag(Qt::LeftButton)
        && event->modifiers().testFlag(Qt::ShiftModifier)) {
        camera_.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()), height());
    } else if (event->buttons().testFlag(Qt::LeftButton)) {
        camera_.rotate(static_cast<float>(delta.x()) * 0.4f,
                       static_cast<float>(delta.y()) * 0.4f);
    } else {
        return;
    }
    update();
}

void BrillouinZoneView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF drag = event->position() - pressPos_;
    if (std::abs(drag.x()) + std::abs(drag.y()) > 4.0)
        return;

    // Pick the nearest high-symmetry point within a screen-space radius.
    const QMatrix4x4 transform = mvp();
    int best = -1;
    double bestDistance = 16.0; // px
    for (std::size_t i = 0; i < points_.size(); ++i) {
        QPointF screen;
        if (!project(points_[i].cartesian, transform, screen))
            continue;
        const QPointF d = screen - event->position();
        const double distance = std::hypot(d.x(), d.y());
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    if (best >= 0)
        Q_EMIT pointPicked(best);
}

void BrillouinZoneView::wheelEvent(QWheelEvent* event)
{
    camera_.zoom(static_cast<float>(event->angleDelta().y()) / 120.0f);
    update();
}

} // namespace calango::gui
