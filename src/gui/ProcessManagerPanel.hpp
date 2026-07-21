#pragma once

#include <QWidget>

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
    void setTaskStatus(int id, Status status);

Q_SIGNALS:
    /// "Load Result" on a task — MainWindow decides what the directory
    /// contains (trajectory, band data, ...) and opens it.
    void loadResultRequested(const QString& directory);

private:
    QTreeWidgetItem* itemForId(int id) const;

    QTreeWidget* tree_;
    int nextId_ = 0;
};

} // namespace calango::gui
