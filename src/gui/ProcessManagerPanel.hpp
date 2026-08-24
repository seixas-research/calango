#pragma once

#include <QString>
#include <QWidget>

#include <vector>

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
    /// Attach a one-line explanation to a task's row, shown as its tooltip.
    ///
    /// Used for failure reasons: a status message scrolls away, and a red row
    /// with no explanation is the state this panel used to leave a failed run
    /// in. The tooltip persists for as long as the row does.
    void setTaskDetail(int id, const QString& detail);
    /// Remove a task's row (the controller purges its data first).
    void removeTask(int id);

    /// A finished task and the directory holding its output.
    struct CompletedRun {
        QString name;
        QString directory;
    };

    /// Completed tasks whose directory contains `fileName`, newest first.
    ///
    /// Filtered on the ARTIFACT rather than on the task's name, which is the
    /// only version of this question that stays true. A name is a translated
    /// label — the same trap `rowStatus()` exists to avoid — and the same file
    /// is produced by more than one route: the wizard, an Orchestration node,
    /// a batch fan-out. "Which finished runs left this file behind" is what
    /// every caller actually wants, and it survives all three.
    ///
    /// A run whose directory was purged drops out on its own, since the check
    /// is against the filesystem and not against what the row remembers.
    std::vector<CompletedRun> completedRunsWith(const QString& fileName) const;

    /// Status of the row at `row` in display order; Queued if out of range.
    ///
    /// Exists because the status column no longer HAS text to read. Anything
    /// that wants to know what a row is doing — a test, a controller
    /// reasoning about the panel rather than its own bookkeeping — asks for
    /// the value instead of matching a translated word, which is what the
    /// column was never a reliable source of anyway.
    Status rowStatus(int row) const;
    /// Status of the task registered under `id`, or Queued if no such task
    /// exists (removed, or never registered). The by-ID counterpart to
    /// rowStatus() — added (Task 3, 2026-08-22) so a baseline picker (an
    /// Electronic Structure/CDD/etc. wizard's "which completed run do I
    /// restart from") can filter OUT a parent that crashed or is still
    /// running instead of only checking whether it left a plausible-looking
    /// file behind, which a VASP CHGCAR (written incrementally through the
    /// SCF, not just at a successful end) does not guarantee at all.
    Status taskStatus(int id) const;

protected:
    /// Delete / Backspace (also ⌘⌫ on macOS) on the tree deletes the selected
    /// process — routed through deleteRequested(), which the controller
    /// confirms and then purges from `.calango_tmp/`.
    bool eventFilter(QObject* watched, QEvent* event) override;

Q_SIGNALS:
    /// "Load Result" on a task — MainWindow decides what the directory
    /// contains (trajectory, band data, ...) and opens it.
    void loadResultRequested(const QString& directory);
    /// A task was DOUBLE-CLICKED. Carries its id and name as well as its
    /// directory, because what a double-click should do depends on what kind
    /// of run it is: a GO/MCMD or GO/MC-Opt row opens that run's Summary
    /// window, every
    /// other row keeps the "load this run's result" behaviour double-click
    /// has always had (which is what this signal used to emit directly, as
    /// loadResultRequested).
    ///
    /// The panel does not decide — it reports the activation and the
    /// controller, which is what knows one run from another, decides.
    void taskActivated(int id, const QString& name, const QString& directory);
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
