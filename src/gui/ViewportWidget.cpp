#include "gui/ViewportWidget.hpp"

#include "core/Structure.hpp"

#include <QMouseEvent>
#include <QWheelEvent>

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

void ViewportWidget::setStructure(std::shared_ptr<const core::Structure> structure)
{
    structure_ = std::move(structure);
    structureDirty_ = true;
    frameStructure();
    update();
}

void ViewportWidget::refreshStructure()
{
    structureDirty_ = true;
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
    structureDirty_ = true;
}

void ViewportWidget::resizeGL(int, int)
{
}

void ViewportWidget::paintGL()
{
    if (structureDirty_) {
        renderer_.setStructure(structure_.get());
        structureDirty_ = false;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const float aspect = height() > 0
        ? static_cast<float>(width()) / static_cast<float>(height())
        : 1.0f;
    renderer_.render(camera_.view(), camera_.projection(aspect));
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    lastMousePos_ = event->position();
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

void ViewportWidget::mouseDoubleClickEvent(QMouseEvent*)
{
    frameStructure();
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    camera_.zoom(static_cast<float>(event->angleDelta().y()) / 120.0f);
    update();
}

} // namespace calango::gui
