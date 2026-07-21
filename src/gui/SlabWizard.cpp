#include "gui/SlabWizard.hpp"

#include "gui/ViewportWidget.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace calango::gui {

namespace {

constexpr int kTallLayers = 8;      ///< bulk repetitions in the reference stack
constexpr double kLayerTol = 0.10;  ///< Å, z-clustering tolerance for layers

QColor elementColor(int z)
{
    const auto& element = core::Elements::data(z);
    return {element.rgb[0], element.rgb[1], element.rgb[2]};
}

/// Reduce an integer triple by its gcd (all-zero stays all-zero).
std::array<int, 3> gcdReduced(std::array<int, 3> v)
{
    const int g = std::gcd(std::gcd(std::abs(v[0]), std::abs(v[1])), std::abs(v[2]));
    if (g > 1)
        for (int& component : v)
            component /= g;
    return v;
}

/// Distinct atomic layer heights (cluster means), sorted ascending.
std::vector<double> clusterLayers(const core::Structure& slab)
{
    std::vector<double> zs;
    zs.reserve(slab.size());
    for (const core::Atom& atom : slab.atoms())
        zs.push_back(atom.position.z);
    std::sort(zs.begin(), zs.end());

    std::vector<double> layers;
    std::size_t start = 0;
    for (std::size_t i = 1; i <= zs.size(); ++i) {
        if (i == zs.size() || zs[i] - zs[i - 1] > kLayerTol) {
            layers.push_back(
                std::accumulate(zs.begin() + static_cast<std::ptrdiff_t>(start),
                                zs.begin() + static_cast<std::ptrdiff_t>(i), 0.0)
                / static_cast<double>(i - start));
            start = i;
        }
    }
    return layers;
}

} // namespace

// ---------------------------------------------------------------------------
// OrientationCanvas
// ---------------------------------------------------------------------------

OrientationCanvas::OrientationCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(420, 360);
    setMouseTracking(false);
}

void OrientationCanvas::setBulk(const core::Structure& bulk)
{
    cell_ = bulk.cell();
    atoms_.assign(bulk.atoms().begin(), bulk.atoms().end());

    lattice_.clear();
    const auto& a = cell_.vectors();
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j)
            for (int m = -2; m <= 2; ++m)
                lattice_.push_back(
                    {{i, j, m}, a[0] * i + a[1] * j + a[2] * m});
    update();
}

core::Vec3 OrientationCanvas::fromCoeff(const std::array<int, 3>& n) const
{
    const auto& a = cell_.vectors();
    return a[0] * n[0] + a[1] * n[1] + a[2] * n[2];
}

void OrientationCanvas::setSurfaceVectors(const core::Vec3& u, const core::Vec3& v)
{
    // The canonical vectors are lattice vectors of the bulk, but
    // ase.build.surface ROTATES the slab (normal onto z), so their
    // fractional coordinates in the unrotated bulk basis are meaningless.
    // Recover the integer coefficients through the rotation-invariant
    // metric instead: find integer triples p, q whose lattice lengths and
    // mutual angle reproduce |u|, |v| and u·v.
    const auto& a = cell_.vectors();
    double metric[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            metric[i][j] = a[static_cast<std::size_t>(i)]
                               .dot(a[static_cast<std::size_t>(j)]);
    const auto quadratic = [&metric](const std::array<int, 3>& p,
                                     const std::array<int, 3>& q) {
        double sum = 0.0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                sum += p[static_cast<std::size_t>(i)] * metric[i][j]
                    * q[static_cast<std::size_t>(j)];
        return sum;
    };

    const double uu = u.dot(u), vv = v.dot(v), uv = u.dot(v);
    const double tolerance = 1e-6 * std::max({uu, vv, 1.0});
    const auto candidates = [&](double lengthSq) {
        std::vector<std::array<int, 3>> matches;
        for (int i = -4; i <= 4; ++i)
            for (int j = -4; j <= 4; ++j)
                for (int m = -4; m <= 4; ++m) {
                    const std::array<int, 3> p{i, j, m};
                    if (p == std::array<int, 3>{0, 0, 0})
                        continue;
                    if (std::abs(quadratic(p, p) - lengthSq) < tolerance)
                        matches.push_back(p);
                }
        return matches;
    };

    for (const auto& p : candidates(uu)) {
        for (const auto& q : candidates(vv)) {
            if (std::abs(quadratic(p, q) - uv) >= tolerance)
                continue;
            const std::array<int, 3> cross{p[1] * q[2] - p[2] * q[1],
                                           p[2] * q[0] - p[0] * q[2],
                                           p[0] * q[1] - p[1] * q[0]};
            if (cross == std::array<int, 3>{0, 0, 0})
                continue;
            coeffU_ = p;
            coeffV_ = q;
            update();
            return;
        }
    }
    // No exact metric match (unexpected) — keep the previous vectors.
    update();
}

std::array<int, 3> OrientationCanvas::currentMiller() const
{
    // u = p·a, v = q·a  =>  Miller indices of span(u, v) are exactly p × q.
    const auto& p = coeffU_;
    const auto& q = coeffV_;
    auto miller = gcdReduced({p[1] * q[2] - p[2] * q[1],
                              p[2] * q[0] - p[0] * q[2],
                              p[0] * q[1] - p[1] * q[0]});
    // ±(hkl) describe the same plane family — prefer the positive-leading
    // form for display.
    for (const int component : miller) {
        if (component > 0)
            break;
        if (component < 0) {
            for (int& value : miller)
                value = -value;
            break;
        }
    }
    return miller;
}

QPointF OrientationCanvas::project(const core::Vec3& p) const
{
    // Fixed axonometric view: yaw about z, then pitch about x; screen up
    // is the crystal's +z after tilt.
    constexpr double yaw = 28.0 * M_PI / 180.0;
    constexpr double pitch = 22.0 * M_PI / 180.0;
    const double x1 = p.x * std::cos(yaw) + p.y * std::sin(yaw);
    const double y1 = -p.x * std::sin(yaw) + p.y * std::cos(yaw);
    const double z2 = y1 * std::sin(pitch) + p.z * std::cos(pitch);
    return {x1, -z2};
}

void OrientationCanvas::updateProjectionFit()
{
    double xMin = std::numeric_limits<double>::max(), xMax = -xMin;
    double yMin = xMin, yMax = -xMin;
    for (const LatticePoint& point : lattice_) {
        const QPointF s = project(point.pos);
        xMin = std::min(xMin, s.x());
        xMax = std::max(xMax, s.x());
        yMin = std::min(yMin, s.y());
        yMax = std::max(yMax, s.y());
    }
    const double margin = 34.0;
    const double spanX = std::max(xMax - xMin, 1e-6);
    const double spanY = std::max(yMax - yMin, 1e-6);
    scale_ = std::min((width() - 2 * margin) / spanX,
                      (height() - 2 * margin) / spanY);
    offset_ = QPointF(width() / 2.0 - scale_ * (xMin + xMax) / 2.0,
                      height() / 2.0 - scale_ * (yMin + yMax) / 2.0);
}

void OrientationCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(20, 22, 27));
    if (lattice_.empty())
        return;
    updateProjectionFit();

    const auto toScreen = [this](const core::Vec3& p) {
        return project(p) * scale_ + offset_;
    };

    // Lattice points.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(120, 126, 138));
    for (const LatticePoint& point : lattice_)
        painter.drawEllipse(toScreen(point.pos), 2.4, 2.4);

    // Unit cell wireframe.
    painter.setPen(QPen(QColor(90, 96, 108), 1.0));
    const auto corners = cell_.corners();
    for (const auto& [i, j] : core::UnitCell::edges())
        painter.drawLine(toScreen(corners[static_cast<std::size_t>(i)]),
                         toScreen(corners[static_cast<std::size_t>(j)]));

    // Atoms of one bulk cell.
    for (const core::Atom& atom : atoms_) {
        painter.setPen(QPen(QColor(15, 16, 20), 1.0));
        painter.setBrush(elementColor(atom.atomicNumber));
        painter.drawEllipse(toScreen(atom.position), 5.0, 5.0);
    }

    // Surface plane parallelogram spanned by u and v.
    const core::Vec3 u = fromCoeff(coeffU_);
    const core::Vec3 v = fromCoeff(coeffV_);
    QPolygonF plane;
    plane << toScreen({0, 0, 0}) << toScreen(u) << toScreen(u + v) << toScreen(v);
    painter.setPen(QPen(QColor(96, 156, 245), 1.4));
    painter.setBrush(QColor(96, 156, 245, 48));
    painter.drawPolygon(plane);

    // In-plane vectors with drag handles.
    const auto drawVector = [&](const core::Vec3& tip, const QColor& color,
                                const QString& label) {
        const QPointF origin = toScreen({0, 0, 0});
        const QPointF end = toScreen(tip);
        painter.setPen(QPen(color, 2.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(origin, end);
        painter.setBrush(color);
        painter.setPen(QPen(QColor(15, 16, 20), 1.2));
        painter.drawEllipse(end, 6.5, 6.5);
        painter.setPen(color);
        painter.drawText(end + QPointF(10, -6), label);
    };
    drawVector(u, QColor(235, 100, 90), QStringLiteral("u"));
    drawVector(v, QColor(110, 210, 130), QStringLiteral("v"));

    // Header: coefficients and the Miller indices they imply.
    const auto miller = currentMiller();
    painter.setPen(QColor(225, 228, 235));
    painter.drawText(
        QRectF(10, 6, width() - 20, 40),
        QStringLiteral("u = (%1, %2, %3)·a   v = (%4, %5, %6)·a   →   (h k l) = "
                       "(%7 %8 %9)%10")
            .arg(coeffU_[0]).arg(coeffU_[1]).arg(coeffU_[2])
            .arg(coeffV_[0]).arg(coeffV_[1]).arg(coeffV_[2])
            .arg(miller[0]).arg(miller[1]).arg(miller[2])
            .arg(dragging_ != 0 ? tr("  (dragging…)") : QString()));
}

void OrientationCanvas::mousePressEvent(QMouseEvent* event)
{
    updateProjectionFit();
    const auto handle = [this](const std::array<int, 3>& coeff) {
        return project(fromCoeff(coeff)) * scale_ + offset_;
    };
    if (QLineF(event->position(), handle(coeffU_)).length() < 13.0)
        dragging_ = 1;
    else if (QLineF(event->position(), handle(coeffV_)).length() < 13.0)
        dragging_ = 2;
    update();
}

void OrientationCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_ == 0)
        return;

    // Snap the dragged tip to the nearest lattice point that keeps the
    // pair non-collinear (a collinear pair spans no plane).
    const LatticePoint* best = nullptr;
    double bestDistance = std::numeric_limits<double>::max();
    for (const LatticePoint& point : lattice_) {
        if (point.n == std::array<int, 3>{0, 0, 0})
            continue;
        const auto& other = dragging_ == 1 ? coeffV_ : coeffU_;
        const std::array<int, 3> cross{
            point.n[1] * other[2] - point.n[2] * other[1],
            point.n[2] * other[0] - point.n[0] * other[2],
            point.n[0] * other[1] - point.n[1] * other[0]};
        if (cross == std::array<int, 3>{0, 0, 0})
            continue;
        const QPointF screen = project(point.pos) * scale_ + offset_;
        const double distance = QLineF(event->position(), screen).length();
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &point;
        }
    }
    if (!best)
        return;
    auto& target = dragging_ == 1 ? coeffU_ : coeffV_;
    if (target != best->n) {
        target = best->n;
        update();
    }
}

void OrientationCanvas::mouseReleaseEvent(QMouseEvent*)
{
    if (dragging_ == 0)
        return;
    dragging_ = 0;
    update();
    const auto miller = currentMiller();
    if (miller != std::array<int, 3>{0, 0, 0})
        Q_EMIT millerDragged(miller[0], miller[1], miller[2]);
}

// ---------------------------------------------------------------------------
// CrossSectionCanvas
// ---------------------------------------------------------------------------

CrossSectionCanvas::CrossSectionCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(420, 360);
}

void CrossSectionCanvas::setSlab(const core::Structure& slab,
                                 const std::vector<double>& layerZ)
{
    atoms_.assign(slab.atoms().begin(), slab.atoms().end());
    layerZ_ = layerZ;

    xMin_ = zMin_ = std::numeric_limits<double>::max();
    xMax_ = zMax_ = std::numeric_limits<double>::lowest();
    for (const core::Atom& atom : atoms_) {
        xMin_ = std::min(xMin_, atom.position.x);
        xMax_ = std::max(xMax_, atom.position.x);
        zMin_ = std::min(zMin_, atom.position.z);
        zMax_ = std::max(zMax_, atom.position.z);
    }
    if (xMax_ - xMin_ < 1.0) {
        xMin_ -= 1.0;
        xMax_ += 1.0;
    }
    update();
}

void CrossSectionCanvas::setSelection(int bottomLayer, int topLayer)
{
    bottom_ = bottomLayer;
    top_ = topLayer;
    update();
}

double CrossSectionCanvas::zToY(double z) const
{
    const double margin = 26.0;
    const double span = std::max(zMax_ - zMin_, 1e-6);
    return height() - margin - (z - zMin_) / span * (height() - 2 * margin);
}

void CrossSectionCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);
    if (layerZ_.empty())
        return;

    const double margin = 26.0;
    const double xSpan = std::max(xMax_ - xMin_, 1e-6);
    const auto xToScreen = [&](double x) {
        return margin + (x - xMin_) / xSpan * (width() - 2 * margin);
    };

    // Selected slab band.
    if (bottom_ >= 0 && top_ < static_cast<int>(layerZ_.size()) && bottom_ <= top_) {
        const double yTop = zToY(layerZ_[static_cast<std::size_t>(top_)]);
        const double yBottom = zToY(layerZ_[static_cast<std::size_t>(bottom_)]);
        painter.fillRect(QRectF(QPointF(margin * 0.4, yTop - 6),
                                QPointF(width() - margin * 0.4, yBottom + 6)),
                         QColor(96, 156, 245, 36));
    }

    // Layer lines + labels; termination boundaries highlighted.
    QFont small = painter.font();
    small.setPointSizeF(small.pointSizeF() - 1.5);
    painter.setFont(small);
    for (int i = 0; i < static_cast<int>(layerZ_.size()); ++i) {
        const double y = zToY(layerZ_[static_cast<std::size_t>(i)]);
        const bool isTop = i == top_;
        const bool isBottom = i == bottom_;
        QPen pen(QColor(200, 204, 212), 1.0, Qt::DashLine);
        if (isTop)
            pen = QPen(QColor(232, 130, 20), 2.2);
        else if (isBottom)
            pen = QPen(QColor(0, 150, 136), 2.2);
        painter.setPen(pen);
        painter.drawLine(QPointF(margin * 0.4, y), QPointF(width() - margin * 0.4, y));

        painter.setPen(QColor(120, 126, 138));
        painter.drawText(QPointF(2, y - 2),
                         QStringLiteral("%1").arg(layerZ_[static_cast<std::size_t>(i)],
                                                  0, 'f', 2));
        if (isTop || isBottom) {
            painter.setPen(pen.color());
            painter.drawText(QPointF(width() - margin * 0.4 - 88, y - 4),
                             isTop ? tr("top termination") : tr("bottom termination"));
        }
    }

    // Atoms.
    for (const core::Atom& atom : atoms_) {
        painter.setPen(QPen(QColor(60, 64, 72), 0.8));
        painter.setBrush(elementColor(atom.atomicNumber));
        painter.drawEllipse(QPointF(xToScreen(atom.position.x), zToY(atom.position.z)),
                            4.6, 4.6);
    }
}

void CrossSectionCanvas::mousePressEvent(QMouseEvent* event)
{
    if (layerZ_.empty())
        return;

    // Nearest layer to the click…
    int nearest = 0;
    double bestDistance = std::numeric_limits<double>::max();
    for (int i = 0; i < static_cast<int>(layerZ_.size()); ++i) {
        const double d =
            std::abs(zToY(layerZ_[static_cast<std::size_t>(i)]) - event->position().y());
        if (d < bestDistance) {
            bestDistance = d;
            nearest = i;
        }
    }

    // …assigned to whichever termination boundary is closer to the click.
    const double dTop =
        std::abs(zToY(layerZ_[static_cast<std::size_t>(top_)]) - event->position().y());
    const double dBottom = std::abs(zToY(layerZ_[static_cast<std::size_t>(bottom_)])
                                    - event->position().y());
    if (dTop <= dBottom)
        top_ = std::max(nearest, bottom_);
    else
        bottom_ = std::min(nearest, top_);
    update();
    Q_EMIT terminationPicked(bottom_, top_);
}

// ---------------------------------------------------------------------------
// OrientationPage
// ---------------------------------------------------------------------------

OrientationPage::OrientationPage(SlabWizard* wizard)
    : QWizardPage(wizard)
    , wizard_(wizard)
    , canvas_(new OrientationCanvas(this))
    , infoLabel_(new QLabel(this))
{
    setTitle(tr("Surface Orientation"));
    setSubTitle(tr("Choose Miller indices, or drag the u / v vector handles to "
                   "another lattice point — the nearest integer (h k l) is "
                   "computed automatically."));

    auto* form = new QFormLayout;
    const char* names[3] = {"h", "k", "l"};
    for (int i = 0; i < 3; ++i) {
        millerSpins_[i] = new QSpinBox(this);
        millerSpins_[i]->setRange(-9, 9);
        millerSpins_[i]->setPrefix(QStringLiteral("%1 = ").arg(QLatin1String(names[i])));
        connect(millerSpins_[i], &QSpinBox::valueChanged,
                this, &OrientationPage::onMillerEdited);
        form->addRow(millerSpins_[i]);
    }
    millerSpins_[0]->setValue(wizard_->h);
    millerSpins_[1]->setValue(wizard_->k);
    millerSpins_[2]->setValue(wizard_->l);

    infoLabel_->setWordWrap(true);
    form->addRow(infoLabel_);

    canvas_->setBulk(*wizard_->bulk);
    connect(canvas_, &OrientationCanvas::millerDragged, this,
            [this](int h, int k, int l) {
                const QSignalBlocker b0(millerSpins_[0]);
                const QSignalBlocker b1(millerSpins_[1]);
                const QSignalBlocker b2(millerSpins_[2]);
                millerSpins_[0]->setValue(h);
                millerSpins_[1]->setValue(k);
                millerSpins_[2]->setValue(l);
                wizard_->h = h;
                wizard_->k = k;
                wizard_->l = l;
                recomputeSurfaceVectors();
            });

    auto* side = new QVBoxLayout;
    side->addLayout(form);
    side->addStretch(1);
    auto* sideWidget = new QWidget(this);
    sideWidget->setLayout(side);
    sideWidget->setFixedWidth(250);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(sideWidget);
    layout->addWidget(canvas_, 1);

    debounce_.setSingleShot(true);
    debounce_.setInterval(200);
    connect(&debounce_, &QTimer::timeout,
            this, &OrientationPage::recomputeSurfaceVectors);
}

void OrientationPage::initializePage()
{
    recomputeSurfaceVectors();
}

void OrientationPage::onMillerEdited()
{
    wizard_->h = millerSpins_[0]->value();
    wizard_->k = millerSpins_[1]->value();
    wizard_->l = millerSpins_[2]->value();
    debounce_.start();
}

void OrientationPage::recomputeSurfaceVectors()
{
    valid_ = false;
    if (wizard_->h == 0 && wizard_->k == 0 && wizard_->l == 0) {
        infoLabel_->setText(tr("Miller indices (0 0 0) are not a valid plane."));
    } else {
        try {
            // One-layer, zero-vacuum cut: its a and b are the canonical
            // in-plane surface cell vectors for this orientation.
            const core::Structure one = pybridge::AseBridge::makeSlab(
                *wizard_->bulk, wizard_->h, wizard_->k, wizard_->l, 1, 0.0);
            const auto& v = one.cell().vectors();
            canvas_->setSurfaceVectors(v[0], v[1]);
            const double lenU = v[0].norm();
            const double lenV = v[1].norm();
            const double angle =
                std::acos(std::clamp(v[0].dot(v[1]) / (lenU * lenV), -1.0, 1.0))
                * 180.0 / M_PI;
            infoLabel_->setText(tr("|u| = %1 Å · |v| = %2 Å · ∠(u, v) = %3°")
                                    .arg(lenU, 0, 'f', 3)
                                    .arg(lenV, 0, 'f', 3)
                                    .arg(angle, 0, 'f', 2));
            valid_ = true;
        } catch (const std::exception& e) {
            infoLabel_->setText(QString::fromUtf8(e.what()));
        }
    }
    Q_EMIT completeChanged();
}

bool OrientationPage::isComplete() const
{
    return valid_;
}

// ---------------------------------------------------------------------------
// TerminationPage
// ---------------------------------------------------------------------------

TerminationPage::TerminationPage(SlabWizard* wizard)
    : QWizardPage(wizard)
    , wizard_(wizard)
    , canvas_(new CrossSectionCanvas(this))
    , layerCountSpin_(new QSpinBox(this))
    , thicknessSpin_(new QDoubleSpinBox(this))
    , infoLabel_(new QLabel(this))
    , statusLabel_(new QLabel(this))
{
    setTitle(tr("Cut, Thickness && Terminations"));
    setSubTitle(tr("Click atomic layers in the cross-section to set the top and "
                   "bottom termination, or dial in a layer count / thickness."));

    layerCountSpin_->setRange(1, 999);
    connect(layerCountSpin_, &QSpinBox::valueChanged,
            this, &TerminationPage::onLayerCountEdited);

    thicknessSpin_->setRange(0.0, 500.0);
    thicknessSpin_->setDecimals(3);
    thicknessSpin_->setSuffix(tr(" Å"));
    thicknessSpin_->setToolTip(tr("Snaps to the closest number of whole atomic "
                                  "layers above the bottom termination"));
    connect(thicknessSpin_, &QDoubleSpinBox::valueChanged,
            this, &TerminationPage::onThicknessEdited);

    connect(canvas_, &CrossSectionCanvas::terminationPicked,
            this, &TerminationPage::onTerminationPicked);

    auto* form = new QFormLayout;
    form->addRow(tr("Layers in slab:"), layerCountSpin_);
    form->addRow(tr("Thickness:"), thicknessSpin_);
    infoLabel_->setWordWrap(true);
    form->addRow(infoLabel_);
    statusLabel_->setWordWrap(true);
    form->addRow(statusLabel_);

    auto* side = new QVBoxLayout;
    side->addLayout(form);
    side->addStretch(1);
    auto* sideWidget = new QWidget(this);
    sideWidget->setLayout(side);
    sideWidget->setFixedWidth(250);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(sideWidget);
    layout->addWidget(canvas_, 1);
}

void TerminationPage::initializePage()
{
    valid_ = wizard_->buildTallSlab();
    if (!valid_) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        statusLabel_->setText(tr("Could not build the reference stack for "
                                 "(%1 %2 %3) — go back and adjust the "
                                 "orientation.")
                                  .arg(wizard_->h)
                                  .arg(wizard_->k)
                                  .arg(wizard_->l));
        Q_EMIT completeChanged();
        return;
    }
    statusLabel_->setStyleSheet(QString());
    statusLabel_->setText(
        tr("Reference stack: %1 bulk repeats, %2 atomic layers.")
            .arg(kTallLayers)
            .arg(wizard_->layerZ.size()));
    canvas_->setSlab(wizard_->tallSlab, wizard_->layerZ);
    syncControls();
    Q_EMIT completeChanged();
}

void TerminationPage::onTerminationPicked(int bottom, int top)
{
    wizard_->bottomLayer = bottom;
    wizard_->topLayer = top;
    syncControls();
}

void TerminationPage::onLayerCountEdited(int count)
{
    if (updating_ || wizard_->layerZ.empty())
        return;
    wizard_->topLayer = std::min(wizard_->bottomLayer + count - 1,
                                 static_cast<int>(wizard_->layerZ.size()) - 1);
    syncControls();
}

void TerminationPage::onThicknessEdited(double angstrom)
{
    if (updating_ || wizard_->layerZ.empty())
        return;
    // Highest top layer whose distance from the bottom stays within the
    // requested thickness (always at least the bottom layer itself).
    const double base = wizard_->layerZ[static_cast<std::size_t>(wizard_->bottomLayer)];
    int top = wizard_->bottomLayer;
    for (int i = wizard_->bottomLayer;
         i < static_cast<int>(wizard_->layerZ.size()); ++i) {
        if (wizard_->layerZ[static_cast<std::size_t>(i)] - base <= angstrom + 1e-6)
            top = i;
    }
    wizard_->topLayer = top;
    syncControls();
}

void TerminationPage::syncControls()
{
    if (wizard_->layerZ.empty())
        return;
    updating_ = true;
    const int count = wizard_->topLayer - wizard_->bottomLayer + 1;
    const double thickness =
        wizard_->layerZ[static_cast<std::size_t>(wizard_->topLayer)]
        - wizard_->layerZ[static_cast<std::size_t>(wizard_->bottomLayer)];
    layerCountSpin_->setMaximum(static_cast<int>(wizard_->layerZ.size()));
    layerCountSpin_->setValue(count);
    thicknessSpin_->setValue(thickness);
    infoLabel_->setText(tr("Selected: layers %1 – %2 of %3 (%4 layers, %5 Å)")
                            .arg(wizard_->bottomLayer + 1)
                            .arg(wizard_->topLayer + 1)
                            .arg(wizard_->layerZ.size())
                            .arg(count)
                            .arg(thickness, 0, 'f', 3));
    canvas_->setSelection(wizard_->bottomLayer, wizard_->topLayer);
    updating_ = false;
}

bool TerminationPage::isComplete() const
{
    return valid_;
}

// ---------------------------------------------------------------------------
// VacuumPage
// ---------------------------------------------------------------------------

VacuumPage::VacuumPage(SlabWizard* wizard)
    : QWizardPage(wizard)
    , wizard_(wizard)
    , topVacuumSpin_(new QDoubleSpinBox(this))
    , bottomVacuumSpin_(new QDoubleSpinBox(this))
    , centeredCheck_(new QCheckBox(tr("Symmetric vacuum (centered slab)"), this))
    , infoLabel_(new QLabel(this))
    , preview_(new ViewportWidget(this))
{
    setTitle(tr("Vacuum && Final Slab"));
    setSubTitle(tr("Set the vacuum spacing above and below the slab, check the "
                   "3D preview, then Finish to insert it into the workspace."));

    for (auto* spin : {topVacuumSpin_, bottomVacuumSpin_}) {
        spin->setRange(0.0, 80.0);
        spin->setDecimals(2);
        spin->setSingleStep(0.5);
        spin->setValue(10.0);
        spin->setSuffix(tr(" Å"));
        connect(spin, &QDoubleSpinBox::valueChanged, this, &VacuumPage::rebuild);
    }

    centeredCheck_->setChecked(true);
    bottomVacuumSpin_->setEnabled(false);
    connect(centeredCheck_, &QCheckBox::toggled, this, [this](bool on) {
        bottomVacuumSpin_->setEnabled(!on);
        rebuild();
    });

    auto* form = new QFormLayout;
    form->addRow(tr("Top vacuum:"), topVacuumSpin_);
    form->addRow(tr("Bottom vacuum:"), bottomVacuumSpin_);
    form->addRow(centeredCheck_);
    infoLabel_->setWordWrap(true);
    form->addRow(infoLabel_);

    auto* side = new QVBoxLayout;
    side->addLayout(form);
    side->addStretch(1);
    auto* sideWidget = new QWidget(this);
    sideWidget->setLayout(side);
    sideWidget->setFixedWidth(250);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(sideWidget);
    layout->addWidget(preview_, 1);
}

void VacuumPage::initializePage()
{
    rebuild();
}

void VacuumPage::rebuild()
{
    if (updating_)
        return;
    updating_ = true;
    wizard_->vacuumTop = topVacuumSpin_->value();
    wizard_->vacuumBottom = centeredCheck_->isChecked() ? topVacuumSpin_->value()
                                                        : bottomVacuumSpin_->value();
    if (centeredCheck_->isChecked()) {
        const QSignalBlocker blocker(bottomVacuumSpin_);
        bottomVacuumSpin_->setValue(wizard_->vacuumBottom);
    }
    updating_ = false;

    const auto slab = wizard_->buildResult();
    preview_->setStructure(slab);
    if (slab) {
        infoLabel_->setText(
            tr("%1 atoms · slab %2 Å · cell height %3 Å")
                .arg(slab->size())
                .arg(wizard_->layerZ.empty()
                         ? 0.0
                         : wizard_->layerZ[static_cast<std::size_t>(wizard_->topLayer)]
                             - wizard_->layerZ[static_cast<std::size_t>(
                                 wizard_->bottomLayer)],
                     0, 'f', 3)
                .arg(slab->cell().vectors()[2].norm(), 0, 'f', 3));
    } else {
        infoLabel_->setText(tr("No slab — check the previous stages."));
    }
    Q_EMIT completeChanged();
}

bool VacuumPage::isComplete() const
{
    return wizard_->result() != nullptr;
}

// ---------------------------------------------------------------------------
// SlabWizard
// ---------------------------------------------------------------------------

SlabWizard::SlabWizard(std::shared_ptr<const core::Structure> bulkStructure,
                       QWidget* parent)
    : QWizard(parent)
    , bulk(std::move(bulkStructure))
{
    setWindowTitle(tr("Surface Slab Builder"));
    setWizardStyle(QWizard::ModernStyle);
    resize(1000, 620);

    addPage(new OrientationPage(this));
    addPage(new TerminationPage(this));
    addPage(new VacuumPage(this));
}

QString SlabWizard::resultLabel() const
{
    return tr("(%1%2%3) slab, %4 layers")
        .arg(h)
        .arg(k)
        .arg(l)
        .arg(topLayer - bottomLayer + 1);
}

bool SlabWizard::buildTallSlab()
{
    tallSlabValid = false;
    layerZ.clear();
    try {
        tallSlab = pybridge::AseBridge::makeSlab(*bulk, h, k, l, kTallLayers, 0.0);
    } catch (const std::exception&) {
        return false;
    }
    layerZ = clusterLayers(tallSlab);
    if (layerZ.empty())
        return false;
    bottomLayer = 0;
    topLayer = std::min(static_cast<int>(layerZ.size()) - 1, 7); // 8-layer default
    tallSlabValid = true;
    return true;
}

std::shared_ptr<core::Structure> SlabWizard::buildResult()
{
    result_.reset();
    if (!tallSlabValid || layerZ.empty() || bottomLayer > topLayer)
        return nullptr;

    const double zBottom = layerZ[static_cast<std::size_t>(bottomLayer)];
    const double zTop = layerZ[static_cast<std::size_t>(topLayer)];
    const double tolerance = kLayerTol * 0.75;

    core::Structure out;
    for (const core::Atom& atom : tallSlab.atoms()) {
        if (atom.position.z < zBottom - tolerance
            || atom.position.z > zTop + tolerance)
            continue;
        core::Atom shifted = atom;
        shifted.position.z += vacuumBottom - zBottom;
        out.addAtom(shifted);
    }
    if (out.empty())
        return nullptr;

    const double height = (zTop - zBottom) + vacuumBottom + vacuumTop;
    const auto& v = tallSlab.cell().vectors();
    out.setCell(core::UnitCell(v[0], v[1], {0.0, 0.0, std::max(height, 1.0)},
                               {true, true, true}));
    result_ = std::make_shared<core::Structure>(std::move(out));
    return result_;
}

} // namespace calango::gui
