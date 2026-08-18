#include "gui/VolumetricPanel.hpp"

#include "render/ShaderProfile.hpp"

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
#include <unordered_map>

namespace calango::gui {

namespace {
constexpr int kRefineFactor = 2; // upsampling factor for grid interpolation

/// The two directions the baked isosurface shading is lit from — the key and
/// fill of StructureRenderer::defaultLights(), negated into "direction toward
/// the light". Reusing the atoms' studio setup is what makes a shaded surface
/// look like it shares their scene instead of being lit from somewhere else.
const core::Vec3 kKeyLight = core::Vec3{0.4, 0.5, 1.0}.normalized();
const core::Vec3 kFillLight = core::Vec3{-0.7, -0.25, 0.55}.normalized();
/// Blinn-Phong half-vector for the Glossy finish. The overlay path is unlit and
/// the geometry is uploaded once, so there is no camera to reflect about; the
/// highlight is fixed to the key light seen from roughly in front, which reads
/// as a sheen on the surface rather than as a moving specular.
const core::Vec3 kHalfVector =
    (kKeyLight + core::Vec3{0.0, 0.0, 1.0}).normalized();
constexpr double kGlossExponent = 24.0;

/// Baked shading factor for a vertex normal, in [0, 1+] (Glossy can exceed 1,
/// which the caller clamps after scaling the colour).
struct ShadeTerms {
    double diffuse = 1.0;
    double specular = 0.0;
};

ShadeTerms shadeNormal(const core::Vec3& normal, const VolumetricStyle& style)
{
    if (style.shading == IsoShading::Flat)
        return {};
    const double length = normal.norm();
    if (length < 1e-12)
        return {}; // a degenerate normal: leave the colour alone
    const core::Vec3 n = normal * (1.0 / length);
    // std::abs on both dots: an isosurface is a closed shell whose back faces
    // are as visible as its front ones (through the lobe, or when the camera
    // is inside it). Signed lighting would paint half of every orbital black.
    const double key = std::abs(n.dot(kKeyLight));
    const double fill = 0.35 * std::abs(n.dot(kFillLight));
    const double ambient = std::clamp(style.ambient, 0.0, 1.0);
    ShadeTerms terms;
    terms.diffuse =
        ambient + (1.0 - ambient) * std::clamp(key + fill, 0.0, 1.0);
    if (style.shading == IsoShading::Glossy) {
        terms.specular = std::clamp(style.specular, 0.0, 1.0)
            * std::pow(std::abs(n.dot(kHalfVector)), kGlossExponent);
    }
    return terms;
}

} // namespace

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
           "— loaded from disk or produced by a calculation (e.g. Wannier Functions "
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
                                         int workspaceId,
                                         const DatasetOrigin& origin)
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
             workspaceId >= 0 ? workspaceId : activeWorkspace_, origin);
}

void VolumetricPanel::addEntry(
    std::shared_ptr<const core::VolumetricData> field, const QString& label,
    const QString& path, const QString& structureLabel, int workspaceId,
    const DatasetOrigin& origin)
{
    if (!field || field->empty())
        return;
    // Resolve the continuation centre once, here. The wannierization's own
    // number when it recorded one; otherwise the field's periodic centroid,
    // which is the same quantity (the circular mean of |psi|^2) computed from
    // the grid instead of read from JSON. Doing it at registration keeps the
    // O(N) sweep off the extraction thread, which reruns on every isovalue
    // nudge.
    core::Vec3 centre{};
    if (origin.wannier)
        centre = origin.hasCentre ? origin.centre
                                  : core::periodicCentroid(*field);
    const int index = static_cast<int>(entries_.size());
    // Wannier functions land UNCHECKED: a wannierization typically produces
    // several orbitals, and rendering every isosurface at once the moment
    // the calculation finishes is a wall of overlapping lobes nobody asked
    // for — the user picks which ones to look at. Every other origin keeps
    // the previous "visible on arrival" default (a single density cube, or
    // a file loaded by hand, is exactly the one thing you want to see).
    entries_.push_back({field, label, path, structureLabel, workspaceId,
                        /*visible=*/!origin.wannier, origin.wannier, centre});

    // A Wannier function arriving in a tab promotes that tab's material to
    // Glossy — ONCE, and never over a choice the user has made.
    //
    // Glossy is what these need: a Wannier lobe's shape is all curvature, and
    // Flat draws it as a silhouette in which two overlapping lobes are one
    // blob. The alternative was a per-dataset material, which this style has
    // never had — `shading` is one knob per workspace tab, applied to every
    // surface in it — so this is a default, applied at the moment the tab first
    // holds something that wants it, rather than a second parallel default.
    if (origin.wannier)
        promoteWannierMaterial(workspaceId);

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
        item->setCheckState(0, origin.wannier ? Qt::Unchecked : Qt::Checked);
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

void VolumetricPanel::promoteWannierMaterial(int workspaceId)
{
    // The style to touch is the LIVE one when the dataset landed in the tab on
    // screen, and the stashed one otherwise — a run can finish while its tab is
    // in the background, and writing style_ then would repaint the wrong tab.
    VolumetricStyle* style = nullptr;
    if (workspaceId < 0 || workspaceId == activeWorkspace_) {
        style = &style_;
    } else {
        const auto it = workspaceStates_.find(workspaceId);
        // A tab never visited has no stashed state yet; seed it from the
        // defaults so the promotion is not lost when it is first shown.
        if (it == workspaceStates_.end())
            style = &workspaceStates_[workspaceId].style;
        else
            style = &it->second.style;
    }
    if (style->shadingChosen)
        return; // the user's material wins, always
    style->shading = IsoShading::Glossy;

    if (style == &style_ && editDialog_) {
        // setStyle() is the dialog's own refresh path; going through it keeps
        // the combo, the enable states and the tool tips consistent, and its
        // `updating_` guard stops the sync from echoing back as a change.
        editDialog_->setStyle(style_, mode_);
        syncEditDialogDatasets();
    }
}

bool VolumetricPanel::activeWorkspaceHasWannier() const
{
    for (const Entry& entry : entries_)
        if (entry.wannier && inActiveWorkspace(entry))
            return true;
    return false;
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

void VolumetricPanel::stashWorkspaceState()
{
    if (activeWorkspace_ >= 0)
        workspaceStates_[activeWorkspace_] = WorkspaceState{style_, mode_};
}

void VolumetricPanel::setActiveWorkspace(int id)
{
    if (activeWorkspace_ == id)
        return;
    // Hand the outgoing tab its render settings back before adopting the
    // incoming tab's. The atoms, bonds and camera have been per-tab for a long
    // time; the volumetric style was the one piece of the scene still held in
    // a single global, so every tab change reset it.
    stashWorkspaceState();
    activeWorkspace_ = id;

    const auto it = workspaceStates_.find(id);
    const bool restored = it != workspaceStates_.end();
    if (restored) {
        style_ = it->second.style;
        mode_ = it->second.mode;
    } else {
        // A tab seen for the first time starts from the defaults rather than
        // inheriting the outgoing tab's: an isovalue is a number in one
        // specific field's units and usually falls outside the next field's
        // range entirely, which shows as an empty viewport.
        style_ = VolumetricStyle{};
        mode_ = VolumetricRenderMode::Isosurface;
    }

    applyWorkspaceFilter();
    // Drop whatever the previous tab was showing before drawing this tab's set.
    ++isoGeneration_;
    isoPending_ = false;
    clearViewportOverlay();
    if (editDialog_)
        editDialog_->setStyle(style_, mode_);
    // After setStyle: the secondary-field index is a registry position, and
    // this re-resolves it against the datasets THIS tab actually owns.
    syncEditDialogDatasets();
    // A restored style carries the user's own isovalue. Re-deriving it from
    // whichever dataset the filter just selected is precisely the reset this
    // per-tab state exists to prevent, so it is suppressed for this one call.
    restoringWorkspace_ = restored;
    onSelectionChanged();
    restoringWorkspace_ = false;
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
    if (!editDialog_)
        return;
    editDialog_->setDatasets(datasetLabels(), currentRow());
    // Every caller of this already runs on the events that change which
    // datasets a tab holds — registration, removal, tab switch — so it is also
    // the right place to keep the Wannier-only controls in step.
    editDialog_->setHasWannier(activeWorkspaceHasWannier());
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
        if (!restoringWorkspace_)
            defaultIsovalueForField();
        if (editDialog_) {
            editDialog_->setFieldRange(field->minValue(), field->maxValue());
            // The isovalue histogram bins this volume's own values once,
            // here, rather than per slider move — the load-time cost this
            // caches against.
            editDialog_->setFieldHistogram(field->values, field->minValue(),
                                           field->maxValue());
        }
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
    if (field)
        editDialog_->setFieldHistogram(field->values, lo, hi);
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
    // The volume texture belongs to DirectVolume alone; leaving it bound while
    // another mode draws would composite a medium nobody asked for over the
    // isosurface that replaced it.
    if (mode_ != VolumetricRenderMode::DirectVolume)
        viewport_->clearVolumeField();
    // Only checked datasets bound to the workspace tab on screen are drawn;
    // everything else (other tabs, unticked rows) stays off the viewport.
    if (renderableRows().empty()) {
        clearViewportOverlay();
        ++isoGeneration_;
        isoPending_ = false;
        return;
    }
    if (mode_ == VolumetricRenderMode::DirectVolume) {
        pushDirectVolume();
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

void VolumetricPanel::pushDirectVolume()
{
    const core::VolumetricData* field = currentField();
    if (!field || field->empty()) {
        viewport_->clearVolumeField();
        viewport_->update();
        return;
    }
    // Normalize into [0,1] against the field's own range, so the transfer
    // function is expressed once against a fixed axis and does not have to be
    // rebuilt when the dataset changes.
    const double lo = field->minValue();
    const double hi = field->maxValue();
    const double range = std::max(hi - lo, 1e-30);
    std::vector<float> values;
    values.reserve(field->values.size());
    for (const double v : field->values)
        values.push_back(
            static_cast<float>(std::clamp((v - lo) / range, 0.0, 1.0)));

    // The transfer function: the dialog's colour ramp, with opacity rising
    // from the threshold. A linear ramp rather than an editable curve for now
    // — the curve editor is a feature of its own, and the threshold plus the
    // density scale already cover the common "show me the shell, not the fog"
    // adjustment.
    constexpr int kLutSize = 256;
    std::vector<float> transfer;
    transfer.reserve(kLutSize * 4);
    for (int i = 0; i < kLutSize; ++i) {
        const float t = static_cast<float>(i) / (kLutSize - 1);
        const QColor c = render::ColorMap::sample(style_.gradient, t,
                                                  style_.invertGradient);
        transfer.push_back(static_cast<float>(c.redF()));
        transfer.push_back(static_cast<float>(c.greenF()));
        transfer.push_back(static_cast<float>(c.blueF()));
        // Opacity grows as t^2: a linear ramp makes the low-value bulk of a
        // density as visible as its core, which buries the structure in haze.
        transfer.push_back(t * t);
    }

    // The unit cube -> the grid's own parallelepiped. Built from the span
    // vectors rather than from a bounding box, so a triclinic cell is handled
    // without a special case.
    QMatrix4x4 box;
    box.setColumn(0, QVector4D(static_cast<float>(field->spanA.x),
                               static_cast<float>(field->spanA.y),
                               static_cast<float>(field->spanA.z), 0.0f));
    box.setColumn(1, QVector4D(static_cast<float>(field->spanB.x),
                               static_cast<float>(field->spanB.y),
                               static_cast<float>(field->spanB.z), 0.0f));
    box.setColumn(2, QVector4D(static_cast<float>(field->spanC.x),
                               static_cast<float>(field->spanC.y),
                               static_cast<float>(field->spanC.z), 0.0f));
    box.setColumn(3, QVector4D(static_cast<float>(field->origin.x),
                               static_cast<float>(field->origin.y),
                               static_cast<float>(field->origin.z), 1.0f));

    viewport_->clearCustomOverlay();
    viewport_->setVolumeField(field->nx, field->ny, field->nz, values, transfer,
                              box);
    viewport_->setVolumeParams(style_.directVolume.steps,
                               static_cast<float>(style_.directVolume.density),
                               static_cast<float>(style_.directVolume.threshold),
                               style_.directVolume.lit);
    viewport_->update();
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

    // Each base carries what it needs to be extracted, because a Wannier
    // function is not extracted the same way as a density: the worker below
    // has no access to entries_.
    struct Base {
        std::shared_ptr<const core::VolumetricData> field;
        bool wannier = false;
        core::Vec3 centre{};
    };
    std::vector<Base> bases;
    for (const int row : renderableRows()) {
        const Entry& e = entries_[static_cast<std::size_t>(row)];
        bases.push_back({e.field, e.wannier, e.centre});
    }
    if (bases.empty())
        return;

    isoPending_ = false;
    isoRunningGeneration_ = ++isoGeneration_;

    const double requestedIso = std::abs(style_.isovalue);
    const core::GridInterpolation interp = style_.gridInterpolation;
    const double margin = std::clamp(style_.continuationMargin, 0.0,
                                     core::kMaxContinuationMargin);
    // Smoothing runs here, not in pushResults(): it is the expensive half of
    // the geometry work (a weld + adjacency build over every vertex), and this
    // is the thread that already exists to keep that off the GUI.
    const int smoothing = std::clamp(style_.smoothing, 0, 20);

    isoWatcher_.setFuture(QtConcurrent::run(
        [bases = std::move(bases), secondary = std::move(secondary),
         requestedIso, potential, interp, margin,
         smoothing]() -> std::vector<ExtractResult> {
            std::vector<ExtractResult> results;
            results.reserve(bases.size());
            for (const auto& entry : bases) {
                const auto& base = entry.field;
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
                } else if (entry.wannier) {
                    // A Wannier function is localized but not confined: its
                    // centre sits wherever the wannierization put it and its
                    // tails cross the cell faces, so extracting over the home
                    // cell alone cuts the lobe flat and strands the rest on
                    // the far side of the box. Re-window on the centre first.
                    //
                    // The centre is in Cartesian angstrom and the refined grid
                    // (if any) covers the same box, so it needs no rescaling.
                    r.positive = core::extractContinuedIsosurface(
                        bf, iso, entry.centre, margin);
                    if (signedField && iso > 0.0)
                        r.negative = core::extractContinuedIsosurface(
                            bf, -iso, entry.centre, margin);
                } else {
                    r.positive = core::extractIsosurface(bf, iso, nullptr);
                    if (signedField && iso > 0.0)
                        r.negative = core::extractIsosurface(bf, -iso, nullptr);
                }
                core::smoothMesh(r.positive, smoothing);
                core::smoothMesh(r.negative, smoothing);
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
    std::vector<float> lines;
    std::vector<render::StructureRenderer::OverlayRange> ranges;
    const float alpha = static_cast<float>(style_.isoOpacity);
    const IsoDrawStyle draw = style_.drawStyle;
    const bool wantFaces =
        draw == IsoDrawStyle::Solid || draw == IsoDrawStyle::SolidMesh;
    const bool wantMesh =
        draw == IsoDrawStyle::Mesh || draw == IsoDrawStyle::SolidMesh;
    const bool wantDots = draw == IsoDrawStyle::Dots;
    // Wires drawn OVER a fill have to be darker than it to be seen; wires that
    // ARE the surface keep its colour.
    const double wireShade = draw == IsoDrawStyle::SolidMesh
        ? std::clamp(style_.meshShade, 0.0, 1.0)
        : 1.0;
    const double dot = std::max(style_.dotSize, 1e-3);
    const std::size_t stride =
        static_cast<std::size_t>(std::max(style_.dotStride, 1));

    // Every extracted dataset appends into one interleaved stream; each mesh
    // gets its own OverlayRange so it blends at the shared opacity. Lines (the
    // Mesh and Dots styles) go into a second stream that blends as a whole.
    const auto appendMesh = [&](const core::IsoMesh& mesh,
                                const QColor& uniformColor, bool colorByValue,
                                double lo, double hi) {
        if (mesh.positions.empty())
            return;
        const double range = std::max(hi - lo, 1e-30);
        const int first = static_cast<int>(
            faces.size()
            / render::StructureRenderer::kOverlayFaceFloats);
        const float ur = static_cast<float>(uniformColor.redF());
        const float ug = static_cast<float>(uniformColor.greenF());
        const float ub = static_cast<float>(uniformColor.blueF());

        // Per-vertex colour: the flat phase colour or the potential-map ramp.
        // The SHADING is folded in only for the legacy profile — see below.
        const bool baked = render::ShaderRegistry::activeProfile(
                               render::ShaderSlot::Isosurfaces)
                               .isLegacy;
        std::vector<std::array<float, 3>> colors(mesh.positions.size());
        for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
            double r = ur, g = ug, b = ub;
            if (colorByValue && i < mesh.colorValues.size()) {
                const QColor c = render::ColorMap::sample(
                    style_.gradient,
                    static_cast<float>(
                        std::clamp((mesh.colorValues[i] - lo) / range, 0.0, 1.0)),
                    style_.invertGradient);
                r = c.redF();
                g = c.greenF();
                b = c.blueF();
            }
            // Baked ONLY for the legacy profile. The lit profile shades on
            // the GPU from the normals emitted below, which is what lets the
            // highlight follow the camera; baking as well would apply the
            // shading twice.
            if (baked && style_.shading != IsoShading::Flat
                && i < mesh.normals.size()) {
                const ShadeTerms terms = shadeNormal(mesh.normals[i], style_);
                r = r * terms.diffuse + terms.specular;
                g = g * terms.diffuse + terms.specular;
                b = b * terms.diffuse + terms.specular;
            }
            colors[i] = {static_cast<float>(std::clamp(r, 0.0, 1.0)),
                         static_cast<float>(std::clamp(g, 0.0, 1.0)),
                         static_cast<float>(std::clamp(b, 0.0, 1.0))};
        }

        const auto emitVertex = [](std::vector<float>& out, const core::Vec3& p,
                                   const std::array<float, 3>& c, double tint) {
            out.insert(out.end(),
                       {static_cast<float>(p.x), static_cast<float>(p.y),
                        static_cast<float>(p.z),
                        static_cast<float>(c[0] * tint),
                        static_cast<float>(c[1] * tint),
                        static_cast<float>(c[2] * tint)});
        };

        // Faces carry a normal; lines do not. Marching cubes already derives
        // one per vertex from the field gradient, so nothing extra is computed
        // to feed the lit profile.
        const auto emitFace = [](std::vector<float>& out, const core::Vec3& p,
                                 const core::Vec3& n,
                                 const std::array<float, 3>& c) {
            out.insert(out.end(),
                       {static_cast<float>(p.x), static_cast<float>(p.y),
                        static_cast<float>(p.z), static_cast<float>(n.x),
                        static_cast<float>(n.y), static_cast<float>(n.z),
                        c[0], c[1], c[2]});
            static_assert(render::StructureRenderer::kOverlayFaceFloats == 9,
                          "emitFace writes 9 floats per vertex");
        };

        if (wantFaces) {
            for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
                const core::Vec3 n = i < mesh.normals.size()
                    ? mesh.normals[i]
                    : core::Vec3{0.0, 0.0, 1.0};
                emitFace(faces, mesh.positions[i], n, colors[i]);
            }
            ranges.push_back(
                {first, static_cast<int>(mesh.positions.size()), alpha});
        }
        if (wantMesh) {
            // Three edges per triangle. Shared edges are emitted twice — the
            // welding that would deduplicate them costs more than the second
            // line does, and the two are collinear so nothing shows.
            const std::size_t triangles = mesh.positions.size() / 3;
            for (std::size_t t = 0; t < triangles; ++t) {
                for (int k = 0; k < 3; ++k) {
                    const std::size_t a = 3 * t + static_cast<std::size_t>(k);
                    const std::size_t b = 3 * t + (k + 1) % 3;
                    emitVertex(lines, mesh.positions[a], colors[a], wireShade);
                    emitVertex(lines, mesh.positions[b], colors[b], wireShade);
                }
            }
        }
        if (wantDots) {
            // A "dot" is a small axis-aligned cross centred on the vertex:
            // core-profile GL gives no per-draw point size here, and three
            // short segments read as a mark at any zoom.
            for (std::size_t i = 0; i < mesh.positions.size(); i += stride) {
                const core::Vec3& p = mesh.positions[i];
                const core::Vec3 axes[3] = {
                    {dot, 0.0, 0.0}, {0.0, dot, 0.0}, {0.0, 0.0, dot}};
                for (const core::Vec3& axis : axes) {
                    emitVertex(lines, p - axis, colors[i], 1.0);
                    emitVertex(lines, p + axis, colors[i], 1.0);
                }
            }
        }
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

    if (faces.empty() && lines.empty()) {
        viewport_->clearCustomOverlay();
        return;
    }
    // The shading controls reach the GPU as uniforms for the lit profile;
    // they still drive the CPU baking above for the legacy one, so the two
    // agree about what the dialog means.
    auto& style = viewport_->style();
    style.isoShadingMode = static_cast<int>(style_.shading);
    style.isoAmbient = static_cast<float>(style_.ambient);
    style.isoSpecular = static_cast<float>(style_.specular);
    style.isoShininess = static_cast<float>(kGlossExponent);
    viewport_->setCustomOverlay(std::move(faces), std::move(lines),
                                std::move(ranges),
                                /*visible=*/true, alpha);
}

} // namespace calango::gui
