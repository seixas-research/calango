#include "gui/VolumetricPanel.hpp"

#include "gui/EditVolumetricRenderDialog.hpp"
#include "gui/ViewportWidget.hpp"
#include "gui/VolumetricMetadataDialog.hpp"
#include "render/ColorMap.hpp"
#include "render/StructureRenderer.hpp"
#include "ui/IconManager.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>

namespace calango::gui {

const QVector<render::ColorGradient>& volumetricGradients()
{
    static const QVector<render::ColorGradient> kGradients{
        render::ColorGradient::Viridis, render::ColorGradient::Plasma,
        render::ColorGradient::Coolwarm, render::ColorGradient::Rainbow,
        render::ColorGradient::Greys};
    return kGradients;
}

QStringList volumetricGradientNames()
{
    return {QStringLiteral("Viridis"), QStringLiteral("Plasma"),
            QStringLiteral("Coolwarm"), QStringLiteral("Rainbow"),
            QStringLiteral("Greys")};
}

VolumetricPanel::VolumetricPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent), viewport_(viewport)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    registry_ = new QTreeWidget(this);
    registry_->setColumnCount(2);
    registry_->setHeaderLabels({tr("Field"), tr("Grid")});
    registry_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    registry_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    registry_->setRootIsDecorated(false);
    registry_->setToolTip(
        tr("Active 3D scalar volumetric grids — loaded from disk or produced by "
           "a calculation (e.g. MLWF orbitals). Select one to visualize it."));
    layout->addWidget(registry_, 1);
    connect(registry_, &QTreeWidget::currentItemChanged, this,
            &VolumetricPanel::onSelectionChanged);

    // Compact icon-only action bar (RemixIcon glyphs, theme-tinted).
    auto* bar = new QHBoxLayout;
    const auto makeButton = [&](const QString& icon, const QString& tip) {
        auto* button = new QPushButton(this);
        button->setIcon(ui::IconManager::icon(icon));
        button->setIconSize(QSize(20, 20));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        bar->addWidget(button);
        return button;
    };
    auto* loadButton = makeButton(
        QStringLiteral("folder-received-line"),
        tr("Load External Files… — import a scalar volumetric dataset "
           "(.cube / .xsf / CHGCAR-family)."));
    auto* metaButton = makeButton(
        QStringLiteral("information-line"),
        tr("Show Metadata… — dimensions, grid origin, voxel spacing, min/max "
           "and the associated structure."));
    auto* editButton = makeButton(
        QStringLiteral("palette-line"),
        tr("Edit Render… — isosurface, color-slice and potential-map styling."));
    bar->addStretch(1);
    showCheck_ = new QCheckBox(tr("Show"), this);
    showCheck_->setChecked(true);
    showCheck_->setToolTip(tr("Show the selected field on the 3D viewport."));
    bar->addWidget(showCheck_);
    layout->addLayout(bar);

    connect(loadButton, &QPushButton::clicked, this,
            &VolumetricPanel::loadExternalFile);
    connect(metaButton, &QPushButton::clicked, this,
            &VolumetricPanel::showMetadata);
    connect(editButton, &QPushButton::clicked, this,
            &VolumetricPanel::openEditDialog);
    connect(showCheck_, &QCheckBox::toggled, this,
            &VolumetricPanel::onShowToggled);

    connect(&isoWatcher_, &QFutureWatcher<PhaseMeshes>::finished, this,
            &VolumetricPanel::onIsoExtractionFinished);

    onSelectionChanged(); // disables actions with an empty registry
}

VolumetricPanel::~VolumetricPanel()
{
    isoWatcher_.disconnect(this);
    if (isoWatcher_.isRunning())
        isoWatcher_.waitForFinished();
}

void VolumetricPanel::loadExternalFile()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Load Volumetric Files"), QString(),
        tr("Volumetric grids (*.cube *.xsf CHGCAR* LOCPOT* PARCHG* ELFCAR*);;"
           "All files (*)"));
    for (const QString& path : paths)
        registerResultFile(path, QFileInfo(path).fileName(),
                           tr("(external file)"));
}

void VolumetricPanel::registerResultFile(const QString& path,
                                         const QString& label,
                                         const QString& structureLabel)
{
    std::shared_ptr<const core::VolumetricData> field;
    try {
        field = std::make_shared<const core::VolumetricData>(
            core::VolumetricData::load(path.toStdString()));
    } catch (const std::exception& e) {
        if (QFileInfo::exists(path) && isVisible())
            QMessageBox::warning(this, tr("Volumetric Data"),
                                 QString::fromUtf8(e.what()));
        return;
    }
    addEntry(std::move(field), label, path, structureLabel);
}

void VolumetricPanel::addEntry(
    std::shared_ptr<const core::VolumetricData> field, const QString& label,
    const QString& path, const QString& structureLabel)
{
    if (!field || field->empty())
        return;
    const int index = static_cast<int>(entries_.size());
    entries_.push_back({field, label, path, structureLabel});

    auto* item = new QTreeWidgetItem(registry_);
    item->setText(0, label);
    item->setText(1, QStringLiteral("%1×%2×%3")
                         .arg(field->nx)
                         .arg(field->ny)
                         .arg(field->nz));
    item->setToolTip(0, path);
    item->setData(0, Qt::UserRole, index);
    registry_->setCurrentItem(item); // selects → onSelectionChanged → render
}

int VolumetricPanel::currentRow() const
{
    const QTreeWidgetItem* item = registry_->currentItem();
    if (!item)
        return -1;
    const int index = item->data(0, Qt::UserRole).toInt();
    return (index >= 0 && index < static_cast<int>(entries_.size())) ? index
                                                                     : -1;
}

const VolumetricPanel::Entry* VolumetricPanel::currentEntry() const
{
    const int row = currentRow();
    return row >= 0 ? &entries_.at(row) : nullptr;
}

const core::VolumetricData* VolumetricPanel::currentField() const
{
    const Entry* e = currentEntry();
    return e ? e->field.get() : nullptr;
}

void VolumetricPanel::defaultIsovalueForField()
{
    const core::VolumetricData* f = currentField();
    if (!f)
        return;
    const double lo = f->minValue(), hi = f->maxValue();
    // Signed fields (e.g. Wannier orbitals ψ): a positive fraction of the peak
    // magnitude, so the ± lobes both show. Otherwise the mid-range.
    style_.isovalue = lo < 0.0
        ? 0.25 * std::max({std::abs(lo), std::abs(hi), 1e-12})
        : lo + 0.5 * (hi - lo);
    if (!style_.potentialUseBounds) {
        style_.potentialMin = lo;
        style_.potentialMax = hi;
    }
}

void VolumetricPanel::onSelectionChanged()
{
    const core::VolumetricData* field = currentField();
    const bool has = field != nullptr;
    showCheck_->setEnabled(has);
    if (has) {
        defaultIsovalueForField();
        if (editDialog_)
            editDialog_->setFieldRange(field->minValue(), field->maxValue());
    }
    render();
}

void VolumetricPanel::onShowToggled(bool /*on*/)
{
    render();
}

void VolumetricPanel::showMetadata()
{
    const Entry* e = currentEntry();
    if (!e) {
        QMessageBox::information(this, tr("Volumetric Metadata"),
                                 tr("Select a volumetric field first."));
        return;
    }
    auto* dialog = new VolumetricMetadataDialog(
        *e->field, e->path.isEmpty() ? e->label : e->path, e->structureLabel,
        this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void VolumetricPanel::openEditDialog()
{
    const core::VolumetricData* field = currentField();
    const double lo = field ? field->minValue() : 0.0;
    const double hi = field ? field->maxValue() : 1.0;
    if (!editDialog_) {
        editDialog_ =
            new EditVolumetricRenderDialog(style_, mode_, lo, hi, this);
        editDialog_->setAttribute(Qt::WA_DeleteOnClose);
        connect(editDialog_, &EditVolumetricRenderDialog::styleChanged, this,
                [this](const VolumetricStyle& s, VolumetricRenderMode m) {
                    style_ = s;
                    mode_ = m;
                    render();
                });
        connect(editDialog_, &QObject::destroyed, this,
                [this] { editDialog_ = nullptr; });
    } else {
        editDialog_->setFieldRange(lo, hi);
    }
    editDialog_->show();
    editDialog_->raise();
    editDialog_->activateWindow();
}

void VolumetricPanel::clearViewportOverlay()
{
    if (!viewport_)
        return;
    viewport_->clearCustomOverlay();
    viewport_->clearLatticePlane();
}

void VolumetricPanel::render()
{
    if (!viewport_)
        return;
    const core::VolumetricData* field = currentField();
    if (!field || !showCheck_->isChecked()) {
        clearViewportOverlay();
        ++isoGeneration_;
        isoPending_ = false;
        return;
    }
    if (mode_ == VolumetricRenderMode::Isosurface) {
        viewport_->clearLatticePlane();
        requestIsosurface();
    } else {
        ++isoGeneration_; // cancel any queued/served isosurface
        isoPending_ = false;
        viewport_->clearCustomOverlay();
        renderSlice(mode_ == VolumetricRenderMode::PotentialMap);
    }
}

void VolumetricPanel::renderSlice(bool potentialRamp)
{
    const core::VolumetricData* field = currentField();
    if (!field || !viewport_)
        return;

    // Color normalization: the potential map may use an explicit ramp window.
    double lo = field->minValue(), hi = field->maxValue();
    if (potentialRamp && style_.potentialUseBounds) {
        lo = style_.potentialMin;
        hi = style_.potentialMax;
    }
    const double range = std::max(hi - lo, 1e-30);
    const int plane = std::clamp(style_.slicePlane, 0, 2);

    const int uAxis = plane == 2 ? 1 : 0;
    const int vAxis = plane == 0 ? 1 : 2;
    const int wAxis = 3 - uAxis - vAxis;
    const int dims[3] = {field->nx, field->ny, field->nz};
    const int nu = dims[uAxis], nv = dims[vAxis];
    const double w = std::clamp(style_.sliceOffset, 0.0, 1.0) * dims[wAxis];

    const auto gridPoint = [&](double u, double v) {
        double g[3];
        g[uAxis] = u;
        g[vAxis] = v;
        g[wAxis] = w;
        return std::array<double, 3>{g[0], g[1], g[2]};
    };

    std::vector<float> faces;
    faces.reserve(static_cast<std::size_t>(nu) * nv * 6 * 6);
    const auto emit_ = [&](const std::array<double, 3>& g) {
        const core::Vec3 p = field->position(g[0], g[1], g[2]);
        const double value = field->samplePeriodic(g[0], g[1], g[2]);
        const QColor c = render::ColorMap::sample(
            style_.gradient,
            static_cast<float>(std::clamp((value - lo) / range, 0.0, 1.0)));
        faces.insert(faces.end(),
                     {static_cast<float>(p.x), static_cast<float>(p.y),
                      static_cast<float>(p.z), static_cast<float>(c.redF()),
                      static_cast<float>(c.greenF()),
                      static_cast<float>(c.blueF())});
    };
    for (int iu = 0; iu < nu; ++iu) {
        for (int iv = 0; iv < nv; ++iv) {
            const std::array<double, 3> q[4] = {
                gridPoint(iu, iv), gridPoint(iu + 1, iv),
                gridPoint(iu + 1, iv + 1), gridPoint(iu, iv + 1)};
            static constexpr int kQuad[6] = {0, 1, 2, 0, 2, 3};
            for (const int k : kQuad)
                emit_(q[k]);
        }
    }
    viewport_->setLatticePlane(std::move(faces), {},
                               static_cast<float>(style_.sliceOpacity),
                               /*visible=*/true, /*showEdges=*/false);
}

void VolumetricPanel::requestIsosurface()
{
    isoPending_ = true;
    pumpIsoExtraction();
}

void VolumetricPanel::pumpIsoExtraction()
{
    if (!isoPending_ || isoWatcher_.isRunning())
        return;
    const int row = currentRow();
    if (row < 0)
        return;
    isoPending_ = false;
    isoRunningGeneration_ = ++isoGeneration_;

    std::shared_ptr<const core::VolumetricData> field = entries_.at(row).field;
    const double iso = std::abs(style_.isovalue);
    const bool signedField = field->minValue() < 0.0;
    isoWatcher_.setFuture(QtConcurrent::run(
        [field = std::move(field), iso, signedField]() -> PhaseMeshes {
            PhaseMeshes m;
            m.positive = core::extractIsosurface(*field, iso, nullptr);
            if (signedField && iso > 0.0)
                m.negative = core::extractIsosurface(*field, -iso, nullptr);
            return m;
        }));
}

void VolumetricPanel::onIsoExtractionFinished()
{
    if (isoRunningGeneration_ != isoGeneration_) {
        pumpIsoExtraction();
        return;
    }
    pushPhaseMeshes(isoWatcher_.result());
    pumpIsoExtraction();
}

void VolumetricPanel::pushPhaseMeshes(const PhaseMeshes& meshes)
{
    if (!viewport_)
        return;
    if (meshes.positive.positions.empty() && meshes.negative.positions.empty()) {
        viewport_->clearCustomOverlay();
        return;
    }
    std::vector<float> faces;
    std::vector<render::StructureRenderer::OverlayRange> ranges;
    const float alpha = static_cast<float>(style_.isoOpacity);

    const auto appendLobe = [&](const core::IsoMesh& mesh, const QColor& color) {
        if (mesh.positions.empty())
            return;
        const float r = static_cast<float>(color.redF());
        const float g = static_cast<float>(color.greenF());
        const float b = static_cast<float>(color.blueF());
        const int first = static_cast<int>(faces.size() / 6);
        for (const core::Vec3& p : mesh.positions)
            faces.insert(faces.end(),
                         {static_cast<float>(p.x), static_cast<float>(p.y),
                          static_cast<float>(p.z), r, g, b});
        ranges.push_back(
            {first, static_cast<int>(mesh.positions.size()), alpha});
    };
    appendLobe(meshes.positive, style_.positiveColor);
    appendLobe(meshes.negative, style_.negativeColor);

    viewport_->setCustomOverlay(std::move(faces), {}, std::move(ranges),
                                /*visible=*/true);
}

} // namespace calango::gui
