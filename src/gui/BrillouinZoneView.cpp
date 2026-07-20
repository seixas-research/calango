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
    camera_.frame({0.0f, 0.0f, 0.0f}, radius);
    update();
}

void BrillouinZoneView::setPath(const std::vector<int>& path)
{
    path_ = path;
    pathDirty_ = true;
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
    for (std::size_t i = 0; i + 1 < path_.size(); ++i) {
        for (const int idx : {path_[i], path_[i + 1]}) {
            const QVector3D p = points_[static_cast<std::size_t>(idx)].cartesian;
            vertices.insert(vertices.end(), {p.x(), p.y(), p.z()});
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
    const QVector4D clip = mvp * QVector4D(point, 1.0f);
    if (clip.w() <= 0.0f)
        return false;
    const QVector3D ndc = clip.toVector3D() / clip.w();
    screen = {(ndc.x() * 0.5 + 0.5) * width(), (0.5 - ndc.y() * 0.5) * height()};
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
        flatProgram_.setUniformValue("uColor", QVector4D(0.78f, 0.80f, 0.84f, 1.0f));
        edgeVao_.bind();
        glDrawArrays(GL_LINES, 0, edgeVertexCount_);
        edgeVao_.release();
    }
    if (pathVertexCount_ > 0) {
        flatProgram_.setUniformValue("uColor", QVector4D(1.0f, 0.62f, 0.12f, 1.0f));
        pathVao_.bind();
        glDrawArrays(GL_LINES, 0, pathVertexCount_);
        pathVao_.release();
    }
    flatProgram_.release();

    // High-symmetry points: path members highlighted orange.
    if (!points_.empty()) {
        pointProgram_.bind();
        pointProgram_.setUniformValue("uMvp", transform);
        pointVao_.bind();
        for (int i = 0; i < static_cast<int>(points_.size()); ++i) {
            const bool onPath = std::find(path_.begin(), path_.end(), i) != path_.end();
            pointProgram_.setUniformValue("uPointSize", onPath ? 17.0f : 12.0f);
            pointProgram_.setUniformValue(
                "uColor", onPath ? QVector4D(1.0f, 0.62f, 0.12f, 1.0f)
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
        flatProgram_.setUniformValue("uColor", QVector4D(0.36f, 0.55f, 0.92f, 0.16f));
        faceVao_.bind();
        glDrawArrays(GL_TRIANGLES, 0, faceVertexCount_);
        faceVao_.release();
        flatProgram_.release();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // Text overlay: labels and path order numbers.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() + 1.5);
    font.setBold(true);
    painter.setFont(font);
    for (std::size_t i = 0; i < points_.size(); ++i) {
        QPointF screen;
        if (!project(points_[i].cartesian, transform, screen))
            continue;
        painter.setPen(QColor(235, 238, 245));
        painter.drawText(screen + QPointF(9, -7), points_[i].label);

        // Order badge(s) for path membership, e.g. "1,4".
        QStringList orders;
        for (std::size_t k = 0; k < path_.size(); ++k)
            if (path_[k] == static_cast<int>(i))
                orders << QString::number(k + 1);
        if (!orders.isEmpty()) {
            painter.setPen(QColor(255, 158, 31));
            painter.drawText(screen + QPointF(9, 14), orders.join(QLatin1Char(',')));
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
