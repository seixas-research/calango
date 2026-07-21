#include "gui/ViewportWidget.hpp"

#include "core/Structure.hpp"

#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace {

constexpr int kAxesMarginPx = 10; // logical pixels, bottom-left corner

const char* kAxesVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMvp;
out vec3 vColor;
void main() { vColor = aColor; gl_Position = uMvp * vec4(aPos, 1.0); }
)";

const char* kAxesFragmentShader = R"(#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main() { fragColor = vec4(vColor, 1.0); }
)";

} // namespace

namespace calango::gui {

ViewportWidget::ViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(480, 360);
    setFocusPolicy(Qt::StrongFocus);
}

ViewportWidget::~ViewportWidget()
{
    // GL resources held by the renderer are released with the context.
    makeCurrent();
    doneCurrent();
}

void ViewportWidget::setStructure(std::shared_ptr<const core::Structure> structure,
                                  bool frameCamera)
{
    structure_ = std::move(structure);
    if (!selection_.empty()) {
        selection_.clear();
        Q_EMIT selectionChanged(0);
    }
    updateColorScalars();
    structureDirty_ = true;
    if (frameCamera)
        frameStructure();
    Q_EMIT structureReplaced();
    update();
}

void ViewportWidget::refreshStructure()
{
    structureDirty_ = true;
    update();
}

void ViewportWidget::clearSelection()
{
    if (selection_.empty())
        return;
    selection_.clear();
    Q_EMIT selectionChanged(0);
    structureDirty_ = true;
    update();
}

void ViewportWidget::setShowCell(bool show)
{
    renderer_.style().showCell = show;
    update();
}

void ViewportWidget::setRepresentation(render::RepresentationMode mode)
{
    renderer_.style().mode = mode;
    refreshStructure();
}

void ViewportWidget::setColorMode(render::ColorMode mode, const QString& customField)
{
    renderer_.style().colorMode = mode;
    customField_ = customField;
    updateColorScalars();
    refreshStructure();
}

void ViewportWidget::setColorGradient(render::ColorGradient gradient)
{
    renderer_.style().gradient = gradient;
    refreshStructure(); // scalars unchanged — only the palette differs
}

void ViewportWidget::setCoordinationOptions(const core::CoordinationOptions& options)
{
    coordinationOptions_ = options;
    const auto mode = renderer_.style().colorMode;
    if (mode == render::ColorMode::CoordinationNumber
        || mode == render::ColorMode::GeneralizedCoordination) {
        updateColorScalars();
        refreshStructure();
    }
}

void ViewportWidget::updateColorScalars()
{
    std::vector<float> scalars;
    if (structure_ && !structure_->empty()) {
        switch (renderer_.style().colorMode) {
        case render::ColorMode::Element:
            break;
        case render::ColorMode::CoordinationNumber: {
            const auto result = core::computeCoordination(*structure_, coordinationOptions_);
            scalars.assign(result.cn.begin(), result.cn.end());
            break;
        }
        case render::ColorMode::GeneralizedCoordination: {
            const auto result = core::computeCoordination(*structure_, coordinationOptions_);
            scalars.assign(result.gcn.begin(), result.gcn.end());
            break;
        }
        case render::ColorMode::CustomScalar: {
            const auto& fields = structure_->scalarFields();
            if (const auto it = fields.find(customField_.toStdString());
                it != fields.end())
                scalars.assign(it->second.begin(), it->second.end());
            break;
        }
        }
    }

    scalarRange_ = {};
    if (!scalars.empty()) {
        const auto [lo, hi] = std::minmax_element(scalars.begin(), scalars.end());
        scalarRange_ = {true, *lo, *hi};
    }
    renderer_.setAtomScalars(std::move(scalars));
    Q_EMIT colorMappingChanged();
}

void ViewportWidget::setBackgroundColor(const QColor& color)
{
    backgroundColor_ = color;
    update();
}

void ViewportWidget::setOrthographic(bool orthographic)
{
    camera_.setProjectionMode(orthographic ? render::CameraProjection::Orthographic
                                           : render::CameraProjection::Perspective);
    update();
}

void ViewportWidget::setShowAxes(bool show)
{
    showAxes_ = show;
    update();
}

void ViewportWidget::setAxesLatticeMode(bool lattice)
{
    axesLatticeMode_ = lattice;
    update();
}

void ViewportWidget::setAxesSize(int px)
{
    axesSizePx_ = std::clamp(px, 32, 512);
    update();
}

std::array<std::pair<QVector3D, QString>, 3> ViewportWidget::axesVectors() const
{
    if (axesLatticeMode_ && structure_ && structure_->cell().isDefined()) {
        const auto& v = structure_->cell().vectors();
        const auto toUnit = [](const core::Vec3& a) {
            const core::Vec3 n = a.normalized();
            return QVector3D(static_cast<float>(n.x), static_cast<float>(n.y),
                             static_cast<float>(n.z));
        };
        return {{{toUnit(v[0]), QStringLiteral("a1")},
                 {toUnit(v[1]), QStringLiteral("a2")},
                 {toUnit(v[2]), QStringLiteral("a3")}}};
    }
    return {{{{1, 0, 0}, QStringLiteral("X")},
             {{0, 1, 0}, QStringLiteral("Y")},
             {{0, 0, 1}, QStringLiteral("Z")}}};
}

void ViewportWidget::drawAxesOverlayGl()
{
    // Orientation-only rotation, orthographic corner viewport.
    const QMatrix4x4 rotation = camera_.rotationOnlyView();
    QMatrix4x4 proj;
    proj.ortho(-1.35f, 1.35f, -1.35f, 1.35f, -2.0f, 2.0f);

    const auto axes = axesVectors();
    const QVector3D colors[3] = {{0.94f, 0.35f, 0.32f},
                                 {0.36f, 0.83f, 0.40f},
                                 {0.35f, 0.58f, 0.98f}};
    std::vector<float> vertices;
    for (int i = 0; i < 3; ++i) {
        vertices.insert(vertices.end(), {0.0f, 0.0f, 0.0f, colors[i].x(),
                                         colors[i].y(), colors[i].z()});
        const QVector3D& a = axes[static_cast<std::size_t>(i)].first;
        vertices.insert(vertices.end(),
                        {a.x(), a.y(), a.z(), colors[i].x(), colors[i].y(),
                         colors[i].z()});
    }
    axesVbo_.bind();
    axesVbo_.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));

    const auto ratio = devicePixelRatioF();
    glViewport(static_cast<GLint>(kAxesMarginPx * ratio),
               static_cast<GLint>(kAxesMarginPx * ratio),
               static_cast<GLsizei>(axesSizePx_ * ratio),
               static_cast<GLsizei>(axesSizePx_ * ratio));
    glDisable(GL_DEPTH_TEST);
    axesProgram_.bind();
    axesProgram_.setUniformValue("uMvp", proj * rotation);
    axesVao_.bind();
    glDrawArrays(GL_LINES, 0, 6);
    axesVao_.release();
    axesProgram_.release();
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, static_cast<GLsizei>(width() * ratio),
               static_cast<GLsizei>(height() * ratio));
}

void ViewportWidget::drawAxesLabels(QPainter& painter)
{
    const QMatrix4x4 transform = [this] {
        QMatrix4x4 proj;
        proj.ortho(-1.35f, 1.35f, -1.35f, 1.35f, -2.0f, 2.0f);
        return proj * camera_.rotationOnlyView();
    }();

    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);

    const QColor colors[3] = {QColor(240, 90, 82), QColor(92, 212, 102),
                              QColor(90, 148, 250)};
    const auto axes = axesVectors();
    const QPointF boxOrigin(kAxesMarginPx, height() - kAxesMarginPx - axesSizePx_);
    for (int i = 0; i < 3; ++i) {
        const QVector3D tip =
            transform.map(axes[static_cast<std::size_t>(i)].first * 1.12f);
        const QPointF screen(boxOrigin.x() + (tip.x() * 0.5 + 0.5) * axesSizePx_,
                             boxOrigin.y() + (0.5 - tip.y() * 0.5) * axesSizePx_);
        painter.setPen(colors[i]);
        painter.drawText(screen, axes[static_cast<std::size_t>(i)].second);
    }
}

void ViewportWidget::styleChanged(bool rebuildGeometry)
{
    if (rebuildGeometry)
        refreshStructure();
    else
        update();
}

QImage ViewportWidget::renderToImage(int width, int height, const QColor& background,
                                     float extraYawDeg)
{
    makeCurrent();
    ensureUploaded();

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    format.setSamples(8);
    format.setInternalTextureFormat(GL_RGBA8);
    QOpenGLFramebufferObject fbo(width, height, format);
    fbo.bind();

    // Depth testing must be (re-)enabled explicitly here: the QPainter
    // overlay at the end of paintGL() resets GL state, and an offline FBO
    // capture inheriting that state draws bonds over atoms in submission
    // order (the GIF/MP4 z-ordering bug). Never rely on ambient state for
    // off-screen passes.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    glViewport(0, 0, width, height);
    glClearColor(static_cast<float>(background.redF()),
                 static_cast<float>(background.greenF()),
                 static_cast<float>(background.blueF()),
                 static_cast<float>(background.alphaF()));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    render::OrbitCamera camera = camera_; // copy: don't disturb the live view
    camera.rotate(extraYawDeg, 0.0f);
    renderer_.render(camera.view(),
                     camera.projection(static_cast<float>(width)
                                       / static_cast<float>(std::max(1, height))));

    fbo.release();
    // toImage() resolves multisampling and flips to Qt orientation.
    // No clear-color restore needed: paintGL() sets it every frame.
    QImage image = fbo.toImage().convertToFormat(QImage::Format_ARGB32);
    doneCurrent();
    return image;
}

void ViewportWidget::alignToPlane(int plane)
{
    // Orbit convention: yaw 0 / pitch 0 looks along -z (XY plane on
    // screen); pitch 90 looks along -y (XZ); yaw 90 looks along -x (YZ).
    switch (plane) {
    case 1:
        camera_.setOrientation(0.0f, 90.0f);
        break;
    case 2:
        camera_.setOrientation(90.0f, 0.0f);
        break;
    default:
        camera_.setOrientation(0.0f, 0.0f);
        break;
    }
    update();
}

void ViewportWidget::frameStructure()
{
    if (!structure_ || structure_->empty())
        return;
    const core::Vec3 center = structure_->centroid();
    const auto radius = static_cast<float>(structure_->boundingRadius(center));
    camera_.frame({static_cast<float>(center.x), static_cast<float>(center.y),
                   static_cast<float>(center.z)},
                  std::max(radius, 2.0f));
    update();
}

void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    renderer_.initialize(this);

    axesProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kAxesVertexShader);
    axesProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kAxesFragmentShader);
    axesProgram_.link();
    axesVao_.create();
    axesVao_.bind();
    axesVbo_.create();
    axesVbo_.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    axesVao_.release();

    structureDirty_ = true;
}

void ViewportWidget::resizeGL(int, int)
{
}

void ViewportWidget::ensureUploaded()
{
    if (structureDirty_) {
        renderer_.setStructure(structure_.get(), &selection_);
        structureDirty_ = false;
    }
}

void ViewportWidget::paintGL()
{
    ensureUploaded();

    // QPainter overlays reset pieces of GL state — reassert what we need.
    glEnable(GL_DEPTH_TEST);
    glClearColor(static_cast<float>(backgroundColor_.redF()),
                 static_cast<float>(backgroundColor_.greenF()),
                 static_cast<float>(backgroundColor_.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const float aspect = height() > 0
        ? static_cast<float>(width()) / static_cast<float>(height())
        : 1.0f;
    renderer_.render(camera_.view(), camera_.projection(aspect));

    if (showAxes_) {
        drawAxesOverlayGl();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        drawAxesLabels(painter);
    }
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    lastMousePos_ = event->position();
    pressPos_ = event->position();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF delta = event->position() - lastMousePos_;
    lastMousePos_ = event->position();

    const bool panning = event->buttons().testFlag(Qt::MiddleButton)
        || (event->buttons().testFlag(Qt::LeftButton)
            && event->modifiers().testFlag(Qt::ShiftModifier));

    if (panning) {
        camera_.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()), height());
    } else if (event->buttons().testFlag(Qt::LeftButton)) {
        camera_.rotate(static_cast<float>(delta.x()) * 0.4f,
                       static_cast<float>(delta.y()) * 0.4f);
    } else {
        return;
    }
    update();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF drag = event->position() - pressPos_;
    if (std::abs(drag.x()) + std::abs(drag.y()) > 4.0) // it was a drag, not a click
        return;
    if (event->modifiers().testFlag(Qt::ShiftModifier))
        return;

    const int picked = pickAtom(event->position());
    const bool toggle = event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::MetaModifier);

    if (picked < 0) {
        if (!toggle)
            selection_.clear();
    } else if (toggle) {
        if (!selection_.erase(picked))
            selection_.insert(picked);
    } else {
        selection_ = {picked};
    }

    Q_EMIT selectionChanged(static_cast<int>(selection_.size()));
    structureDirty_ = true;
    update();
}

void ViewportWidget::mouseDoubleClickEvent(QMouseEvent*)
{
    frameStructure();
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    camera_.zoom(static_cast<float>(event->angleDelta().y()) / 120.0f);
    update();
}

int ViewportWidget::pickAtom(const QPointF& screenPos) const
{
    if (!structure_ || structure_->empty() || height() <= 0)
        return -1;

    // Unproject the pixel to a world-space ray.
    const float aspect = static_cast<float>(width()) / static_cast<float>(height());
    bool invertible = false;
    const QMatrix4x4 inverse =
        (camera_.projection(aspect) * camera_.view()).inverted(&invertible);
    if (!invertible)
        return -1;

    const float ndcX = 2.0f * static_cast<float>(screenPos.x()) / width() - 1.0f;
    const float ndcY = 1.0f - 2.0f * static_cast<float>(screenPos.y()) / height();
    QVector4D nearPoint = inverse * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D farPoint = inverse * QVector4D(ndcX, ndcY, 1.0f, 1.0f);
    if (qFuzzyIsNull(nearPoint.w()) || qFuzzyIsNull(farPoint.w()))
        return -1;
    nearPoint /= nearPoint.w();
    farPoint /= farPoint.w();

    const QVector3D origin = nearPoint.toVector3D();
    const QVector3D direction = (farPoint - nearPoint).toVector3D().normalized();

    // Nearest ray-sphere intersection over all atoms.
    int best = -1;
    float bestT = std::numeric_limits<float>::max();
    const auto& atoms = structure_->atoms();
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        const QVector3D center(static_cast<float>(atoms[i].position.x),
                               static_cast<float>(atoms[i].position.y),
                               static_cast<float>(atoms[i].position.z));
        const float radius =
            render::StructureRenderer::displayRadius(atoms[i].atomicNumber, renderer_.style());

        const QVector3D oc = origin - center;
        const float b = QVector3D::dotProduct(direction, oc);
        const float c = QVector3D::dotProduct(oc, oc) - radius * radius;
        const float discriminant = b * b - c;
        if (discriminant < 0.0f)
            continue;
        const float sqrtDisc = std::sqrt(discriminant);
        float t = -b - sqrtDisc;
        if (t < 0.0f)
            t = -b + sqrtDisc; // camera inside the sphere
        if (t >= 0.0f && t < bestT) {
            bestT = t;
            best = static_cast<int>(i);
        }
    }
    return best;
}

} // namespace calango::gui
