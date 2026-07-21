#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace calango::remote {

/// Connection parameters for one HPC cluster. The password (or key
/// passphrase) lives only in memory and is handed to the helper process
/// over stdin — it is never persisted or put on a command line.
struct SshConfig {
    enum class Auth { Key, Password };

    QString host;
    int port = 22;
    QString username;
    Auth auth = Auth::Key;
    QString keyPath;  ///< "" = let SSH agent / default keys authenticate
    QString password; ///< password, or key passphrase in Key mode
    QString remoteDir = QStringLiteral("calango_jobs");
};

/// Asynchronous SSH/SFTP operations against one cluster, implemented by
/// spawning the bundled paramiko helper (assets/remote/calango_remote.py)
/// through the embedded interpreter's Python. Each operation is its own
/// short-lived QProcess emitting JSON events; `monitor` is a long-lived
/// process that polls the queue and tails the job's stdout/stderr.
///
/// Out-of-process SSH mirrors the JobRunner philosophy: the GUI thread
/// never blocks on the network, and a wedged connection is always
/// recoverable by killing the helper.
class RemoteClient : public QObject {
    Q_OBJECT

public:
    explicit RemoteClient(QString pythonExe, QObject* parent = nullptr);

    void setConfig(const SshConfig& config) { config_ = config; }
    const SshConfig& config() const { return config_; }

    /// True while a one-shot operation (probe/upload/submit/download) runs.
    bool isBusy() const;
    bool isMonitoring() const;

public Q_SLOTS:
    /// Verify the connection; reports $HOME and the detected scheduler.
    void probe();
    /// Create `remoteDir` (mkdir -p) and upload the given local files.
    void upload(const QString& remoteDir, const QStringList& localFiles);
    /// Run `command` inside `remoteDir` (e.g. "sbatch job.sh").
    void submit(const QString& remoteDir, const QString& command);
    /// Poll job state and stream calango_job.out/.err until the job
    /// leaves the queue.
    void startMonitor(const QString& remoteDir, const QString& schedulerKey,
                      const QString& jobId, int pollSeconds = 10);
    void stopMonitor();
    /// Fetch files matching `patterns` from remoteDir into localDir.
    void download(const QString& remoteDir, const QString& localDir,
                  const QStringList& patterns);
    /// Abort the current one-shot operation, if any.
    void abort();

Q_SIGNALS:
    void busyChanged(bool busy);

    void probeFinished(bool ok, const QString& message, const QString& scheduler);
    void fileUploaded(const QString& name);
    void uploadFinished(bool ok, const QString& error);
    void submitFinished(bool ok, const QString& jobId, const QString& message);

    void jobStateChanged(const QString& state);
    /// Incremental remote log text; `stream` is "out" or "err".
    void remoteLog(const QString& stream, const QString& text);
    /// The job left the queue (finalState is the last observed state).
    void monitorFinished(const QString& finalState);

    void fileDownloaded(const QString& name);
    void downloadFinished(bool ok, const QString& localDir,
                          const QStringList& files, const QString& error);

private:
    enum class Op { None, Probe, Upload, Submit, Download };

    /// Write the helper to disk (app-data) and return its path.
    QString ensureHelperScript();
    QProcess* spawn(const QString& op, const QJsonObject& args);
    void startOp(Op op, const QString& opName, const QJsonObject& args);
    void handleLine(Op op, const QJsonObject& event);
    void finishOp(Op op, const QJsonObject& result);

    QString pythonExe_;
    QString helperPath_;
    SshConfig config_;

    QProcess* opProcess_ = nullptr;
    Op currentOp_ = Op::None;
    QString lineBuffer_;

    QProcess* monitorProcess_ = nullptr;
    QString monitorBuffer_;
    QString lastState_;

    QString downloadDir_;
    QStringList downloadedFiles_;
};

} // namespace calango::remote
