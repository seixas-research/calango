#include "gui/VolumetricPanel.hpp"

#include "core/GridInterpolation.hpp"
#include "gui/EditVolumetricRenderDialog.hpp"
#include "gui/ViewportWidget.hpp"
#include "gui/VolumetricMetadataDialog.hpp"
#include "render/ColorMap.hpp"
#include "render/StructureRenderer.hpp"
#include "ui/IconManager.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>

namespace calango::gui {

namespace {
constexpr int kRefineFactor = 2; // upsampling factor for grid interpolation
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
        tr("3D scalar volumetric grids registered for the active workspace tab "
           "— loaded from disk or produced by a calculation (e.g. MLWF "
           "orbitals). Tick a row to render it on the 3D viewport; select one "
           "to style it, and press Delete to remove it."));
    registry_->installEventFilter(this); // Delete / Backspace removal
    layout->addWidget(registry_, 1);
    connect(registry_, &QTreeWidget::currentItemChanged, this,
            &VolumetricPanel::onSelectionChanged);
    // Per-row check boxes replace the former single global "Show" toggle.
    connect(registry_, &QTreeWidget::itemChanged, this,
            &VolumetricPanel::onItemChanged);

    // Compact icon-only action bar (RemixIcon glyphs, theme-tinted).
    auto* bar = new QHBoxLayout;
    const auto makeButton = [&](const QString& icon, const QString& tip) {
        auto* button = new QPushButton(this);
        ui::IconManager::bind(button, icon);
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
    auto* removeButton = makeButton(
        QStringLiteral("delete"),
        tr("Remove Dataset — unload the selected field from memory and the 3D "
           "viewport (or press Delete)."));
    bar->addStretch(1);
    layout->addLayout(bar);

    connect(loadButton, &QPushButton::clicked, this,
            &VolumetricPanel::loadExternalFile);
    connect(metaButton, &QPushButton::clicked, this,
            &VolumetricPanel::showMetadata);
    connect(editButton, &QPushButton::clicked, this,
            &VolumetricPanel::openEditDialog);
    connect(removeButton, &QPushButton::clicked, this,
            &VolumetricPanel::removeCurrentDataset);

    connect(&isoWatcher_, &QFutureWatcher<std::vector<ExtractResult>>::finished,
            this, &VolumetricPanel::onIsoExtractionFinished);

    onSelectionChanged(); // disables actions with an empty registry
}

VolumetricPanel::~VolumetricPanel()
{
    isoWatcher_.disconnect(this);
    if (isoWatcher_.isRunning())
        isoWatcher_.waitForFinished();
}

bool VolumetricPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == registry_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
            removeCurrentDataset();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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
                                         const QString& structureLabel,
                                         int workspaceId)
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
    // Callers that don't name a tab bind to the one on screen — a calculation
    // result belongs to the workspace it was launched from.
    addEntry(std::move(field), label, path, structureLabel,
             workspaceId >= 0 ? workspaceId : activeWorkspace_);
}

void VolumetricPanel::addEntry(
    std::shared_ptr<const core::VolumetricData> field, const QString& label,
    const QString& path, const QString& structureLabel, int workspaceId)
{
    if (!field || field->empty())
        return;
    const int index = static_cast<int>(entries_.size());
    entries_.push_back({field, label, path, structureLabel, workspaceId,
                        /*visible=*/true});

    // Build the row with itemChanged muted: setCheckState() during
    // construction would otherwise re-enter onItemChanged before the item is
    // fully populated.
    QTreeWidgetItem* item = nullptr;
    {
        const QSignalBlocker blocker(registry_);
        item = new QTreeWidgetItem(registry_);
        item->setText(0, label);
        item->setText(1, QStringLiteral("%1×%2×%3")
                             .arg(field->nx)
                             .arg(field->ny)
                             .arg(field->nz));
        item->setToolTip(0, path);
        item->setData(0, Qt::UserRole, index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
    }
    // A record registered while another tab is on screen stays hidden until
    // its own tab comes forward.
    const bool active = inActiveWorkspace(entries_.back());
    item->setHidden(!active);
    if (active)
        registry_->setCurrentItem(item); // → onSelectionChanged → render
    // Nothing to redraw otherwise: the set on screen is unchanged.
    syncEditDialogDatasets();
}

bool VolumetricPanel::inActiveWorkspace(const Entry& entry) const
{
    // A record with no workspace (registered before any tab existed) is never
    // orphaned — it stays available whichever tab is forward.
    return entry.workspaceId < 0 || entry.workspaceId == activeWorkspace_;
}

std::vector<int> VolumetricPanel::renderableRows() const
{
    std::vector<int> rows;
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const Entry& e = entries_[static_cast<std::size_t>(i)];
        if (e.visible && inActiveWorkspace(e) && e.field && !e.field->empty())
            rows.push_back(i);
    }
    return rows;
}

void VolumetricPanel::applyWorkspaceFilter()
{
    const QTreeWidgetItem* previous = registry_->currentItem();
    bool currentHidden = false;
    QTreeWidgetItem* firstVisible = nullptr;
    for (int i = 0; i < registry_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = registry_->topLevelItem(i);
        const int index = item->data(0, Qt::UserRole).toInt();
        if (index < 0 || index >= static_cast<int>(entries_.size()))
            continue;
        const bool show = inActiveWorkspace(entries_[static_cast<std::size_t>(index)]);
        item->setHidden(!show);
        if (show && !firstVisible)
            firstVisible = item;
        if (!show && item == previous)
            currentHidden = true;
    }
    if (currentHidden || !previous) {
        const QSignalBlocker blocker(registry_);
        registry_->setCurrentItem(firstVisible); // null clears the selection
    }
}

void VolumetricPanel::setActiveWorkspace(int id)
{
    if (activeWorkspace_ == id)
        return;
    activeWorkspace_ = id;
    applyWorkspaceFilter();
    // Drop whatever the previous tab was showing before drawing this tab's set.
    ++isoGeneration_;
    isoPending_ = false;
    clearViewportOverlay();
    syncEditDialogDatasets();
    onSelectionChanged();
}

void VolumetricPanel::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (!item || column != 0)
        return;
    const int index = item->data(0, Qt::UserRole).toInt();
    if (index < 0 || index >= static_cast<int>(entries_.size()))
        return;
    const bool visible = item->checkState(0) == Qt::Checked;
    Entry& entry = entries_[static_cast<std::size_t>(index)];
    if (entry.visible == visible)
        return;
    entry.visible = visible;
    render();
}

void VolumetricPanel::removeCurrentDataset()
{
    const int row = currentRow();
    if (row < 0) {
        QMessageBox::information(this, tr("Remove Dataset"),
                                 tr("Select a volumetric field first."));
        return;
    }
    {
        // Mutate the registry with signals blocked so the intermediate empty /
        // stale-index states never reach onSelectionChanged.
        const QSignalBlocker blocker(registry_);
        delete registry_->takeTopLevelItem(row); // items mirror entries_ order
        entries_.erase(entries_.begin() + row);
        // Re-index the remaining items to their new positions.
        for (int i = 0; i < registry_->topLevelItemCount(); ++i)
            registry_->topLevelItem(i)->setData(0, Qt::UserRole, i);
        // Select a sensible neighbor (or nothing when the list is now empty).
        if (registry_->topLevelItemCount() > 0)
            registry_->setCurrentItem(registry_->topLevelItem(
                std::min(row, registry_->topLevelItemCount() - 1)));
        // Re-indexing may have landed the selection on another tab's record.
        applyWorkspaceFilter();
    }
    // Drop any in-flight extraction result and refresh the viewport for the new
    // selection (or clear it when nothing remains).
    ++isoGeneration_;
    isoPending_ = false;
    clearViewportOverlay();
    syncEditDialogDatasets();
    onSelectionChanged();
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

QStringList VolumetricPanel::datasetLabels() const
{
    // One slot per registry index so a label's position stays the entry index
    // the style's base/secondary selectors store. Records belonging to another
    // workspace tab yield an empty slot, which the dialog skips — tab
    // isolation without renumbering.
    QStringList labels;
    for (const Entry& e : entries_)
        labels << (inActiveWorkspace(e) ? e.label : QString());
    return labels;
}

std::shared_ptr<const core::VolumetricData>
VolumetricPanel::fieldForIndex(int index) const
{
    if (index < 0 || index >= static_cast<int>(entries_.size()))
        return nullptr;
    // A stale index left over from another tab must not resurrect that tab's
    // field on this structure.
    const Entry& entry = entries_.at(static_cast<std::size_t>(index));
    return inActiveWorkspace(entry) ? entry.field : nullptr;
}

void VolumetricPanel::syncEditDialogDatasets()
{
    if (editDialog_)
        editDialog_->setDatasets(datasetLabels(), currentRow());
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
    if (const core::VolumetricData* field = currentField()) {
        defaultIsovalueForField();
        if (editDialog_)
            editDialog_->setFieldRange(field->minValue(), field->maxValue());
    }
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
    syncEditDialogDatasets();
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
    // Only checked datasets bound to the workspace tab on screen are drawn;
    // everything else (other tabs, unticked rows) stays off the viewport.
    if (renderableRows().empty()) {
        clearViewportOverlay();
        ++isoGeneration_;
        isoPending_ = false;
        return;
    }
    if (mode_ == VolumetricRenderMode::ColorSlice) {
        ++isoGeneration_; // cancel any queued/served extraction
        isoPending_ = false;
        viewport_->clearCustomOverlay();
        renderSlice();
    } else { // Isosurface or Potential Map — both extract a surface off-thread
        viewport_->clearLatticePlane();
        requestExtraction();
    }
}

void VolumetricPanel::renderSlice()
{
    // A color slice is a single cutting plane, so it follows the selected row
    // (falling back to the first visible one when the selection is unticked).
    const std::vector<int> rows = renderableRows();
    if (rows.empty() || !viewport_)
        return;
    const int row = currentRow();
    const int useRow =
        std::find(rows.begin(), rows.end(), row) != rows.end() ? row : rows.front();
    const std::shared_ptr<const core::VolumetricData> entryField =
        entries_[static_cast<std::size_t>(useRow)].field;
    if (!entryField)
        return;

    // Optional voxel-grid refinement before the plane samples it. Held in a
    // local so `field` can point at either the original or the refined copy
    // without a second code path below.
    core::VolumetricData refined;
    if (style_.sliceInterpolation != core::GridInterpolation::None)
        refined = core::refineGrid(*entryField, kRefineFactor,
                                   style_.sliceInterpolation);
    const core::VolumetricData* field =
        style_.sliceInterpolation == core::GridInterpolation::None
            ? entryField.get()
            : &refined;

    // Custom bounds pin the ramp; otherwise it spans the field's own range.
    const double lo =
        style_.sliceUseBounds ? std::min(style_.sliceMin, style_.sliceMax)
                              : field->minValue();
    const double hi =
        style_.sliceUseBounds ? std::max(style_.sliceMin, style_.sliceMax)
                              : field->maxValue();
    const double range = std::max(hi - lo, 1e-30);

    // --- Plane orientation from the Miller indices -------------------------
    // The grid's spanning vectors ARE the lattice the field was sampled on, so
    // the (hkl) family is defined against them: the plane normal is the
    // reciprocal-lattice vector G = h·(b×c) + k·(c×a) + l·(a×b).
    const core::Vec3 a = field->spanA, b = field->spanB, c = field->spanC;
    core::Vec3 normal = b.cross(c) * static_cast<double>(style_.millerH)
        + c.cross(a) * static_cast<double>(style_.millerK)
        + a.cross(b) * static_cast<double>(style_.millerL);
    if (normal.norm() < 1e-9)
        normal = a.cross(b); // degenerate (0 0 0) → the ab-plane normal
    if (normal.norm() < 1e-9)
        return;              // degenerate grid box
    normal = normal.normalized();

    // In-plane orthonormal basis (any pair perpendicular to the normal).
    const core::Vec3 helper =
        std::abs(normal.x) < 0.9 ? core::Vec3{1, 0, 0} : core::Vec3{0, 1, 0};
    const core::Vec3 uAxis = helper.cross(normal).normalized();
    const core::Vec3 vAxis = normal.cross(uAxis).normalized();

    // Sweep the plane through the whole box: project the three spanning
    // vectors on the normal to get the box's thickness along it, and let the
    // 0…1 offset travel exactly that span, centered on the box.
    const core::Vec3 center = field->origin + (a + b + c) * 0.5;
    const double thickness = std::abs(a.dot(normal)) + std::abs(b.dot(normal))
        + std::abs(c.dot(normal));
    const core::Vec3 planeOrigin =
        center + normal * ((std::clamp(style_.sliceOffset, 0.0, 1.0) - 0.5)
                           * thickness);
    // How far the quad is drawn, and how much of it survives. The parametric
    // sweep still uses a bounding-sphere half-extent so an oblique plane is
    // covered from any orientation — but the quad is then CLIPPED to whole
    // unit cells, which is what makes "just the cell" and "tiled over the
    // neighbours" mean something. Without the clip the plane always overshot,
    // and for a triclinic cell it visibly floated past the structure.
    const int replicas = std::clamp(style_.sliceReplicas, 1, 5);
    const double extent = static_cast<double>(replicas)
        * (0.5 * (a + b + c).norm()
           + 0.5 * std::max({a.norm(), b.norm(), c.norm()}));
    // Fractional window: 1 cell -> [0, 1], 3 cells -> [-1, 2]. Centred on the
    // cell, so growing the extent adds neighbours symmetrically instead of
    // marching off in one direction.
    const double margin = 0.5 * (replicas - 1);
    const double clipLo = -margin;
    const double clipHi = 1.0 + margin;

    // Cartesian → fractional grid coordinates (Cramer against the spans), so
    // an oblique plane can sample a triclinic grid.
    const core::Vec3 fBC = b.cross(c), fCA = c.cross(a), fAB = a.cross(b);
    const double det = a.dot(fBC);
    const double invDet = std::abs(det) > 1e-12 ? 1.0 / det : 0.0;
    if (invDet == 0.0)
        return;

    // Resolution: match the grid's own sampling along the two densest axes so
    // the slice never invents detail the data doesn't carry.
    const int divisions =
        std::clamp(std::max({field->nx, field->ny, field->nz}), 16, 256);

    std::vector<float> faces;
    faces.reserve(static_cast<std::size_t>(divisions) * divisions * 6 * 6);
    const auto point = [&](int i, int j) {
        const double s = (static_cast<double>(i) / divisions * 2.0 - 1.0) * extent;
        const double t = (static_cast<double>(j) / divisions * 2.0 - 1.0) * extent;
        return planeOrigin + uAxis * s + vAxis * t;
    };
    /// Fractional coordinates of a point against the grid's own spanning
    /// vectors — the same Cramer solve the sampler uses, reused here so the
    /// clip and the sampling agree exactly.
    const auto fractional = [&](const core::Vec3& p) {
        const core::Vec3 d = p - field->origin;
        return core::Vec3{d.dot(fBC) * invDet, d.dot(fCA) * invDet,
                          d.dot(fAB) * invDet};
    };
    const auto inside = [&](const core::Vec3& p) {
        const core::Vec3 f = fractional(p);
        // A hair of tolerance: a corner landing exactly on a cell face would
        // otherwise drop a row of quads along every boundary.
        constexpr double kEps = 1e-9;
        return f.x >= clipLo - kEps && f.x <= clipHi + kEps
            && f.y >= clipLo - kEps && f.y <= clipHi + kEps
            && f.z >= clipLo - kEps && f.z <= clipHi + kEps;
    };
    const auto emit_ = [&](const core::Vec3& p) {
        const core::Vec3 d = p - field->origin;
        const double value = field->samplePeriodic(d.dot(fBC) * invDet * field->nx,
                                                   d.dot(fCA) * invDet * field->ny,
                                                   d.dot(fAB) * invDet * field->nz);
        const QColor col = render::ColorMap::sample(
            style_.gradient,
            static_cast<float>(std::clamp((value - lo) / range, 0.0, 1.0)),
            style_.invertGradient);
        faces.insert(faces.end(),
                     {static_cast<float>(p.x), static_cast<float>(p.y),
                      static_cast<float>(p.z), static_cast<float>(col.redF()),
                      static_cast<float>(col.greenF()),
                      static_cast<float>(col.blueF())});
    };
    // Track the drawn corners so the optional border outlines what was
    // actually kept rather than the un-clipped sweep.
    double minS = extent, maxS = -extent, minT = extent, maxT = -extent;
    for (int i = 0; i < divisions; ++i) {
        for (int j = 0; j < divisions; ++j) {
            const core::Vec3 q[4] = {point(i, j), point(i + 1, j),
                                     point(i + 1, j + 1), point(i, j + 1)};
            // Whole quads, not individual triangles: a quad split across the
            // boundary would leave a sawtooth edge at the grid's resolution.
            if (!inside(q[0]) || !inside(q[1]) || !inside(q[2]) || !inside(q[3]))
                continue;
            const double s0 =
                (static_cast<double>(i) / divisions * 2.0 - 1.0) * extent;
            const double t0 =
                (static_cast<double>(j) / divisions * 2.0 - 1.0) * extent;
            const double step = 2.0 * extent / divisions;
            minS = std::min(minS, s0);
            maxS = std::max(maxS, s0 + step);
            minT = std::min(minT, t0);
            maxT = std::max(maxT, t0 + step);
            static constexpr int kQuad[6] = {0, 1, 2, 0, 2, 3};
            for (const int k : kQuad)
                emit_(q[k]);
        }
    }

    std::vector<float> edges;
    if (style_.sliceShowBorder && minS <= maxS && minT <= maxT) {
        const QColor border = style_.invertGradient
            ? render::ColorMap::sample(style_.gradient, 0.0f, false)
            : render::ColorMap::sample(style_.gradient, 1.0f, false);
        const core::Vec3 corners[4] = {
            planeOrigin + uAxis * minS + vAxis * minT,
            planeOrigin + uAxis * maxS + vAxis * minT,
            planeOrigin + uAxis * maxS + vAxis * maxT,
            planeOrigin + uAxis * minS + vAxis * maxT};
        for (int k = 0; k < 4; ++k) {
            for (const core::Vec3& p : {corners[k], corners[(k + 1) % 4]}) {
                edges.insert(edges.end(),
                             {static_cast<float>(p.x), static_cast<float>(p.y),
                              static_cast<float>(p.z),
                              static_cast<float>(border.redF()),
                              static_cast<float>(border.greenF()),
                              static_cast<float>(border.blueF())});
            }
        }
    }

    viewport_->setLatticePlane(std::move(faces), std::move(edges),
                               static_cast<float>(style_.sliceOpacity),
                               /*visible=*/true,
                               /*showEdges=*/style_.sliceShowBorder);
}

void VolumetricPanel::requestExtraction()
{
    isoPending_ = true;
    pumpIsoExtraction();
}

void VolumetricPanel::pumpIsoExtraction()
{
    if (!isoPending_ || isoWatcher_.isRunning())
        return;

    // Potential-map colouring is an OPTION on the isosurface now, not a mode
    // of its own — so it no longer replaces the base with a single chosen
    // field. Every ticked dataset is still extracted, and each gets painted by
    // the secondary field, which is what makes "compare these two orbitals,
    // both coloured by the potential" possible at all.
    std::shared_ptr<const core::VolumetricData> secondary =
        style_.potentialColoring ? fieldForIndex(style_.potentialSecondaryIndex)
                                 : nullptr;
    // Asking for the colouring without choosing a field to colour by would
    // extract a surface with no vertex colours and render it black.
    const bool potential = secondary != nullptr;

    std::vector<std::shared_ptr<const core::VolumetricData>> bases;
    for (const int row : renderableRows())
        bases.push_back(entries_[static_cast<std::size_t>(row)].field);
    if (bases.empty())
        return;

    isoPending_ = false;
    isoRunningGeneration_ = ++isoGeneration_;

    const double requestedIso = std::abs(style_.isovalue);
    const core::GridInterpolation interp = style_.gridInterpolation;

    isoWatcher_.setFuture(QtConcurrent::run(
        [bases = std::move(bases), secondary = std::move(secondary),
         requestedIso, potential, interp]() -> std::vector<ExtractResult> {
            std::vector<ExtractResult> results;
            results.reserve(bases.size());
            for (const auto& base : bases) {
                ExtractResult r;
                r.potential = potential;
                // Each field has its own value range: an isovalue chosen for
                // one may not be spanned by another, so fall back to its
                // mid-range rather than extracting an empty surface.
                const double bmin = base->minValue(), bmax = base->maxValue();
                double iso = requestedIso;
                if (iso < bmin || iso > bmax)
                    iso = bmin + 0.5 * (bmax - bmin);
                const bool signedField = bmin < 0.0;
                // Refine the grid before marching cubes when requested; skip
                // the copy entirely for None.
                const core::VolumetricData refined =
                    interp == core::GridInterpolation::None
                        ? core::VolumetricData{}
                        : core::refineGrid(*base, kRefineFactor, interp);
                const core::VolumetricData& bf =
                    interp == core::GridInterpolation::None ? *base : refined;
                if (potential) {
                    r.positive = core::extractIsosurface(bf, iso, secondary.get());
                } else {
                    r.positive = core::extractIsosurface(bf, iso, nullptr);
                    if (signedField && iso > 0.0)
                        r.negative = core::extractIsosurface(bf, -iso, nullptr);
                }
                results.push_back(std::move(r));
            }
            return results;
        }));
}

void VolumetricPanel::onIsoExtractionFinished()
{
    if (isoRunningGeneration_ != isoGeneration_) {
        pumpIsoExtraction();
        return;
    }
    pushResults(isoWatcher_.result());
    pumpIsoExtraction();
}

void VolumetricPanel::pushResults(const std::vector<ExtractResult>& results)
{
    if (!viewport_)
        return;
    std::vector<float> faces;
    std::vector<render::StructureRenderer::OverlayRange> ranges;
    const float alpha = static_cast<float>(style_.isoOpacity);

    // Every extracted dataset appends into one interleaved stream; each mesh
    // gets its own OverlayRange so it blends at the shared opacity.
    const auto appendMesh = [&](const core::IsoMesh& mesh,
                                const QColor& uniformColor, bool colorByValue,
                                double lo, double hi) {
        if (mesh.positions.empty())
            return;
        const double range = std::max(hi - lo, 1e-30);
        const int first = static_cast<int>(faces.size() / 6);
        const float ur = static_cast<float>(uniformColor.redF());
        const float ug = static_cast<float>(uniformColor.greenF());
        const float ub = static_cast<float>(uniformColor.blueF());
        for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
            const core::Vec3& p = mesh.positions[i];
            float r = ur, g = ug, b = ub;
            if (colorByValue && i < mesh.colorValues.size()) {
                const QColor c = render::ColorMap::sample(
                    style_.gradient,
                    static_cast<float>(
                        std::clamp((mesh.colorValues[i] - lo) / range, 0.0, 1.0)),
                    style_.invertGradient);
                r = static_cast<float>(c.redF());
                g = static_cast<float>(c.greenF());
                b = static_cast<float>(c.blueF());
            }
            faces.insert(faces.end(),
                         {static_cast<float>(p.x), static_cast<float>(p.y),
                          static_cast<float>(p.z), r, g, b});
        }
        ranges.push_back({first, static_cast<int>(mesh.positions.size()), alpha});
    };

    for (const ExtractResult& result : results) {
        if (result.potential) {
            // Color each vertex of the base isosurface by the secondary scalar
            // field (colorValues), through the colormap + ramp bounds.
            const core::IsoMesh& mesh = result.positive;
            double lo = style_.potentialMin, hi = style_.potentialMax;
            if (!style_.potentialUseBounds) {
                if (!mesh.colorValues.empty()) {
                    const auto [mn, mx] = std::minmax_element(
                        mesh.colorValues.begin(), mesh.colorValues.end());
                    lo = *mn;
                    hi = *mx;
                } else {
                    lo = 0.0;
                    hi = 1.0;
                }
            }
            appendMesh(mesh, style_.positiveColor,
                       /*colorByValue=*/!mesh.colorValues.empty(), lo, hi);
        } else {
            appendMesh(result.positive, style_.positiveColor, false, 0.0, 1.0);
            appendMesh(result.negative, style_.negativeColor, false, 0.0, 1.0);
        }
    }

    if (faces.empty()) {
        viewport_->clearCustomOverlay();
        return;
    }
    viewport_->setCustomOverlay(std::move(faces), {}, std::move(ranges),
                                /*visible=*/true);
}

} // namespace calango::gui
