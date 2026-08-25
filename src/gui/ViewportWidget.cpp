#include "gui/ViewportWidget.hpp"

#include "core/Element.hpp"
#include "core/Structure.hpp"
#include "gui/ShortcutRegistry.hpp"

#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QRubberBand>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
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
    destroyPostTarget();
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
    // Cast assignments are per-atom-index, so they only survive a swap that
    // keeps the atom count — which is exactly the trajectory-playback case,
    // where the casts must NOT be reset between frames. A different atom count
    // means different atoms, and keeping the old indices would draw arbitrary
    // atoms in the adsorbate's cast.
    if (renderer_.style().atomCasts.size()
        != (structure_ ? structure_->size() : 0))
        renderer_.style().atomCasts.clear();
    updateColorScalars();
    structureDirty_ = true;
    // Contacts are a property of the geometry, so a new structure invalidates
    // them; recompute rather than leaving the previous frame's dashes hanging
    // in space (a no-op clear when detection is off).
    refreshHydrogenBonds();
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

void ViewportWidget::setShowCell(bool show)
{
    renderer_.style().showCell = show;
    update();
}

void ViewportWidget::setColorMode(render::ColorMode mode, const QString& customField)
{
    renderer_.style().colorMode = mode;
    customField_ = customField;
    updateColorScalars();
    refreshStructure();
}

void ViewportWidget::refreshColorScalars()
{
    updateColorScalars();
    refreshStructure();
}

void ViewportWidget::setPointOfView(const render::PointOfView& pov)
{
    if (!pov.valid)
        return;
    camera_.setPointOfView(pov);
    Q_EMIT cameraChanged();
    update();
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

void ViewportWidget::setCustomScalarRange(bool enabled, float min, float max)
{
    auto& style = renderer_.style();
    // The panel re-pushes the window on every color-mapping change; skip the
    // instance-buffer rebuild when nothing actually moved.
    if (style.useCustomScalarRange == enabled && style.customScalarMin == min
        && style.customScalarMax == max)
        return;
    style.useCustomScalarRange = enabled;
    style.customScalarMin = min;
    style.customScalarMax = max;
    refreshStructure(); // scalars unchanged — only their normalization differs
}

void ViewportWidget::refreshHydrogenBonds()
{
    hydrogenBondSegments_.clear();
    hbondCount_ = 0;
    if (hbondStyle_.enabled && structure_ && !structure_->empty()) {
        const auto contacts =
            core::detectHydrogenBonds(*structure_, hbondStyle_.options);
        hbondCount_ = static_cast<int>(contacts.size());
        // Only the H···A leg is drawn: D–H is already an ordinary covalent
        // bond, and drawing it again dashed would double every N–H and O–H.
        std::vector<std::pair<QVector3D, QVector3D>> segments;
        segments.reserve(contacts.size());
        for (const core::HydrogenBond& bond : contacts) {
            const core::Vec3& h =
                structure_->atoms()[static_cast<std::size_t>(bond.hydrogen)]
                    .position;
            const core::Vec3 a =
                structure_->atoms()[static_cast<std::size_t>(bond.acceptor)]
                    .position
                + bond.acceptorOffset;
            segments.emplace_back(
                QVector3D(static_cast<float>(h.x), static_cast<float>(h.y),
                          static_cast<float>(h.z)),
                QVector3D(static_cast<float>(a.x), static_cast<float>(a.y),
                          static_cast<float>(a.z)));
        }
        render::StructureRenderer::buildHydrogenBondDashes(
            segments, hbondStyle_.color, hbondStyle_.dashLength,
            hbondStyle_.lineStyle, hbondStyle_.lineWidth,
            hydrogenBondSegments_);
    }
    hydrogenBondsDirty_ = true;
    update();
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

void ViewportWidget::setStructuralPhaseOptions(
    const core::StructuralPhaseOptions& options)
{
    phaseOptions_ = options;
    // Only re-run when something is actually drawn from it: the analysis walks
    // every atom's neighbourhood, and no cast asking for Phase means the result
    // would be computed and immediately thrown away.
    for (int cast = 0; cast < renderer_.style().castCount(); ++cast) {
        if (renderer_.style().castStyle(cast).colorMode
            == render::ColorMode::Phase) {
            updateColorScalars();
            refreshStructure();
            return;
        }
    }
}

void ViewportWidget::setPhaseColor(core::StructuralPhase phase,
                                   const QColor& color)
{
    const auto index = static_cast<std::size_t>(phase);
    if (index >= renderer_.style().phaseColors.size())
        return;
    renderer_.style().phaseColors[index] = color;
    // Colours live in the instance buffer, so this is a rebuild rather than a
    // redraw — the same treatment cast colours get.
    refreshStructure();
}

std::array<int, core::kStructuralPhaseCount> ViewportWidget::phaseCounts() const
{
    std::array<int, core::kStructuralPhaseCount> counts{};
    for (const core::StructuralPhase phase : renderer_.style().atomPhases)
        ++counts[static_cast<std::size_t>(phase)];
    return counts;
}

void ViewportWidget::updateColorScalars()
{
    renderer_.clearAtomScalars();
    // A stale phase assignment would colour the NEW structure's atoms by the
    // OLD one's labels, which is worse than not colouring them: the picture
    // stays plausible. Cleared here and refilled below only if some cast still
    // asks for it, exactly like the scalar fields.
    renderer_.style().atomPhases.clear();
    scalarRange_ = {};
    if (!structure_ || structure_->empty()) {
        Q_EMIT colorMappingChanged();
        return;
    }

    // Every colour mode any CAST asks for needs its own field: a scene can hold
    // a coordination-coloured slab and a custom-property-coloured adsorbate at
    // the same time. Collected as a set so the (expensive) coordination
    // analysis runs once even when several casts share the mode.
    std::set<render::ColorMode> modes;
    const auto& style = renderer_.style();
    for (int cast = 0; cast < style.castCount(); ++cast)
        modes.insert(style.castStyle(cast).colorMode);

    // Coordination and generalized coordination come out of one analysis.
    const bool needsCoordination =
        modes.count(render::ColorMode::CoordinationNumber) > 0
        || modes.count(render::ColorMode::GeneralizedCoordination) > 0;
    if (needsCoordination) {
        const auto result =
            core::computeCoordination(*structure_, coordinationOptions_);
        if (modes.count(render::ColorMode::CoordinationNumber) > 0)
            renderer_.setAtomScalars(
                render::ColorMode::CoordinationNumber,
                std::vector<float>(result.cn.begin(), result.cn.end()));
        if (modes.count(render::ColorMode::GeneralizedCoordination) > 0)
            renderer_.setAtomScalars(
                render::ColorMode::GeneralizedCoordination,
                std::vector<float>(result.gcn.begin(), result.gcn.end()));
    }
    if (modes.count(render::ColorMode::CustomScalar) > 0) {
        const auto& fields = structure_->scalarFields();
        if (const auto it = fields.find(customField_.toStdString());
            it != fields.end())
            renderer_.setAtomScalars(
                render::ColorMode::CustomScalar,
                std::vector<float>(it->second.begin(), it->second.end()));
    }

    // Local structural phase. Not a scalar field — the labels are nominal — so
    // it lands in the style's own per-atom vector rather than in the scalar
    // map, and like the coordination analysis it runs once however many casts
    // ask for it.
    if (modes.count(render::ColorMode::Phase) > 0) {
        renderer_.style().atomPhases =
            core::identifyStructuralPhases(*structure_, phaseOptions_).phases;
    }

    // The legend describes ONE mapping, so it reports cast 0's — the cast that
    // exists in every scene and that a single-cast scene is entirely made of.
    const auto range = renderer_.scalarRangeFor(style.colorMode);
    scalarRange_ = {range.valid, range.min, range.max};
    Q_EMIT colorMappingChanged();
}

void ViewportWidget::setTrajectory(
    std::vector<std::shared_ptr<const core::Structure>> frames)
{
    // A live run appends to the list it already pushed, and re-scanning every
    // frame each time would make the colour scale quadratic in the run length.
    // Same first frame and no shrink == an append, so the cache survives and
    // trajectoryScalarRange() merges only what is new.
    const bool appended = !trajectory_.empty() && !frames.empty()
        && frames.size() >= trajectory_.size()
        && frames.front() == trajectory_.front();
    trajectory_ = std::move(frames);
    if (!appended) {
        rangeCacheField_.clear();
        rangeCacheFrames_ = 0;
        rangeCache_ = {};
    }
}

ViewportWidget::ScalarRange
ViewportWidget::trajectoryScalarRange(const QString& field) const
{
    if (trajectory_.size() < 2 || field.isEmpty())
        return {};
    if (rangeCacheField_ != field) {
        rangeCacheField_ = field;
        rangeCacheFrames_ = 0;
        rangeCache_ = {};
    }
    const std::string name = field.toStdString();
    for (std::size_t i = rangeCacheFrames_; i < trajectory_.size(); ++i) {
        const auto& frame = trajectory_[i];
        if (!frame)
            continue;
        const auto& fields = frame->scalarFields();
        const auto it = fields.find(name);
        if (it == fields.end())
            continue; // a frame that simply does not carry this column
        for (const double value : it->second) {
            const auto v = static_cast<float>(value);
            if (!rangeCache_.valid) {
                rangeCache_ = {true, v, v};
                continue;
            }
            rangeCache_.min = std::min(rangeCache_.min, v);
            rangeCache_.max = std::max(rangeCache_.max, v);
        }
    }
    rangeCacheFrames_ = trajectory_.size();
    return rangeCache_;
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
    Q_EMIT cameraChanged();
    update();
}

void ViewportWidget::setShowAxes(bool show)
{
    showAxes_ = show;
    update();
}

void ViewportWidget::setShowAxesArrows(bool show)
{
    if (axesArrows_ == show)
        return;
    axesArrows_ = show;
    update(); // overlay-only: no GPU buffers to rebuild
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

void ViewportWidget::setShowElementLabels(bool show)
{
    if (showElementLabels_ == show)
        return;
    showElementLabels_ = show;
    update(); // overlay-only: no GPU buffers to rebuild
}

void ViewportWidget::setShowAtomIndexLabels(bool show)
{
    if (showIndexLabels_ == show)
        return;
    showIndexLabels_ = show;
    update();
}

void ViewportWidget::setShowCoordinationLabels(bool show)
{
    if (showCoordinationLabels_ == show)
        return;
    showCoordinationLabels_ = show;
    update(); // overlay-only: the scalars are already on the renderer
}

void ViewportWidget::drawAtomLabelsOverlay(QPainter& painter)
{
    if (!structure_ || structure_->empty())
        return;
    const auto& atoms = structure_->atoms();
    // Per-atom text is only legible for modestly sized systems; past this the
    // labels overlap into an unreadable smear and cost a QPainter text layout
    // per atom every frame. Skip rather than choke on large structures.
    constexpr std::size_t kMaxLabelledAtoms = 600;
    if (atoms.size() > kMaxLabelledAtoms)
        return;

    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    const QFontMetricsF metrics(font);

    // High-contrast text tinted to the background so labels read on either a
    // light or a dark canvas, with a translucent pill behind each for legibility
    // over busy geometry.
    const bool darkBg = backgroundColor_.lightnessF() < 0.5;
    const QColor textColor = darkBg ? QColor(238, 240, 244) : QColor(24, 26, 30);
    const QColor pillColor = darkBg ? QColor(20, 22, 26, 170)
                                    : QColor(245, 246, 248, 190);

    // Scalar overlay ("Show CN / GCN values"): each atom's value of whatever
    // its OWN cast is coloured by, since a scene can hold a coordination-
    // coloured slab beside a custom-property-coloured adsorbate and the two
    // fields are unrelated. Resolved once here rather than per atom.
    std::vector<render::StructureRenderer::CastStyle> casts;
    if (showCoordinationLabels_) {
        casts = render::StructureRenderer::atomCastStyles(structure_.get(),
                                                          renderer_.style());
    }
    const auto scalarLabel = [this, &casts](std::size_t index) -> QString {
        if (!showCoordinationLabels_ || index >= casts.size())
            return {};
        const render::ColorMode mode = casts[index].colorMode;
        const std::vector<float>* values = renderer_.atomScalars(mode);
        if (!values || index >= values->size())
            return {};
        const double value = static_cast<double>((*values)[index]);
        // A coordination NUMBER is a count and reads as one; the generalized
        // coordination number and any custom property are genuinely fractional,
        // and rounding them would erase the distinction the overlay exists to
        // show.
        return mode == render::ColorMode::CoordinationNumber
            ? QString::number(value, 'f', 0)
            : QString::number(value, 'f', 2);
    };

    for (std::size_t i = 0; i < atoms.size(); ++i) {
        if (!renderer_.style().showHydrogens && atoms[i].atomicNumber == 1)
            continue; // no sphere under it to label
        QPointF center;
        if (!projectAtomToScreen(static_cast<int>(i), center))
            continue; // behind the camera

        QString label;
        if (showElementLabels_)
            label = QLatin1String(atoms[i].symbol());
        if (showIndexLabels_) {
            // 1-based, matching the atom numbering used in tables and the
            // Bond Editor; prefixed so it never reads as an element symbol.
            const QString idx = QStringLiteral("#%1").arg(i + 1);
            label = label.isEmpty() ? idx : label + QLatin1Char(' ') + idx;
        }
        if (const QString scalar = scalarLabel(i); !scalar.isEmpty())
            label = label.isEmpty() ? scalar : label + QLatin1Char(' ') + scalar;
        if (label.isEmpty())
            continue;

        QRectF pill = metrics.boundingRect(label).adjusted(-4, -2, 4, 2);
        pill.moveCenter(center);
        painter.setPen(Qt::NoPen);
        painter.setBrush(pillColor);
        painter.drawRoundedRect(pill, 4, 4);
        painter.setPen(textColor);
        painter.drawText(pill, Qt::AlignCenter, label);
    }
}

bool ViewportWidget::textOverlayRect(const TextOverlay& overlay,
                                     QRectF& out) const
{
    if (!overlay.visible || overlay.text.isEmpty())
        return false;
    QPointF anchor;
    if (!projectToScreen(overlay.position, anchor))
        return false;
    // Qt::TextExpandTabs|Qt::AlignCenter over the multi-line flag: the text
    // may be several lines, and boundingRect(QString) alone measures only the
    // first, which left a two-line label hanging out of its own plate.
    const QFontMetricsF metrics(overlay.font);
    out = metrics.boundingRect(QRectF(0, 0, 0, 0),
                               Qt::AlignCenter | Qt::TextWordWrap,
                               overlay.text)
              .adjusted(-5, -3, 5, 3);
    out.moveCenter(anchor);
    return true;
}

void ViewportWidget::drawTextOverlays(QPainter& painter)
{
    // Plate colour and both alphas come from the overlay itself now — the
    // viewport no longer picks a pill shade from the canvas brightness, because
    // the user can (and on a figure with a white background, must) choose it.
    const auto withAlpha = [](QColor color, double alpha) {
        color.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
        return color;
    };
    for (std::size_t i = 0; i < textOverlays_.size(); ++i) {
        const TextOverlay& overlay = textOverlays_[i];
        QRectF box;
        if (!textOverlayRect(overlay, box))
            continue;
        const double opacity = std::clamp(overlay.opacity, 0.0, 1.0);
        painter.setFont(overlay.font);
        const double plateAlpha =
            std::clamp(overlay.backgroundOpacity, 0.0, 1.0) * opacity;
        if (plateAlpha > 0.0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(withAlpha(overlay.backgroundColor, plateAlpha));
            painter.drawRoundedRect(box, 4, 4);
        }
        // The one being dragged gets an outline, so it is obvious which label
        // the cursor has hold of when several overlap. Drawn at full strength
        // whatever the overlay opacity: it is interaction feedback, not part of
        // the annotation.
        if (static_cast<int>(i) == draggedTextOverlay_) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(overlay.color, 1.5));
            painter.drawRoundedRect(box, 4, 4);
        }
        painter.setPen(withAlpha(overlay.color, overlay.color.alphaF()
                                     * opacity));
        painter.drawText(box, Qt::AlignCenter | Qt::TextWordWrap, overlay.text);
    }
}

void ViewportWidget::drawSelectionInfoOverlay(QPainter& painter)
{
    if (!structure_ || selection_.empty())
        return;
    const auto& atoms = structure_->atoms();

    // Two genuinely different read-outs, because two different questions are
    // being asked. One atom selected is "what and where is this?" — identity
    // and coordinates. Several selected is "what did I just grab?" — a
    // composition tally, since listing 200 sets of coordinates answers nothing.
    QStringList lines;
    if (selection_.size() == 1) {
        const int index = *selection_.begin();
        if (index < 0 || static_cast<std::size_t>(index) >= atoms.size())
            return;
        const core::Atom& atom = atoms[static_cast<std::size_t>(index)];
        lines << tr("%1  ·  atom #%2")
                     .arg(QLatin1String(atom.symbol()))
                     .arg(index + 1);
        lines << tr("xyz   %1, %2, %3 Å")
                     .arg(atom.position.x, 0, 'f', 3)
                     .arg(atom.position.y, 0, 'f', 3)
                     .arg(atom.position.z, 0, 'f', 3);
        // Fractional coordinates only exist with a lattice to be a fraction
        // OF; for an isolated molecule the row is omitted rather than filled
        // with zeros that would read as a position.
        if (structure_->cell().isDefined()) {
            const core::Vec3 f =
                structure_->cell().cartesianToFractional(atom.position);
            lines << tr("uvw   %1, %2, %3")
                         .arg(f.x, 0, 'f', 4)
                         .arg(f.y, 0, 'f', 4)
                         .arg(f.z, 0, 'f', 4);
        }
    } else {
        std::map<QString, int> counts;
        for (const int index : selection_) {
            if (index < 0 || static_cast<std::size_t>(index) >= atoms.size())
                continue;
            ++counts[QLatin1String(atoms[static_cast<std::size_t>(index)].symbol())];
        }
        if (counts.empty())
            return;
        lines << tr("%n atom(s) selected", nullptr,
                    static_cast<int>(selection_.size()));
        QStringList tally;
        for (const auto& [symbol, count] : counts)
            tally << QStringLiteral("%1 %2").arg(symbol).arg(count);
        lines << tally.join(QStringLiteral("   "));
    }

    QFont font = painter.font();
    font.setBold(true);
    const QFontMetricsF metrics(font);
    painter.setFont(font);

    const bool darkBg = backgroundColor_.lightnessF() < 0.5;
    const QColor textColor = darkBg ? QColor(238, 240, 244) : QColor(24, 26, 30);
    const QColor pillColor = darkBg ? QColor(20, 22, 26, 190)
                                    : QColor(245, 246, 248, 205);

    double widest = 0.0;
    for (const QString& line : lines)
        widest = std::max(widest, metrics.horizontalAdvance(line));
    const double lineHeight = metrics.height();
    // Bottom-left: out of the way of the axes triad (bottom-left corner is the
    // triad's own home, so this sits just above it) and of the measurement
    // read-out, which uses the top of the canvas.
    const double margin = 10.0;
    const double boxHeight = lineHeight * lines.size() + 10.0;
    QRectF box(margin, height() - margin - boxHeight - axesSizePx_,
               widest + 16.0, boxHeight);

    painter.setPen(Qt::NoPen);
    painter.setBrush(pillColor);
    painter.drawRoundedRect(box, 5, 5);
    painter.setPen(textColor);
    double y = box.top() + 5.0;
    for (const QString& line : lines) {
        painter.drawText(QRectF(box.left() + 8.0, y, box.width() - 16.0,
                                lineHeight),
                         Qt::AlignLeft | Qt::AlignVCenter, line);
        y += lineHeight;
    }
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
    // Arrowhead size scales with the triad so it stays proportional as the
    // user resizes it; ~9% of the triad box is small enough not to swallow
    // short (foreshortened) axes.
    const qreal headLength = axesSizePx_ * 0.09;
    const qreal headHalfWidth = headLength * 0.42;

    for (int i = 0; i < 3; ++i) {
        const QVector3D& axis = axes[static_cast<std::size_t>(i)].first;
        const QPointF tip = toScreen(axis);
        painter.setPen(QPen(colors[i], kAxisStrokeWidth, Qt::SolidLine,
                            Qt::RoundCap));
        painter.drawLine(origin, tip);

        if (axesArrows_) {
            // Head is built in the axis's *projected* 2D direction, so it
            // stays aligned with the drawn segment under any camera
            // orientation. An axis pointing nearly at the viewer projects to
            // (almost) a point — skip it rather than draw a head with an
            // undefined direction.
            const QPointF along = tip - origin;
            const qreal length = std::hypot(along.x(), along.y());
            if (length > headLength) {
                const QPointF unit = along / length;
                const QPointF normal(-unit.y(), unit.x());
                const QPointF base = tip - unit * headLength;
                const QPolygonF head({tip, base + normal * headHalfWidth,
                                      base - normal * headHalfWidth});
                painter.setPen(Qt::NoPen);
                painter.setBrush(colors[i]);
                painter.drawPolygon(head);
                painter.setBrush(Qt::NoBrush);
            }
        }

        painter.setPen(colors[i]);
        // Push the label past the arrowhead so the two never overlap.
        painter.drawText(toScreen(axis * (axesArrows_ ? 1.22f : 1.12f)),
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
    // Orbit convention, as measured from the view matrix (tests/CameraTest):
    //     XY (yaw 0,  pitch 0)    looks along -z,  up +y,  right +x
    //     XZ (yaw 0,  pitch -90)  looks along +y,  up +z,  right +x
    //     YZ (yaw 90, pitch 0)    looks along +x,  up +y,  right +z
    //
    // Every alignment sets roll explicitly to zero. "Aligned with a plane" is a
    // statement about the screen axes as well as the view direction, and an
    // alignment that inherited the previous view's tilt would not be one.
    switch (plane) {
    case 1:
        // Pitch -90, not +90: this looks at the XZ plane from BELOW (along +y),
        // which puts +x to the right and +z up on screen — the orientation the
        // XZ toolbar icon draws.
        camera_.setOrientation(0.0f, -90.0f, 0.0f);
        Q_EMIT cameraChanged();
        break;
    case 2:
        camera_.setOrientation(90.0f, 0.0f, 0.0f);
        Q_EMIT cameraChanged();
        break;
    default:
        camera_.setOrientation(0.0f, 0.0f, 0.0f);
        Q_EMIT cameraChanged();
        break;
    }
    update();
}

void ViewportWidget::frameStructure()
{
    if (!structure_ || structure_->empty())
        return;

    // Reset Camera fully restores the default view: clear any accumulated
    // orbit/scene rotation (from dragging or the X/Y/Z buttons) before
    // re-centering and re-zooming, so the structure returns to its initial
    // orientation and bounding-box fit rather than staying tilted.
    camera_.resetOrientation();
    Q_EMIT cameraChanged();

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
        Q_EMIT cameraChanged();
        update();
        return;
    }

    const core::Vec3 center = structure_->centroid();
    const auto radius = static_cast<float>(structure_->boundingRadius(center));
    camera_.frameToFraction({static_cast<float>(center.x),
                             static_cast<float>(center.y),
                             static_cast<float>(center.z)},
                            std::max(radius, 1.5f), 0.5f);
    Q_EMIT cameraChanged();
    update();
}

void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    renderer_.initialize(this);

    initializePostProcessing();

    structureDirty_ = true;
}

void ViewportWidget::initializePostProcessing()
{
    // All four post passes draw the same fullscreen triangle, so they share
    // dof.vert (and the empty VAO below — the triangle comes from gl_VertexID).
    const auto link = [](QOpenGLShaderProgram& program, const char* fragment) {
        program.addShaderFromSourceFile(
            QOpenGLShader::Vertex, QStringLiteral(":/assets/shaders/dof.vert"));
        program.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                        QLatin1String(fragment));
        program.link();
    };
    link(dofProgram_, ":/assets/shaders/dof.frag");
    link(ssaoProgram_, ":/assets/shaders/ssao.frag");
    link(ssaoBlurProgram_, ":/assets/shaders/ssao_blur.frag");
    link(ssaoCompositeProgram_, ":/assets/shaders/ssao_composite.frag");
    dofVao_.create();

    // -- SSAO sample kernel -------------------------------------------------
    // Points in the +z hemisphere, pushed toward the origin: occlusion falls
    // off with distance, so clustering samples near the shaded point spends
    // the budget where it changes the result most. A fixed seed keeps the
    // shading identical across runs — an AO pattern that shifted between
    // sessions would look like a rendering bug in a saved figure.
    constexpr int kKernelSize = kMaxSsaoSamples;
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> signed01(-1.0f, 1.0f);
    ssaoKernel_.clear();
    ssaoKernel_.reserve(kKernelSize);
    for (int i = 0; i < kKernelSize; ++i) {
        QVector3D sample(signed01(rng), signed01(rng), unit(rng));
        sample.normalize();
        sample *= unit(rng);
        float scale = static_cast<float>(i) / kKernelSize;
        scale = 0.1f + 0.9f * scale * scale; // accelerate toward the origin
        ssaoKernel_.push_back(sample * scale);
    }

    // -- Rotation noise -----------------------------------------------------
    // A 4x4 tile of random in-plane rotations, repeated across the screen. It
    // converts the banding a fixed kernel would produce into high-frequency
    // noise, which the blur pass (matched to this tile size) removes.
    constexpr int kNoiseSide = 4;
    std::vector<float> noise;
    noise.reserve(kNoiseSide * kNoiseSide * 3);
    for (int i = 0; i < kNoiseSide * kNoiseSide; ++i) {
        noise.push_back(signed01(rng));
        noise.push_back(signed01(rng));
        noise.push_back(0.0f); // rotation about the surface normal only
    }
    glGenTextures(1, &ssaoNoiseTex_);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, kNoiseSide, kNoiseSide, 0, GL_RGB,
                 GL_FLOAT, noise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ViewportWidget::ensurePostTarget(int w, int h)
{
    if (postFbo_ && postWidth_ == w && postHeight_ == h)
        return;
    destroyPostTarget();
    postWidth_ = w;
    postHeight_ = h;

    const auto makeColorTexture = [this, w, h](unsigned& tex, GLint internal,
                                               GLenum format, GLenum type) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };

    // -- G-buffer: shaded color + view-space normals + depth ---------------
    makeColorTexture(postColorTex_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    // Normals need signed values and more than 8 bits per channel, or the
    // reconstructed hemisphere basis visibly quantizes into facets.
    makeColorTexture(postNormalTex_, GL_RGBA16F, GL_RGBA, GL_FLOAT);

    glGenTextures(1, &postDepthTex_);
    glBindTexture(GL_TEXTURE_2D, postDepthTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &postFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, postFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           postColorTex_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           postNormalTex_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           postDepthTex_, 0);
    // Both attachments must be listed or the scene shaders' second output is
    // discarded and the SSAO pass reads an untouched normal buffer.
    const GLenum drawBuffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, drawBuffers);

    // -- AO ping-pong + composited color -----------------------------------
    const auto makeAoTarget = [&](unsigned& fbo, unsigned& tex) {
        makeColorTexture(tex, GL_R16F, GL_RED, GL_FLOAT);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);
        const GLenum one[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, one);
    };
    makeAoTarget(ssaoFbo_, ssaoTex_);
    makeAoTarget(ssaoBlurFbo_, ssaoBlurTex_);

    makeColorTexture(ssaoCompositeTex_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    glGenFramebuffers(1, &ssaoCompositeFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoCompositeFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           ssaoCompositeTex_, 0);
    const GLenum one[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, one);

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
}

void ViewportWidget::destroyPostTarget()
{
    if (!postFbo_)
        return;
    const unsigned framebuffers[4] = {postFbo_, ssaoFbo_, ssaoBlurFbo_,
                                      ssaoCompositeFbo_};
    glDeleteFramebuffers(4, framebuffers);
    const unsigned textures[6] = {postColorTex_,   postNormalTex_,
                                  postDepthTex_,   ssaoTex_,
                                  ssaoBlurTex_,    ssaoCompositeTex_};
    glDeleteTextures(6, textures);
    postFbo_ = ssaoFbo_ = ssaoBlurFbo_ = ssaoCompositeFbo_ = 0;
    postColorTex_ = postNormalTex_ = postDepthTex_ = 0;
    ssaoTex_ = ssaoBlurTex_ = ssaoCompositeTex_ = 0;
}

void ViewportWidget::drawFullscreenTriangle()
{
    dofVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    dofVao_.release();
}

void ViewportWidget::renderSsaoPasses(int w, int h, const QMatrix4x4& projection)
{
    glDisable(GL_DEPTH_TEST);

    // -- Occlusion ----------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFbo_);
    glViewport(0, 0, w, h);
    ssaoProgram_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, postDepthTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, postNormalTex_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex_);
    ssaoProgram_.setUniformValue("uDepth", 0);
    ssaoProgram_.setUniformValue("uNormal", 1);
    ssaoProgram_.setUniformValue("uNoise", 2);
    ssaoProgram_.setUniformValue("uProjection", projection);
    ssaoProgram_.setUniformValue("uInvProjection", projection.inverted());
    ssaoProgram_.setUniformValueArray("uKernel", ssaoKernel_.data(),
                                      static_cast<int>(ssaoKernel_.size()));
    // The full kernel is uploaded once; uKernelSize decides how much of it the
    // shader walks, so the sample count is a live control with no re-upload.
    ssaoProgram_.setUniformValue(
        "uKernelSize",
        std::clamp(ssao_.samples, 1, static_cast<int>(ssaoKernel_.size())));
    ssaoProgram_.setUniformValue("uRadius", ssao_.radius);
    // The bias scales with the radius: a fixed epsilon that is invisible at
    // 2 Å becomes self-occlusion acne at 0.2 Å.
    ssaoProgram_.setUniformValue("uBias", 0.02f * ssao_.radius);
    // Framebuffer size over the 4x4 noise texture, times the user's scale.
    const float noiseTile = 4.0f / std::max(ssao_.noiseScale, 0.05f);
    ssaoProgram_.setUniformValue(
        "uNoiseScale", QVector2D(static_cast<float>(w) / noiseTile,
                                 static_cast<float>(h) / noiseTile));
    drawFullscreenTriangle();
    ssaoProgram_.release();

    // -- Bilateral blur -----------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFbo_);
    ssaoBlurProgram_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssaoTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, postDepthTex_);
    ssaoBlurProgram_.setUniformValue("uAo", 0);
    ssaoBlurProgram_.setUniformValue("uDepth", 1);
    ssaoBlurProgram_.setUniformValue(
        "uPixelSize", QVector2D(1.0f / static_cast<float>(w),
                                1.0f / static_cast<float>(h)));
    ssaoBlurProgram_.setUniformValue("uRadius", 2); // covers the 4x4 noise tile
    ssaoBlurProgram_.setUniformValue("uDepthSigma", 0.0005f);
    drawFullscreenTriangle();
    ssaoBlurProgram_.release();

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
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
    if (latticePlaneDirty_) {
        renderer_.setLatticePlane(latticePlaneFaces_, latticePlaneEdges_,
                                  latticePlaneAlpha_, latticePlaneVisible_,
                                  latticePlaneEdgesOn_);
        latticePlaneDirty_ = false;
    }
    if (customOverlayDirty_) {
        renderer_.setCustomOverlay(customOverlayFaces_, customOverlayEdges_,
                                   customOverlayRanges_, customOverlayVisible_,
                                   customOverlayEdgeAlpha_);
        customOverlayDirty_ = false;
    }
    if (managedOverlayDirty_) {
        renderer_.setManagedOverlay(managedOverlayFaces_, managedOverlayEdges_,
                                    managedOverlayRanges_,
                                    managedOverlayVisible_);
        managedOverlayDirty_ = false;
    }
    if (hydrogenBondsDirty_) {
        renderer_.setHydrogenBonds(hydrogenBondSegments_);
        hydrogenBondsDirty_ = false;
    }
}

void ViewportWidget::setManagedOverlay(
    std::vector<float> faceTris, std::vector<float> edgeLines,
    std::vector<render::StructureRenderer::OverlayRange> faceRanges, bool visible)
{
    managedOverlayFaces_ = std::move(faceTris);
    managedOverlayEdges_ = std::move(edgeLines);
    managedOverlayRanges_ = std::move(faceRanges);
    managedOverlayVisible_ = visible;
    managedOverlayDirty_ = true;
    update();
}

void ViewportWidget::clearManagedOverlay()
{
    managedOverlayFaces_.clear();
    managedOverlayEdges_.clear();
    managedOverlayRanges_.clear();
    managedOverlayVisible_ = false;
    managedOverlayDirty_ = true;
    update();
}

void ViewportWidget::setTextOverlays(std::vector<TextOverlay> overlays)
{
    textOverlays_ = std::move(overlays);
    update(); // painter-only: no GPU buffers to rebuild
}

void ViewportWidget::setCustomOverlay(
    std::vector<float> faceTris, std::vector<float> edgeLines,
    std::vector<render::StructureRenderer::OverlayRange> faceRanges,
    bool visible, float edgeAlpha)
{
    customOverlayFaces_ = std::move(faceTris);
    customOverlayEdges_ = std::move(edgeLines);
    customOverlayRanges_ = std::move(faceRanges);
    customOverlayVisible_ = visible;
    customOverlayEdgeAlpha_ = edgeAlpha;
    customOverlayDirty_ = true;
    update();
}

void ViewportWidget::setVolumeField(int nx, int ny, int nz,
                                    const std::vector<float>& values,
                                    const std::vector<float>& transfer,
                                    const QMatrix4x4& boxTransform)
{
    // makeCurrent, because the upload allocates GL textures and the caller is
    // a panel reacting to a UI event, not a paint. Every other upload in this
    // widget is staged and flushed in ensureUploaded(); a 3D texture is big
    // enough that staging a second copy of it in RAM is the worse trade.
    makeCurrent();
    renderer_.setVolumeField(nx, ny, nz, values, transfer, boxTransform);
    doneCurrent();
    update();
}

void ViewportWidget::clearVolumeField()
{
    renderer_.clearVolumeField();
    update();
}

void ViewportWidget::setVolumeParams(int steps, float density, float isoLevel,
                                     bool lit)
{
    renderer_.setVolumeParams(steps, density, isoLevel, lit);
    update();
}

void ViewportWidget::clearCustomOverlay()
{
    customOverlayFaces_.clear();
    customOverlayEdges_.clear();
    customOverlayRanges_.clear();
    customOverlayVisible_ = false;
    customOverlayEdgeAlpha_ = 1.0f;
    customOverlayDirty_ = true;
    update();
}

void ViewportWidget::setLatticePlane(std::vector<float> faceTris,
                                     std::vector<float> edgeLines, float alpha,
                                     bool visible, bool showEdges)
{
    latticePlaneFaces_ = std::move(faceTris);
    latticePlaneEdges_ = std::move(edgeLines);
    latticePlaneAlpha_ = alpha;
    latticePlaneVisible_ = visible;
    latticePlaneEdgesOn_ = showEdges;
    latticePlaneDirty_ = true;
    update();
}

void ViewportWidget::clearLatticePlane()
{
    latticePlaneFaces_.clear();
    latticePlaneEdges_.clear();
    latticePlaneVisible_ = false;
    latticePlaneDirty_ = true;
    update();
}

void ViewportWidget::clearScene()
{
    glEnable(GL_DEPTH_TEST);
    glClearColor(static_cast<float>(backgroundColor_.redF()),
                 static_cast<float>(backgroundColor_.greenF()),
                 static_cast<float>(backgroundColor_.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void ViewportWidget::drawSceneGeometry()
{
    const float aspect = height() > 0
        ? static_cast<float>(width()) / static_cast<float>(height())
        : 1.0f;
    renderer_.render(camera_.view(), camera_.projection(aspect));
}

void ViewportWidget::renderScene()
{
    clearScene();
    drawSceneGeometry();
}

void ViewportWidget::paintGL()
{
    ensureUploaded();

    if (!dof_.enabled && !ssao_.enabled) {
        // QPainter overlays reset pieces of GL state — reassert then draw.
        renderScene();
    } else {
        // Post-processing path: the scene goes into the offscreen G-buffer
        // (color + view normals + depth), then SSAO and/or depth-of-field
        // composite it onto the default framebuffer. Both effects need the
        // same offscreen render, so they share one target and one scene pass.
        const qreal dpr = devicePixelRatioF();
        const int w = std::max(1, static_cast<int>(width() * dpr));
        const int h = std::max(1, static_cast<int>(height() * dpr));
        ensurePostTarget(w, h);

        glBindFramebuffer(GL_FRAMEBUFFER, postFbo_);
        glViewport(0, 0, w, h);
        clearScene();
        // The normal attachment must be zeroed AFTER the scene clear: glClear
        // fills every attached color buffer with the background color, whose
        // alpha is 1 — which the SSAO pass would read as a valid normal and
        // shade occlusion across the empty background. Zeroing alpha marks
        // those pixels "no geometry here".
        const float clearNormal[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        glClearBufferfv(GL_COLOR, 1, clearNormal);
        drawSceneGeometry();

        const float aspect = height() > 0
            ? static_cast<float>(width()) / static_cast<float>(height())
            : 1.0f;
        const QMatrix4x4 projection = camera_.projection(aspect);

        // `sourceColor` is what the final pass reads: either the raw scene or
        // the AO-modulated version.
        unsigned sourceColor = postColorTex_;
        if (ssao_.enabled) {
            renderSsaoPasses(w, h, projection);
            // Multiply AO into the color. When DoF follows, this goes to an
            // offscreen texture so the blur acts on the composited image —
            // otherwise sharp occlusion would survive on top of blurred
            // geometry, which reads as a compositing error.
            glBindFramebuffer(GL_FRAMEBUFFER, dof_.enabled
                                                  ? ssaoCompositeFbo_
                                                  : defaultFramebufferObject());
            glViewport(0, 0, w, h);
            glDisable(GL_DEPTH_TEST);
            ssaoCompositeProgram_.bind();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, postColorTex_);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, ssaoBlurTex_);
            ssaoCompositeProgram_.setUniformValue("uColor", 0);
            ssaoCompositeProgram_.setUniformValue("uAo", 1);
            ssaoCompositeProgram_.setUniformValue("uIntensity", ssao_.intensity);
            drawFullscreenTriangle();
            ssaoCompositeProgram_.release();
            glActiveTexture(GL_TEXTURE0);
            glEnable(GL_DEPTH_TEST);
            sourceColor = ssaoCompositeTex_;
        }

        if (dof_.enabled) {
            glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
            glViewport(0, 0, w, h);
            glDisable(GL_DEPTH_TEST);
            dofProgram_.bind();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sourceColor);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, postDepthTex_);
            dofProgram_.setUniformValue("uColor", 0);
            dofProgram_.setUniformValue("uDepth", 1);
            const float distance = camera_.distance();
            dofProgram_.setUniformValue("uNear", std::max(0.01f, distance * 0.01f));
            dofProgram_.setUniformValue("uFar", distance * 50.0f);
            dofProgram_.setUniformValue("uFocusDistance",
                                        distance + dof_.focusOffset);
            dofProgram_.setUniformValue("uFocusRange", dof_.focusRange);
            dofProgram_.setUniformValue("uStrength",
                                        dof_.strength * static_cast<float>(dpr));
            dofProgram_.setUniformValue(
                "uPixelSize", QVector2D(1.0f / w, 1.0f / h));
            drawFullscreenTriangle();
            dofProgram_.release();
            glEnable(GL_DEPTH_TEST);
            glActiveTexture(GL_TEXTURE0);
        }
    }

    if (showAxes_ || showElementLabels_ || showIndexLabels_
        || showCoordinationLabels_ || !measureAtoms_.empty() || filmFade_ < 1.0f
        || !filmCrossfadeImage_.isNull() || !textOverlays_.empty()
        || !selection_.empty()) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        // The dissolve goes UNDER the overlays and over the 3D render: it is
        // part of the picture being composited, not an annotation on top of it.
        if (!filmCrossfadeImage_.isNull() && filmCrossfadeWeight_ < 1.0f) {
            painter.setOpacity(1.0 - static_cast<double>(filmCrossfadeWeight_));
            painter.drawImage(rect(), filmCrossfadeImage_);
            painter.setOpacity(1.0);
        }
        if (showAxes_)
            drawAxesOverlay(painter);
        if (showElementLabels_ || showIndexLabels_ || showCoordinationLabels_)
            drawAtomLabelsOverlay(painter);
        drawTextOverlays(painter);
        drawMeasurementOverlay(painter);
        // Last of the annotations: it is a read-out of the CURRENT action, so
        // it should sit over anything it happens to overlap.
        drawSelectionInfoOverlay(painter);
        // Film fade, painted over EVERYTHING including the overlays: a fade to
        // black that left the axis triad and the atom labels floating on the
        // black would not read as a cut. Last, for the same reason.
        if (filmFade_ < 1.0f) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0,
                                    static_cast<int>(std::lround(
                                        255.0f * (1.0f - filmFade_)))));
            painter.drawRect(rect());
        }
    }
}

void ViewportWidget::setFilmCrossfade(const QImage& outgoing, float weight)
{
    filmCrossfadeImage_ = outgoing;
    filmCrossfadeWeight_ = std::clamp(weight, 0.0f, 1.0f);
    update();
}

void ViewportWidget::clearFilmCrossfade()
{
    if (filmCrossfadeImage_.isNull())
        return;
    filmCrossfadeImage_ = QImage();
    filmCrossfadeWeight_ = 1.0f;
    update();
}

void ViewportWidget::setFilmFade(float visibility)
{
    const float clamped = std::clamp(visibility, 0.0f, 1.0f);
    if (qFuzzyCompare(filmFade_ + 1.0f, clamped + 1.0f))
        return;
    filmFade_ = clamped;
    update();
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
                Q_EMIT cameraChanged();
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

    draggedTextOverlay_ = -1;
    if (event->button() != Qt::LeftButton)
        return;

    // A text overlay under the cursor is grabbed before anything else looks at
    // the click. It sits ON TOP of the scene visually, so it has to be on top
    // for input too, or a label over an atom could never be picked up. Topmost
    // wins, matching the paint order (last drawn is last hit-tested).
    if (!event->modifiers().testFlag(Qt::ShiftModifier)) {
        for (int i = static_cast<int>(textOverlays_.size()) - 1; i >= 0; --i) {
            QRectF box;
            if (!textOverlayRect(textOverlays_[static_cast<std::size_t>(i)], box))
                continue;
            if (!box.contains(pressPos_))
                continue;
            draggedTextOverlay_ = i;
            textDragGrabOffset_ = box.center() - pressPos_;
            update();
            return;
        }
    }

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

    // A grabbed text overlay follows the cursor in the viewer-facing plane
    // through its own depth — the same unprojection a dragged atom uses, so a
    // label keeps the distance from the camera it was placed at instead of
    // sliding toward or away from the viewer as it moves.
    if (draggedTextOverlay_ >= 0
        && draggedTextOverlay_ < static_cast<int>(textOverlays_.size())
        && event->buttons().testFlag(Qt::LeftButton)) {
        TextOverlay& overlay =
            textOverlays_[static_cast<std::size_t>(draggedTextOverlay_)];
        core::Vec3 moved;
        if (unprojectToPlane(event->position() + textDragGrabOffset_,
                             overlay.position, moved)) {
            overlay.position = moved;
            Q_EMIT textOverlayMoved(overlay.id, moved);
            update();
        }
        return; // consume the drag — do not orbit the camera
    }

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

    // Rotation mode + Shift: roll the structure about the screen normal (the
    // view direction), i.e. a 2D in-plane rotation. Dragging up or right rolls
    // clockwise; down or left rolls counter-clockwise.
    if (interactionMode_ == InteractionMode::Rotate
        && event->buttons().testFlag(Qt::LeftButton)
        && event->modifiers().testFlag(Qt::ShiftModifier)) {
        const QVector3D viewDir =
            (camera_.target() - camera_.worldPosition()).normalized();
        // right (+x) and up (−y screen) both contribute positively → clockwise.
        const float angle =
            static_cast<float>(delta.x() - delta.y()) * 0.4f;
        camera_.rotateScene(viewDir, angle);
        Q_EMIT cameraChanged();
        update();
        return;
    }

    // Middle-drag / Shift+left-drag pans in the other modes (muscle memory).
    const bool forcePan = event->buttons().testFlag(Qt::MiddleButton)
        || (event->buttons().testFlag(Qt::LeftButton)
            && event->modifiers().testFlag(Qt::ShiftModifier));

    if (forcePan) {
        camera_.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()), height());
        Q_EMIT cameraChanged();
    } else if (event->buttons().testFlag(Qt::LeftButton)) {
        switch (interactionMode_) {
        case InteractionMode::Rotate:
        case InteractionMode::MeasureDistance:
        case InteractionMode::MeasureAngle:
            // Measure modes keep orbit-on-drag so the structure can be
            // turned between the measurement clicks.
            // Arcball: the FROM and TO cursor positions, not a delta.
            // The rotation depends on where on the virtual ball the drag
            // happened, so the two endpoints are the input — a delta alone
            // cannot express it.
            camera_.rotateArcball(lastMousePos_ - delta, lastMousePos_,
                                  width(), height());
            Q_EMIT cameraChanged();
            break;
        case InteractionMode::Pan:
            camera_.pan(static_cast<float>(delta.x()),
                        static_cast<float>(delta.y()), height());
            Q_EMIT cameraChanged();
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
    // A dragged text overlay applied its moves live; releasing just ends the
    // grab. Consumed, so the release does not also register as an atom pick.
    if (draggedTextOverlay_ >= 0) {
        draggedTextOverlay_ = -1;
        update();
        return;
    }
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
    Q_EMIT cameraChanged();
    update();
}

bool ViewportWidget::focusNextPrevChild(bool next)
{
    // THIS is the real interception point for [Tab]/[Shift+Tab] cycling —
    // not keyPressEvent() below, even though that override looks like it
    // should handle it. QWidget::event() special-cases a literal
    // Tab/Shift+Tab/Backtab keypress BEFORE keyPressEvent() ever runs: it
    // calls focusNextPrevChild(bool) directly and only falls through to
    // keyPressEvent() if THAT returns false. A keyPressEvent() override
    // alone — however correct its own logic — is simply never reached for
    // this specific family of keys, which is exactly why the previous
    // implementation (which had ONLY the keyPressEvent() branch below, with
    // no focusNextPrevChild() override at all) silently did nothing: Qt's
    // own focus traversal ran first, moved focus to the next widget in the
    // window, returned true, and keyPressEvent() was never called.
    //
    // Overriding focusNextPrevChild() here also gets the "only while the
    // viewport has focus" scoping for free, by construction rather than by
    // a runtime check: Qt only calls THIS widget's focusNextPrevChild()
    // when THIS widget currently holds keyboard focus (Qt::StrongFocus,
    // set in the constructor, is what lets it hold focus at all). A
    // spinbox or line edit elsewhere in the window has its own focus chain
    // and never reaches this override, so their Tab handling is untouched.
    //
    // Deferring to ShortcutRegistry's CURRENT binding — not a hardcoded
    // Qt::Key_Tab — is what makes a remap behave correctly: if the user
    // rebinds "viewport.tab.next" away from Tab, a literal Tab keypress
    // here no longer matches `wanted` below, so it falls through to
    // QOpenGLWidget::focusNextPrevChild(), i.e. ordinary focus traversal
    // resumes — and the new binding (now some non-Tab key Qt never routes
    // through this method) fires through the ordinary keyPressEvent() path
    // below instead, with no special-casing needed for that side at all.
    const QKeySequence wanted = next
        ? ShortcutRegistry::binding(QStringLiteral("viewport.tab.next"))
        : ShortcutRegistry::binding(QStringLiteral("viewport.tab.previous"));
    const bool matches = next ? wanted == QKeySequence(Qt::Key_Tab)
                              : wanted == QKeySequence(Qt::SHIFT | Qt::Key_Tab);
    if (matches) {
        Q_EMIT cycleTabRequested(next ? 1 : -1);
        return true;
    }
    return QOpenGLWidget::focusNextPrevChild(next);
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    // Defensive fallback only: focusNextPrevChild() above is the real
    // interception point and handles the common case entirely on its own
    // (see its own comment for why). This branch only still matters if
    // QOpenGLWidget::focusNextPrevChild() ever returns false for a literal
    // Tab/Backtab press that ISN'T currently bound to tab-cycling (e.g.
    // nowhere else in the window can take focus) — QWidget::event() falls
    // through to keyPressEvent() in exactly that case, and the check here
    // (correctly, via the same registry lookup) still declines to cycle,
    // so behaviour stays correct either way. Kept rather than deleted so
    // that guarantee doesn't rest on an unwritten assumption about when
    // Qt's own traversal succeeds.
    //
    // Key_Backtab is not a platform quirk to route around, it is the
    // regular way Shift+Tab arrives on several platforms (X11 among them) —
    // a distinct key code rather than Key_Tab + ShiftModifier — so both
    // spellings are normalized to the one canonical form before comparing
    // against what ShortcutRegistry has stored.
    int key = event->key();
    Qt::KeyboardModifiers modifiers = event->modifiers();
    if (key == Qt::Key_Backtab) {
        key = Qt::Key_Tab;
        modifiers |= Qt::ShiftModifier;
    }
    const QKeySequence pressed(
        QKeyCombination(modifiers, static_cast<Qt::Key>(key)));
    if (pressed == ShortcutRegistry::binding(QStringLiteral("viewport.tab.next"))) {
        Q_EMIT cycleTabRequested(1);
        return;
    }
    if (pressed
        == ShortcutRegistry::binding(QStringLiteral("viewport.tab.previous"))) {
        Q_EMIT cycleTabRequested(-1);
        return;
    }

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
        // Same rule as pickAtom(): a box drag selects what is on screen, and
        // a hidden hydrogen is not — otherwise Delete would silently take
        // hydrogens the user never saw inside the box.
        if (!renderer_.style().showHydrogens && atoms[i].atomicNumber == 1)
            continue;
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

bool ViewportWidget::projectToScreen(const core::Vec3& world, QPointF& out) const
{
    if (height() <= 0)
        return false;
    const float aspect = static_cast<float>(width()) / static_cast<float>(height());
    const QMatrix4x4 mvp = camera_.projection(aspect) * camera_.view();
    const QVector4D clip = mvp
        * QVector4D(static_cast<float>(world.x), static_cast<float>(world.y),
                    static_cast<float>(world.z), 1.0f);
    if (clip.w() <= 0.0f)
        return false; // behind the eye
    out = QPointF((clip.x() / clip.w() * 0.5f + 0.5f) * width(),
                  (0.5f - clip.y() / clip.w() * 0.5f) * height());
    return true;
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
    // Each atom is picked at the radius it is DRAWN at, which with casts in
    // play differs per atom: a CPK substrate sphere is several times the
    // ball-and-stick node next to it, and picking at one shared radius would
    // miss the big ones and grab empty space around the small ones.
    const auto casts = render::StructureRenderer::atomCastStyles(
        structure_.get(), renderer_.style());
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        // A hidden hydrogen is not there to be clicked: picking one would
        // select, measure from, or delete an atom the user cannot see.
        if (!renderer_.style().showHydrogens && atoms[i].atomicNumber == 1)
            continue;
        const QVector3D center(static_cast<float>(atoms[i].position.x),
                               static_cast<float>(atoms[i].position.y),
                               static_cast<float>(atoms[i].position.z));
        const float radius = render::StructureRenderer::displayRadius(
            atoms[i].atomicNumber, casts[i]);

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
