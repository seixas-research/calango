#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QTableWidget;

namespace calango::gui {

/// "MDMC Summary" — a GO-MDMC run's counters, in a window of their own.
///
/// This used to be a tab in the Results dock, which is the wrong shape for it
/// twice over: the dock is short (zone 10) and shared with the Log and the
/// four metric plots, so the five run counters and the per-move-kind
/// breakdown were always competing for a strip of it; and the numbers are
/// what a user wants to keep visible WHILE watching the run in the viewport,
/// which a tab in a dock cannot do. The *Energy* and *Acceptance* tabs stay
/// where they are — they are plots, and a plot is what that dock is for.
///
/// TWO WAYS IN, both through MainWindow:
///
///   * the run FINISHING (successfully — a failed or aborted run does not pop
///     a window, because the job monitor already reports the failure and the
///     counters of a run that died are not the thing to put in front of
///     someone);
///   * DOUBLE-CLICKING the process in the Processes panel, during the run or
///     long after it.
///
/// MODELESS AND SINGLE-INSTANCE. The counters update underneath an open
/// window while its run is live: MainWindow repaints it from the same
/// metrics.json poll that drives the Results plots. It is bound to ONE
/// process (`processId()`), so a poll for a different run leaves it alone,
/// and re-activating another process rebinds it rather than opening a second
/// window.
///
/// Every number shown is computed by MainWindow, which owns the process
/// record; this class owns the presentation and the CSV export, and holds the
/// two tables that the numbers are written into.
class MdmcSummaryDialog : public QDialog {
    Q_OBJECT

public:
    explicit MdmcSummaryDialog(QWidget* parent = nullptr);

    /// Bind the window to a process: its id, the label its row carries, and
    /// whether that run is still going (which only changes the subtitle).
    void bindProcess(int id, const QString& label, bool running);
    int processId() const { return processId_; }
    void setRunning(bool running);

    /// The five whole-run counters — cycles done/total, MD steps, accepted,
    /// acceptance %, elapsed. Two columns, written by
    /// MainWindow::updateMdmcRunSummary().
    QTableWidget* runSummaryTable() const { return runSummaryTable_; }
    /// Attempts / accepted / ratio per move kind, written by
    /// MainWindow::updateMdmcSummaryTable().
    QTableWidget* moveBreakdownTable() const { return moveBreakdownTable_; }

private Q_SLOTS:
    /// Both blocks, run first, separated by a blank line — byte for byte the
    /// file the Results tab's "Export CSV…" wrote, so anything that already
    /// parses it still finds what it was reading.
    void exportCsv();

private:
    void refreshSubtitle();

    int processId_ = -1;
    QString processLabel_;
    bool running_ = false;

    QLabel* subtitle_ = nullptr;
    QTableWidget* runSummaryTable_ = nullptr;
    QTableWidget* moveBreakdownTable_ = nullptr;
};

} // namespace calango::gui
