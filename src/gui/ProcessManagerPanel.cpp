#include "gui/ProcessManagerPanel.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
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
    layout->addWidget(tree_, 1);

    auto* buttons = new QHBoxLayout;
    auto* openButton = new QPushButton(tr("Open Folder"), this);
    auto* loadButton = new QPushButton(tr("Load Result"), this);
    buttons->addWidget(openButton);
    buttons->addWidget(loadButton);
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
    connect(loadButton, &QPushButton::clicked, this, [this, selectedDir] {
        if (const QString dir = selectedDir(); !dir.isEmpty())
            Q_EMIT loadResultRequested(dir);
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                const QString dir = item->data(0, kDirRole).toString();
                if (!dir.isEmpty())
                    Q_EMIT loadResultRequested(dir);
            });
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
