#include "gui/MlwfViewer.hpp"

#include "core/IsosurfaceContinuation.hpp"
#include "core/MarchingCubes.hpp"
#include "gui/ViewportWidget.hpp"
#include "render/ColorMap.hpp"
#include "render/StructureRenderer.hpp"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

MlwfViewer::MlwfViewer(std::shared_ptr<const core::Structure> structure,
                       ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent), structure_(std::move(structure)), viewport_(viewport)
{
    setWindowTitle(tr("Wannier Functions Viewer"));
    resize(620, 560);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Wannier Functions. Tick an orbital to overlay "
           "its real-space isosurface ψₙ(r) on the 3D viewport."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Centres + spreads. Column 0 carries the show/hide checkbox.
    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels(
        {tr("show / #"), tr("x̄ (Å)"), tr("ȳ (Å)"), tr("z̄ (Å)"),
         tr("spread Ω (Å²)")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);
    connect(table_, &QTableWidget::itemChanged, this,
            &MlwfViewer::onOrbitalToggled);

    totalSpreadLabel_ = new QLabel(tr("Total spread Ω_tot: —"), this);
    totalSpreadLabel_->setWordWrap(true);
    layout->addWidget(totalSpreadLabel_);

    functionalLabel_ = new QLabel(tr("Localization functional: —"), this);
    functionalLabel_->setWordWrap(true);
    functionalLabel_->setToolTip(
        tr("The minimized Marzari-Vanderbilt spread functional Ω = Ω_I + Ω_D̃ — "
           "the trial-projection overlap metric of the localization."));
    layout->addWidget(functionalLabel_);

    // The Wannier post-processes that consume this run — interpolation,
    // Fermi surface, topological charge — are standalone Electronics-menu
    // modules with their own completed-MLWF prerequisite check, so this
    // window is purely the orbital read-out.
    auto* hint = new QLabel(
        tr("Post-process this run under <b>Electronics</b>: Wannier "
           "Interpolation, Fermi Surface, Topological Invariants."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

MlwfViewer::~MlwfViewer()
{
    // Remove our orbital isosurfaces from the (longer-lived) main viewport.
    if (viewport_)
        viewport_->clearCustomOverlay();
}

void MlwfViewer::loadResults(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not open %1").arg(jsonPath));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray centers = root.value(QStringLiteral("centers")).toArray();
    if (centers.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("No 'centers' array found in %1").arg(jsonPath));
        return;
    }
    jobDir_ = QFileInfo(jsonPath).absolutePath();
    cubeCache_.clear();
    cubes_.clear();
    for (const QJsonValue& c : root.value(QStringLiteral("cubes")).toArray())
        cubes_.push_back(c.toString());

    const QJsonArray spreads = root.value(QStringLiteral("spreads")).toArray();

    // Filling the table emits itemChanged; guard the overlay rebuild until the
    // table is fully populated.
    const QSignalBlocker blocker(table_);
    table_->setRowCount(centers.size());
    centres_.assign(static_cast<std::size_t>(centers.size()), core::Vec3{});
    for (int row = 0; row < centers.size(); ++row) {
        const QJsonArray c = centers.at(row).toArray();
        if (c.size() >= 3)
            centres_[static_cast<std::size_t>(row)] = {c.at(0).toDouble(),
                                                       c.at(1).toDouble(),
                                                       c.at(2).toDouble()};
        auto* check = new QTableWidgetItem(QString::number(row));
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled
                        | Qt::ItemIsSelectable);
        check->setCheckState(Qt::Unchecked);
        table_->setItem(row, 0, check);
        for (int k = 0; k < 3; ++k)
            table_->setItem(row, k + 1,
                            new QTableWidgetItem(QString::number(
                                c.at(k).toDouble(), 'f', 4)));
        const bool hasSpread = row < spreads.size();
        table_->setItem(
            row, 4,
            new QTableWidgetItem(
                hasSpread ? QString::number(spreads.at(row).toDouble(), 'f', 4)
                          : QStringLiteral("—")));
    }

    if (root.contains(QStringLiteral("total_spread"))) {
        totalSpreadLabel_->setText(
            tr("Total spread Ω_tot = Ω_I + Ω_D̃: %1 Å²")
                .arg(root.value(QStringLiteral("total_spread")).toDouble(), 0,
                     'f', 4));
    }
    const QJsonValue fv = root.value(QStringLiteral("functional_value"));
    const QString projection =
        root.value(QStringLiteral("projection")).toString();
    if (!fv.isUndefined() && !fv.isNull() && std::isfinite(fv.toDouble())) {
        functionalLabel_->setText(
            tr("Localization functional Ω: %1 Å²%2")
                .arg(fv.toDouble(), 0, 'f', 4)
                .arg(projection.isEmpty()
                         ? QString()
                         : tr("   (trial projection: %1)").arg(projection)));
    } else if (!projection.isEmpty()) {
        functionalLabel_->setText(
            tr("Trial projection: %1").arg(projection));
    }

    rebuildOverlay();
}

std::shared_ptr<const core::VolumetricData> MlwfViewer::cubeFor(int orbital)
{
    const auto cached = cubeCache_.find(orbital);
    if (cached != cubeCache_.end())
        return cached->second;

    // Prefer the filename recorded in wannier.json; fall back to the
    // conventional wannier_<n>.cube name.
    QString name = orbital < static_cast<int>(cubes_.size())
        ? cubes_.at(orbital)
        : QStringLiteral("wannier_%1.cube").arg(orbital);
    const QString path = QFileInfo(name).isAbsolute()
        ? name
        : jobDir_ + QLatin1Char('/') + name;

    std::shared_ptr<const core::VolumetricData> field;
    try {
        field = std::make_shared<const core::VolumetricData>(
            core::VolumetricData::load(path.toStdString()));
    } catch (const std::exception&) {
        field = nullptr; // missing/unreadable cube — skipped in the overlay
    }
    cubeCache_[orbital] = field;
    return field;
}

core::Vec3 MlwfViewer::centreForRow(int row,
                                    const core::VolumetricData& field) const
{
    if (row >= 0 && row < static_cast<int>(centres_.size()))
        return centres_[static_cast<std::size_t>(row)];
    return core::periodicCentroid(field);
}

void MlwfViewer::onOrbitalToggled(QTableWidgetItem* item)
{
    if (item && item->column() == 0)
        rebuildOverlay();
}

void MlwfViewer::rebuildOverlay()
{
    if (!viewport_)
        return;

    const int rows = table_->rowCount();
    std::vector<float> faces;
    std::vector<render::StructureRenderer::OverlayRange> ranges;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (int row = 0; row < rows; ++row) {
        const QTableWidgetItem* check = table_->item(row, 0);
        if (!check || check->checkState() != Qt::Checked)
            continue;
        const std::shared_ptr<const core::VolumetricData> field = cubeFor(row);
        if (!field || field->empty())
            continue;

        // Wannier amplitudes are signed; show the positive lobe at a fraction
        // of the peak magnitude.
        const double peak = std::max(std::abs(field->minValue()),
                                     std::abs(field->maxValue()));
        const double isovalue = 0.25 * std::max(peak, 1e-12);
        // Continued into the neighbouring periodic images rather than cut at
        // the cell faces: a Wannier centre lands wherever the wannierization
        // put it, so a lobe straddling a face would otherwise come out sliced
        // flat with its remainder stranded across the box.
        //
        // The centre comes from the table this viewer is built around (it IS
        // the centres + spreads table), so the row's own centre is used when
        // the cell parses and the field's periodic centroid otherwise.
        const core::IsoMesh mesh = core::extractContinuedIsosurface(
            *field, isovalue, centreForRow(row, *field),
            core::kDefaultContinuationMargin);
        if (mesh.positions.empty())
            continue;

        // Distinct color per orbital across the Rainbow gradient.
        const double t = rows > 1 ? static_cast<double>(row) / (rows - 1) : 0.0;
        const QColor color =
            render::ColorMap::sample(render::ColorGradient::Rainbow,
                                     static_cast<float>(t));
        const float r = static_cast<float>(color.redF());
        const float g = static_cast<float>(color.greenF());
        const float b = static_cast<float>(color.blueF());

        // pos(3) + normal(3) + colour(3). The normals come straight from
        // marching cubes (the field gradient), which is what lets the lit
        // isosurface profile shade the lobes on the GPU.
        const int first = static_cast<int>(
            faces.size()
            / render::StructureRenderer::kOverlayFaceFloats);
        for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
            const core::Vec3& p = mesh.positions[i];
            const core::Vec3 n = i < mesh.normals.size() ? mesh.normals[i]
                                                        : core::Vec3{0, 0, 1};
            faces.insert(faces.end(),
                         {static_cast<float>(p.x), static_cast<float>(p.y),
                          static_cast<float>(p.z), static_cast<float>(n.x),
                          static_cast<float>(n.y), static_cast<float>(n.z),
                          r, g, b});
        }
        ranges.push_back(
            {first, static_cast<int>(mesh.positions.size()), 0.80f});
    }
    QApplication::restoreOverrideCursor();

    if (faces.empty())
        viewport_->clearCustomOverlay();
    else
        viewport_->setCustomOverlay(std::move(faces), {}, std::move(ranges),
                                    /*visible=*/true);
}

} // namespace calango::gui
