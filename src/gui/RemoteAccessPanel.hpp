#pragma once

#include "core/SchedulerScript.hpp"
#include "remote/RemoteClient.hpp"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace calango::gui {

/// Zone-11 "Remote Access" dock: SSH connection to an HPC cluster,
/// scheduler configuration (SLURM / PBS / SGE), job submission over
/// SFTP + SSH, live queue state, remote log streaming, and result
/// download. Three compact tabs (Connection / Scheduler / Queue & Logs)
/// keep it usable at bottom-row height.
///
/// The submission flow is driven by MainWindow: a simulation wizard's
/// Stage-4 "Run (Remote)" makes it stage run.py + structure.extxyz into a
/// fresh job directory (same staging as local jobs — run.py is self-contained,
/// so those two files are the whole job)
/// and hand it to submitStagedJob(). This panel supplies the scheduler
/// settings that submission is wrapped with, and monitors the result.
class RemoteAccessPanel : public QWidget {
    Q_OBJECT

public:
    explicit RemoteAccessPanel(const QString& pythonExe,
                               QWidget* parent = nullptr);

    /// Generate job.sh next to run.py, upload the directory's files to
    /// the cluster, submit, and start monitoring.
    void submitStagedJob(const QString& localJobDir, const QString& jobName);

Q_SIGNALS:
    /// Results were downloaded into `localDir` (after job completion).
    void resultsReady(const QString& localDir);

private Q_SLOTS:
    void testConnection();
    void onProbeFinished(bool ok, const QString& message, const QString& scheduler);
    void onSubmitFinished(bool ok, const QString& jobId, const QString& message);
    void onMonitorFinished(const QString& finalState);
    void downloadResults();
    void cancelRemoteJob();

private:
    remote::SshConfig configFromUi() const;
    core::RemoteJobSpec specFromUi(const QString& jobName) const;
    core::Scheduler scheduler() const;
    void appendLog(const QString& text, bool isError = false);
    void setStatus(const QString& text, bool ok = true);
    void saveSettings() const;
    void restoreSettings();

    remote::RemoteClient* client_;

    // Connection tab
    QLineEdit* hostEdit_;
    QSpinBox* portSpin_;
    QLineEdit* userEdit_;
    QComboBox* authCombo_;
    QLineEdit* keyPathEdit_;
    QPushButton* keyBrowseButton_;
    QLineEdit* passwordEdit_;
    QLineEdit* remoteDirEdit_;
    QPushButton* testButton_;
    QLabel* statusLabel_;

    // Scheduler tab
    QComboBox* schedulerCombo_;
    QLineEdit* queueEdit_;
    QSpinBox* tasksSpin_;
    QLineEdit* walltimeEdit_;
    QPlainTextEdit* setupEdit_;

    // Queue & Logs tab
    QLabel* jobLabel_;
    QLabel* stateLabel_;
    QPlainTextEdit* logView_;
    QPushButton* cancelButton_;
    QPushButton* downloadButton_;

    QString localJobDir_;  ///< staging dir of the active remote job
    QString remoteJobDir_; ///< its counterpart on the cluster
    QString jobId_;
    bool cancelPending_ = false;
};

} // namespace calango::gui
