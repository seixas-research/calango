#include "gui/ProcessManagerPanel.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace calango::gui {

namespace {
constexpr int kIdRole = Qt::UserRole;
constexpr int kDirRole = Qt::UserRole + 1;
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
    tree_->setRootIsDecorated(false);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    // Delete / Backspace on a selected process triggers deletion (see
    // eventFilter); the tree has keyboard focus, so we filter its events.
    tree_->installEventFilter(this);
    layout->addWidget(tree_, 1);

    // Icon-only action bar: intuitive glyphs from the active style, with the
    // former text labels moved to descriptive hover tooltips.
    auto* buttons = new QHBoxLayout;
    const auto icon = [this](QStyle::StandardPixmap sp) {
        return style()->standardIcon(sp);
    };
    const auto makeButton = [&](QStyle::StandardPixmap sp, const QString& tip) {
        auto* button = new QPushButton(this);
        button->setIcon(icon(sp));
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFlat(false);
        buttons->addWidget(button);
        return button;
    };
    auto* openButton = makeButton(
        QStyle::SP_DirOpenIcon,
        tr("Open Folder — reveal this task's working directory."));
    auto* loadButton = makeButton(
        QStyle::SP_FileDialogContentsView,
        tr("Load Result — open this task's trajectory / bands / final "
           "structure in the workspace."));
    auto* scriptButton = makeButton(
        QStyle::SP_FileIcon,
        tr("View ASE Script — show the exact Python/ASE run.py that was "
           "executed."));
    auto* deleteButton = makeButton(
        QStyle::SP_TrashIcon,
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
    switch (status) {
    case Status::Queued:
        item->setText(1, tr("queued"));
        item->setForeground(1, QBrush(QColor(160, 160, 170)));
        break;
    case Status::Running:
        item->setText(1, tr("running"));
        item->setForeground(1, QBrush(QColor(102, 153, 255)));
        break;
    case Status::Completed:
        item->setText(1, tr("completed"));
        item->setForeground(1, QBrush(QColor(110, 210, 130)));
        break;
    case Status::Failed:
        item->setText(1, tr("failed"));
        item->setForeground(1, QBrush(QColor(224, 108, 96)));
        break;
    }
}

} // namespace calango::gui
