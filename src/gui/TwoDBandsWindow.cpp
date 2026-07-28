#include "gui/TwoDBandsWindow.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/VolumeViewWidget.hpp"
#include "gui/VolumetricStyle.hpp"
#include "render/ColorMap.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

/// Read a JSON array of arrays of numbers into a [row][col] grid.
std::vector<std::vector<double>> readGrid(const QJsonArray& rows)
{
    std::vector<std::vector<double>> grid;
    grid.reserve(static_cast<std::size_t>(rows.size()));
    for (const QJsonValue& row : rows) {
        const QJsonArray cells = row.toArray();
        std::vector<double> values;
        values.reserve(static_cast<std::size_t>(cells.size()));
        for (const QJsonValue& cell : cells)
            values.push_back(cell.toDouble());
        grid.push_back(std::move(values));
    }
    return grid;
}

void pushVertex(std::vector<float>& out, float x, float y, float z,
                const QVector3D& normal, const QColor& color)
{
    out.insert(out.end(),
               {x, y, z, normal.x(), normal.y(), normal.z(),
                static_cast<float>(color.redF()),
                static_cast<float>(color.greenF()),
                static_cast<float>(color.blueF())});
}

/// Catmull-Rom weight set for a fractional position between p1 and p2.
/// Interpolating rather than approximating matters here: the refined surface
/// must still pass through the computed eigenvalues, or the plot is no longer
/// showing the calculation's own numbers.
double catmullRom(double p0, double p1, double p2, double p3, double t)
{
    const double t2 = t * t;
    const double t3 = t2 * t;
    return 0.5
        * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
           + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

/// Sample `grid` at fractional index (u, v), clamping at the edges.
double sampleGrid(const std::vector<std::vector<double>>& grid, double u,
                  double v, bool cubic)
{
    const auto nx = static_cast<int>(grid.size());
    if (nx == 0)
        return 0.0;
    const auto ny = static_cast<int>(grid.front().size());
    if (ny == 0)
        return 0.0;
    const auto at = [&grid, nx, ny](int i, int j) {
        i = std::clamp(i, 0, nx - 1);
        j = std::clamp(j, 0, ny - 1);
        return grid[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
    };
    const int i = static_cast<int>(std::floor(u));
    const int j = static_cast<int>(std::floor(v));
    const double fu = u - i;
    const double fv = v - j;
    if (!cubic) {
        const double a = at(i, j) * (1 - fu) + at(i + 1, j) * fu;
        const double b = at(i, j + 1) * (1 - fu) + at(i + 1, j + 1) * fu;
        return a * (1 - fv) + b * fv;
    }
    double column[4];
    for (int k = 0; k < 4; ++k) {
        column[k] = catmullRom(at(i - 1, j - 1 + k), at(i, j - 1 + k),
                               at(i + 1, j - 1 + k), at(i + 2, j - 1 + k), fu);
    }
    return catmullRom(column[0], column[1], column[2], column[3], fv);
}

/// Clip a convex polygon against `k·n ≤ d` (Sutherland-Hodgman). Vertices
/// carry (x, y, z); the z of a new vertex is interpolated along the cut edge,
/// which is what keeps a clipped surface lying on the surface.
std::vector<QVector3D> clipHalfPlane(const std::vector<QVector3D>& polygon,
                                     double nx, double ny, double d)
{
    std::vector<QVector3D> out;
    const auto n = polygon.size();
    for (std::size_t i = 0; i < n; ++i) {
        const QVector3D& a = polygon[i];
        const QVector3D& b = polygon[(i + 1) % n];
        const double da = nx * a.x() + ny * a.y() - d;
        const double db = nx * b.x() + ny * b.y() - d;
        if (da <= 0.0)
            out.push_back(a);
        if ((da < 0.0 && db > 0.0) || (da > 0.0 && db < 0.0)) {
            const auto t = static_cast<float>(da / (da - db));
            out.push_back(a + (b - a) * t);
        }
    }
    return out;
}

} // namespace

TwoDBandsWindow::TwoDBandsWindow(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("2D Band Surfaces"));
    resize(940, 680);

    auto* layout = new QVBoxLayout(this);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::RichText);
    layout->addWidget(summary_);

    auto* body = new QHBoxLayout;

    // The band list is a side panel rather than a combo: several surfaces at
    // once IS the plot for a band touching or a Fermi surface, so selecting
    // more than one has to be a single gesture.
    bandList_ = new QListWidget(this);
    bandList_->setSelectionMode(QAbstractItemView::NoSelection);
    bandList_->setMaximumWidth(210);
    bandList_->setToolTip(
        tr("Bands drawn as surfaces. Several at once is usually the point — a "
           "band touching or an avoided crossing is a relationship between two "
           "surfaces, not a feature of either."));
    connect(bandList_, &QListWidget::itemChanged, this,
            [this] { rebuild(); });
    body->addWidget(bandList_);

    canvas_ = new VolumeViewWidget(this);
    // Opaque: stacked band sheets seen through each other read as noise.
    canvas_->setMeshOpacity(1.0f);
    body->addWidget(canvas_, 1);
    layout->addLayout(body, 1);

    auto* controls = new QHBoxLayout;

    controls->addWidget(new QLabel(tr("Colormap:"), this));
    gradientCombo_ = new QComboBox(this);
    gradientCombo_->addItems(volumetricGradientNames());
    gradientCombo_->setCurrentIndex(
        std::max(0, static_cast<int>(volumetricGradients().indexOf(
                        render::ColorGradient::Viridis))));
    connect(gradientCombo_, &QComboBox::currentIndexChanged, this,
            [this] { rebuild(); });
    controls->addWidget(gradientCombo_);

    controls->addWidget(new QLabel(tr("Energy scale:"), this));
    energyScaleSpin_ = new QDoubleSpinBox(this);
    energyScaleSpin_->setRange(0.01, 100.0);
    energyScaleSpin_->setDecimals(3);
    energyScaleSpin_->setSingleStep(0.05);
    energyScaleSpin_->setSuffix(tr(" Å⁻¹/eV"));
    energyScaleSpin_->setKeyboardTracking(false);
    energyScaleSpin_->setToolTip(
        tr("How many Å⁻¹ of vertical axis one eV occupies.\n\n"
           "k spans a few Å⁻¹ while the bands span tens of eV, so a 1:1 plot "
           "is a vertical wall. This is the exaggeration that makes the shape "
           "readable; it is set from the data on load and is stated in the "
           "caption, so the distortion is never silent."));
    connect(energyScaleSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { rebuild(); });
    controls->addWidget(energyScaleSpin_);

    QCheckBox* shift = nullptr;
    QWidget* shiftRow =
        richTextCheckBox(tr("E − E<sub>F</sub>"), shift, this);
    shiftFermiCheck_ = shift;
    shiftFermiCheck_->setChecked(true);
    shiftRow->setToolTip(tr("Plot energies relative to the Fermi level."));
    connect(shiftFermiCheck_, &QCheckBox::toggled, this,
            [this] { rebuild(); });
    controls->addWidget(shiftRow);

    fermiPlaneCheck_ = new QCheckBox(tr("Fermi plane"), this);
    fermiPlaneCheck_->setChecked(true);
    fermiPlaneCheck_->setToolTip(
        tr("Outline the plane at the Fermi level. Where a surface crosses it "
           "is the Fermi surface of the sheet."));
    connect(fermiPlaneCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    controls->addWidget(fermiPlaneCheck_);

    controls->addStretch(1);
    layout->addLayout(controls);

    // Second control row: what the surface is drawn OVER and how smoothly,
    // as opposed to the first row's colour and vertical scale.
    auto* controls2 = new QHBoxLayout;

    controls2->addWidget(new QLabel(tr("Interpolation:"), this));
    interpolationCombo_ = new QComboBox(this);
    interpolationCombo_->addItem(tr("None (computed grid)"), 0);
    interpolationCombo_->addItem(tr("Bilinear"), 1);
    interpolationCombo_->addItem(tr("Bicubic (Catmull-Rom)"), 2);
    interpolationCombo_->setCurrentIndex(2);
    interpolationCombo_->setToolTip(
        tr("Smooth the surface by resampling it between the computed "
           "k-points.\n\n"
           "Both schemes pass exactly through the eigenvalues that were "
           "calculated, so no computed value is altered — what changes is only "
           "what is drawn between them. Bicubic follows curvature and is the "
           "better choice for a band edge or a cone; bilinear is faceted but "
           "never overshoots, which matters next to a sharp crossing.\n\n"
           "Interpolation is not a substitute for sampling: a feature the "
           "k-grid missed cannot be recovered by drawing through it."));
    connect(interpolationCombo_, &QComboBox::currentIndexChanged, this,
            [this] { rebuild(); });
    controls2->addWidget(interpolationCombo_);

    refineSpin_ = new QSpinBox(this);
    refineSpin_->setRange(1, 8);
    refineSpin_->setValue(3);
    refineSpin_->setPrefix(tr("×"));
    refineSpin_->setToolTip(
        tr("Refinement factor: how many drawn cells replace each computed one "
           "along each axis. The triangle count grows with its square."));
    connect(refineSpin_, &QSpinBox::valueChanged, this, [this] { rebuild(); });
    controls2->addWidget(refineSpin_);

    axesCheck_ = new QCheckBox(tr("Axes"), this);
    axesCheck_->setChecked(true);
    axesCheck_->setToolTip(
        tr("Draw the k_x, k_y and energy axes through the origin, with a tick "
           "and a caption on each. Turn them off for a clean figure of the "
           "surface alone."));
    connect(axesCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    controls2->addWidget(axesCheck_);

    brillouinCheck_ = new QCheckBox(tr("First Brillouin zone"), this);
    brillouinCheck_->setToolTip(
        tr("Clip the surface to the first Brillouin zone — the Wigner-Seitz "
           "(Voronoi) cell of the reciprocal lattice, the set of k closer to Γ "
           "than to any other reciprocal-lattice point.\n\n"
           "The sampled grid is a parallelogram spanning one reciprocal unit "
           "cell, which covers the same area but is NOT the same region: for a "
           "hexagonal lattice it cuts through the K corners and folds parts of "
           "the second zone into view. Clipping shows the zone the band "
           "structure is conventionally drawn over."));
    connect(brillouinCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    controls2->addWidget(brillouinCheck_);

    labelsCheck_ = new QCheckBox(tr("k-point labels"), this);
    labelsCheck_->setChecked(true);
    labelsCheck_->setToolTip(
        tr("Mark and name the high-symmetry points of this lattice (Γ, M, K, "
           "…). The labels come from ASE's Bravais-lattice recognition, so "
           "they are the conventional ones for the cell that was actually "
           "calculated."));
    connect(labelsCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    controls2->addWidget(labelsCheck_);

    controls2->addStretch(1);
    auto* exportImageButton = new QPushButton(tr("Export Image…"), this);
    connect(exportImageButton, &QPushButton::clicked, this,
            &TwoDBandsWindow::exportImage);
    controls2->addWidget(exportImageButton);
    auto* exportDataButton = new QPushButton(tr("Export Data…"), this);
    connect(exportDataButton, &QPushButton::clicked, this,
            &TwoDBandsWindow::exportData);
    controls2->addWidget(exportDataButton);
    layout->addLayout(controls2);
}

bool TwoDBandsWindow::loadResults(const QString& jsonPath)
{
    const QJsonObject root = readJsonObject(jsonPath);
    if (root.isEmpty())
        return false;
    sourcePath_ = jsonPath;

    fermiEv_ = root.value(QStringLiteral("fermi_eV")).toDouble();
    samples_ = root.value(QStringLiteral("samples")).toInt();
    spinOrbit_ = root.value(QStringLiteral("spin_orbit")).toBool();
    kx_ = readGrid(root.value(QStringLiteral("kx_per_A")).toArray());
    ky_ = readGrid(root.value(QStringLiteral("ky_per_A")).toArray());

    const auto recip =
        readGrid(root.value(QStringLiteral("reciprocal_2pi_per_A")).toArray());
    reciprocal_ = {};
    for (std::size_t i = 0; i < 3 && i < recip.size(); ++i)
        for (std::size_t j = 0; j < 3 && j < recip[i].size(); ++j)
            reciprocal_[i][j] = recip[i][j];

    specialPoints_.clear();
    const QJsonObject special =
        root.value(QStringLiteral("special_points")).toObject();
    for (auto it = special.begin(); it != special.end(); ++it) {
        const QJsonArray cart =
            it.value().toObject().value(QStringLiteral("k_per_A")).toArray();
        if (cart.size() < 2)
            continue;
        // ASE spells the zone centre "G"; every band-structure figure ever
        // drawn calls it Γ.
        const QString label = it.key() == QLatin1String("G")
            ? QStringLiteral("Γ")
            : it.key();
        specialPoints_.push_back(
            {label, cart.at(0).toDouble(), cart.at(1).toDouble()});
    }

    surfaces_.clear();
    for (const QJsonValue& value : root.value(QStringLiteral("bands")).toArray()) {
        const QJsonObject entry = value.toObject();
        Surface surface;
        surface.band = entry.value(QStringLiteral("band")).toInt();
        surface.spin = entry.value(QStringLiteral("spin")).toInt();
        surface.minEv = entry.value(QStringLiteral("min_eV")).toDouble();
        surface.maxEv = entry.value(QStringLiteral("max_eV")).toDouble();
        surface.energies =
            readGrid(entry.value(QStringLiteral("energies_eV")).toArray());
        if (surface.energies.size() >= 2)
            surfaces_.push_back(std::move(surface));
    }
    if (surfaces_.empty() || kx_.size() < 2 || ky_.size() < 2)
        return false;

    // Half-extent of the sampled zone, which sets both the camera framing and
    // the default energy exaggeration.
    double maxK = 0.0;
    for (std::size_t i = 0; i < kx_.size(); ++i)
        for (std::size_t j = 0; j < kx_[i].size(); ++j)
            maxK = std::max({maxK, std::abs(kx_[i][j]), std::abs(ky_[i][j])});
    kExtent_ = std::max(maxK, 1e-6);

    // Default exaggeration: map the full energy span of the loaded surfaces
    // onto roughly the k extent, so the first view shows the dispersion rather
    // than a wall or a flat sheet — whatever the energy range happens to be.
    double lo = surfaces_.front().minEv;
    double hi = surfaces_.front().maxEv;
    for (const Surface& s : surfaces_) {
        lo = std::min(lo, s.minEv);
        hi = std::max(hi, s.maxEv);
    }
    const double span = std::max(hi - lo, 1e-6);
    {
        const QSignalBlocker blocker(energyScaleSpin_);
        energyScaleSpin_->setValue(
            std::clamp(kExtent_ / span, 0.01, 100.0));
    }

    summary_->setText(
        tr("<b>%1</b> band surface(s) on a %2 × %2 grid over the 2D Brillouin "
           "zone · E<sub>F</sub> = %3 eV%4")
            .arg(surfaces_.size())
            .arg(samples_)
            .arg(fermiEv_, 0, 'f', 4)
            .arg(spinOrbit_ ? tr(" · spin-orbit coupling included")
                            : QString()));

    populateBandList();
    rebuild();
    return true;
}

void TwoDBandsWindow::populateBandList()
{
    const QSignalBlocker blocker(bandList_);
    bandList_->clear();
    for (std::size_t i = 0; i < surfaces_.size(); ++i) {
        const Surface& s = surfaces_[i];
        // Labelled by where the band sits relative to E_F, which is what a
        // reader is actually looking for — a bare band index says nothing.
        QString role;
        if (s.maxEv < fermiEv_)
            role = tr("valence");
        else if (s.minEv > fermiEv_)
            role = tr("conduction");
        else
            role = tr("crosses E_F");
        auto* item = new QListWidgetItem(
            spinOrbit_ || surfaces_.front().spin == surfaces_.back().spin
                ? tr("Band %1 — %2").arg(s.band).arg(role)
                : tr("Band %1 (spin %2) — %3")
                      .arg(s.band)
                      .arg(s.spin)
                      .arg(role),
            bandList_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        // Start with the bands around the Fermi level only: checking all of
        // them by default would open on a stack of sheets that hides the one
        // feature anybody came to see.
        const bool interesting =
            s.minEv <= fermiEv_ && s.maxEv >= fermiEv_;
        item->setCheckState(interesting ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(tr("%1 … %2 eV").arg(s.minEv, 0, 'f', 3)
                             .arg(s.maxEv, 0, 'f', 3));
        item->setData(Qt::UserRole, static_cast<int>(i));
    }
    // Nothing crosses E_F (an insulator): show the two bands that bracket the
    // gap rather than an empty canvas.
    bool any = false;
    for (int i = 0; i < bandList_->count(); ++i)
        any = any || bandList_->item(i)->checkState() == Qt::Checked;
    if (!any) {
        for (int i = 0; i < bandList_->count(); ++i) {
            const Surface& s = surfaces_[static_cast<std::size_t>(i)];
            const bool edge = (s.maxEv < fermiEv_ && (i + 1 >= bandList_->count()
                                   || surfaces_[static_cast<std::size_t>(i) + 1]
                                              .minEv > fermiEv_))
                || (s.minEv > fermiEv_
                    && (i == 0
                        || surfaces_[static_cast<std::size_t>(i) - 1].maxEv
                            < fermiEv_));
            if (edge)
                bandList_->item(i)->setCheckState(Qt::Checked);
        }
    }
}

std::vector<std::array<double, 3>> TwoDBandsWindow::brillouinHalfPlanes() const
{
    // The Wigner-Seitz cell of the reciprocal lattice: k is inside when it is
    // no further from Γ than from any other reciprocal-lattice point G, i.e.
    // |k| <= |k - G|, which reduces to k·Ĝ <= |G|/2. Neighbours out to two
    // cells in each direction are more than enough — in 2D the cell is bounded
    // by the first or second shell for every Bravais lattice.
    std::vector<std::array<double, 3>> planes;
    const double b1x = reciprocal_[0][0], b1y = reciprocal_[0][1];
    const double b2x = reciprocal_[1][0], b2y = reciprocal_[1][1];
    if (std::hypot(b1x, b1y) < 1e-9 || std::hypot(b2x, b2y) < 1e-9)
        return planes;
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            if (i == 0 && j == 0)
                continue;
            const double gx = i * b1x + j * b2x;
            const double gy = i * b1y + j * b2y;
            const double length = std::hypot(gx, gy);
            if (length < 1e-9)
                continue;
            planes.push_back({gx / length, gy / length, 0.5 * length});
        }
    }
    return planes;
}

std::vector<std::array<double, 2>> TwoDBandsWindow::brillouinPolygon() const
{
    const auto planes = brillouinHalfPlanes();
    if (planes.size() < 3)
        return {};
    // Intersect a generous starting square with every half-plane; what
    // survives IS the zone. Cheaper and far less error-prone than enumerating
    // vertex candidates from plane pairs and filtering them.
    double reach = 0.0;
    for (const auto& p : planes)
        reach = std::max(reach, p[2]);
    reach *= 2.5;
    std::vector<QVector3D> polygon = {
        {static_cast<float>(-reach), static_cast<float>(-reach), 0.0f},
        {static_cast<float>(reach), static_cast<float>(-reach), 0.0f},
        {static_cast<float>(reach), static_cast<float>(reach), 0.0f},
        {static_cast<float>(-reach), static_cast<float>(reach), 0.0f}};
    for (const auto& plane : planes) {
        polygon = clipHalfPlane(polygon, plane[0], plane[1], plane[2]);
        if (polygon.size() < 3)
            return {};
    }
    // Drop duplicate and collinear vertices. For a square lattice the four
    // diagonal half-planes pass exactly through the corners, so the clipper
    // emits each corner twice with a zero-length edge between — which would
    // draw as a degenerate segment and makes the vertex count lie about the
    // shape (an 8-gon that is really a square).
    std::vector<std::array<double, 2>> out;
    out.reserve(polygon.size());
    for (const QVector3D& v : polygon) {
        const std::array<double, 2> point{v.x(), v.y()};
        if (!out.empty()
            && std::hypot(point[0] - out.back()[0], point[1] - out.back()[1])
                < 1e-9)
            continue;
        out.push_back(point);
    }
    while (out.size() > 1
           && std::hypot(out.front()[0] - out.back()[0],
                         out.front()[1] - out.back()[1])
               < 1e-9)
        out.pop_back();
    if (out.size() < 3)
        return {};
    std::vector<std::array<double, 2>> simplified;
    simplified.reserve(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto& prev = out[(i + out.size() - 1) % out.size()];
        const auto& here = out[i];
        const auto& next = out[(i + 1) % out.size()];
        const double cross = (here[0] - prev[0]) * (next[1] - prev[1])
            - (here[1] - prev[1]) * (next[0] - prev[0]);
        // Scaled tolerance: the zone's own size sets what "collinear" means,
        // and these coordinates are inverse ångström of order 1.
        const double span = std::hypot(next[0] - prev[0], next[1] - prev[1]);
        if (std::abs(cross) > 1e-9 * std::max(span, 1e-9))
            simplified.push_back(here);
    }
    return simplified.size() >= 3 ? simplified : out;
}

bool TwoDBandsWindow::refine(const std::vector<std::vector<double>>& energies,
                             std::vector<std::vector<double>>& outKx,
                             std::vector<std::vector<double>>& outKy,
                             std::vector<std::vector<double>>& outEnergies) const
{
    const int scheme =
        interpolationCombo_ ? interpolationCombo_->currentData().toInt() : 0;
    const int factor = refineSpin_ ? refineSpin_->value() : 1;
    if (scheme == 0 || factor <= 1 || energies.size() < 2)
        return false;
    const bool cubic = scheme == 2;
    const auto nx = static_cast<int>(energies.size());
    const auto ny = static_cast<int>(energies.front().size());
    if (ny < 2 || static_cast<int>(kx_.size()) < nx)
        return false;
    // (n - 1) * factor + 1 keeps both endpoints and puts `factor` new cells
    // where each computed cell was — so the refined grid still contains every
    // original node, at which every scheme here reproduces the input exactly.
    const int rx = (nx - 1) * factor + 1;
    const int ry = (ny - 1) * factor + 1;
    const auto resize = [rx, ry](std::vector<std::vector<double>>& grid) {
        grid.assign(static_cast<std::size_t>(rx),
                    std::vector<double>(static_cast<std::size_t>(ry), 0.0));
    };
    resize(outKx);
    resize(outKy);
    resize(outEnergies);
    for (int i = 0; i < rx; ++i) {
        const double u = static_cast<double>(i) / factor;
        for (int j = 0; j < ry; ++j) {
            const double v = static_cast<double>(j) / factor;
            const auto a = static_cast<std::size_t>(i);
            const auto b = static_cast<std::size_t>(j);
            // The k-grid is refined with the SAME scheme as the energies: a
            // non-orthogonal reciprocal cell has a sheared k-grid, and
            // refining E on a linearly-spaced k would slide the surface off
            // the points it was computed at.
            outKx[a][b] = sampleGrid(kx_, u, v, cubic);
            outKy[a][b] = sampleGrid(ky_, u, v, cubic);
            outEnergies[a][b] = sampleGrid(energies, u, v, cubic);
        }
    }
    return true;
}

void TwoDBandsWindow::rebuild()
{
    if (!canvas_ || surfaces_.empty())
        return;

    const bool shift = shiftFermiCheck_ && shiftFermiCheck_->isChecked();
    const double zero = shift ? fermiEv_ : 0.0;
    const double scale = energyScaleSpin_ ? energyScaleSpin_->value() : 1.0;
    const auto& gradients = volumetricGradients();
    const int gi = gradientCombo_ ? gradientCombo_->currentIndex() : 0;
    const render::ColorGradient gradient =
        (gi >= 0 && gi < gradients.size()) ? gradients.at(gi)
                                           : render::ColorGradient::Viridis;

    // Colour range over the SELECTED surfaces only, so a ramp is not wasted on
    // bands that are not on screen.
    double lo = 0.0;
    double hi = 0.0;
    bool first = true;
    std::vector<int> selected;
    for (int i = 0; i < bandList_->count(); ++i) {
        if (bandList_->item(i)->checkState() != Qt::Checked)
            continue;
        const int index = bandList_->item(i)->data(Qt::UserRole).toInt();
        if (index < 0 || index >= static_cast<int>(surfaces_.size()))
            continue;
        selected.push_back(index);
        const Surface& s = surfaces_[static_cast<std::size_t>(index)];
        lo = first ? s.minEv : std::min(lo, s.minEv);
        hi = first ? s.maxEv : std::max(hi, s.maxEv);
        first = false;
    }
    const double range = std::max(hi - lo, 1e-12);

    std::vector<float> mesh;
    const auto height = [&](double energy) {
        return static_cast<float>((energy - zero) * scale);
    };
    const bool clipToZone = brillouinCheck_ && brillouinCheck_->isChecked();
    const auto planes =
        clipToZone ? brillouinHalfPlanes() : std::vector<std::array<double, 3>>{};

    // Scratch buffers reused across bands so a ×8 refinement of a 64×64 grid
    // does not reallocate once per surface.
    std::vector<std::vector<double>> rkx;
    std::vector<std::vector<double>> rky;
    std::vector<std::vector<double>> renergies;

    for (const int index : selected) {
        const Surface& s = surfaces_[static_cast<std::size_t>(index)];
        const bool refined = refine(s.energies, rkx, rky, renergies);
        const std::vector<std::vector<double>>& gx = refined ? rkx : kx_;
        const std::vector<std::vector<double>>& gy = refined ? rky : ky_;
        const std::vector<std::vector<double>>& ge =
            refined ? renergies : s.energies;

        const auto color = [&](double e) {
            return render::ColorMap::sample(
                gradient, static_cast<float>((e - lo) / range));
        };
        // One triangle, optionally clipped to the zone. Clipping a triangle
        // against a convex region yields a convex polygon of up to 3 + planes
        // vertices, which is fan-triangulated back out.
        const auto emit = [&](const QVector3D& a, double ea, const QVector3D& b,
                              double eb, const QVector3D& c, double ec) {
            QVector3D normal = QVector3D::crossProduct(b - a, c - a).normalized();
            // Face the +z half-space: the surface is a graph over the k-plane,
            // so a downward normal is only ever a winding artifact and would
            // leave that triangle unlit.
            if (normal.z() < 0.0f)
                normal = -normal;
            // Energy travels as a 4th channel would, but the clipper only
            // carries xyz — so the colour is taken from the interpolated
            // HEIGHT, which is an affine function of the energy and therefore
            // exact under the same linear interpolation.
            const auto energyOf = [&](const QVector3D& p) {
                return zero + static_cast<double>(p.z()) / scale;
            };
            if (planes.empty()) {
                pushVertex(mesh, a.x(), a.y(), a.z(), normal, color(ea));
                pushVertex(mesh, b.x(), b.y(), b.z(), normal, color(eb));
                pushVertex(mesh, c.x(), c.y(), c.z(), normal, color(ec));
                return;
            }
            std::vector<QVector3D> polygon = {a, b, c};
            for (const auto& plane : planes) {
                polygon = clipHalfPlane(polygon, plane[0], plane[1], plane[2]);
                if (polygon.size() < 3)
                    return; // entirely outside the zone
            }
            for (std::size_t k = 1; k + 1 < polygon.size(); ++k) {
                pushVertex(mesh, polygon[0].x(), polygon[0].y(), polygon[0].z(),
                           normal, color(energyOf(polygon[0])));
                pushVertex(mesh, polygon[k].x(), polygon[k].y(), polygon[k].z(),
                           normal, color(energyOf(polygon[k])));
                pushVertex(mesh, polygon[k + 1].x(), polygon[k + 1].y(),
                           polygon[k + 1].z(), normal,
                           color(energyOf(polygon[k + 1])));
            }
        };

        const std::size_t nx = ge.size();
        for (std::size_t i = 0; i + 1 < nx; ++i) {
            const std::size_t ny = ge[i].size();
            for (std::size_t j = 0; j + 1 < ny; ++j) {
                if (i + 1 >= gx.size() || j + 1 >= gx[i].size()
                    || j + 1 >= ge[i + 1].size())
                    continue;
                const QVector3D p00(static_cast<float>(gx[i][j]),
                                    static_cast<float>(gy[i][j]),
                                    height(ge[i][j]));
                const QVector3D p10(static_cast<float>(gx[i + 1][j]),
                                    static_cast<float>(gy[i + 1][j]),
                                    height(ge[i + 1][j]));
                const QVector3D p01(static_cast<float>(gx[i][j + 1]),
                                    static_cast<float>(gy[i][j + 1]),
                                    height(ge[i][j + 1]));
                const QVector3D p11(static_cast<float>(gx[i + 1][j + 1]),
                                    static_cast<float>(gy[i + 1][j + 1]),
                                    height(ge[i + 1][j + 1]));
                // The quad's corners are not coplanar in general, so the
                // normal is taken per triangle; a shared one makes a saddle
                // look faceted exactly where the curvature matters.
                emit(p00, ge[i][j], p10, ge[i + 1][j], p11, ge[i + 1][j + 1]);
                emit(p00, ge[i][j], p11, ge[i + 1][j + 1], p01, ge[i][j + 1]);
            }
        }
    }
    canvas_->setMesh(std::move(mesh));

    // --- Guide lines: axes, the zone boundary, the Fermi plane -------------
    std::vector<float> lines;
    const auto line = [&lines](const QVector3D& a, const QVector3D& b,
                               const QColor& color) {
        pushVertex(lines, a.x(), a.y(), a.z(), QVector3D(0, 0, 1), color);
        pushVertex(lines, b.x(), b.y(), b.z(), QVector3D(0, 0, 1), color);
    };
    const auto extent = static_cast<float>(kExtent_);
    const float base = height(shift ? fermiEv_ : 0.0);
    std::vector<VolumeViewWidget::Label> labels;

    if (axesCheck_ && axesCheck_->isChecked()) {
        const QColor axisColor(150, 152, 160);
        line({-extent, 0.0f, base}, {extent, 0.0f, base}, axisColor);
        line({0.0f, -extent, base}, {0.0f, extent, base}, axisColor);
        // The energy axis spans the drawn range rather than a fixed length:
        // its whole job is to say how tall the vertical exaggeration has made
        // the plot.
        line({0.0f, 0.0f, height(lo)}, {0.0f, 0.0f, height(hi)}, axisColor);
        labels.push_back({{extent, 0.0f, base}, QStringLiteral("k_x (Å⁻¹)"),
                          axisColor});
        labels.push_back({{0.0f, extent, base}, QStringLiteral("k_y (Å⁻¹)"),
                          axisColor});
        labels.push_back({{0.0f, 0.0f, height(hi)},
                          shift ? tr("E − E_F (eV)") : tr("E (eV)"),
                          axisColor});
    }

    if (clipToZone) {
        // The zone outline is drawn at the Fermi plane's height, where it
        // reads as the footprint of the region the surface now covers.
        const auto polygon = brillouinPolygon();
        const QColor zoneColor(120, 190, 255);
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const auto& a = polygon[i];
            const auto& b = polygon[(i + 1) % polygon.size()];
            line({static_cast<float>(a[0]), static_cast<float>(a[1]), base},
                 {static_cast<float>(b[0]), static_cast<float>(b[1]), base},
                 zoneColor);
        }
    }

    if (fermiPlaneCheck_ && fermiPlaneCheck_->isChecked()) {
        const float z = height(fermiEv_);
        const QColor fermiColor(217, 83, 79);
        line({-extent, -extent, z}, {extent, -extent, z}, fermiColor);
        line({extent, -extent, z}, {extent, extent, z}, fermiColor);
        line({extent, extent, z}, {-extent, extent, z}, fermiColor);
        line({-extent, extent, z}, {-extent, -extent, z}, fermiColor);
    }

    if (labelsCheck_ && labelsCheck_->isChecked()) {
        const QColor pointColor(255, 214, 120);
        for (const SpecialPoint& point : specialPoints_) {
            // Pinned to the Fermi plane rather than to a surface: which band
            // it would sit on is arbitrary when several are drawn, and a label
            // that jumps between sheets as the selection changes is worse than
            // one in a fixed place.
            labels.push_back({{static_cast<float>(point.kx),
                               static_cast<float>(point.ky), base},
                              point.label, pointColor});
        }
    }

    canvas_->setLines(std::move(lines));
    canvas_->setLabels(std::move(labels));

    // Frame the content: half the energy span in plot units, combined with the
    // k half-extent, is the radius that holds the whole surface stack.
    const float halfEnergy =
        static_cast<float>(std::abs(hi - lo) * scale * 0.5);
    canvas_->setBounds(
        QVector3D(0.0f, 0.0f, height(0.5 * (lo + hi))),
        std::max(extent * 1.2f, halfEnergy * 1.2f));
}

void TwoDBandsWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export 2D band surfaces"),
        QStringLiteral("bands_2d.png"), tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    if (!canvas_->grabFramebuffer().save(path))
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
}

void TwoDBandsWindow::exportData()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export 2D band data"), QStringLiteral("bands_2d.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    // Long format (one row per k-point per band) rather than a matrix per
    // band: it loads into pandas/gnuplot without reshaping, and the band index
    // stays attached to its values.
    writeTextFile(this, path, [this](QTextStream& out) {
        out << "band,spin,kx_per_A,ky_per_A,energy_eV,energy_minus_EF_eV\n";
        for (const Surface& s : surfaces_) {
            for (std::size_t i = 0; i < s.energies.size() && i < kx_.size(); ++i) {
                for (std::size_t j = 0;
                     j < s.energies[i].size() && j < kx_[i].size(); ++j) {
                    out << s.band << ',' << s.spin << ','
                        << QString::number(kx_[i][j], 'g', 8) << ','
                        << QString::number(ky_[i][j], 'g', 8) << ','
                        << QString::number(s.energies[i][j], 'g', 8) << ','
                        << QString::number(s.energies[i][j] - fermiEv_, 'g', 8)
                        << '\n';
                }
            }
        }
    });
}

} // namespace calango::gui
