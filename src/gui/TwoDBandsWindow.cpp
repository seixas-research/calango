#include "gui/TwoDBandsWindow.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/VolumeViewWidget.hpp"
#include "gui/VolumetricStyle.hpp"
#include "render/ColorMap.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

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

// The Wigner-Seitz construction of the first Brillouin zone, shared by the 3D
// surface clipper and the flat map view. Free functions taking the reciprocal
// rows explicitly: the surfaces build the zone from the file's
// reciprocal_2pi_per_A, the map from its own bz_map.reciprocal_A_inv, and two
// copies of a geometric derivation is how the two views drift apart.

/// The half-planes bounding the first Brillouin zone: k is inside when it is
/// no further from Γ than from any other reciprocal-lattice point G, i.e.
/// |k| <= |k - G|, which reduces to k·Ĝ <= |G|/2. Neighbours out to two cells
/// in each direction are more than enough — in 2D the cell is bounded by the
/// first or second shell for every Bravais lattice.
std::vector<std::array<double, 3>> zoneHalfPlanes(double b1x, double b1y,
                                                  double b2x, double b2y)
{
    std::vector<std::array<double, 3>> planes;
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

/// Vertices of the first-BZ polygon, counter-clockwise.
std::vector<std::array<double, 2>> zonePolygon(
    const std::vector<std::array<double, 3>>& planes)
{
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

// The map view's palette, shared with the effective-bands heatmap so the two
// hand-painted plots read as one family.
const QColor kMapBackground(18, 20, 24);
const QColor kMapText(210, 213, 220);
const QColor kMapFrame(120, 124, 134);
// And these two match the 3D view's zone outline and label colours, so the
// same objects keep the same colour across the selector.
const QColor kMapZone(120, 190, 255);
const QColor kMapGamma(255, 214, 120);

} // namespace

/// The flat map view: ONE band's E(k_x, k_y) painted as colour over the exact
/// first Brillouin zone.
///
/// The zone is produced by folding, not by clipping a sampled patch: every
/// pixel maps its Cartesian k to fractional coordinates along b1/b2, wraps
/// them periodically into the sampled cell, and interpolates bilinearly
/// between the four surrounding Monkhorst-Pack samples. The wrap IS the
/// periodic tiling — the zone's corners, which lie outside the sampled
/// parallelogram, take their values from the equivalent points inside it, and
/// because the mesh never duplicates the cell edge the interpolation is
/// seamless across it.
///
/// Deliberately not a Q_OBJECT: it declares no signals or slots of its own,
/// and staying moc-free is what lets it live inside this .cpp next to the
/// window that owns it. User-visible strings borrow TwoDBandsWindow's
/// translation context for the same reason.
class BzMapView : public QWidget {
public:
    /// The parsed "bz_map" object, exactly as the generator writes it.
    struct Data {
        int n = 0;
        std::vector<std::array<double, 2>> kptsFrac; ///< nk × (f1, f2)
        std::vector<std::vector<double>> energies;   ///< [k][band], eV
        double efermi = 0.0;
        /// In-plane reciprocal rows b1, b2 (Å⁻¹, 2π included).
        double b1x = 0.0, b1y = 0.0, b2x = 0.0, b2y = 0.0;

        /// Bands present at EVERY mesh point — the minimum row length, so a
        /// truncated row cannot push an index out of range mid-image.
        int bandCount() const
        {
            if (energies.empty())
                return 0;
            std::size_t count = energies.front().size();
            for (const auto& row : energies)
                count = std::min(count, row.size());
            return static_cast<int>(count);
        }

        bool valid() const
        {
            const std::size_t nk =
                static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
            return n >= 2 && nk > 0 && kptsFrac.size() == nk
                && energies.size() == nk && bandCount() > 0
                && std::abs(b1x * b2y - b1y * b2x) > 1e-12;
        }
    };

    explicit BzMapView(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(320, 280);
    }

    void setData(Data data)
    {
        data_ = std::move(data);
        planes_.clear();
        polygon_.clear();
        if (data_.valid()) {
            planes_ = zoneHalfPlanes(data_.b1x, data_.b1y, data_.b2x, data_.b2y);
            polygon_ = zonePolygon(planes_);
            // Mesh origin and spacing are read off the samples themselves
            // rather than re-derived from n, so a changed mesh convention in
            // the generator cannot silently shear the fold. Ordering matches
            // the generator's ravel: index = i1 * n + i2, f1 slowest.
            const auto n = static_cast<std::size_t>(data_.n);
            f0a_ = data_.kptsFrac[0][0];
            f0b_ = data_.kptsFrac[0][1];
            stepA_ = data_.kptsFrac[n][0] - data_.kptsFrac[0][0];
            stepB_ = data_.kptsFrac[1][1] - data_.kptsFrac[0][1];
        }
        if (polygon_.size() < 3 || std::abs(stepA_) < 1e-12
            || std::abs(stepB_) < 1e-12) {
            // Unusable geometry is indistinguishable from no data: better an
            // honest empty view than a map folded through a degenerate cell.
            data_ = {};
            planes_.clear();
            polygon_.clear();
        }
        if (!polygon_.empty()) {
            boundsMinX_ = boundsMaxX_ = polygon_.front()[0];
            boundsMinY_ = boundsMaxY_ = polygon_.front()[1];
            for (const auto& vertex : polygon_) {
                boundsMinX_ = std::min(boundsMinX_, vertex[0]);
                boundsMaxX_ = std::max(boundsMaxX_, vertex[0]);
                boundsMinY_ = std::min(boundsMinY_, vertex[1]);
                boundsMaxY_ = std::max(boundsMaxY_, vertex[1]);
            }
            const double margin = 0.04
                * std::max(boundsMaxX_ - boundsMinX_, boundsMaxY_ - boundsMinY_);
            boundsMinX_ -= margin;
            boundsMaxX_ += margin;
            boundsMinY_ -= margin;
            boundsMaxY_ += margin;
        }
        band_ = defaultBand();
        dirty_ = true;
        update();
    }

    bool hasData() const { return data_.valid(); }
    int bandCount() const { return data_.bandCount(); }
    int band() const { return band_; }
    int meshSamples() const { return data_.n; }

    /// The band whose energies come closest to E_F anywhere in the zone: a
    /// metal's crossing band gives distance zero, an insulator's frontier
    /// band the gap edge — either way, the band the map is opened for.
    int defaultBand() const
    {
        const int bands = data_.bandCount();
        int best = 0;
        double bestDistance = std::numeric_limits<double>::max();
        for (int b = 0; b < bands; ++b) {
            double distance = std::numeric_limits<double>::max();
            for (const auto& row : data_.energies)
                distance = std::min(
                    distance,
                    std::abs(row[static_cast<std::size_t>(b)] - data_.efermi));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = b;
            }
        }
        return best;
    }

    void setBand(int band)
    {
        const int clamped =
            std::clamp(band, 0, std::max(0, data_.bandCount() - 1));
        if (clamped == band_)
            return;
        band_ = clamped;
        dirty_ = true;
        update();
    }

    void setShiftFermi(bool shift)
    {
        if (shift_ == shift)
            return;
        // Labels only: the colour ramp always spans the selected band's own
        // range, so shifting the reference changes what the numbers SAY, not
        // what the picture shows — no image rebuild needed.
        shift_ = shift;
        update();
    }

    void setGradient(render::ColorGradient gradient)
    {
        if (gradient_ == gradient)
            return;
        gradient_ = gradient;
        dirty_ = true;
        update();
    }

    void exportImage(QWidget* dialogParent)
    {
        if (!data_.valid()) {
            QMessageBox::information(dialogParent,
                                     TwoDBandsWindow::tr("Export Image"),
                                     TwoDBandsWindow::tr("No map loaded."));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            dialogParent, TwoDBandsWindow::tr("Export Brillouin-zone map"),
            QStringLiteral("bands_2d_bz_map.png"),
            TwoDBandsWindow::tr("PNG image (*.png)"));
        if (path.isEmpty())
            return;
        // 3x for print, matching the effective-bands heatmap exporter.
        QImage image(width() * 3, height() * 3, QImage::Format_ARGB32);
        image.fill(kMapBackground);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(3.0, 3.0);
        render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        painter.end();
        if (!image.save(path)) {
            QMessageBox::critical(
                dialogParent, TwoDBandsWindow::tr("Export Image"),
                TwoDBandsWindow::tr("Could not write %1").arg(path));
        }
    }

    void exportCsv(QWidget* dialogParent)
    {
        if (!data_.valid()) {
            QMessageBox::information(dialogParent,
                                     TwoDBandsWindow::tr("Export CSV"),
                                     TwoDBandsWindow::tr("No map loaded."));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            dialogParent, TwoDBandsWindow::tr("Export Brillouin-zone map data"),
            QStringLiteral("bands_2d_bz_map.csv"),
            TwoDBandsWindow::tr("CSV (*.csv)"));
        if (path.isEmpty())
            return;
        // The SAMPLED mesh, not the folded pixels: these are the computed
        // eigenvalues at the computed k-points, which is what a re-plot or a
        // fit wants — the fold is presentation, and anyone can repeat it from
        // these rows.
        writeTextFile(dialogParent, path, [this](QTextStream& out) {
            out << "kx_frac,ky_frac,kx_1_per_A,ky_1_per_A,energy_eV\n";
            for (std::size_t k = 0; k < data_.kptsFrac.size(); ++k) {
                const double fa = data_.kptsFrac[k][0];
                const double fb = data_.kptsFrac[k][1];
                out << QString::number(fa, 'g', 8) << ','
                    << QString::number(fb, 'g', 8) << ','
                    << QString::number(fa * data_.b1x + fb * data_.b2x, 'g', 8)
                    << ','
                    << QString::number(fa * data_.b1y + fb * data_.b2y, 'g', 8)
                    << ','
                    << QString::number(
                           data_.energies[k][static_cast<std::size_t>(band_)],
                           'g', 8)
                    << '\n';
            }
        });
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), kMapBackground);

        if (!data_.valid()) {
            painter.setPen(QColor(150, 150, 150));
            painter.drawText(
                rect(), Qt::AlignCenter | Qt::TextWordWrap,
                TwoDBandsWindow::tr(
                    "This run carries no Brillouin-zone map.\n"
                    "Re-run 2D Bands with \"Also sample the full first "
                    "Brillouin zone\" enabled."));
            return;
        }
        if (dirty_) {
            rebuildImage();
            dirty_ = false;
        }

        QFont font = painter.font();
        font.setPointSizeF(15.0); // matches the band/PDOS and heatmap plots
        painter.setFont(font);

        // Margins: caption row below, rotated caption left, colourbar with
        // its labels and rotated title on the right.
        const QRectF plot = rect().adjusted(52, 14, -118, -46);
        if (plot.width() < 40 || plot.height() < 40 || image_.isNull())
            return;

        // Fit the zone's bounding box into the plot rect PRESERVING aspect:
        // the whole point of the map is that the zone's geometry is exact,
        // and an anisotropic stretch would turn a hexagon into "roughly a
        // hexagon".
        const double spanX = std::max(boundsMaxX_ - boundsMinX_, 1e-12);
        const double spanY = std::max(boundsMaxY_ - boundsMinY_, 1e-12);
        const double scale =
            std::min(plot.width() / spanX, plot.height() / spanY);
        const QRectF target(plot.center().x() - 0.5 * spanX * scale,
                            plot.center().y() - 0.5 * spanY * scale,
                            spanX * scale, spanY * scale);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, image_);

        const auto toScreen = [&](double kx, double ky) {
            return QPointF(target.left() + (kx - boundsMinX_) * scale,
                           target.top() + (boundsMaxY_ - ky) * scale);
        };

        // The zone boundary — here it is the frame: the coloured region is
        // polygon-shaped, so a rectangular frame would outline nothing real.
        QPolygonF outline;
        for (const auto& vertex : polygon_)
            outline << toScreen(vertex[0], vertex[1]);
        painter.setPen(QPen(kMapZone, 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(outline);

        // Γ, the one point every reader orients by.
        const QPointF gamma = toScreen(0.0, 0.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(kMapGamma);
        painter.drawEllipse(gamma, 3.0, 3.0);
        painter.setPen(kMapGamma);
        painter.drawText(QPointF(gamma.x() + 6.0, gamma.y() - 6.0),
                         QStringLiteral("Γ"));

        // Axis captions, same spelling as the 3D view's axis labels.
        painter.setPen(kMapText);
        drawWithSubscripts(
            painter,
            QRectF(target.left(), plot.bottom() + 14, target.width(), 24),
            QStringLiteral("k_x (Å⁻¹)"));
        painter.save();
        painter.translate(18.0, target.center().y());
        painter.rotate(-90.0);
        drawWithSubscripts(painter,
                           QRectF(-target.height() / 2.0, -12,
                                  target.height(), 24),
                           QStringLiteral("k_y (Å⁻¹)"));
        painter.restore();

        // Colourbar: the ramp with its endpoints named, which is what turns
        // a pretty picture back into numbers.
        const QRectF bar(plot.right() + 22.0, target.top(), 16.0,
                         target.height());
        const int steps = std::max(2, static_cast<int>(bar.height()));
        for (int i = 0; i < steps; ++i) {
            const double t = 1.0 - static_cast<double>(i) / (steps - 1);
            painter.fillRect(
                QRectF(bar.left(), bar.top() + i, bar.width(), 1.5),
                render::ColorMap::sample(gradient_, static_cast<float>(t)));
        }
        painter.setPen(QPen(kMapFrame, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(bar);
        const double shownLo = shift_ ? lo_ - data_.efermi : lo_;
        const double shownHi = shift_ ? hi_ - data_.efermi : hi_;
        painter.setPen(kMapText);
        painter.drawText(QRectF(bar.right() + 5, bar.top() - 11, 72, 22),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(shownHi, 'f', 2));
        painter.drawText(QRectF(bar.right() + 5, bar.bottom() - 11, 72, 22),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(shownLo, 'f', 2));
        painter.save();
        painter.translate(width() - 10.0, bar.center().y());
        painter.rotate(-90.0);
        drawWithSubscripts(painter,
                           QRectF(-bar.height() / 2.0, -12, bar.height(), 24),
                           shift_ ? TwoDBandsWindow::tr("E − E_F (eV)")
                                  : TwoDBandsWindow::tr("E (eV)"));
        painter.restore();
    }

private:
    void rebuildImage()
    {
        image_ = QImage();
        const int bands = data_.bandCount();
        if (!data_.valid() || band_ < 0 || band_ >= bands
            || polygon_.size() < 3)
            return;

        // Colour range over the selected band's own extremes, so the full
        // ramp is always in use whatever the band's bandwidth is.
        lo_ = hi_ = data_.energies.front()[static_cast<std::size_t>(band_)];
        for (const auto& row : data_.energies) {
            const double e = row[static_cast<std::size_t>(band_)];
            lo_ = std::min(lo_, e);
            hi_ = std::max(hi_, e);
        }
        const double range = std::max(hi_ - lo_, 1e-12);

        // Fixed resolution rather than the widget's: enough that the zone
        // boundary stays crisp at typical window sizes and acceptable in the
        // 3x export, cheap enough (~1 M pixels) to rebuild on a band switch.
        const double spanX = std::max(boundsMaxX_ - boundsMinX_, 1e-12);
        const double spanY = std::max(boundsMaxY_ - boundsMinY_, 1e-12);
        const int imageW = 960;
        const int imageH = std::clamp(
            static_cast<int>(std::lround(imageW * spanY / spanX)), 64, 2048);
        image_ = QImage(imageW, imageH, QImage::Format_ARGB32);
        image_.fill(Qt::transparent);

        const int n = data_.n;
        const double det =
            data_.b1x * data_.b2y - data_.b1y * data_.b2x; // nonzero: valid()
        const auto energyAt = [&](int i, int j) {
            const std::size_t k = static_cast<std::size_t>(i)
                    * static_cast<std::size_t>(n)
                + static_cast<std::size_t>(j);
            return data_.energies[k][static_cast<std::size_t>(band_)];
        };

        for (int py = 0; py < imageH; ++py) {
            // Direct scanline writes: setPixelColor would construct a QColor
            // per pixel, and a million of those is the difference between an
            // instant band switch and a sluggish one.
            auto* row = reinterpret_cast<QRgb*>(image_.scanLine(py));
            const double ky =
                boundsMaxY_ - (py + 0.5) * spanY / imageH;
            for (int px = 0; px < imageW; ++px) {
                const double kx =
                    boundsMinX_ + (px + 0.5) * spanX / imageW;
                // First-zone test against the Wigner-Seitz half-planes, with
                // a whisker of tolerance so the boundary itself is painted
                // rather than left as a one-pixel seam.
                bool inside = true;
                for (const auto& plane : planes_) {
                    if (kx * plane[0] + ky * plane[1]
                        > plane[2] + 1e-7 * plane[2]) {
                        inside = false;
                        break;
                    }
                }
                if (!inside)
                    continue;
                // Fold: Cartesian -> fractional along b1/b2, then wrap into
                // the sampled cell. The wrap is the periodic tiling, and
                // bilinear interpolation ACROSS the wrap is exact because the
                // Monkhorst-Pack mesh never duplicates the cell edge.
                const double fa = (kx * data_.b2y - ky * data_.b2x) / det;
                const double fb = (data_.b1x * ky - data_.b1y * kx) / det;
                const double u = (fa - f0a_) / stepA_;
                const double v = (fb - f0b_) / stepB_;
                int i0 = static_cast<int>(std::floor(u));
                int j0 = static_cast<int>(std::floor(v));
                const double tu = u - i0;
                const double tv = v - j0;
                i0 = ((i0 % n) + n) % n;
                j0 = ((j0 % n) + n) % n;
                const int i1 = (i0 + 1) % n;
                const int j1 = (j0 + 1) % n;
                const double e = (1.0 - tu)
                        * ((1.0 - tv) * energyAt(i0, j0)
                           + tv * energyAt(i0, j1))
                    + tu
                        * ((1.0 - tv) * energyAt(i1, j0)
                           + tv * energyAt(i1, j1));
                row[px] = render::ColorMap::sample(
                              gradient_,
                              static_cast<float>((e - lo_) / range))
                              .rgba();
            }
        }
    }

    Data data_;
    std::vector<std::array<double, 3>> planes_;
    std::vector<std::array<double, 2>> polygon_;
    double boundsMinX_ = 0.0, boundsMaxX_ = 0.0;
    double boundsMinY_ = 0.0, boundsMaxY_ = 0.0;
    /// Mesh origin and spacing in fractional coordinates, per axis.
    double f0a_ = 0.0, f0b_ = 0.0, stepA_ = 0.0, stepB_ = 0.0;
    int band_ = 0;
    bool shift_ = true;
    render::ColorGradient gradient_ = render::ColorGradient::Viridis;
    double lo_ = 0.0, hi_ = 0.0;
    QImage image_;
    bool dirty_ = true;
};

TwoDBandsWindow::TwoDBandsWindow(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("2D Band Surfaces"));
    resize(940, 680);

    auto* layout = new QVBoxLayout(this);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::RichText);
    layout->addWidget(summary_);

    // Two projections of the same run behind one selector: the orbitable
    // surfaces (every selected band, dispersion as shape) and the flat map
    // (one band's energy as colour over the zone's exact geometry). A
    // selector rather than a split view because each projection wants the
    // whole canvas.
    auto* viewRow = new QHBoxLayout;
    viewRow->addWidget(new QLabel(tr("View:"), this));
    viewCombo_ = new QComboBox(this);
    viewCombo_->addItem(tr("Band surfaces (3D)"));
    viewCombo_->addItem(tr("Brillouin-zone map"));
    viewCombo_->setToolTip(
        tr("Band surfaces show every selected band's dispersion as shape; "
           "the Brillouin-zone map paints one band's energy over the first "
           "Brillouin zone, folded from the run's sampled reciprocal cell."));
    connect(viewCombo_, &QComboBox::currentIndexChanged, this,
            [this](int index) { viewStack_->setCurrentIndex(index); });
    viewRow->addWidget(viewCombo_);
    viewRow->addStretch(1);
    layout->addLayout(viewRow);

    viewStack_ = new QStackedWidget(this);

    auto* surfacePage = new QWidget(viewStack_);
    auto* body = new QHBoxLayout(surfacePage);
    body->setContentsMargins(0, 0, 0, 0);

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
    connect(canvas_, &VolumeViewWidget::viewChanged, this,
            &TwoDBandsWindow::syncOrientationReadout);
    body->addWidget(canvas_, 1);

    // Everything configurable lives in one column to the RIGHT of the canvas.
    // It used to be two rows underneath, which cost the plot vertical space —
    // the scarce dimension for a surface seen in perspective — and put related
    // controls (the colour ramp and the solid-colour swatch) on different
    // lines with unrelated ones between them.
    body->addWidget(buildSettingsPanel());
    viewStack_->addWidget(surfacePage);
    viewStack_->addWidget(buildMapPage());
    layout->addWidget(viewStack_, 1);
}

QWidget* TwoDBandsWindow::buildMapPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    mapView_ = new BzMapView(page);

    // One row of controls rather than a side panel: the map has exactly three
    // knobs, and the zone wants the full width for its geometry to read true.
    auto* controls = new QHBoxLayout;

    controls->addWidget(new QLabel(tr("Band:"), page));
    mapBandSpin_ = new QSpinBox(page);
    mapBandSpin_->setRange(0, 0);
    mapBandSpin_->setToolTip(
        tr("Which band the map colours, by index into the run's own energy "
           "ordering at each k-point (a spin-polarized run's two channels "
           "are merged and sorted).\n\n"
           "Opens on the band nearest the Fermi level — a metal's crossing "
           "band, an insulator's gap edge."));
    connect(mapBandSpin_, &QSpinBox::valueChanged, this,
            [this](int band) { mapView_->setBand(band); });
    controls->addWidget(mapBandSpin_);

    QCheckBox* shift = nullptr;
    QWidget* shiftRow = richTextCheckBox(tr("E − E<sub>F</sub>"), shift, page);
    mapShiftFermiCheck_ = shift;
    mapShiftFermiCheck_->setChecked(true);
    shiftRow->setToolTip(
        tr("Label the colour scale relative to the Fermi level. The picture "
           "is unchanged — the ramp always spans the selected band's full "
           "range."));
    connect(mapShiftFermiCheck_, &QCheckBox::toggled, this,
            [this](bool on) { mapView_->setShiftFermi(on); });
    controls->addWidget(shiftRow);

    controls->addWidget(new QLabel(tr("Colormap:"), page));
    mapGradientCombo_ = new QComboBox(page);
    // The effective-bands heatmap's shortlist, for the same reason it is
    // short there: these five cover the sequential, diverging and
    // colour-blind-safe cases without burying the choice in near-duplicates.
    mapGradientCombo_->addItem(
        tr("Viridis"), static_cast<int>(render::ColorGradient::Viridis));
    mapGradientCombo_->addItem(
        tr("Plasma"), static_cast<int>(render::ColorGradient::Plasma));
    mapGradientCombo_->addItem(
        tr("Coolwarm"), static_cast<int>(render::ColorGradient::Coolwarm));
    mapGradientCombo_->addItem(
        tr("Inferno"), static_cast<int>(render::ColorGradient::Inferno));
    mapGradientCombo_->addItem(
        tr("Cividis"), static_cast<int>(render::ColorGradient::Cividis));
    connect(mapGradientCombo_, &QComboBox::currentIndexChanged, this, [this] {
        mapView_->setGradient(static_cast<render::ColorGradient>(
            mapGradientCombo_->currentData().toInt()));
    });
    controls->addWidget(mapGradientCombo_);
    controls->addStretch(1);

    auto* exportImageButton = new QPushButton(tr("Export Image…"), page);
    connect(exportImageButton, &QPushButton::clicked, this,
            [this] { mapView_->exportImage(this); });
    controls->addWidget(exportImageButton);
    auto* exportCsvButton = new QPushButton(tr("Export CSV…"), page);
    exportCsvButton->setToolTip(
        tr("The sampled mesh, one row per k-point of the selected band — the "
           "computed numbers, not the folded pixels."));
    connect(exportCsvButton, &QPushButton::clicked, this,
            [this] { mapView_->exportCsv(this); });
    controls->addWidget(exportCsvButton);
    layout->addLayout(controls);

    layout->addWidget(mapView_, 1);
    return page;
}

QWidget* TwoDBandsWindow::buildSettingsPanel()
{
    auto* panel = new QWidget(this);
    panel->setFixedWidth(268);
    auto* outer = new QVBoxLayout(panel);
    outer->setContentsMargins(0, 0, 0, 0);

    // Scrollable: the panel holds four groups and a short dialog would
    // otherwise clip the last one with no way to reach it.
    auto* scroll = new QScrollArea(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget(scroll);
    auto* layout = new QVBoxLayout(inner);
    layout->setContentsMargins(0, 0, 6, 0);

    // ---------------------------------------------------------------- View --
    auto* viewGroup = new QGroupBox(tr("View"), inner);
    auto* viewLayout = new QVBoxLayout(viewGroup);

    auto* viewNote = new QLabel(
        tr("Drag to rotate (arcball); drag outside the centre circle to roll. "
           "Shift-drag or middle-drag pans, the wheel zooms, double-click "
           "re-frames."),
        viewGroup);
    viewNote->setWordWrap(true);
    viewLayout->addWidget(viewNote);

    // Named orientations. The arcball is good at exploring and bad at landing
    // exactly on an axis, which is what a reproducible figure needs.
    auto* presetGrid = new QGridLayout;
    const struct {
        QString label;
        float yaw, pitch, roll;
        QString tip;
    } kPresets[] = {
        {tr("Top"), 0.0f, -90.0f, 0.0f,
         tr("Straight down the energy axis — the surface as a map over the "
            "k-plane, which is where a Fermi contour is read off.")},
        {tr("Front"), 0.0f, 0.0f, 0.0f, tr("Along −k_y, energy up.")},
        {tr("Side"), 90.0f, 0.0f, 0.0f, tr("Along −k_x, energy up.")},
        {tr("Iso"), 0.0f, -70.0f, 20.0f,
         tr("The default three-quarter view: all three axes at once.")},
    };
    for (int i = 0; i < 4; ++i) {
        auto* button = new QPushButton(kPresets[i].label, viewGroup);
        button->setToolTip(kPresets[i].tip);
        const float y = kPresets[i].yaw;
        const float p = kPresets[i].pitch;
        const float r = kPresets[i].roll;
        connect(button, &QPushButton::clicked, this, [this, y, p, r] {
            canvas_->setViewOrientation(y, p, r);
            syncOrientationReadout();
        });
        presetGrid->addWidget(button, i / 2, i % 2);
    }
    viewLayout->addLayout(presetGrid);

    // Nudges about the CAMERA axes, so each arrow does what it looks like
    // whatever the current orientation is.
    auto* nudgeGrid = new QGridLayout;
    const struct {
        QString label;
        int axis;
        float degrees;
        int row, column;
    } kNudges[] = {
        {QStringLiteral("↑"), 0, -15.0f, 0, 1},
        {QStringLiteral("←"), 1, -15.0f, 1, 0},
        {QStringLiteral("→"), 1, 15.0f, 1, 2},
        {QStringLiteral("↓"), 0, 15.0f, 2, 1},
    };
    for (const auto& nudge : kNudges) {
        auto* button = new QPushButton(nudge.label, viewGroup);
        button->setFixedWidth(38);
        button->setToolTip(tr("Turn 15° about the screen %1 axis.")
                               .arg(nudge.axis == 0 ? tr("horizontal")
                                                    : tr("vertical")));
        const int axis = nudge.axis;
        const float degrees = nudge.degrees;
        connect(button, &QPushButton::clicked, this, [this, axis, degrees] {
            canvas_->nudgeView(axis, degrees);
            syncOrientationReadout();
        });
        nudgeGrid->addWidget(button, nudge.row, nudge.column);
    }
    auto* resetButton = new QPushButton(tr("Reset"), viewGroup);
    resetButton->setToolTip(tr("Back to the default view, re-framed."));
    connect(resetButton, &QPushButton::clicked, this, [this] {
        canvas_->resetView();
        syncOrientationReadout();
    });
    nudgeGrid->addWidget(resetButton, 1, 1);
    viewLayout->addLayout(nudgeGrid);

    auto* rollRow = new QHBoxLayout;
    rollRow->addWidget(new QLabel(tr("Roll:"), viewGroup));
    rollSlider_ = new QSlider(Qt::Horizontal, viewGroup);
    rollSlider_->setRange(-180, 180);
    rollSlider_->setValue(0);
    rollSlider_->setToolTip(
        tr("Turn the picture in the screen plane. The viewpoint and the "
           "surface are unchanged — this is the camera tilting its head, which "
           "is how a figure is levelled without re-orbiting it."));
    connect(rollSlider_, &QSlider::valueChanged, this, [this](int value) {
        canvas_->setViewRoll(static_cast<float>(value));
        syncOrientationReadout();
    });
    rollRow->addWidget(rollSlider_, 1);
    viewLayout->addLayout(rollRow);

    orientationLabel_ = new QLabel(viewGroup);
    orientationLabel_->setWordWrap(true);
    viewLayout->addWidget(orientationLabel_);
    layout->addWidget(viewGroup);

    // ---------------------------------------------------------- Appearance --
    auto* appearance = new QGroupBox(tr("Appearance"), inner);
    auto* appearanceForm = new QFormLayout(appearance);

    colorModeCombo_ = new QComboBox(appearance);
    colorModeCombo_->addItem(tr("Colormap by energy"), 0);
    colorModeCombo_->addItem(tr("Solid colour"), 1);
    colorModeCombo_->setToolTip(
        tr("A colormap encodes energy as hue, which is what a single surface "
           "wants.\n\n"
           "Several surfaces at once are better told apart by giving each a "
           "flat colour — and a figure often wants one deliberate colour "
           "rather than a rainbow that competes with the shape."));
    appearanceForm->addRow(tr("Colouring:"), colorModeCombo_);
    connect(colorModeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        const bool solid = colorModeCombo_->currentData().toInt() == 1;
        gradientCombo_->setEnabled(!solid);
        solidColorButton_->setEnabled(solid);
        rebuild();
    });

    solidColorButton_ = new QPushButton(appearance);
    solidColorButton_->setFixedHeight(22);
    setButtonColor(solidColorButton_, solidColor_);
    solidColorButton_->setEnabled(false);
    solidColorButton_->setToolTip(tr("The single colour every drawn surface "
                                     "takes in solid mode."));
    connect(solidColorButton_, &QPushButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(
            solidColor_, this, tr("Band surface colour"));
        if (!chosen.isValid())
            return;
        solidColor_ = chosen;
        setButtonColor(solidColorButton_, solidColor_);
        rebuild();
    });
    appearanceForm->addRow(tr("Solid colour:"), solidColorButton_);

    gradientCombo_ = new QComboBox(appearance);
    gradientCombo_->addItems(volumetricGradientNames());
    gradientCombo_->setCurrentIndex(
        std::max(0, static_cast<int>(volumetricGradients().indexOf(
                        render::ColorGradient::Viridis))));
    connect(gradientCombo_, &QComboBox::currentIndexChanged, this,
            [this] { rebuild(); });
    appearanceForm->addRow(tr("Colormap:"), gradientCombo_);

    energyScaleSpin_ = new QDoubleSpinBox(appearance);
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
    appearanceForm->addRow(tr("Energy scale:"), energyScaleSpin_);

    QCheckBox* shift = nullptr;
    QWidget* shiftRow = richTextCheckBox(tr("E − E<sub>F</sub>"), shift, appearance);
    shiftFermiCheck_ = shift;
    shiftFermiCheck_->setChecked(true);
    shiftRow->setToolTip(tr("Plot energies relative to the Fermi level."));
    connect(shiftFermiCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    appearanceForm->addRow(shiftRow);

    fermiPlaneCheck_ = new QCheckBox(tr("Fermi plane"), appearance);
    fermiPlaneCheck_->setChecked(true);
    fermiPlaneCheck_->setToolTip(
        tr("Outline the plane at the Fermi level. Where a surface crosses it "
           "is the Fermi surface of the sheet."));
    connect(fermiPlaneCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    appearanceForm->addRow(fermiPlaneCheck_);
    layout->addWidget(appearance);

    // ------------------------------------------------------------- Surface --
    auto* surface = new QGroupBox(tr("Surface"), inner);
    auto* surfaceForm = new QFormLayout(surface);

    interpolationCombo_ = new QComboBox(surface);
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
    surfaceForm->addRow(tr("Interpolation:"), interpolationCombo_);

    refineSpin_ = new QSpinBox(surface);
    refineSpin_->setRange(1, 8);
    refineSpin_->setValue(3);
    refineSpin_->setPrefix(tr("×"));
    refineSpin_->setToolTip(
        tr("Refinement factor: how many drawn cells replace each computed one "
           "along each axis. The triangle count grows with its square."));
    connect(refineSpin_, &QSpinBox::valueChanged, this, [this] { rebuild(); });
    surfaceForm->addRow(tr("Refinement:"), refineSpin_);
    layout->addWidget(surface);

    // -------------------------------------------------------------- Domain --
    auto* domain = new QGroupBox(tr("Domain && annotation"), inner);
    auto* domainLayout = new QVBoxLayout(domain);

    axesCheck_ = new QCheckBox(tr("Axes"), domain);
    axesCheck_->setChecked(true);
    axesCheck_->setToolTip(
        tr("Draw the k_x, k_y and energy axes through the origin, with a tick "
           "and a caption on each. Turn them off for a clean figure of the "
           "surface alone."));
    connect(axesCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    domainLayout->addWidget(axesCheck_);

    brillouinCheck_ = new QCheckBox(tr("First Brillouin zone"), domain);
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
    domainLayout->addWidget(brillouinCheck_);

    labelsCheck_ = new QCheckBox(tr("k-point labels"), domain);
    labelsCheck_->setChecked(true);
    labelsCheck_->setToolTip(
        tr("Mark and name the high-symmetry points of this lattice (Γ, M, K, "
           "…). The labels come from ASE's Bravais-lattice recognition, so "
           "they are the conventional ones for the cell that was actually "
           "calculated."));
    connect(labelsCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    domainLayout->addWidget(labelsCheck_);
    layout->addWidget(domain);

    layout->addStretch(1);
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);

    auto* exportRow = new QHBoxLayout;
    auto* exportImageButton = new QPushButton(tr("Image…"), panel);
    connect(exportImageButton, &QPushButton::clicked, this,
            &TwoDBandsWindow::exportImage);
    exportRow->addWidget(exportImageButton);
    auto* exportDataButton = new QPushButton(tr("Data…"), panel);
    connect(exportDataButton, &QPushButton::clicked, this,
            &TwoDBandsWindow::exportData);
    exportRow->addWidget(exportDataButton);
    outer->addLayout(exportRow);

    syncOrientationReadout();
    return panel;
}

void TwoDBandsWindow::syncOrientationReadout()
{
    if (!orientationLabel_ || !canvas_)
        return;
    orientationLabel_->setText(
        tr("<span style='color:gray;'>yaw %1° · pitch %2° · roll %3°</span>")
            .arg(canvas_->viewYaw(), 0, 'f', 1)
            .arg(canvas_->viewPitch(), 0, 'f', 1)
            .arg(canvas_->viewRoll(), 0, 'f', 1));
    if (rollSlider_) {
        const QSignalBlocker blocker(rollSlider_);
        rollSlider_->setValue(static_cast<int>(std::lround(canvas_->viewRoll())));
    }
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

    // --- Optional Brillouin-zone map ---------------------------------------
    // Only runs whose wizard asked for it carry a "bz_map"; an older
    // bands_2d.json simply parses to an invalid Data here, which is the one
    // signal everything below keys off — no schema version, no second flag.
    BzMapView::Data map;
    const QJsonObject mapObject =
        root.value(QStringLiteral("bz_map")).toObject();
    if (!mapObject.isEmpty()) {
        map.n = mapObject.value(QStringLiteral("n")).toInt();
        for (const QJsonValue& value :
             mapObject.value(QStringLiteral("kpts_frac")).toArray()) {
            const QJsonArray pair = value.toArray();
            map.kptsFrac.push_back(
                {pair.at(0).toDouble(), pair.at(1).toDouble()});
        }
        map.energies =
            readGrid(mapObject.value(QStringLiteral("energies_eV")).toArray());
        map.efermi = mapObject.value(QStringLiteral("efermi_eV")).toDouble();
        const auto rows = readGrid(
            mapObject.value(QStringLiteral("reciprocal_A_inv")).toArray());
        if (rows.size() >= 2 && rows[0].size() >= 2 && rows[1].size() >= 2) {
            map.b1x = rows[0][0];
            map.b1y = rows[0][1];
            map.b2x = rows[1][0];
            map.b2y = rows[1][1];
        }
    }
    mapView_->setData(std::move(map));
    const bool hasMap = mapView_->hasData();

    // Without a map the selector's entry is greyed rather than removed: a
    // vanished option looks like a regression, a greyed one is an instruction
    // — and the instruction is its tooltip.
    if (auto* model = qobject_cast<QStandardItemModel*>(viewCombo_->model())) {
        if (QStandardItem* item = model->item(1)) {
            item->setEnabled(hasMap);
            item->setToolTip(
                hasMap ? QString()
                       : tr("This run carries no Brillouin-zone map. Re-run "
                            "2D Bands with \"Also sample the full first "
                            "Brillouin zone\" enabled."));
        }
    }
    if (!hasMap && viewCombo_->currentIndex() != 0)
        viewCombo_->setCurrentIndex(0);
    {
        // Blocked: setRange/setValue would otherwise push setBand at the view
        // that just chose this exact default for itself.
        const QSignalBlocker blocker(mapBandSpin_);
        mapBandSpin_->setRange(0, std::max(0, mapView_->bandCount() - 1));
        mapBandSpin_->setValue(mapView_->band());
    }
    mapBandSpin_->setEnabled(hasMap);

    summary_->setText(
        tr("<b>%1</b> band surface(s) on a %2 × %2 grid over the 2D Brillouin "
           "zone · E<sub>F</sub> = %3 eV%4%5")
            .arg(surfaces_.size())
            .arg(samples_)
            .arg(fermiEv_, 0, 'f', 4)
            .arg(spinOrbit_ ? tr(" · spin-orbit coupling included")
                            : QString())
            .arg(hasMap ? tr(" · Brillouin-zone map (%1 × %1 mesh)")
                              .arg(mapView_->meshSamples())
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
    // Delegates to the construction shared with the map view (the free
    // functions above): the surfaces build the zone from the file's
    // reciprocal_2pi_per_A rows, the map from its own bz_map copy, and one
    // derivation serving both is what keeps the two outlines congruent.
    return zoneHalfPlanes(reciprocal_[0][0], reciprocal_[0][1],
                          reciprocal_[1][0], reciprocal_[1][1]);
}

std::vector<std::array<double, 2>> TwoDBandsWindow::brillouinPolygon() const
{
    return zonePolygon(brillouinHalfPlanes());
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

        // Solid mode ignores the energy entirely — that is the point of it: the
        // height already carries the energy, so a flat colour frees hue to
        // separate one surface from another, or simply to make a printable
        // figure. The parameter stays so the two modes are interchangeable at
        // every call site below.
        const bool solid =
            colorModeCombo_ && colorModeCombo_->currentData().toInt() == 1;
        const auto color = [&](double e) {
            if (solid)
                return solidColor_;
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
