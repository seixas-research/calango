#include "gui/ViewportWidget.hpp"

#include "core/Element.hpp"
#include "core/Structure.hpp"

#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QRubberBand>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace {

constexpr int kAxesMarginPx = 10; // logical pixels, bottom-left corner

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
    destroyDofTarget();
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
    // Measurement atom indices would dangle across a structure swap.
    measureAtoms_.clear();
    measurementLabel_.clear();
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

void ViewportWidget::setGradientInverted(bool inverted)
{
    renderer_.style().invertGradient = inverted;
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
    renderer_.style().fogColor = color; // fog fades into the background
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

void ViewportWidget::drawAxesOverlay(QPainter& painter)
{
    const QMatrix4x4 transform = [this] {
        QMatrix4x4 proj;
        proj.ortho(-1.35f, 1.35f, -1.35f, 1.35f, -2.0f, 2.0f);
        return proj * camera_.rotationOnlyView();
    }();

    const QPointF boxOrigin(kAxesMarginPx, height() - kAxesMarginPx - axesSizePx_);
    const auto toScreen = [&](const QVector3D& v) {
        const QVector3D mapped = transform.map(v);
        return QPointF(boxOrigin.x() + (mapped.x() * 0.5 + 0.5) * axesSizePx_,
                       boxOrigin.y() + (0.5 - mapped.y() * 0.5) * axesSizePx_);
    };

    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);

    const QColor colors[3] = {QColor(240, 90, 82), QColor(92, 212, 102),
                              QColor(90, 148, 250)};
    const auto axes = axesVectors();
    const QPointF origin = toScreen({0.0f, 0.0f, 0.0f});
    // 2.4 px logical strokes ≈ double the old 1-device-px GL lines and
    // stay crisp (properly scaled) on high-DPI displays.
    constexpr qreal kAxisStrokeWidth = 2.4;
    for (int i = 0; i < 3; ++i) {
        const QVector3D& axis = axes[static_cast<std::size_t>(i)].first;
        painter.setPen(QPen(colors[i], kAxisStrokeWidth, Qt::SolidLine,
                            Qt::RoundCap));
        painter.drawLine(origin, toScreen(axis));
        painter.setPen(colors[i]);
        painter.drawText(toScreen(axis * 1.12f),
                         axes[static_cast<std::size_t>(i)].second);
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

    // Intelligent auto-zoom. Periodic crystals: fit the whole unit-cell box
    // (its 8 corners) into ~90% of the view so the full lattice is visible.
    // Isolated molecules/clusters (no cell): size the structure to span
    // exactly 50% of the viewport's vertical height so it reads comfortably
    // rather than filling the frame edge-to-edge.
    if (structure_->cell().isDefined()) {
        const auto corners = structure_->cell().corners();
        core::Vec3 center{0.0, 0.0, 0.0};
        for (const auto& c : corners) {
            center.x += c.x;
            center.y += c.y;
            center.z += c.z;
        }
        center.x /= 8.0;
        center.y /= 8.0;
        center.z /= 8.0;
        double radius = 0.0;
        for (const auto& c : corners) {
            const double dx = c.x - center.x, dy = c.y - center.y,
                         dz = c.z - center.z;
            radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        // Guard against atoms that spill outside the drawn cell box.
        radius = std::max(radius, structure_->boundingRadius(center));
        camera_.frameToFraction(
            {static_cast<float>(center.x), static_cast<float>(center.y),
             static_cast<float>(center.z)},
            std::max(static_cast<float>(radius), 2.0f), 0.9f);
        update();
        return;
    }

    const core::Vec3 center = structure_->centroid();
    const auto radius = static_cast<float>(structure_->boundingRadius(center));
    camera_.frameToFraction({static_cast<float>(center.x),
                             static_cast<float>(center.y),
                             static_cast<float>(center.z)},
                            std::max(radius, 1.5f), 0.5f);
    update();
}

void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    renderer_.initialize(this);

    dofProgram_.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                        QStringLiteral(":/assets/shaders/dof.vert"));
    dofProgram_.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                        QStringLiteral(":/assets/shaders/dof.frag"));
    dofProgram_.link();
    dofVao_.create(); // empty — the fullscreen triangle comes from gl_VertexID

    structureDirty_ = true;
}

void ViewportWidget::ensureDofTarget(int w, int h)
{
    if (dofFbo_ && dofWidth_ == w && dofHeight_ == h)
        return;
    destroyDofTarget();
    dofWidth_ = w;
    dofHeight_ = h;

    glGenTextures(1, &dofColorTex_);
    glBindTexture(GL_TEXTURE_2D, dofColorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &dofDepthTex_);
    glBindTexture(GL_TEXTURE_2D, dofDepthTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &dofFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, dofFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           dofColorTex_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           dofDepthTex_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
}

void ViewportWidget::destroyDofTarget()
{
    if (dofFbo_) {
        glDeleteFramebuffers(1, &dofFbo_);
        glDeleteTextures(1, &dofColorTex_);
        glDeleteTextures(1, &dofDepthTex_);
        dofFbo_ = dofColorTex_ = dofDepthTex_ = 0;
    }
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

void ViewportWidget::renderScene()
{
    glEnable(GL_DEPTH_TEST);
    glClearColor(static_cast<float>(backgroundColor_.redF()),
                 static_cast<float>(backgroundColor_.greenF()),
                 static_cast<float>(backgroundColor_.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const float aspect = height() > 0
        ? static_cast<float>(width()) / static_cast<float>(height())
        : 1.0f;
    renderer_.render(camera_.view(), camera_.projection(aspect));
}

void ViewportWidget::paintGL()
{
    ensureUploaded();

    if (!dof_.enabled) {
        // QPainter overlays reset pieces of GL state — reassert then draw.
        renderScene();
    } else {
        // Depth-of-field: scene into an offscreen color+depth pair, then
        // a fullscreen composite with the circle-of-confusion blur.
        const qreal dpr = devicePixelRatioF();
        const int w = std::max(1, static_cast<int>(width() * dpr));
        const int h = std::max(1, static_cast<int>(height() * dpr));
        ensureDofTarget(w, h);

        glBindFramebuffer(GL_FRAMEBUFFER, dofFbo_);
        glViewport(0, 0, w, h);
        renderScene();

        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, w, h);
        glDisable(GL_DEPTH_TEST);
        dofProgram_.bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, dofColorTex_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, dofDepthTex_);
        dofProgram_.setUniformValue("uColor", 0);
        dofProgram_.setUniformValue("uDepth", 1);
        const float distance = camera_.distance();
        dofProgram_.setUniformValue("uNear", std::max(0.01f, distance * 0.01f));
        dofProgram_.setUniformValue("uFar", distance * 50.0f);
        dofProgram_.setUniformValue("uFocusDistance",
                                    distance + dof_.focusOffset);
        dofProgram_.setUniformValue("uFocusRange", dof_.focusRange);
        dofProgram_.setUniformValue("uStrength", dof_.strength * static_cast<float>(dpr));
        dofProgram_.setUniformValue(
            "uPixelSize", QVector2D(1.0f / w, 1.0f / h));
        dofVao_.bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        dofVao_.release();
        dofProgram_.release();
        glEnable(GL_DEPTH_TEST);
        glActiveTexture(GL_TEXTURE0);
    }

    if (showAxes_ || !measureAtoms_.empty()) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (showAxes_)
            drawAxesOverlay(painter);
        drawMeasurementOverlay(painter);
    }
}

void ViewportWidget::rotateSceneAxis(int axis, double degrees)
{
    static const QVector3D kAxes[3] = {{1.0f, 0.0f, 0.0f},
                                       {0.0f, 1.0f, 0.0f},
                                       {0.0f, 0.0f, 1.0f}};
    if (axis < 0 || axis > 2 || degrees == 0.0)
        return;

    // Incremental animation: each tick applies only the delta since the
    // previous one, so several in-flight animations compose exactly.
    auto* animation = new QVariantAnimation(this);
    animation->setStartValue(0.0);
    animation->setEndValue(degrees);
    animation->setDuration(200);
    animation->setEasingCurve(QEasingCurve::InOutQuad);
    auto applied = std::make_shared<double>(0.0);
    connect(animation, &QVariantAnimation::valueChanged, this,
            [this, axis, applied](const QVariant& value) {
                const double now = value.toDouble();
                camera_.rotateScene(kAxes[axis],
                                    static_cast<float>(now - *applied));
                *applied = now;
                update();
            });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ViewportWidget::setInteractionMode(InteractionMode mode)
{
    interactionMode_ = mode;
    insertDragFromAtom_ = -1;
    measureAtoms_.clear();
    measurementLabel_.clear();
    if (rubberBand_)
        rubberBand_->hide();
    // Cursor as a mode reminder: crosshair while placing/selecting.
    switch (mode) {
    case InteractionMode::Rotate:
        setCursor(Qt::ArrowCursor);
        break;
    case InteractionMode::Pan:
        setCursor(Qt::OpenHandCursor);
        break;
    case InteractionMode::Select:
    case InteractionMode::Insert:
    case InteractionMode::MeasureDistance:
    case InteractionMode::MeasureAngle:
        setCursor(Qt::CrossCursor);
        break;
    }
    update();
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    lastMousePos_ = event->position();
    pressPos_ = event->position();
    shiftDragAtom_ = -1;
    shiftDragBegan_ = false;

    if (event->button() != Qt::LeftButton)
        return;

    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        // Translation (Pan) mode: Shift+drag on an atom grabs it for a
        // single-atom move instead of panning. Anywhere else, Shift stays a
        // camera-pan override handled in mouseMoveEvent.
        if (interactionMode_ == InteractionMode::Pan && structure_) {
            const int atom = pickAtom(pressPos_);
            if (atom >= 0 && atom < static_cast<int>(structure_->size())) {
                shiftDragAtom_ = atom;
                shiftDragAtomStart_ =
                    structure_->atoms()[static_cast<std::size_t>(atom)].position;
                unprojectToPlane(pressPos_, shiftDragAtomStart_,
                                 shiftDragPlaneStart_);
            }
        }
        return;
    }

    if (interactionMode_ == InteractionMode::Select) {
        if (!rubberBand_)
            rubberBand_ = new QRubberBand(QRubberBand::Rectangle, this);
        rubberBand_->setGeometry(
            QRect(event->position().toPoint(), QSize()));
        rubberBand_->show();
    } else if (interactionMode_ == InteractionMode::Insert) {
        insertDragFromAtom_ = pickAtom(event->position());
    }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF delta = event->position() - lastMousePos_;
    lastMousePos_ = event->position();

    // Translation (Pan) mode Shift+drag on a grabbed atom: move only that atom,
    // following the cursor in the viewer-facing plane through its start depth.
    if (shiftDragAtom_ >= 0 && event->buttons().testFlag(Qt::LeftButton)) {
        core::Vec3 planeNow;
        if (unprojectToPlane(event->position(), shiftDragAtomStart_, planeNow)) {
            const core::Vec3 newPos{
                shiftDragAtomStart_.x + (planeNow.x - shiftDragPlaneStart_.x),
                shiftDragAtomStart_.y + (planeNow.y - shiftDragPlaneStart_.y),
                shiftDragAtomStart_.z + (planeNow.z - shiftDragPlaneStart_.z)};
            Q_EMIT atomTranslateRequested(shiftDragAtom_, newPos, !shiftDragBegan_);
            shiftDragBegan_ = true;
        }
        return; // consume the drag — do not pan the camera
    }

    // Middle-drag / Shift+left-drag pans in every mode (muscle memory).
    const bool forcePan = event->buttons().testFlag(Qt::MiddleButton)
        || (event->buttons().testFlag(Qt::LeftButton)
            && event->modifiers().testFlag(Qt::ShiftModifier));

    if (forcePan) {
        camera_.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()), height());
    } else if (event->buttons().testFlag(Qt::LeftButton)) {
        switch (interactionMode_) {
        case InteractionMode::Rotate:
        case InteractionMode::MeasureDistance:
        case InteractionMode::MeasureAngle:
            // Measure modes keep orbit-on-drag so the structure can be
            // turned between the measurement clicks.
            camera_.rotate(static_cast<float>(delta.x()) * 0.4f,
                           static_cast<float>(delta.y()) * 0.4f);
            break;
        case InteractionMode::Pan:
            camera_.pan(static_cast<float>(delta.x()),
                        static_cast<float>(delta.y()), height());
            break;
        case InteractionMode::Select:
            if (rubberBand_)
                rubberBand_->setGeometry(
                    QRect(pressPos_.toPoint(), event->position().toPoint())
                        .normalized());
            return; // no repaint needed — the band is a child widget
        case InteractionMode::Insert:
            return; // nothing to preview; release decides atom vs bond
        }
    } else {
        return;
    }
    update();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    // A single-atom Shift+drag already applied its moves live — just end it.
    if (shiftDragAtom_ >= 0) {
        shiftDragAtom_ = -1;
        shiftDragBegan_ = false;
        return;
    }
    const QPointF drag = event->position() - pressPos_;
    const bool wasClick = std::abs(drag.x()) + std::abs(drag.y()) <= 4.0;
    const bool toggle = event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::MetaModifier);

    // --- Select mode: rubber-band box selection ----------------------------
    if (interactionMode_ == InteractionMode::Select && rubberBand_
        && rubberBand_->isVisible()) {
        const QRectF rect = rubberBand_->geometry();
        rubberBand_->hide();
        if (!wasClick) {
            const std::set<int> boxed = atomsInRect(rect);
            if (toggle)
                selection_.insert(boxed.begin(), boxed.end());
            else
                selection_ = boxed;
            Q_EMIT selectionChanged(static_cast<int>(selection_.size()));
            structureDirty_ = true;
            update();
            return;
        }
        // A click in Select mode falls through to single-atom picking.
    }

    // --- Insert mode: place an atom / draw a bond --------------------------
    if (interactionMode_ == InteractionMode::Insert
        && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        const int from = insertDragFromAtom_;
        insertDragFromAtom_ = -1;
        if (wasClick) {
            if (pickAtom(event->position()) < 0) {
                core::Vec3 position;
                if (unprojectToTargetPlane(event->position(), position))
                    Q_EMIT atomInsertRequested(position);
                return;
            }
            // Clicking an existing atom picks it (fall through below).
        } else {
            const int to = pickAtom(event->position());
            if (from >= 0 && to >= 0 && from != to)
                Q_EMIT bondInsertRequested(from, to);
            return;
        }
    }

    if (!wasClick)
        return; // camera drag

    // Shift+click on an atom: substitute it (Insert mode) or append it to the
    // selection (Select mode). Other modes fall through to the pan early-out.
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        const int picked = pickAtom(event->position());
        if (picked >= 0 && interactionMode_ == InteractionMode::Insert) {
            Q_EMIT atomReplaceRequested(picked);
            return;
        }
        if (picked >= 0 && interactionMode_ == InteractionMode::Select) {
            selection_.insert(picked); // append without resetting the group
            Q_EMIT selectionChanged(static_cast<int>(selection_.size()));
            structureDirty_ = true;
            update();
            return;
        }
        return;
    }

    // --- Measurement modes: clicks accumulate atoms ------------------------
    if (interactionMode_ == InteractionMode::MeasureDistance
        || interactionMode_ == InteractionMode::MeasureAngle) {
        advanceMeasurement(pickAtom(event->position()));
        update();
        return;
    }

    const int picked = pickAtom(event->position());

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

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    // Select mode: Delete/Backspace removes the boxed/picked atoms. The
    // Edit-menu action already binds the Del key window-wide; this path
    // adds Backspace and works whenever the viewport has focus.
    if (interactionMode_ == InteractionMode::Select
        && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && !selection_.empty()) {
        Q_EMIT deleteSelectionRequested();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

bool ViewportWidget::screenRay(const QPointF& screenPos, QVector3D& origin,
                               QVector3D& direction) const
{
    if (height() <= 0)
        return false;
    const float aspect = static_cast<float>(width()) / static_cast<float>(height());
    bool invertible = false;
    const QMatrix4x4 inverse =
        (camera_.projection(aspect) * camera_.view()).inverted(&invertible);
    if (!invertible)
        return false;

    const float ndcX = 2.0f * static_cast<float>(screenPos.x()) / width() - 1.0f;
    const float ndcY = 1.0f - 2.0f * static_cast<float>(screenPos.y()) / height();
    QVector4D nearPoint = inverse * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D farPoint = inverse * QVector4D(ndcX, ndcY, 1.0f, 1.0f);
    if (qFuzzyIsNull(nearPoint.w()) || qFuzzyIsNull(farPoint.w()))
        return false;
    nearPoint /= nearPoint.w();
    farPoint /= farPoint.w();

    origin = nearPoint.toVector3D();
    direction = (farPoint - nearPoint).toVector3D().normalized();
    return true;
}

bool ViewportWidget::unprojectToTargetPlane(const QPointF& screenPos,
                                            core::Vec3& out) const
{
    QVector3D origin, direction;
    if (!screenRay(screenPos, origin, direction))
        return false;
    // Plane through the camera target, facing the viewer: what you click
    // is where the atom appears, at the depth the camera orbits around.
    const QVector3D normal =
        (camera_.target() - camera_.worldPosition()).normalized();
    const float denom = QVector3D::dotProduct(direction, normal);
    if (qFuzzyIsNull(denom))
        return false;
    const float t =
        QVector3D::dotProduct(camera_.target() - origin, normal) / denom;
    if (t < 0.0f)
        return false;
    const QVector3D hit = origin + direction * t;
    out = {static_cast<double>(hit.x()), static_cast<double>(hit.y()),
           static_cast<double>(hit.z())};
    return true;
}

bool ViewportWidget::unprojectToPlane(const QPointF& screenPos,
                                      const core::Vec3& planePoint,
                                      core::Vec3& out) const
{
    QVector3D origin, direction;
    if (!screenRay(screenPos, origin, direction))
        return false;
    const QVector3D plane(static_cast<float>(planePoint.x),
                          static_cast<float>(planePoint.y),
                          static_cast<float>(planePoint.z));
    const QVector3D normal =
        (camera_.target() - camera_.worldPosition()).normalized();
    const float denom = QVector3D::dotProduct(direction, normal);
    if (qFuzzyIsNull(denom))
        return false;
    const float t = QVector3D::dotProduct(plane - origin, normal) / denom;
    const QVector3D hit = origin + direction * t;
    out = {static_cast<double>(hit.x()), static_cast<double>(hit.y()),
           static_cast<double>(hit.z())};
    return true;
}

std::set<int> ViewportWidget::atomsInRect(const QRectF& rect) const
{
    std::set<int> hits;
    if (!structure_ || structure_->empty() || height() <= 0)
        return hits;
    const float aspect = static_cast<float>(width()) / static_cast<float>(height());
    const QMatrix4x4 mvp = camera_.projection(aspect) * camera_.view();

    const auto& atoms = structure_->atoms();
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        const auto& p = atoms[i].position;
        const QVector4D clip = mvp
            * QVector4D(static_cast<float>(p.x), static_cast<float>(p.y),
                        static_cast<float>(p.z), 1.0f);
        if (clip.w() <= 0.0f)
            continue; // behind the camera
        const float sx = (clip.x() / clip.w() * 0.5f + 0.5f) * width();
        const float sy = (0.5f - clip.y() / clip.w() * 0.5f) * height();
        if (rect.contains(QPointF(sx, sy)))
            hits.insert(static_cast<int>(i));
    }
    return hits;
}

bool ViewportWidget::projectAtomToScreen(int index, QPointF& out) const
{
    if (!structure_ || index < 0
        || static_cast<std::size_t>(index) >= structure_->size() || height() <= 0)
        return false;
    const float aspect = static_cast<float>(width()) / static_cast<float>(height());
    const QMatrix4x4 mvp = camera_.projection(aspect) * camera_.view();
    const auto& p = structure_->atoms()[static_cast<std::size_t>(index)].position;
    const QVector4D clip = mvp
        * QVector4D(static_cast<float>(p.x), static_cast<float>(p.y),
                    static_cast<float>(p.z), 1.0f);
    if (clip.w() <= 0.0f)
        return false;
    out = QPointF((clip.x() / clip.w() * 0.5f + 0.5f) * width(),
                  (0.5f - clip.y() / clip.w() * 0.5f) * height());
    return true;
}

void ViewportWidget::advanceMeasurement(int atom)
{
    if (atom < 0) { // empty space cancels the running measurement
        measureAtoms_.clear();
        measurementLabel_.clear();
        return;
    }
    const std::size_t needed =
        interactionMode_ == InteractionMode::MeasureDistance ? 2u : 3u;
    if (measureAtoms_.size() >= needed) { // completed — start a new one
        measureAtoms_.clear();
        measurementLabel_.clear();
    }
    if (!measureAtoms_.empty() && measureAtoms_.back() == atom)
        return; // same atom clicked twice
    measureAtoms_.push_back(atom);
    if (!structure_ || measureAtoms_.size() < needed)
        return;

    const auto& atoms = structure_->atoms();
    const auto tag = [&atoms](int i) {
        return QStringLiteral("%1(%2)")
            .arg(QLatin1String(core::Elements::data(
                     atoms[static_cast<std::size_t>(i)].atomicNumber)
                     .symbol))
            .arg(i);
    };

    if (interactionMode_ == InteractionMode::MeasureDistance) {
        const core::Vec3 d =
            atoms[static_cast<std::size_t>(measureAtoms_[1])].position
            - atoms[static_cast<std::size_t>(measureAtoms_[0])].position;
        measurementLabel_ = QStringLiteral("%1 Å").arg(d.norm(), 0, 'f', 3);
        Q_EMIT measurementMade(tr("Distance %1 – %2: %3 Å")
                                   .arg(tag(measureAtoms_[0]),
                                        tag(measureAtoms_[1]))
                                   .arg(d.norm(), 0, 'f', 3));
    } else {
        const auto& vertex =
            atoms[static_cast<std::size_t>(measureAtoms_[1])].position;
        const core::Vec3 u =
            atoms[static_cast<std::size_t>(measureAtoms_[0])].position - vertex;
        const core::Vec3 v =
            atoms[static_cast<std::size_t>(measureAtoms_[2])].position - vertex;
        const double norms = u.norm() * v.norm();
        if (norms < 1e-12)
            return;
        const double angle =
            std::acos(std::clamp(u.dot(v) / norms, -1.0, 1.0)) * 180.0 / M_PI;
        measurementLabel_ = QStringLiteral("%1°").arg(angle, 0, 'f', 2);
        Q_EMIT measurementMade(tr("Angle %1 – %2 – %3: %4°")
                                   .arg(tag(measureAtoms_[0]),
                                        tag(measureAtoms_[1]),
                                        tag(measureAtoms_[2]))
                                   .arg(angle, 0, 'f', 2));
    }
}

void ViewportWidget::drawMeasurementOverlay(QPainter& painter)
{
    if (measureAtoms_.empty() || !structure_)
        return;
    std::vector<QPointF> points;
    points.reserve(measureAtoms_.size());
    for (const int index : measureAtoms_) {
        QPointF p;
        if (!projectAtomToScreen(index, p))
            return; // an endpoint is behind the camera — skip this frame
        points.push_back(p);
    }

    const QColor accent(255, 199, 88);
    painter.setPen(QPen(accent, 2.0, Qt::DashLine, Qt::RoundCap));
    for (std::size_t i = 1; i < points.size(); ++i)
        painter.drawLine(points[i - 1], points[i]);

    painter.setPen(QPen(accent, 2.0));
    painter.setBrush(Qt::NoBrush);
    for (const QPointF& p : points)
        painter.drawEllipse(p, 7.0, 7.0);

    if (measurementLabel_.isEmpty())
        return;
    // Distance: label at the segment midpoint; angle: near the vertex.
    const QPointF anchor = points.size() == 2
        ? (points[0] + points[1]) / 2.0
        : points[1] + QPointF(0, -14);
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.15);
    painter.setFont(font);
    const QRectF text =
        QFontMetricsF(font).boundingRect(measurementLabel_).adjusted(-6, -3, 6, 3);
    QRectF box = text;
    box.moveCenter(anchor + QPointF(0, -16));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 22, 26, 200));
    painter.drawRoundedRect(box, 5, 5);
    painter.setPen(accent);
    painter.drawText(box, Qt::AlignCenter, measurementLabel_);
}

int ViewportWidget::pickAtom(const QPointF& screenPos) const
{
    if (!structure_ || structure_->empty() || height() <= 0)
        return -1;

    // Unproject the pixel to a world-space ray.
    QVector3D origin, direction;
    if (!screenRay(screenPos, origin, direction))
        return -1;

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
