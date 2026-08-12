#pragma once

#include <QWidget>

class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace calango::gui {

/// Compact "Processes" dock (between the branding and Structure panels):
/// every background task — local jobs, remote submissions, band/PDOS
/// runs — is registered here with its live status and its session
/// working directory under the project's `.calango_tmp/` store, so
/// results remain one click away for post-processing after (or during)
/// the run: open the folder, or load the task's trajectory/plots back
/// into the workspace without recomputing anything.
class ProcessManagerPanel : public QWidget {
    Q_OBJECT

public:
    enum class Status { Queued, Running, Completed, Failed };

    explicit ProcessManagerPanel(QWidget* parent = nullptr);

    /// Register a task; returns its id. `directory` is the task's
    /// working/staging directory ("" for tasks without one).
    int registerTask(const QString& name, const QString& directory);
    /// Update a task's working directory after it has been staged.
    void setTaskDirectory(int id, const QString& directory);
    void setTaskStatus(int id, Status status);
    /// Remove a task's row (the controller purges its data first).
    void removeTask(int id);

    /// How many task rows the panel is showing.
    int taskCount() const;
    /// Status of the row at `row` in display order; Queued if out of range.
    ///
    /// Exists because the status column no longer HAS text to read. Anything
    /// that wants to know what a row is doing — a test, a controller
    /// reasoning about the panel rather than its own bookkeeping — asks for
    /// the value instead of matching a translated word, which is what the
    /// column was never a reliable source of anyway.
    Status rowStatus(int row) const;

protected:
    /// Delete / Backspace (also ⌘⌫ on macOS) on the tree deletes the selected
    /// process — routed through deleteRequested(), which the controller
    /// confirms and then purges from `.calango_tmp/`.
    bool eventFilter(QObject* watched, QEvent* event) override;

Q_SIGNALS:
    /// "Load Result" on a task — MainWindow decides what the directory
    /// contains (trajectory, band data, ...) and opens it.
    void loadResultRequested(const QString& directory);
    /// Right-click on a task. MainWindow builds the menu, since what belongs
    /// in it depends on which result files that run left behind.
    void contextMenuRequested(const QString& directory, const QPoint& globalPos);
    /// "View ASE Script" — MainWindow opens the run.py in that directory in a
    /// syntax-highlighted viewer.
    void viewScriptRequested(const QString& directory);
    /// "Delete Process" on a task — MainWindow confirms, stops it if running,
    /// purges its proc_<id> directory, then calls removeTask(id).
    void deleteRequested(int id);
    /// "Abort Process" on a task that is running or queued.
    ///
    /// Distinct from deleteRequested, and the difference is the whole point:
    /// Delete stops the run AND destroys its directory, so it is unusable for
    /// the ordinary case of a job that is clearly heading nowhere but whose
    /// partial output — the frames so far, the log, the input that provoked
    /// it — is exactly what you want to look at next. Abort stops it and
    /// leaves every byte on disk.
    void abortRequested(int id);

private:
    QTreeWidgetItem* itemForId(int id) const;
    /// Enable Abort for exactly the selections it applies to: a task that is
    /// running or still queued. A finished one has nothing to stop.
    void updateAbortButton();
    /// Repaint the Walltime cell of one row from its stored timestamps.
    void refreshWalltime(QTreeWidgetItem* item) const;
    /// Tick every running row, and stop the timer once none are left.
    ///
    /// Started and stopped rather than left running: a dock that wakes the UI
    /// thread once a second forever is a real cost on a laptop, and there is
    /// nothing to recompute when every task has finished — a frozen duration
    /// stays frozen.
    void updateWalltimes();
    /// Start the tick if anything is running, stop it if nothing is.
    void syncWalltimeTimer();

    QTreeWidget* tree_;
    QPushButton* abortButton_ = nullptr;
    /// Drives the live stopwatch; only active while a task is running.
    QTimer* walltimeTimer_ = nullptr;
    int nextId_ = 0;
};

} // namespace calango::gui
