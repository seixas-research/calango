#include "gui/FermiSurfaceWindow.hpp"

#include "core/BrillouinZone.hpp"
#include "core/MarchingCubes.hpp"
#include "core/VolumetricData.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/VolumeViewWidget.hpp"
#include "render/ColorMap.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

void pushVertex(std::vector<float>& out, const QVector3D& p,
                const QVector3D& n, const QColor& c)
{
    out.insert(out.end(),
               {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(),
                static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                static_cast<float>(c.blueF())});
}

/// Clip a convex polygon against the half-space k·n ≤ d (Sutherland-Hodgman
/// in 3D). New vertices land on the plane by linear interpolation, which for
/// a triangle of an isosurface keeps the cut edge on the surface.
std::vector<QVector3D> clipHalfSpace(const std::vector<QVector3D>& polygon,
                                     const std::array<double, 4>& plane)
{
    std::vector<QVector3D> out;
    const auto n = polygon.size();
    const auto side = [&plane](const QVector3D& v) {
        return plane[0] * v.x() + plane[1] * v.y() + plane[2] * v.z() - plane[3];
    };
    for (std::size_t i = 0; i < n; ++i) {
        const QVector3D& a = polygon[i];
        const QVector3D& b = polygon[(i + 1) % n];
        const double da = side(a);
        const double db = side(b);
        if (da <= 0.0)
            out.push_back(a);
        if ((da < 0.0 && db > 0.0) || (da > 0.0 && db < 0.0))
            out.push_back(a + (b - a) * static_cast<float>(da / (da - db)));
    }
    return out;
}

} // namespace

FermiSurfaceWindow::FermiSurfaceWindow(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Fermi Surface"));
    resize(960, 700);

    auto* layout = new QVBoxLayout(this);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::RichText);
    layout->addWidget(summary_);

    auto* body = new QHBoxLayout;
    bandList_ = new QListWidget(this);
    bandList_->setSelectionMode(QAbstractItemView::NoSelection);
    bandList_->setMaximumWidth(230);
    bandList_->setToolTip(
        tr("One sheet per band that crosses the target energy. They are kept "
           "separate because which sheet is which carries the physics: an "
           "electron pocket and a hole pocket are different objects, and "
           "merging them into one surface hides that."));
    connect(bandList_, &QListWidget::itemChanged, this, [this] { rebuild(); });
    body->addWidget(bandList_);

    canvas_ = new VolumeViewWidget(this);
    canvas_->setMeshOpacity(0.92f);
    body->addWidget(canvas_, 1);
    layout->addLayout(body, 1);

    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Energy:"), this));
    energySpin_ = new QDoubleSpinBox(this);
    energySpin_->setRange(-50.0, 50.0);
    energySpin_->setDecimals(4);
    energySpin_->setSingleStep(0.05);
    energySpin_->setSuffix(tr(" eV"));
    energySpin_->setKeyboardTracking(false);
    energySpin_->setToolTip(
        tr("Energy the isosurface is taken at, relative to E_F.\n\n"
           "Scanning it is a rigid-band doping study: the surface at +0.2 eV "
           "is the one an n-doped sample would have, to the extent the bands "
           "do not themselves move."));
    connect(energySpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { rebuild(); });
    controls->addWidget(energySpin_);

    clipCheck_ = new QCheckBox(tr("Clip to first Brillouin zone"), this);
    clipCheck_->setChecked(true);
    clipCheck_->setToolTip(
        tr("Restrict the sheets to the Wigner-Seitz cell of the reciprocal "
           "lattice.\n\n"
           "The bands were interpolated on a grid spanning the reciprocal "
           "UNIT CELL — a parallelepiped, since that is what a regular grid "
           "fits. It covers the same volume as the zone but is not the same "
           "region, so without clipping the sheets extend past the zone "
           "boundary and fold pieces of the next zone into view."));
    connect(clipCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    controls->addWidget(clipCheck_);

    zoneCheck_ = new QCheckBox(tr("Zone edges"), this);
    zoneCheck_->setChecked(true);
    connect(zoneCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    controls->addWidget(zoneCheck_);

    labelsCheck_ = new QCheckBox(tr("Axes"), this);
    labelsCheck_->setChecked(true);
    connect(labelsCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    controls->addWidget(labelsCheck_);

    controls->addStretch(1);
    auto* exportImageButton = new QPushButton(tr("Export Image…"), this);
    connect(exportImageButton, &QPushButton::clicked, this,
            &FermiSurfaceWindow::exportImage);
    controls->addWidget(exportImageButton);
    layout->addLayout(controls);
}

std::vector<std::array<double, 4>> FermiSurfaceWindow::zoneHalfSpaces() const
{
    // Wigner-Seitz cell of the reciprocal lattice: k is inside when it is no
    // further from Γ than from any other reciprocal-lattice point G, which is
    // k·Ĝ ≤ |G|/2. Two shells is enough for any Niggli-reasonable cell.
    std::vector<std::array<double, 4>> planes;
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            for (int k = -2; k <= 2; ++k) {
                if (i == 0 && j == 0 && k == 0)
                    continue;
                const core::Vec3 g = reciprocal_[0] * static_cast<double>(i)
                    + reciprocal_[1] * static_cast<double>(j)
                    + reciprocal_[2] * static_cast<double>(k);
                const double length = g.norm();
                if (length < 1e-9)
                    continue;
                planes.push_back(
                    {g.x / length, g.y / length, g.z / length, 0.5 * length});
            }
        }
    }
    return planes;
}

bool FermiSurfaceWindow::loadResults(const QString& jsonPath)
{
    data_ = readJsonObject(jsonPath);
    if (data_.isEmpty())
        return false;
    sourcePath_ = jsonPath;

    fermiEv_ = data_.value(QStringLiteral("fermi_eV")).toDouble();
    samples_ = data_.value(QStringLiteral("samples")).toInt();

    const QJsonArray recip =
        data_.value(QStringLiteral("reciprocal_2pi_per_A")).toArray();
    for (int i = 0; i < 3 && i < recip.size(); ++i) {
        const QJsonArray row = recip.at(i).toArray();
        if (row.size() >= 3)
            reciprocal_[static_cast<std::size_t>(i)] = {
                row.at(0).toDouble(), row.at(1).toDouble(),
                row.at(2).toDouble()};
    }

    bands_.clear();
    for (const QJsonValue& value : data_.value(QStringLiteral("bands")).toArray()) {
        const QJsonObject entry = value.toObject();
        Band band;
        band.index = entry.value(QStringLiteral("band")).toInt();
        band.minEv = entry.value(QStringLiteral("min_eV")).toDouble();
        band.maxEv = entry.value(QStringLiteral("max_eV")).toDouble();
        band.crosses = entry.value(QStringLiteral("crosses_fermi")).toBool();
        for (const QJsonValue& e :
             entry.value(QStringLiteral("energies_eV")).toArray())
            band.energies.push_back(e.toDouble());
        bands_.push_back(std::move(band));
    }
    if (samples_ < 2 || bands_.empty())
        return false;

    {
        const QSignalBlocker blocker(energySpin_);
        energySpin_->setValue(
            data_.value(QStringLiteral("energy_offset_eV")).toDouble());
    }

    const int crossing =
        data_.value(QStringLiteral("crossing_bands")).toArray().size();
    summary_->setText(
        tr("<b>%1</b> · %2³ interpolated k-points over %3 Wannier bands · "
           "E<sub>F</sub> = %4 eV · <b>%5</b> band(s) cross it")
            .arg(data_.value(QStringLiteral("formula")).toString())
            .arg(samples_)
            .arg(data_.value(QStringLiteral("nwannier")).toInt())
            .arg(fermiEv_, 0, 'f', 4)
            .arg(crossing));

    populateBandList();
    rebuild();
    return true;
}

void FermiSurfaceWindow::populateBandList()
{
    const QSignalBlocker blocker(bandList_);
    bandList_->clear();
    for (std::size_t i = 0; i < bands_.size(); ++i) {
        const Band& band = bands_[i];
        auto* item = new QListWidgetItem(
            band.crosses
                ? tr("Band %1 — crosses E_F").arg(band.index)
                : tr("Band %1 — no sheet").arg(band.index),
            bandList_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        // Bands that never reach the target energy cannot produce a sheet, so
        // they are listed (to say they were considered) but not checked.
        item->setCheckState(band.crosses ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(tr("%1 … %2 eV")
                             .arg(band.minEv, 0, 'f', 3)
                             .arg(band.maxEv, 0, 'f', 3));
        item->setData(Qt::UserRole, static_cast<int>(i));
        if (!band.crosses)
            item->setForeground(palette().color(QPalette::PlaceholderText));
    }
}

void FermiSurfaceWindow::rebuild()
{
    if (!canvas_ || bands_.empty())
        return;
    const double target = fermiEv_ + energySpin_->value();
    const auto planes =
        clipCheck_ && clipCheck_->isChecked() ? zoneHalfSpaces()
                                              : std::vector<std::array<double, 4>>{};

    // The grid spans one reciprocal cell centred on Γ, sampled without its
    // upper endpoint — exactly how the generator laid it out, so the box has
    // to be described the same way here or the surface lands off-centre.
    core::VolumetricData field;
    field.nx = field.ny = field.nz = samples_;
    field.spanA = reciprocal_[0];
    field.spanB = reciprocal_[1];
    field.spanC = reciprocal_[2];
    field.origin = (reciprocal_[0] + reciprocal_[1] + reciprocal_[2]) * -0.5;

    std::vector<float> mesh;
    int drawn = 0;
    for (int row = 0; row < bandList_->count(); ++row) {
        if (bandList_->item(row)->checkState() != Qt::Checked)
            continue;
        const int index = bandList_->item(row)->data(Qt::UserRole).toInt();
        if (index < 0 || index >= static_cast<int>(bands_.size()))
            continue;
        const Band& band = bands_[static_cast<std::size_t>(index)];
        if (band.energies.size()
            != static_cast<std::size_t>(samples_) * samples_ * samples_)
            continue;
        field.values = band.energies;
        const core::IsoMesh iso = core::extractIsosurface(field, target);
        if (iso.positions.empty())
            continue;
        ++drawn;

        const QColor color = render::ColorMap::sample(
            render::ColorGradient::Turbo,
            bands_.size() > 1
                ? static_cast<float>(index) / (bands_.size() - 1)
                : 0.5f);
        for (std::size_t t = 0; t + 2 < iso.positions.size(); t += 3) {
            std::vector<QVector3D> polygon;
            std::vector<QVector3D> normals;
            for (std::size_t v = 0; v < 3; ++v) {
                const core::Vec3& p = iso.positions[t + v];
                const core::Vec3& n = iso.normals[t + v];
                polygon.emplace_back(static_cast<float>(p.x),
                                     static_cast<float>(p.y),
                                     static_cast<float>(p.z));
                normals.emplace_back(static_cast<float>(n.x),
                                     static_cast<float>(n.y),
                                     static_cast<float>(n.z));
            }
            // One normal per triangle after clipping: the clipped fragment is
            // planar and coincident with the original face, so its normal is
            // the face's.
            QVector3D normal = normals[0] + normals[1] + normals[2];
            normal = normal.lengthSquared() > 1e-12
                ? normal.normalized()
                : QVector3D::crossProduct(polygon[1] - polygon[0],
                                          polygon[2] - polygon[0])
                      .normalized();
            for (const auto& plane : planes) {
                polygon = clipHalfSpace(polygon, plane);
                if (polygon.size() < 3)
                    break;
            }
            for (std::size_t k = 1; k + 1 < polygon.size(); ++k) {
                pushVertex(mesh, polygon[0], normal, color);
                pushVertex(mesh, polygon[k], normal, color);
                pushVertex(mesh, polygon[k + 1], normal, color);
            }
        }
    }
    canvas_->setMesh(std::move(mesh));

    // --- Zone wireframe and axes -------------------------------------------
    std::vector<float> lines;
    const auto line = [&lines](const QVector3D& a, const QVector3D& b,
                               const QColor& color) {
        pushVertex(lines, a, QVector3D(0, 0, 1), color);
        pushVertex(lines, b, QVector3D(0, 0, 1), color);
    };
    double reach = 0.0;
    for (const core::Vec3& b : reciprocal_)
        reach = std::max(reach, 0.5 * b.norm());

    std::vector<VolumeViewWidget::Label> labels;
    if (zoneCheck_ && zoneCheck_->isChecked()) {
        try {
            // The zone is built from the reciprocal vectors as a real cell:
            // computeBrillouinZone takes a UnitCell and returns the
            // Wigner-Seitz cell of ITS reciprocal, so handing it the direct
            // cell gives the zone the sheets live in.
            const QJsonArray cellArray =
                data_.value(QStringLiteral("cell_A")).toArray();
            std::array<core::Vec3, 3> vectors{};
            for (int i = 0; i < 3 && i < cellArray.size(); ++i) {
                const QJsonArray row = cellArray.at(i).toArray();
                if (row.size() >= 3)
                    vectors[static_cast<std::size_t>(i)] = {
                        row.at(0).toDouble(), row.at(1).toDouble(),
                        row.at(2).toDouble()};
            }
            const core::UnitCell cell(vectors[0], vectors[1], vectors[2]);
            const core::BrillouinZoneData zone = core::computeBrillouinZone(cell);
            const QColor zoneColor(150, 152, 160);
            for (const auto& face : zone.faces) {
                for (std::size_t i = 0; i < face.size(); ++i) {
                    const core::Vec3& a = zone.vertices[static_cast<std::size_t>(
                        face[i])];
                    const core::Vec3& b = zone.vertices[static_cast<std::size_t>(
                        face[(i + 1) % face.size()])];
                    line({static_cast<float>(a.x), static_cast<float>(a.y),
                          static_cast<float>(a.z)},
                         {static_cast<float>(b.x), static_cast<float>(b.y),
                          static_cast<float>(b.z)},
                         zoneColor);
                }
            }
            for (const core::Vec3& v : zone.vertices)
                reach = std::max(reach, v.norm());
        } catch (const std::exception&) {
            // A degenerate cell has no zone to draw; the sheets still do.
        }
    }

    if (labelsCheck_ && labelsCheck_->isChecked()) {
        const auto axis = static_cast<float>(reach * 1.15);
        const QColor axisColor(150, 152, 160);
        line({-axis, 0, 0}, {axis, 0, 0}, axisColor);
        line({0, -axis, 0}, {0, axis, 0}, axisColor);
        line({0, 0, -axis}, {0, 0, axis}, axisColor);
        labels.push_back({{axis, 0, 0}, QStringLiteral("k_x"), axisColor});
        labels.push_back({{0, axis, 0}, QStringLiteral("k_y"), axisColor});
        labels.push_back({{0, 0, axis}, QStringLiteral("k_z"), axisColor});
        labels.push_back({{0, 0, 0}, QStringLiteral("Γ"),
                          QColor(255, 214, 120)});
    }
    canvas_->setLines(std::move(lines));
    canvas_->setLabels(std::move(labels));
    canvas_->setBounds(QVector3D(0, 0, 0),
                       static_cast<float>(std::max(reach, 1e-3) * 1.4));

    if (drawn == 0)
        summary_->setToolTip(
            tr("No sheet at this energy — no band's range contains it."));
}

void FermiSurfaceWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Fermi surface"), QStringLiteral("fermi_surface.png"),
        tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    if (!canvas_->grabFramebuffer().save(path))
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
}

} // namespace calango::gui
