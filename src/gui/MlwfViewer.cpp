#include "gui/MlwfViewer.hpp"

#include "core/MarchingCubes.hpp"
#include "core/WannierScriptGenerator.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/ViewportWidget.hpp"
#include "gui/WannierInterpolationDialog.hpp"
#include "render/ColorMap.hpp"
#include "render/StructureRenderer.hpp"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace calango::gui {

MlwfViewer::MlwfViewer(std::shared_ptr<const core::Structure> structure,
                       ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent), structure_(std::move(structure)), viewport_(viewport)
{
    setWindowTitle(tr("MLWF Viewer"));
    resize(620, 560);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Maximally Localized Wannier Functions. Tick an orbital to overlay "
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

    auto* actionRow = new QHBoxLayout;
    bandsButton_ = new QPushButton(tr("Wannier Interpolation…"), this);
    bandsButton_->setToolTip(
        tr("Configure and run a Wannier interpolation (H(R) → H(k)): "
           "interpolated band structure + projected DOS from the saved "
           "wavefunctions. Opens in the band/PDOS viewer when it finishes."));
    connect(bandsButton_, &QPushButton::clicked, this,
            &MlwfViewer::openInterpolationDialog);
    actionRow->addWidget(bandsButton_);
    actionRow->addStretch(1);
    layout->addLayout(actionRow);

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
    for (int row = 0; row < centers.size(); ++row) {
        const QJsonArray c = centers.at(row).toArray();
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

    bandsButton_->setEnabled(!jobDir_.isEmpty());
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
        const core::IsoMesh mesh =
            core::extractIsosurface(*field, isovalue, nullptr);
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

        const int first = static_cast<int>(faces.size() / 6);
        for (const core::Vec3& p : mesh.positions)
            faces.insert(faces.end(),
                         {static_cast<float>(p.x), static_cast<float>(p.y),
                          static_cast<float>(p.z), r, g, b});
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

void MlwfViewer::openInterpolationDialog()
{
    if (jobDir_.isEmpty())
        return;
    // Pre-flight, because the alternative is what used to happen: the user
    // fills in the dialog, the job is staged and queued, and it dies on its
    // first line with "No GPAW wavefunction (.gpw) found in <dir>". The
    // interpolation restarts GPAW from the wavefunctions the MLWF run used,
    // and an MLWF that started from a single-point baseline read those from
    // another job's directory without writing a copy of its own.
    const QDir dir(jobDir_);
    const QJsonObject meta =
        readJsonObject(dir.filePath(QStringLiteral("wannier.json")));
    const QString recorded = meta.value(QStringLiteral("gpw")).toString();
    const bool haveRecorded =
        !recorded.isEmpty() && QFileInfo::exists(recorded);
    const bool haveLocal =
        !dir.entryList({QStringLiteral("*.gpw")}, QDir::Files).isEmpty();
    if (!haveRecorded && !haveLocal) {
        QMessageBox::warning(
            this, tr("Wannier Interpolation"),
            recorded.isEmpty()
                ? tr("This MLWF run recorded no path to the GPAW wavefunctions "
                     "it localized, and left no .gpw in its own directory.\n\n"
                     "That happens when it started from a single-point "
                     "baseline: it read the wavefunctions from that job's "
                     "directory and wrote none of its own. Runs from this "
                     "version of Calango record the path; re-run the MLWF "
                     "calculation and the interpolation will find it.")
                : tr("The GPAW wavefunctions this MLWF run localized are no "
                     "longer at\n\n%1\n\nRe-run the MLWF calculation, or "
                     "restore that file.")
                      .arg(recorded));
        return;
    }

    WannierInterpolationDialog dialog(structure_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString script = QString::fromStdString(
        core::generateWannierInterpolationScript(jobDir_.toStdString(),
                                                 dialog.config()));
    Q_EMIT runRequested(script, tr("Wannier Interpolation"));
}

} // namespace calango::gui
