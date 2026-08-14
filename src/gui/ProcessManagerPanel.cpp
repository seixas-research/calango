#include "gui/ProcessManagerPanel.hpp"

#include "ui/IconManager.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPushButton>
#include <QSize>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {
constexpr int kIdRole = Qt::UserRole;
constexpr int kDirRole = Qt::UserRole + 1;
/// The row's Status, so a control can ask what a task is DOING rather than
/// parsing the translated word in its status column. Load-bearing now that the
/// status column carries no text at all: there is nothing left to parse.
constexpr int kStatusRole = Qt::UserRole + 2;
/// When the task STARTED RUNNING (ms since epoch), 0 if it never did.
///
/// Deliberately not the registration time the "Started" column shows. Calango
/// queues jobs rather than refusing them, so a task can sit queued for a long
/// while; counting that as walltime would report a number that has nothing to
/// do with how long the calculation took.
constexpr int kRunStartRole = Qt::UserRole + 3;
/// When it stopped (ms since epoch), 0 while still running. Freezing the end
/// rather than the duration keeps the two timestamps symmetric and lets the
/// formatter stay the single place that subtracts.
constexpr int kRunEndRole = Qt::UserRole + 4;

/// Columns. Named because four of them addressed by literal index is how a
/// column gets written into the wrong cell.
enum Column { ColTask = 0, ColStatus = 1, ColStarted = 2, ColWalltime = 3 };

// RemixIcon stems + per-status tint for the Status column.
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

/// The word for a status. No longer shown in the column — it is the status
/// cell's TOOLTIP, so removing the text saved the width without costing the
/// meaning for anyone who does not already know the glyphs.
QString statusText(ProcessManagerPanel::Status status)
{
    switch (status) {
    case ProcessManagerPanel::Status::Queued:
        return ProcessManagerPanel::tr("queued");
    case ProcessManagerPanel::Status::Running:
        return ProcessManagerPanel::tr("running");
    case ProcessManagerPanel::Status::Completed:
        return ProcessManagerPanel::tr("completed");
    case ProcessManagerPanel::Status::Failed:
        return ProcessManagerPanel::tr("failed");
    }
    return {};
}

/// Elapsed time as a stopwatch reading.
///
/// Units grow with the magnitude rather than being fixed: a column wide enough
/// for "0d 00:00:07" on every row wastes the space this change set out to
/// reclaim, and a bare seconds count stops being readable within the first
/// minute of a job that will run for days.
QString formatWalltime(qint64 ms)
{
    const qint64 total = ms / 1000;
    const qint64 seconds = total % 60;
    const qint64 minutes = (total / 60) % 60;
    const qint64 hours = (total / 3600) % 24;
    const qint64 days = total / 86400;
    const auto pad = [](qint64 v) {
        return QStringLiteral("%1").arg(v, 2, 10, QLatin1Char('0'));
    };
    if (days > 0)
        return QStringLiteral("%1d %2:%3:%4")
            .arg(days).arg(pad(hours), pad(minutes), pad(seconds));
    if (hours > 0)
        return QStringLiteral("%1:%2:%3")
            .arg(hours).arg(pad(minutes), pad(seconds));
    return QStringLiteral("%1:%2").arg(minutes).arg(pad(seconds));
}

} // namespace

ProcessManagerPanel::ProcessManagerPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(4);
    tree_->setHeaderLabels(
        {tr("Task"), tr("Status"), tr("Started"), tr("Walltime")});
    // Tree-item icons enlarged 20% (default 16 → 17-ish; the status glyphs are
    // rendered at 17 px to match).
    tree_->setIconSize(QSize(17, 17));
    tree_->setRootIsDecorated(false);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(ColTask, QHeaderView::Stretch);
    // The status column now holds a glyph and nothing else, so it should be
    // exactly glyph-wide — that reclaimed width is the point of dropping the
    // text, and leaving the column sized for the word would give it away
    // again. The two time columns are fixed-width strings, so they can size
    // to their contents too and stop competing with the task name.
    for (const int column : {ColStatus, ColStarted, ColWalltime})
        tree_->header()->setSectionResizeMode(column,
                                              QHeaderView::ResizeToContents);
    // The dock is narrow and the task name is the one column worth reading in
    // full; the header text for the rest is what the tooltips explain.
    tree_->header()->setToolTip(
        tr("Walltime measures how long the run has been EXECUTING — the clock "
           "starts when a task leaves the queue, not when it was submitted."));
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
    // Molecular Dynamics, MLWF, GW, Born charges) have no button of their own:
    // they are on the right-click menu, which is built per process and so can
    // offer only the viewers that run's own output files support. A button
    // has to be present for every selection, viewable or not.
    auto* scriptButton = makeButton(
        QStringLiteral("code-box-fill"),
        tr("View ASE Script — show the exact Python/ASE run.py that was "
           "executed."));
    abortButton_ = makeButton(
        QStringLiteral("stop-circle-fill"),
        tr("Abort Process — stop the selected run, keeping everything it has "
           "written so far.\n\n"
           "A queued process is dropped from the queue; a running one is "
           "stopped and marked failed. Its folder, its log and any frames it "
           "already produced are left in place — use Delete Process to remove "
           "those as well."));
    abortButton_->setObjectName(QStringLiteral("abortProcessButton"));
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
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        if (const auto* item = tree_->currentItem())
            Q_EMIT deleteRequested(item->data(0, kIdRole).toInt());
    });
    connect(abortButton_, &QPushButton::clicked, this, [this] {
        if (const auto* item = tree_->currentItem())
            Q_EMIT abortRequested(item->data(0, kIdRole).toInt());
    });
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this] { updateAbortButton(); });
    updateAbortButton();
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                const QString dir = item->data(0, kDirRole).toString();
                if (!dir.isEmpty())
                    Q_EMIT loadResultRequested(dir);
            });

    // Right-click a process for everything that can be done WITH that process,
    // viewers included. The menu itself is built by the controller, which is
    // what knows how to tell a completed GW run from a completed MD one.
    // The live stopwatch. One second is the resolution a duration is read at;
    // faster would repaint the dock for digits nobody is watching change.
    // Created stopped — syncWalltimeTimer() starts it when something runs.
    walltimeTimer_ = new QTimer(this);
    walltimeTimer_->setInterval(1000);
    connect(walltimeTimer_, &QTimer::timeout, this,
            &ProcessManagerPanel::updateWalltimes);

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
    // The status cell carries NO text — see setTaskStatus. Walltime starts
    // empty rather than at 0:00: a queued task has not run for zero seconds,
    // it has not run.
    auto* item = new QTreeWidgetItem(
        {name, QString(),
         QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
         QString()});
    const auto queued = statusVisual(Status::Queued);
    // Status glyph enlarged 20% (14 → 17).
    item->setIcon(ColStatus,
                  ui::IconManager::icon(QLatin1String(queued.iconName),
                                        queued.color, 17));
    item->setToolTip(ColStatus, statusText(Status::Queued));
    // Durations read as a column of numbers, so they line up on the right.
    item->setTextAlignment(ColWalltime, Qt::AlignRight | Qt::AlignVCenter);
    item->setData(0, kIdRole, nextId_);
    item->setData(0, kDirRole, directory);
    item->setData(0, kStatusRole, static_cast<int>(Status::Queued));
    item->setData(0, kRunStartRole, static_cast<qint64>(0));
    item->setData(0, kRunEndRole, static_cast<qint64>(0));
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
    // Deleting the last running task leaves nothing to tick.
    syncWalltimeTimer();
}

void ProcessManagerPanel::setTaskStatus(int id, Status status)
{
    QTreeWidgetItem* item = itemForId(id);
    if (!item)
        return;
    const auto visual = statusVisual(status);
    const auto previous = static_cast<Status>(item->data(0, kStatusRole).toInt());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // -- The stopwatch's two edges -----------------------------------------
    // Entering Running starts the clock, but only on the TRANSITION: a
    // controller that re-asserts Running (a progress update, a reconnect to a
    // remote job) must not restart a duration that has been accumulating.
    if (status == Status::Running && previous != Status::Running)
        item->setData(0, kRunStartRole, now);
    // Leaving Running freezes it. A task that never ran — queued and then
    // aborted — keeps a zero start and shows no duration at all, which is the
    // honest answer rather than 0:00.
    if (status == Status::Completed || status == Status::Failed) {
        if (item->data(0, kRunStartRole).toLongLong() != 0
            && item->data(0, kRunEndRole).toLongLong() == 0)
            item->setData(0, kRunEndRole, now);
    } else {
        // Re-queued or restarted: the previous end no longer applies.
        item->setData(0, kRunEndRole, static_cast<qint64>(0));
    }

    item->setData(0, kStatusRole, static_cast<int>(status));
    // Icon ONLY. The word lives in the tooltip: the column is a fixed-width
    // glyph in a dock that is always short of horizontal space, and the status
    // of a run is exactly the kind of thing a colour-coded symbol conveys
    // faster than a word anyway.
    item->setIcon(ColStatus,
                  ui::IconManager::icon(QLatin1String(visual.iconName),
                                        visual.color, 17));
    item->setToolTip(ColStatus, statusText(status));
    refreshWalltime(item);
    syncWalltimeTimer();
    updateAbortButton();
}

int ProcessManagerPanel::taskCount() const
{
    return tree_->topLevelItemCount();
}

std::vector<ProcessManagerPanel::CompletedRun>
ProcessManagerPanel::completedRunsWith(const QString& fileName) const
{
    std::vector<CompletedRun> runs;
    // Walked backwards so the newest run is offered first: rows are appended
    // in registration order, and the run someone wants to load is almost
    // always the one that just finished.
    for (int row = tree_->topLevelItemCount() - 1; row >= 0; --row) {
        const QTreeWidgetItem* item = tree_->topLevelItem(row);
        if (static_cast<Status>(item->data(0, kStatusRole).toInt())
            != Status::Completed)
            continue;
        const QString directory = item->data(0, kDirRole).toString();
        if (directory.isEmpty())
            continue;
        if (!QFileInfo::exists(directory + QLatin1Char('/') + fileName))
            continue;
        runs.push_back({item->text(ColTask), directory});
    }
    return runs;
}

ProcessManagerPanel::Status ProcessManagerPanel::rowStatus(int row) const
{
    if (row < 0 || row >= tree_->topLevelItemCount())
        return Status::Queued;
    return static_cast<Status>(
        tree_->topLevelItem(row)->data(0, kStatusRole).toInt());
}

void ProcessManagerPanel::refreshWalltime(QTreeWidgetItem* item) const
{
    if (!item)
        return;
    const qint64 start = item->data(0, kRunStartRole).toLongLong();
    if (start == 0) {
        item->setText(ColWalltime, QString());
        return;
    }
    const qint64 end = item->data(0, kRunEndRole).toLongLong();
    const qint64 until = end != 0 ? end : QDateTime::currentMSecsSinceEpoch();
    item->setText(ColWalltime, formatWalltime(std::max<qint64>(0, until - start)));
}

void ProcessManagerPanel::updateWalltimes()
{
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree_->topLevelItem(i);
        // Only the running rows move. A finished row's duration is frozen and
        // rewriting it every second would be a repaint for an unchanged
        // string.
        if (static_cast<Status>(item->data(0, kStatusRole).toInt())
            == Status::Running)
            refreshWalltime(item);
    }
    syncWalltimeTimer();
}

void ProcessManagerPanel::syncWalltimeTimer()
{
    if (!walltimeTimer_)
        return;
    bool anyRunning = false;
    for (int i = 0; i < tree_->topLevelItemCount() && !anyRunning; ++i)
        anyRunning = static_cast<Status>(
                         tree_->topLevelItem(i)->data(0, kStatusRole).toInt())
            == Status::Running;
    if (anyRunning && !walltimeTimer_->isActive())
        walltimeTimer_->start();
    else if (!anyRunning && walltimeTimer_->isActive())
        walltimeTimer_->stop();
}

void ProcessManagerPanel::updateAbortButton()
{
    if (!abortButton_)
        return;
    const QTreeWidgetItem* item = tree_->currentItem();
    const auto status = item
        ? static_cast<Status>(item->data(0, kStatusRole).toInt())
        : Status::Completed;
    abortButton_->setEnabled(item != nullptr
                             && (status == Status::Running
                                 || status == Status::Queued));
}

} // namespace calango::gui
