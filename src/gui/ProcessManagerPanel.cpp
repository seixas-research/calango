#include "gui/ProcessManagerPanel.hpp"

#include "ui/IconManager.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPushButton>
#include <QSize>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace calango::gui {

namespace {
constexpr int kIdRole = Qt::UserRole;
constexpr int kDirRole = Qt::UserRole + 1;

// RemixIcon stems + per-status tint for the Status column. Colors match the
// status text so the glyph and label read as one.
struct StatusVisual {
    const char* iconName;
    QColor color;
};
StatusVisual statusVisual(ProcessManagerPanel::Status status)
{
    switch (status) {
    case ProcessManagerPanel::Status::Queued:
        return {"time-line", QColor(160, 160, 170)};
    case ProcessManagerPanel::Status::Running:
        return {"loader-4-line", QColor(102, 153, 255)};
    case ProcessManagerPanel::Status::Completed:
        return {"checkbox-circle-line", QColor(110, 210, 130)};
    case ProcessManagerPanel::Status::Failed:
        return {"close-circle-line", QColor(224, 108, 96)};
    }
    return {"time-line", QColor(160, 160, 170)};
}
} // namespace

ProcessManagerPanel::ProcessManagerPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({tr("Task"), tr("Status"), tr("Started")});
    // Tree-item icons enlarged 20% (default 16 → 17-ish; the status glyphs are
    // rendered at 17 px to match).
    tree_->setIconSize(QSize(17, 17));
    tree_->setRootIsDecorated(false);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    // Delete / Backspace on a selected process triggers deletion (see
    // eventFilter); the tree has keyboard focus, so we filter its events.
    tree_->installEventFilter(this);
    layout->addWidget(tree_, 1);

    // Icon-only action bar: clean RemixIcon glyphs (theme-tinted via
    // IconManager), with the former text labels moved to descriptive tooltips.
    auto* buttons = new QHBoxLayout;
    const auto makeButton = [&](const QString& iconName, const QString& tip) {
        auto* button = new QPushButton(this);
        ui::IconManager::bind(button, iconName);
        // Action-button icons enlarged 20% (16 → 20) for visual clarity.
        button->setIconSize(QSize(20, 20));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFlat(false);
        buttons->addWidget(button);
        return button;
    };
    auto* openButton = makeButton(
        QStringLiteral("folder-open-line"),
        tr("Open Folder — reveal this task's working directory."));
    auto* loadButton = makeButton(
        QStringLiteral("slideshow-3-fill"),
        tr("Load Result — open this task's trajectory / bands / final "
           "structure in the workspace."));
    // The dedicated result viewers (Single-Point, Geometry Optimization,
    // Molecular Dynamics, MLWF, GW) are reached from HERE rather than from a
    // top-level menu: a viewer is meaningless without a process to view, and
    // asking for the process first means only the viewers this run actually
    // produced are ever offered.
    auto* viewerButton = makeButton(
        QStringLiteral("line-chart-line"),
        tr("Open Viewer — the dedicated results viewer for this run "
           "(Single-Point, Geometry Optimization, Molecular Dynamics, MLWF, "
           "GW, Born charges), chosen from the files it produced."));
    auto* scriptButton = makeButton(
        QStringLiteral("code-box-fill"),
        tr("View ASE Script — show the exact Python/ASE run.py that was "
           "executed."));
    auto* deleteButton = makeButton(
        QStringLiteral("delete-bin-line"),
        tr("Delete Process — stop it if running, delete its temporary data "
           "folder, and remove it from the list."));
    buttons->addStretch(1);
    layout->addLayout(buttons);

    const auto selectedDir = [this]() -> QString {
        const auto* item = tree_->currentItem();
        return item ? item->data(0, kDirRole).toString() : QString();
    };
    connect(openButton, &QPushButton::clicked, this, [selectedDir] {
        if (const QString dir = selectedDir(); !dir.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(scriptButton, &QPushButton::clicked, this, [this, selectedDir] {
        if (const QString dir = selectedDir(); !dir.isEmpty())
            Q_EMIT viewScriptRequested(dir);
    });
    connect(loadButton, &QPushButton::clicked, this, [this, selectedDir] {
        if (const QString dir = selectedDir(); !dir.isEmpty())
            Q_EMIT loadResultRequested(dir);
    });
    connect(viewerButton, &QPushButton::clicked, this, [this, selectedDir] {
        if (const QString dir = selectedDir(); !dir.isEmpty())
            Q_EMIT openViewerRequested(dir);
    });
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        if (const auto* item = tree_->currentItem())
            Q_EMIT deleteRequested(item->data(0, kIdRole).toInt());
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                const QString dir = item->data(0, kDirRole).toString();
                if (!dir.isEmpty())
                    Q_EMIT loadResultRequested(dir);
            });

    // Right-click a process for everything that can be done WITH that process,
    // viewers included. The menu itself is built by the controller, which is
    // what knows how to tell a completed GW run from a completed MD one.
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                const QTreeWidgetItem* item = tree_->itemAt(pos);
                if (!item)
                    return;
                // Right-clicking an unselected row acts on THAT row, which is
                // what every other tree in the app does; without this the menu
                // would silently describe a different process than the one
                // under the cursor.
                tree_->setCurrentItem(const_cast<QTreeWidgetItem*>(item));
                const QString dir = item->data(0, kDirRole).toString();
                if (!dir.isEmpty())
                    Q_EMIT contextMenuRequested(dir, tree_->viewport()->mapToGlobal(pos));
            });
}

bool ProcessManagerPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == tree_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        // Delete, Backspace, and ⌘⌫ (Backspace + Control on macOS) all delete.
        if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) {
            if (const auto* item = tree_->currentItem()) {
                Q_EMIT deleteRequested(item->data(0, kIdRole).toInt());
                return true; // consume so the tree doesn't also handle it
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

int ProcessManagerPanel::registerTask(const QString& name,
                                      const QString& directory)
{
    auto* item = new QTreeWidgetItem(
        {name, tr("queued"),
         QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))});
    const auto queued = statusVisual(Status::Queued);
    // Status glyph enlarged 20% (14 → 17).
    item->setIcon(1, ui::IconManager::icon(QLatin1String(queued.iconName),
                                           queued.color, 17));
    item->setForeground(1, QBrush(queued.color));
    item->setData(0, kIdRole, nextId_);
    item->setData(0, kDirRole, directory);
    item->setToolTip(0, directory.isEmpty() ? name : directory);
    tree_->addTopLevelItem(item);
    tree_->scrollToItem(item);
    tree_->setCurrentItem(item);
    return nextId_++;
}

QTreeWidgetItem* ProcessManagerPanel::itemForId(int id) const
{
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        if (tree_->topLevelItem(i)->data(0, kIdRole).toInt() == id)
            return tree_->topLevelItem(i);
    return nullptr;
}

void ProcessManagerPanel::setTaskDirectory(int id, const QString& directory)
{
    QTreeWidgetItem* item = itemForId(id);
    if (!item)
        return;
    item->setData(0, kDirRole, directory);
    item->setToolTip(0, directory.isEmpty() ? item->text(0) : directory);
}

void ProcessManagerPanel::removeTask(int id)
{
    if (QTreeWidgetItem* item = itemForId(id))
        delete tree_->takeTopLevelItem(tree_->indexOfTopLevelItem(item));
}

void ProcessManagerPanel::setTaskStatus(int id, Status status)
{
    QTreeWidgetItem* item = itemForId(id);
    if (!item)
        return;
    QString label;
    switch (status) {
    case Status::Queued:    label = tr("queued");    break;
    case Status::Running:   label = tr("running");   break;
    case Status::Completed: label = tr("completed"); break;
    case Status::Failed:    label = tr("failed");    break;
    }
    const auto visual = statusVisual(status);
    item->setText(1, label);
    item->setForeground(1, QBrush(visual.color));
    item->setIcon(1, ui::IconManager::icon(QLatin1String(visual.iconName),
                                           visual.color, 17));
}

} // namespace calango::gui
