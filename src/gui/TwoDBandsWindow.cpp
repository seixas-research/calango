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
    auto* exportImageButton = new QPushButton(tr("Export Image…"), this);
    connect(exportImageButton, &QPushButton::clicked, this,
            &TwoDBandsWindow::exportImage);
    controls->addWidget(exportImageButton);
    auto* exportDataButton = new QPushButton(tr("Export Data…"), this);
    connect(exportDataButton, &QPushButton::clicked, this,
            &TwoDBandsWindow::exportData);
    controls->addWidget(exportDataButton);
    layout->addLayout(controls);
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

    for (const int index : selected) {
        const Surface& s = surfaces_[static_cast<std::size_t>(index)];
        const std::size_t nx = s.energies.size();
        for (std::size_t i = 0; i + 1 < nx; ++i) {
            const std::size_t ny = s.energies[i].size();
            for (std::size_t j = 0; j + 1 < ny; ++j) {
                if (i + 1 >= kx_.size() || j + 1 >= kx_[i].size()
                    || j + 1 >= s.energies[i + 1].size())
                    continue;
                // One grid cell -> two triangles. The quad's corners are not
                // coplanar in general, so the normal is taken per triangle
                // rather than per cell; a shared normal makes a saddle look
                // faceted exactly where the curvature matters.
                const QVector3D p00(static_cast<float>(kx_[i][j]),
                                    static_cast<float>(ky_[i][j]),
                                    height(s.energies[i][j]));
                const QVector3D p10(static_cast<float>(kx_[i + 1][j]),
                                    static_cast<float>(ky_[i + 1][j]),
                                    height(s.energies[i + 1][j]));
                const QVector3D p01(static_cast<float>(kx_[i][j + 1]),
                                    static_cast<float>(ky_[i][j + 1]),
                                    height(s.energies[i][j + 1]));
                const QVector3D p11(static_cast<float>(kx_[i + 1][j + 1]),
                                    static_cast<float>(ky_[i + 1][j + 1]),
                                    height(s.energies[i + 1][j + 1]));
                const double e00 = s.energies[i][j];
                const double e10 = s.energies[i + 1][j];
                const double e01 = s.energies[i][j + 1];
                const double e11 = s.energies[i + 1][j + 1];
                const auto color = [&](double e) {
                    return render::ColorMap::sample(
                        gradient, static_cast<float>((e - lo) / range));
                };
                const auto triangle = [&](const QVector3D& a, double ea,
                                          const QVector3D& b, double eb,
                                          const QVector3D& c, double ec) {
                    QVector3D normal =
                        QVector3D::crossProduct(b - a, c - a).normalized();
                    // Face the +z half-space: the surface is a graph over the
                    // k-plane, so a downward normal is only ever a winding
                    // artifact and would leave that triangle unlit.
                    if (normal.z() < 0.0f)
                        normal = -normal;
                    pushVertex(mesh, a.x(), a.y(), a.z(), normal, color(ea));
                    pushVertex(mesh, b.x(), b.y(), b.z(), normal, color(eb));
                    pushVertex(mesh, c.x(), c.y(), c.z(), normal, color(ec));
                };
                triangle(p00, e00, p10, e10, p11, e11);
                triangle(p00, e00, p11, e11, p01, e01);
            }
        }
    }
    canvas_->setMesh(std::move(mesh));

    // --- Guide lines: the k axes, and the Fermi plane's outline -------------
    std::vector<float> lines;
    const auto line = [&lines](const QVector3D& a, const QVector3D& b,
                               const QColor& color) {
        pushVertex(lines, a.x(), a.y(), a.z(), QVector3D(0, 0, 1), color);
        pushVertex(lines, b.x(), b.y(), b.z(), QVector3D(0, 0, 1), color);
    };
    const auto extent = static_cast<float>(kExtent_);
    const float base = height(shift ? fermiEv_ : 0.0);
    const QColor axisColor(150, 152, 160);
    line({-extent, 0.0f, base}, {extent, 0.0f, base}, axisColor); // kx
    line({0.0f, -extent, base}, {0.0f, extent, base}, axisColor); // ky
    if (fermiPlaneCheck_ && fermiPlaneCheck_->isChecked()) {
        const float z = height(fermiEv_);
        const QColor fermiColor(217, 83, 79);
        line({-extent, -extent, z}, {extent, -extent, z}, fermiColor);
        line({extent, -extent, z}, {extent, extent, z}, fermiColor);
        line({extent, extent, z}, {-extent, extent, z}, fermiColor);
        line({-extent, extent, z}, {-extent, -extent, z}, fermiColor);
    }
    canvas_->setLines(std::move(lines));

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
