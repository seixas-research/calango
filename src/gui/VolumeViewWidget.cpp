#include "gui/VolumeViewWidget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>

#include <algorithm>

namespace calango::gui {

namespace {

void pushVertex(std::vector<float>& out, const calango::core::Vec3& p,
                const calango::core::Vec3& n, const QColor& c)
{
    out.insert(out.end(),
               {static_cast<float>(p.x), static_cast<float>(p.y),
                static_cast<float>(p.z), static_cast<float>(n.x),
                static_cast<float>(n.y), static_cast<float>(n.z),
                static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                static_cast<float>(c.blueF())});
}

} // namespace

VolumeViewWidget::VolumeViewWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(420, 360);
}

VolumeViewWidget::~VolumeViewWidget()
{
    makeCurrent();
    for (Buffer* buffer : {&isoBuffer_, &sliceBuffer_, &boxBuffer_}) {
        buffer->vbo.destroy();
        buffer->vao.destroy();
    }
    doneCurrent();
}

void VolumeViewWidget::setIsoMesh(const core::IsoMesh& mesh,
                                  render::ColorGradient gradient,
                                  double colorMin, double colorMax, bool colored)
{
    isoBuffer_.staging.clear();
    isoBuffer_.staging.reserve(mesh.positions.size() * 9);
    const double range = std::max(colorMax - colorMin, 1e-30);
    const QColor plain(255, 179, 71); // amber default isosurface
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        QColor color = plain;
        if (colored && i < mesh.colorValues.size())
            color = render::ColorMap::sample(
                gradient,
                static_cast<float>((mesh.colorValues[i] - colorMin) / range));
        pushVertex(isoBuffer_.staging, mesh.positions[i], mesh.normals[i], color);
    }
    isoBuffer_.vertexCount = static_cast<int>(mesh.positions.size());
    isoBuffer_.dirty = true;
    update();
}

void VolumeViewWidget::clearIsoMesh()
{
    isoBuffer_.staging.clear();
    isoBuffer_.vertexCount = 0;
    isoBuffer_.dirty = true;
    update();
}

void VolumeViewWidget::setSlice(const std::vector<float>& interleavedPosColor)
{
    // Incoming stream is pos(3)+color(3); expand with a dummy normal so
    // every buffer shares one vertex layout.
    sliceBuffer_.staging.clear();
    sliceBuffer_.staging.reserve(interleavedPosColor.size() / 6 * 9);
    for (std::size_t i = 0; i + 5 < interleavedPosColor.size(); i += 6) {
        sliceBuffer_.staging.insert(
            sliceBuffer_.staging.end(),
            {interleavedPosColor[i], interleavedPosColor[i + 1],
             interleavedPosColor[i + 2], 0.0f, 0.0f, 1.0f,
             interleavedPosColor[i + 3], interleavedPosColor[i + 4],
             interleavedPosColor[i + 5]});
    }
    sliceBuffer_.vertexCount = static_cast<int>(sliceBuffer_.staging.size() / 9);
    sliceBuffer_.dirty = true;
    update();
}

void VolumeViewWidget::clearSlice()
{
    sliceBuffer_.staging.clear();
    sliceBuffer_.vertexCount = 0;
    sliceBuffer_.dirty = true;
    update();
}

void VolumeViewWidget::setBox(const core::VolumetricData& field)
{
    const core::Vec3 o = field.origin;
    const core::Vec3 a = field.spanA, b = field.spanB, c = field.spanC;
    const core::Vec3 corners[8] = {o,         o + a,         o + b,
                                   o + a + b, o + c,         o + a + c,
                                   o + b + c, o + a + b + c};
    static constexpr int kEdges[12][2] = {{0, 1}, {0, 2}, {1, 3}, {2, 3},
                                          {4, 5}, {4, 6}, {5, 7}, {6, 7},
                                          {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    boxBuffer_.staging.clear();
    const QColor gray(150, 152, 160);
    for (const auto& edge : kEdges) {
        pushVertex(boxBuffer_.staging, corners[edge[0]], {0, 0, 1}, gray);
        pushVertex(boxBuffer_.staging, corners[edge[1]], {0, 0, 1}, gray);
    }
    boxBuffer_.vertexCount = 24;
    boxBuffer_.dirty = true;

    const core::Vec3 center = o + (a + b + c) * 0.5;
    boxCenter_ = QVector3D(static_cast<float>(center.x),
                           static_cast<float>(center.y),
                           static_cast<float>(center.z));
    boxRadius_ = static_cast<float>((a + b + c).norm()) * 0.5f;
    frameBox();
}

void VolumeViewWidget::frameBox()
{
    camera_.frame(boxCenter_, std::max(1.0f, boxRadius_));
    update();
}

void VolumeViewWidget::setMesh(std::vector<float> interleavedPosNormalColor)
{
    isoBuffer_.staging = std::move(interleavedPosNormalColor);
    isoBuffer_.vertexCount = static_cast<int>(isoBuffer_.staging.size() / 9);
    isoBuffer_.dirty = true;
    update();
}

void VolumeViewWidget::setLines(std::vector<float> interleavedPosNormalColor)
{
    boxBuffer_.staging = std::move(interleavedPosNormalColor);
    boxBuffer_.vertexCount = static_cast<int>(boxBuffer_.staging.size() / 9);
    boxBuffer_.dirty = true;
    update();
}

void VolumeViewWidget::setBounds(const QVector3D& center, float radius)
{
    boxCenter_ = center;
    boxRadius_ = std::max(radius, 1e-3f);
    frameBox();
}

void VolumeViewWidget::setMeshOpacity(float alpha)
{
    meshAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
    update();
}

void VolumeViewWidget::setLabels(std::vector<Label> labels)
{
    labels_ = std::move(labels);
    update();
}

void VolumeViewWidget::initializeGL()
{
    initializeOpenGLFunctions();
    program_.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                     QStringLiteral(":/assets/shaders/volume.vert"));
    program_.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                     QStringLiteral(":/assets/shaders/volume.frag"));
    program_.link();
    initialized_ = true;
}

void VolumeViewWidget::upload(Buffer& buffer)
{
    if (!buffer.dirty)
        return;
    if (!buffer.vao.isCreated()) {
        buffer.vao.create();
        buffer.vbo.create();
    }
    buffer.vao.bind();
    buffer.vbo.bind();
    buffer.vbo.allocate(buffer.staging.data(),
                        static_cast<int>(buffer.staging.size() * sizeof(float)));
    constexpr int stride = 9 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(6 * sizeof(float)));
    buffer.vao.release();
    buffer.dirty = false;
}

void VolumeViewWidget::draw(Buffer& buffer, GLenum mode, bool unlit, float alpha)
{
    if (buffer.vertexCount == 0)
        return;
    program_.setUniformValue("uUnlit", unlit ? 1 : 0);
    program_.setUniformValue("uAlpha", alpha);
    buffer.vao.bind();
    glDrawArrays(mode, 0, buffer.vertexCount);
    buffer.vao.release();
}

void VolumeViewWidget::paintGL()
{
    // QPainter is constructed FIRST and the raw GL is bracketed by
    // begin/endNativePainting: that is the supported way to mix the two on a
    // QOpenGLWidget, and it is what lets the text overlay below be ordinary
    // Qt text rather than a glyph atlas and a text shader.
    QPainter painter(this);
    painter.beginNativePainting();

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (Buffer* buffer : {&isoBuffer_, &sliceBuffer_, &boxBuffer_})
        upload(*buffer);

    const float aspect = height() > 0
        ? static_cast<float>(width()) / static_cast<float>(height())
        : 1.0f;
    program_.bind();
    program_.setUniformValue("uView", camera_.view());
    program_.setUniformValue("uProj", camera_.projection(aspect));

    draw(boxBuffer_, GL_LINES, true, 1.0f);
    draw(sliceBuffer_, GL_TRIANGLES, true, 1.0f);
    // The isosurface last, slightly translucent, so slices stay readable.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw(isoBuffer_, GL_TRIANGLES, false, meshAlpha_);
    glDisable(GL_BLEND);
    program_.release();

    painter.endNativePainting();
    drawLabels(painter);
}

void VolumeViewWidget::drawLabels(QPainter& painter)
{
    if (labels_.empty())
        return;
    const float aspect = height() > 0
        ? static_cast<float>(width()) / static_cast<float>(height())
        : 1.0f;
    const QMatrix4x4 viewProjection = camera_.projection(aspect) * camera_.view();

    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);

    for (const Label& label : labels_) {
        const QVector4D clip =
            viewProjection * QVector4D(label.position, 1.0f);
        // Behind the eye: the perspective divide would fold it back into view
        // as a mirrored ghost on the far side of the screen.
        if (clip.w() <= 0.0f)
            continue;
        const QVector3D ndc = clip.toVector3DAffine();
        if (ndc.x() < -1.2f || ndc.x() > 1.2f || ndc.y() < -1.2f
            || ndc.y() > 1.2f)
            continue;
        const QPointF screen(
            (ndc.x() * 0.5f + 0.5f) * static_cast<float>(width()),
            (1.0f - (ndc.y() * 0.5f + 0.5f)) * static_cast<float>(height()));
        // A dark outline behind the glyphs: the canvas runs from a near-black
        // background to a bright surface crest, and a single ink colour is
        // illegible against one end or the other.
        painter.setPen(QPen(QColor(10, 11, 13, 200), 3.0));
        painter.drawText(screen + QPointF(6.0, -6.0), label.text);
        painter.setPen(label.color);
        painter.drawText(screen + QPointF(6.0, -6.0), label.text);
        painter.setPen(QPen(label.color, 1.5));
        painter.drawEllipse(screen, 2.5, 2.5);
    }
}

void VolumeViewWidget::mousePressEvent(QMouseEvent* event)
{
    lastMousePos_ = event->position();
}

void VolumeViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF delta = event->position() - lastMousePos_;
    lastMousePos_ = event->position();
    const bool pan = event->buttons().testFlag(Qt::MiddleButton)
        || (event->buttons().testFlag(Qt::LeftButton)
            && event->modifiers().testFlag(Qt::ShiftModifier));
    if (pan)
        camera_.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()),
                    height());
    else if (event->buttons().testFlag(Qt::LeftButton))
        camera_.rotate(static_cast<float>(delta.x()) * 0.4f,
                       static_cast<float>(delta.y()) * 0.4f);
    else
        return;
    update();
}

void VolumeViewWidget::wheelEvent(QWheelEvent* event)
{
    camera_.zoom(static_cast<float>(event->angleDelta().y()) / 120.0f);
    update();
}

void VolumeViewWidget::mouseDoubleClickEvent(QMouseEvent*)
{
    frameBox();
}

} // namespace calango::gui
