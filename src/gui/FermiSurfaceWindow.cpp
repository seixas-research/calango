#include "gui/FermiSurfaceWindow.hpp"

#include "core/BrillouinZone.hpp"
#include "core/GridInterpolation.hpp"
#include "core/MarchingCubes.hpp"
#include "core/VolumetricData.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/VolumeViewWidget.hpp"
#include "render/ColorMap.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTextStream>
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
    // Opaque by default. The canvas blends translucent triangles in buffer
    // order with no depth pre-pass, so below 1.0 a closed sheet shows its own
    // far side through its near one — which reads as a torn mesh rather than
    // as transparency. The old hardcoded 0.92 shipped exactly that look; the
    // opacity control below is what makes it a choice, for the case it is
    // actually wanted (seeing a pocket nested inside another sheet).
    canvas_->setMeshOpacity(1.0f);
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
    layout->addLayout(controls);

    // --- Interpolation + appearance ----------------------------------------
    auto* second = new QHBoxLayout;

    second->addWidget(new QLabel(tr("Interpolation:"), this));
    interpolationCombo_ = new QComboBox(this);
    // Order matches core::GridInterpolation.
    interpolationCombo_->addItem(tr("None (raw grid)"));
    interpolationCombo_->addItem(tr("Trilinear"));
    interpolationCombo_->addItem(tr("Tricubic"));
    interpolationCombo_->setCurrentIndex(
        static_cast<int>(core::GridInterpolation::Trilinear));
    interpolationCombo_->setToolTip(
        tr("How the band grid is refined before the isosurface is "
           "extracted.\n\n"
           "Marching cubes reproduces the grid it is handed, so a coarse grid "
           "gives a faceted sheet no amount of shading can hide — the facets "
           "are geometry, not lighting. Refining the field first is what "
           "actually smooths it.\n\n"
           "• Trilinear is cheap and removes the staircase.\n"
           "• Tricubic (Catmull-Rom) also matches the slope across cell "
           "boundaries, so a curved sheet stays curved instead of turning "
           "into flats meeting at angles. It is the one to use for a figure — "
           "and, being an interpolant of the same samples, it invents no "
           "features the grid does not contain."));
    connect(interpolationCombo_, &QComboBox::currentIndexChanged, this,
            [this] { rebuild(); });
    second->addWidget(interpolationCombo_);

    refineSpin_ = new QSpinBox(this);
    refineSpin_->setRange(1, 4);
    refineSpin_->setValue(2);
    refineSpin_->setPrefix(tr("×"));
    refineSpin_->setKeyboardTracking(false);
    refineSpin_->setToolTip(
        tr("Refinement factor per axis. ×2 turns an N³ grid into (2N)³ — eight "
           "times the marching-cubes work, which is why this is capped low. "
           "×2 with Tricubic is usually already smooth."));
    connect(refineSpin_, &QSpinBox::valueChanged, this, [this] { rebuild(); });
    second->addWidget(refineSpin_);

    second->addSpacing(16);
    second->addWidget(new QLabel(tr("Colors:"), this));
    gradientCombo_ = new QComboBox(this);
    // The same short list the other result windows offer, plus Turbo (the
    // default here): a Fermi surface needs colours that separate a handful of
    // sheets, not a perceptually-uniform ramp over a continuum.
    gradientCombo_->addItem(tr("Turbo"),
                            static_cast<int>(render::ColorGradient::Turbo));
    gradientCombo_->addItem(tr("Viridis"),
                            static_cast<int>(render::ColorGradient::Viridis));
    gradientCombo_->addItem(tr("Plasma"),
                            static_cast<int>(render::ColorGradient::Plasma));
    gradientCombo_->addItem(tr("Coolwarm"),
                            static_cast<int>(render::ColorGradient::Coolwarm));
    gradientCombo_->addItem(tr("Spectral"),
                            static_cast<int>(render::ColorGradient::Spectral));
    gradientCombo_->addItem(tr("Greys"),
                            static_cast<int>(render::ColorGradient::Greys));
    gradientCombo_->setToolTip(
        tr("Palette the per-band sheets are coloured from. One colour per "
           "band, spread across the ramp — the sheets are distinct objects "
           "(an electron pocket and a hole pocket are not the same thing), so "
           "what this picks is how easily they are told apart."));
    connect(gradientCombo_, &QComboBox::currentIndexChanged, this, [this] {
        populateBandList();
        rebuild();
    });
    second->addWidget(gradientCombo_);

    second->addWidget(new QLabel(tr("Opacity:"), this));
    opacitySpin_ = new QDoubleSpinBox(this);
    opacitySpin_->setRange(0.05, 1.0);
    opacitySpin_->setSingleStep(0.05);
    opacitySpin_->setDecimals(2);
    opacitySpin_->setValue(1.0);
    opacitySpin_->setKeyboardTracking(false);
    opacitySpin_->setToolTip(
        tr("Sheet opacity. Below 1 the inner sheets of a multi-band surface "
           "become visible through the outer ones, which is the only way to "
           "see a nested pocket at all; at 1 the outermost sheet hides "
           "everything behind it.\n\n"
           "The translucent pass blends in draw order without a depth sort, so "
           "below 1 a closed sheet also shows its own far side through its "
           "near one — the speckled look is that, not a hole in the surface."));
    connect(opacitySpin_, &QDoubleSpinBox::valueChanged, this,
            [this](double alpha) {
                canvas_->setMeshOpacity(static_cast<float>(alpha));
            });
    second->addWidget(opacitySpin_);

    litCheck_ = new QCheckBox(tr("Lighting"), this);
    litCheck_->setChecked(true);
    litCheck_->setToolTip(
        tr("Shade the sheets with the scene light. Off draws them in flat "
           "colour, which removes the shape cue but makes a printed figure "
           "reproduce exactly and keeps small pockets from disappearing into "
           "a dark side."));
    connect(litCheck_, &QCheckBox::toggled, this, [this] { rebuild(); });
    second->addWidget(litCheck_);

    second->addStretch(1);
    auto* exportImageButton = new QPushButton(tr("Export Image…"), this);
    exportImageButton->setToolTip(
        tr("Save the 3D view exactly as it is on screen, at the canvas's own "
           "resolution."));
    connect(exportImageButton, &QPushButton::clicked, this,
            &FermiSurfaceWindow::exportImage);
    second->addWidget(exportImageButton);

    auto* exportDataButton = new QPushButton(tr("Export Data (.csv)…"), this);
    exportDataButton->setToolTip(
        tr("Write the interpolated grid as CSV: one row per k-point, with its "
           "grid indices, its Cartesian k in 1/Å, and one energy column per "
           "band.\n\n"
           "Ordered with k₃ fastest and a full header, so ParaView rebuilds it "
           "with CSV Reader → Table To Structured Grid, and Mayavi with a "
           "numpy reshape — no reconstruction of the layout by guesswork."));
    connect(exportDataButton, &QPushButton::clicked, this,
            &FermiSurfaceWindow::exportData);
    second->addWidget(exportDataButton);
    layout->addLayout(second);
}

QColor FermiSurfaceWindow::bandColor(int index) const
{
    const auto gradient = gradientCombo_
        ? static_cast<render::ColorGradient>(
              gradientCombo_->currentData().toInt())
        : render::ColorGradient::Turbo;
    return render::ColorMap::sample(
        gradient,
        bands_.size() > 1 ? static_cast<float>(index) / (bands_.size() - 1)
                          : 0.5f);
}

std::size_t FermiSurfaceWindow::pointCount() const
{
    return static_cast<std::size_t>(samples_[0])
        * static_cast<std::size_t>(samples_[1])
        * static_cast<std::size_t>(samples_[2]);
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
    // "samples" is a three-element array now. Runs from before the mesh became
    // per-axis wrote a single int, and those results are still on disk — so an
    // int is read as an isotropic grid rather than rejected.
    if (const QJsonValue samples = data_.value(QStringLiteral("samples"));
        samples.isArray()) {
        const QJsonArray array = samples.toArray();
        for (int i = 0; i < 3; ++i) {
            samples_[static_cast<std::size_t>(i)] =
                i < array.size() ? array.at(i).toInt() : 0;
        }
    } else {
        samples_.fill(samples.toInt());
    }

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
    if (*std::min_element(samples_.begin(), samples_.end()) < 2
        || bands_.empty() || pointCount() == 0) {
        return false;
    }

    {
        const QSignalBlocker blocker(energySpin_);
        energySpin_->setValue(
            data_.value(QStringLiteral("energy_offset_eV")).toDouble());
    }

    const int crossing =
        data_.value(QStringLiteral("crossing_bands")).toArray().size();
    summary_->setText(
        tr("<b>%1</b> · %2 × %3 × %4 interpolated k-points over %5 Wannier "
           "bands · E<sub>F</sub> = %6 eV · <b>%7</b> band(s) cross it")
            .arg(data_.value(QStringLiteral("formula")).toString())
            .arg(samples_[0])
            .arg(samples_[1])
            .arg(samples_[2])
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
        // The swatch is the sheet's own colour, so the list doubles as the
        // legend — without it, telling which sheet is band 3 means toggling
        // them off one at a time.
        QPixmap swatch(12, 12);
        swatch.fill(bandColor(static_cast<int>(i)));
        item->setIcon(QIcon(swatch));
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
    field.nx = samples_[0];
    field.ny = samples_[1];
    field.nz = samples_[2];
    field.spanA = reciprocal_[0];
    field.spanB = reciprocal_[1];
    field.spanC = reciprocal_[2];
    field.origin = (reciprocal_[0] + reciprocal_[1] + reciprocal_[2]) * -0.5;

    // Refinement scheme + factor. Applied to the FIELD, before extraction:
    // marching cubes reproduces whatever grid it is handed, so smoothing the
    // triangles afterwards would move the surface off the data, while
    // interpolating the samples keeps it on an interpolant of them.
    const auto scheme = interpolationCombo_
        ? static_cast<core::GridInterpolation>(
              interpolationCombo_->currentIndex())
        : core::GridInterpolation::None;
    const int refine = refineSpin_ ? refineSpin_->value() : 1;
    const bool lit = !litCheck_ || litCheck_->isChecked();

    std::vector<float> mesh;
    int drawn = 0;
    for (int row = 0; row < bandList_->count(); ++row) {
        if (bandList_->item(row)->checkState() != Qt::Checked)
            continue;
        const int index = bandList_->item(row)->data(Qt::UserRole).toInt();
        if (index < 0 || index >= static_cast<int>(bands_.size()))
            continue;
        const Band& band = bands_[static_cast<std::size_t>(index)];
        if (band.energies.size() != pointCount())
            continue;
        field.values = band.energies;
        const core::VolumetricData refined =
            core::refineGrid(field, refine, scheme);
        const core::IsoMesh iso = core::extractIsosurface(refined, target);
        if (iso.positions.empty())
            continue;
        ++drawn;

        const QColor color = bandColor(index);
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
            // "Lighting off" is expressed as a normal facing the viewer on
            // every triangle: the shader stays one shader, and every facet
            // then takes the same diffuse term, which is flat colour.
            const QVector3D shadeNormal =
                lit ? normal : QVector3D(0.0f, 0.0f, 1.0f);
            for (std::size_t k = 1; k + 1 < polygon.size(); ++k) {
                pushVertex(mesh, polygon[0], shadeNormal, color);
                pushVertex(mesh, polygon[k], shadeNormal, color);
                pushVertex(mesh, polygon[k + 1], shadeNormal, color);
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

void FermiSurfaceWindow::exportData()
{
    if (bands_.empty() || pointCount() == 0) {
        QMessageBox::information(this, windowTitle(),
                                 tr("There is no grid loaded to export."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Fermi surface data"),
        QStringLiteral("fermi_surface.csv"),
        tr("CSV table (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;

    if (!writeCsv(path)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not write %1").arg(path));
        return;
    }

    const std::array<int, 3> n = samples_;
    // The extent is what ParaView asks for by hand, and it is not in the file
    // — so it is stated here rather than left to be counted off the rows.
    QMessageBox::information(
        this, windowTitle(),
        tr("Wrote %1 — %2 rows over %3 band(s).\n\n"
           "In ParaView: CSV Reader → Table To Structured Grid, with Whole "
           "Extent 0 %4  0 %5  0 %6 and kx/ky/kz as the point coordinates.\n\n"
           "In Mayavi/numpy: loadtxt, then reshape a band column to "
           "(%7, %8, %9).")
            .arg(QFileInfo(path).fileName())
            .arg(pointCount())
            .arg(bands_.size())
            .arg(n[0] - 1)
            .arg(n[1] - 1)
            .arg(n[2] - 1)
            .arg(n[0])
            .arg(n[1])
            .arg(n[2]));
}

bool FermiSurfaceWindow::writeCsv(const QString& path) const
{
    if (bands_.empty() || pointCount() == 0)
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);

    // Wide format: one row per k-point, one energy column per band.
    //
    // That shape is what makes it reconstructable without the reader knowing
    // anything about this application. In ParaView it is CSV Reader → Table To
    // Structured Grid with whole extent (0..nx-1, 0..ny-1, 0..nz-1) and the
    // kx/ky/kz columns as the point coordinates; in Mayavi/numpy it is one
    // loadtxt and a reshape. A long ("tidy") table with a band column would be
    // smaller to write and would need pivoting first in both.
    //
    // The index columns are written explicitly rather than left implicit in the
    // row order: they are what lets a reader verify the ordering instead of
    // trusting a convention it cannot see.
    out << "i,j,k,kx_1_per_A,ky_1_per_A,kz_1_per_A";
    for (const Band& band : bands_)
        out << ",band_" << band.index << "_eV";
    out << "\n";

    // Same box the sheets are extracted in: Γ-centred, spanning one reciprocal
    // cell, upper endpoint excluded (−1/2 … 1/2 in fractional coordinates).
    const core::Vec3 origin =
        (reciprocal_[0] + reciprocal_[1] + reciprocal_[2]) * -0.5;
    const std::array<int, 3> n = samples_;

    // k fastest, matching the row-major (i, j, k) storage the bands arrive in —
    // so the CSV row order IS the array order and a reshape needs no transpose.
    for (int i = 0; i < n[0]; ++i) {
        for (int j = 0; j < n[1]; ++j) {
            for (int k = 0; k < n[2]; ++k) {
                const core::Vec3 kvec = origin
                    + reciprocal_[0] * (static_cast<double>(i) / n[0])
                    + reciprocal_[1] * (static_cast<double>(j) / n[1])
                    + reciprocal_[2] * (static_cast<double>(k) / n[2]);
                const std::size_t flat =
                    (static_cast<std::size_t>(i) * n[1] + j) * n[2] + k;
                out << i << ',' << j << ',' << k << ','
                    << QString::number(kvec.x, 'g', 8) << ','
                    << QString::number(kvec.y, 'g', 8) << ','
                    << QString::number(kvec.z, 'g', 8);
                for (const Band& band : bands_) {
                    out << ',';
                    // A band whose array is the wrong length is written as an
                    // empty field rather than silently shifting every column
                    // after it.
                    if (flat < band.energies.size())
                        out << QString::number(band.energies[flat], 'g', 10);
                }
                out << "\n";
            }
        }
    }

    out.flush();
    return file.commit();
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
