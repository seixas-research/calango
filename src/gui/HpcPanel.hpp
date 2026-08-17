#pragma once

#include "core/SchedulerScript.hpp"
#include "gui/ClusterPreset.hpp"
#include "remote/RemoteClient.hpp"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QFormLayout;
class QSpinBox;

namespace calango::gui {

/// Zone-11 "HPC" dock: SSH connection to an HPC cluster,
/// scheduler configuration (SLURM / PBS / SGE), job submission over
/// SFTP + SSH, live queue state, remote log streaming, and result
/// download. Three compact tabs (Connection / Scheduler / Queue & Logs)
/// keep it usable at bottom-row height.
///
/// The connection is a SESSION, not a series of logins: RemoteClient keeps
/// one authenticated transport alive and everything below rides it. That is
/// what makes a cluster with two-factor authentication usable — the code is
/// typed once, into the dialog this panel raises, rather than once per
/// status poll.
///
/// The submission flow is driven by MainWindow: a simulation wizard's
/// Stage-4 "Run (Remote)" makes it stage run.py + structure.extxyz into a
/// fresh job directory (same staging as local jobs — run.py is self-contained,
/// so those two files are the whole job)
/// and hand it to submitStagedJob(). This panel supplies the scheduler
/// settings that submission is wrapped with, and monitors the result.
class HpcPanel : public QWidget {
    Q_OBJECT

public:
    explicit HpcPanel(const QString& pythonExe,
                               QWidget* parent = nullptr);

    /// Generate job.sh next to run.py, upload the directory's files to
    /// the cluster, submit, and start monitoring.
    void submitStagedJob(const QString& localJobDir, const QString& jobName);

Q_SIGNALS:
    /// Results were downloaded into `localDir` (after job completion).
    void resultsReady(const QString& localDir);

private Q_SLOTS:
    /// (Re)authenticate. This is the ONLY place a 2FA challenge is ever
    /// raised from — see onSessionStateChanged().
    void testConnection();
    void closeConnection();
    /// Put the server's challenge on screen and send back what is typed.
    void onAuthPromptRequested(const QString& name, const QString& instruction,
                               const QVector<remote::AuthPrompt>& prompts);
    void onSessionStateChanged(remote::RemoteClient::SessionState state,
                               const QString& detail);
    /// Save the current form under the name in the preset combo.
    void saveCurrentPreset();
    /// Forget the selected preset.
    void deleteCurrentPreset();
    /// Load the chosen preset into the form.
    void applySelectedPreset();
    void onProbeFinished(bool ok, const QString& message, const QString& scheduler);
    void onSubmitFinished(bool ok, const QString& jobId, const QString& message);
    void onMonitorFinished(const QString& finalState);
    void downloadResults();
    void cancelRemoteJob();

private:
    remote::SshConfig configFromUi() const;
    /// The form as a preset (minus the password, which is never persisted).
    ClusterPreset presetFromUi(const QString& name) const;
    void applyPreset(const ClusterPreset& preset);
    /// Rebuild the combo from `presets_`, selecting `name` when given.
    void refreshPresetCombo(const QString& select = QString());
    /// Show the SGE parallel-environment row only for SGE.
    void updateSchedulerRows();
    core::RemoteJobSpec specFromUi(const QString& jobName) const;
    core::Scheduler scheduler() const;
    void appendLog(const QString& text, bool isError = false);
    /// Set the terse status line. `detail`, when given, becomes the label's
    /// tooltip (the full text `text` is shortened from) — empty falls back
    /// to `text` itself, so an ordinary short status still has a tooltip.
    void setStatus(const QString& text, bool ok = true,
                   const QString& detail = QString());
    /// A short, human-readable cause for a connection failure — "the raw
    /// SSH/library error goes in setStatus()'s detail (tooltip) and the log,
    /// never on the status line itself, which must stay short enough that it
    /// cannot widen the dock.
    QString shortConnectionError(const QString& raw,
                                 remote::RemoteClient::ErrorKind kind) const;
    /// Reflect connectionState_/connectionBusy_ onto the single Connect /
    /// Disconnect toggle button.
    void updateConnectionButton();
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
    /// Single Connect/Disconnect toggle — label and enabled state are driven
    /// by connectionState_/connectionBusy_ via updateConnectionButton().
    QPushButton* connectionButton_;
    remote::RemoteClient::SessionState connectionState_ =
        remote::RemoteClient::SessionState::Disconnected;
    bool connectionBusy_ = false;
    QLabel* statusLabel_;

    // Cluster presets
    QComboBox* presetCombo_ = nullptr;
    QPushButton* presetSaveButton_ = nullptr;
    QPushButton* presetDeleteButton_ = nullptr;
    QVector<ClusterPreset> presets_;

    // Scheduler tab
    QComboBox* schedulerCombo_;
    QLineEdit* queueEdit_;
    QSpinBox* nodesSpin_ = nullptr;
    QSpinBox* tasksSpin_;
    QSpinBox* memorySpin_ = nullptr;
    QLineEdit* peEdit_ = nullptr;
    QFormLayout* schedulerForm_ = nullptr;
    int peRow_ = -1;
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
