#include "gui/OverlayPanel.hpp"

#include "gui/OverlayEditDialog.hpp"
#include "gui/ViewportWidget.hpp"
#include "ui/IconManager.hpp"

#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

OverlayPanel::OverlayPanel(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent)
    , viewport_(viewport)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    list_ = new QListWidget(this);
    list_->setToolTip(
        tr("Everything drawn over the structure. Unchecking an entry hides it "
           "without deleting it, so a figure can be built up and stripped back "
           "without losing the settings."));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list_, 1);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(4);
    const auto makeButton = [this, buttons](const QString& text,
                                            const QString& icon,
                                            const QString& tip) {
        auto* button = new QPushButton(text, this);
        ui::IconManager::bind(button, icon);
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        buttons->addWidget(button);
        return button;
    };
    addButton_ = makeButton(
        tr("Add"), QStringLiteral("add-circle-fill"),
        tr("Add overlay… — choose a type (lattice plane, text, box, sphere, "
           "tube…) and set its properties."));
    removeButton_ = makeButton(
        tr("Remove"), QStringLiteral("indeterminate-circle-fill"),
        tr("Remove overlay… — delete the selected entry."));
    editButton_ = makeButton(
        tr("Edit"), QStringLiteral("edit-fill"),
        tr("Edit overlay… — reopen the selected entry's properties. Changes "
           "apply live; Cancel puts it back as it was."));
    resetButton_ = makeButton(
        tr("Reset"), QStringLiteral("arrow-go-back-line"),
        tr("Reset overlays — remove every overlay and clear the viewport."));
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(addButton_, &QPushButton::clicked, this, &OverlayPanel::addOverlay);
    connect(removeButton_, &QPushButton::clicked, this,
            &OverlayPanel::removeOverlay);
    connect(editButton_, &QPushButton::clicked, this, &OverlayPanel::editOverlay);
    connect(resetButton_, &QPushButton::clicked, this,
            &OverlayPanel::resetOverlays);
    connect(list_, &QListWidget::currentRowChanged, this,
            &OverlayPanel::onSelectionChanged);
    connect(list_, &QListWidget::itemDoubleClicked, this,
            &OverlayPanel::editOverlay);
    // A check-state change is a visibility toggle, not an edit.
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const int row = list_->row(item);
        if (row < 0 || row >= static_cast<int>(overlays_.size()))
            return;
        const bool visible = item->checkState() == Qt::Checked;
        if (overlays_[static_cast<std::size_t>(row)].visible == visible)
            return;
        overlays_[static_cast<std::size_t>(row)].visible = visible;
        pushToViewport();
    });

    if (viewport_) {
        connect(viewport_, &ViewportWidget::textOverlayMoved, this,
                &OverlayPanel::onTextOverlayMoved);
        // A lattice plane is defined against the cell it is drawn in, so a new
        // structure means every plane has to be re-tessellated.
        connect(viewport_, &ViewportWidget::structureReplaced, this,
                &OverlayPanel::refresh);
    }
    onSelectionChanged();
}

Overlay* OverlayPanel::current()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(overlays_.size()))
        return nullptr;
    return &overlays_[static_cast<std::size_t>(row)];
}

void OverlayPanel::onSelectionChanged()
{
    const bool has = current() != nullptr;
    removeButton_->setEnabled(has);
    editButton_->setEnabled(has);
    resetButton_->setEnabled(!overlays_.empty());
}

void OverlayPanel::refreshList()
{
    const int keep = list_->currentRow();
    // Repopulating fires itemChanged for every row; the visibility handler
    // would then write the half-built list back over the model.
    const QSignalBlocker blocker(list_);
    list_->clear();
    for (const Overlay& overlay : overlays_) {
        auto* item = new QListWidgetItem(overlay.displayName(), list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(overlay.visible ? Qt::Checked : Qt::Unchecked);
    }
    list_->setCurrentRow(
        std::clamp(keep, overlays_.empty() ? -1 : 0,
                   static_cast<int>(overlays_.size()) - 1));
    onSelectionChanged();
}

void OverlayPanel::pushToViewport()
{
    if (!viewport_)
        return;
    const auto held = viewport_->structure();
    const core::Structure* structure = held ? held.get() : nullptr;

    std::vector<float> faces;
    std::vector<float> edges;
    std::vector<render::StructureRenderer::OverlayRange> ranges;
    std::vector<ViewportWidget::TextOverlay> texts;
    for (const Overlay& overlay : overlays_) {
        appendOverlayGeometry(overlay, structure, faces, edges, ranges);
        if (overlay.kind != Overlay::Kind::Text || !overlay.visible
            || overlay.text.isEmpty())
            continue;
        ViewportWidget::TextOverlay text;
        text.id = overlay.id;
        text.text = overlay.text;
        text.position = overlay.center;
        text.font = overlay.font;
        text.color = overlay.color;
        text.visible = true;
        texts.push_back(std::move(text));
    }

    viewport_->setManagedOverlay(std::move(faces), std::move(edges),
                                 std::move(ranges), !overlays_.empty());
    viewport_->setTextOverlays(std::move(texts));
}

void OverlayPanel::refresh()
{
    pushToViewport();
}

bool OverlayPanel::editInDialog(Overlay& overlay, const QString& title)
{
    const auto held = viewport_ ? viewport_->structure() : nullptr;
    const bool hasCell = held && held->cell().isDefined();

    // The editor writes into its own copy and signals on every keystroke, so
    // the viewport previews the change as it is made. Cancel restores the
    // snapshot taken here — which is why the preview can be applied live
    // without committing anything.
    const Overlay snapshot = overlay;
    OverlayEditDialog dialog(overlay, hasCell, this);
    dialog.setWindowTitle(title);
    connect(&dialog, &OverlayEditDialog::changed, this, [this, &overlay, &dialog] {
        overlay = dialog.overlay();
        pushToViewport();
    });
    if (dialog.exec() == QDialog::Accepted) {
        overlay = dialog.overlay();
        return true;
    }
    overlay = snapshot;
    pushToViewport();
    return false;
}

void OverlayPanel::addOverlay()
{
    Overlay overlay;
    overlay.id = nextId_++;
    // Seed a new overlay at the centre of what is on screen rather than at the
    // world origin, which for a slab or a molecule placed in a vacuum box can
    // be far outside the view — the user would add something and see nothing.
    if (viewport_) {
        const QVector3D target = viewport_->camera().target();
        overlay.center = {target.x(), target.y(), target.z()};
        overlay.endPoint = {target.x(), target.y(), target.z() + 4.0};
    }
    overlay.font.setPointSize(14);
    overlay.font.setBold(true);
    overlay.text = tr("Label");

    if (!editInDialog(overlay, tr("Add Overlay")))
        return;
    overlays_.push_back(std::move(overlay));
    refreshList();
    list_->setCurrentRow(static_cast<int>(overlays_.size()) - 1);
    pushToViewport();
}

void OverlayPanel::removeOverlay()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(overlays_.size()))
        return;
    overlays_.erase(overlays_.begin() + row);
    refreshList();
    pushToViewport();
}

void OverlayPanel::editOverlay()
{
    Overlay* overlay = current();
    if (!overlay)
        return;
    editInDialog(*overlay, tr("Edit Overlay"));
    refreshList();
    pushToViewport();
}

void OverlayPanel::resetOverlays()
{
    if (overlays_.empty())
        return;
    // Deleting several things at once with no undo behind it deserves a
    // question; removing one does not, which is why only this asks.
    if (QMessageBox::question(
            this, tr("Reset Overlays"),
            tr("Remove all %n overlay(s)?", nullptr,
               static_cast<int>(overlays_.size())))
        != QMessageBox::Yes)
        return;
    overlays_.clear();
    refreshList();
    if (viewport_) {
        viewport_->clearManagedOverlay();
        viewport_->setTextOverlays({});
    }
}

void OverlayPanel::onTextOverlayMoved(int id, const core::Vec3& position)
{
    const auto it = std::find_if(overlays_.begin(), overlays_.end(),
                                 [id](const Overlay& o) { return o.id == id; });
    if (it == overlays_.end())
        return;
    it->center = position;
    // No pushToViewport() here: the viewport already moved its own copy while
    // dragging, and re-pushing mid-drag would fight the interaction. This only
    // keeps the model in step so the next edit or rebuild sees the new anchor.
}

} // namespace calango::gui
