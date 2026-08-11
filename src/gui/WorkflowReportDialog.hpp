#pragma once

#include "core/WorkflowReport.hpp"

#include <QDialog>

class QTreeWidget;

namespace calango::gui {

/// "Workflow Execution Report": what an orchestration run did, shown when it
/// ends.
///
/// A pipeline can be an afternoon of computing across a dozen structures, and
/// until this existed the only account of it was the canvas — which by the end
/// of a batch shows the LAST pass's statuses and nothing about the eleven
/// before it — plus a Processes list of rows to scroll. The one question
/// everybody asks first ("did it work, and what came out?") had no answer
/// anywhere.
///
/// Reads a core::WorkflowReport and nothing else. It cannot reach the canvas,
/// the nodes or the job runner, which is what lets the same report be rendered
/// from a file written by an earlier run or by the headless CLI — the dialog
/// is a view, not a second source of the truth.
class WorkflowReportDialog : public QDialog {
    Q_OBJECT

public:
    explicit WorkflowReportDialog(const core::WorkflowReport& report,
                                  QWidget* parent = nullptr);

Q_SIGNALS:
    /// The user asked to open a node's job directory.
    void openDirectoryRequested(const QString& directory);

private:
    void populate(const core::WorkflowReport& report);

    QTreeWidget* tree_ = nullptr;
};

} // namespace calango::gui
