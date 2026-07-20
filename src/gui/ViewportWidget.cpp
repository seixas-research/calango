#include "gui/ViewportWidget.hpp"

#include "core/Structure.hpp"

#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QWheelEvent>

#include <cmath>
#include <limits>

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
    structureDirty_ = true;
    if (frameCamera)
        frameStructure();
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

    glClearColor(static_cast<float>(backgroundColor_.redF()),
                 static_cast<float>(backgroundColor_.greenF()),
                 static_cast<float>(backgroundColor_.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const float aspect = height() > 0
        ? static_cast<float>(width()) / static_cast<float>(height())
        : 1.0f;
    renderer_.render(camera_.view(), camera_.projection(aspect));
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
