#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

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

/// One line of a keyboard-interactive challenge, as the server worded it.
/// `echo` false means the answer must not be shown while typed — every
/// one-time code arrives that way.
struct AuthPrompt {
    QString text;
    bool echo = false;
};

/// A PERSISTENT SSH session against one cluster: one long-lived helper
/// process (assets/remote/calango_remote.py, run by the embedded
/// interpreter) that authenticates once and then multiplexes every
/// operation — upload, submit, status poll, log tail, download — over the
/// same transport.
///
/// It used to spawn a fresh process, and therefore a fresh SSH connection,
/// per operation. That is merely wasteful with a key; with keyboard-
/// interactive two-factor authentication it is unusable, because a job whose
/// queue state is polled every 30 s would demand a new one-time code every
/// 30 s. Multiplexing is the whole feature: the human authenticates once.
///
/// Out-of-process SSH still mirrors the JobRunner philosophy: the GUI thread
/// never blocks on the network, and a wedged connection is always
/// recoverable by killing the helper.
class RemoteClient : public QObject {
    Q_OBJECT

public:
    /// Where the session is, as far as this side knows.
    enum class SessionState {
        Disconnected, ///< no helper, or it was told to hang up
        Connecting,   ///< TCP + SSH handshake
        Authenticating, ///< credentials/2FA in flight — a prompt may be open
        Connected,
        Reconnecting, ///< dropped, and the helper is silently rebuilding it
        NeedsReauth,  ///< dropped, and only the human can restore it
    };
    Q_ENUM(SessionState)

    /// Why something failed. The GUI must not treat these alike: a rejected
    /// password is the user's to fix, a dropped link is not, and a changed
    /// host key is a security event that must never be retried past.
    enum class ErrorKind {
        None, Auth, AuthRequired, HostKey, Network, Remote, Internal
    };
    Q_ENUM(ErrorKind)

    explicit RemoteClient(QString pythonExe, QObject* parent = nullptr);
    ~RemoteClient() override;

    /// Point at a cluster. Changing WHERE we connect closes an open session,
    /// so the next operation logs into the cluster now on the form.
    void setConfig(const SshConfig& config);
    const SshConfig& config() const { return config_; }

    bool isConnected() const { return state_ == SessionState::Connected; }
    /// True once a human answered a challenge for this session — the flag
    /// that forbids silent reconnection.
    bool usedInteractiveAuth() const { return interactiveUsed_; }

    /// Run a helper from `path` instead of the copy unpacked from Qt
    /// resources. Only the test uses this, so it can drive the file in the
    /// source tree against a local SSH server; leaving it empty is the
    /// shipping behaviour.
    void setHelperScriptPath(const QString& path) { helperOverride_ = path; }

public Q_SLOTS:
    /// Authenticate (prompting if the server asks). Any operation issued
    /// while disconnected does this by itself and runs once it succeeds.
    void connectToHost();
    /// Hang up. Cancels everything in flight, including the monitor.
    void disconnectFromHost();
    /// Answer the challenge announced by authPromptRequested().
    void answerAuthPrompt(const QStringList& responses);
    /// Give up on the open challenge; the connection attempt then fails.
    void cancelAuthPrompt();

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
    /// Abort the one-shot operations in flight, if any.
    void abort();

Q_SIGNALS:
    void busyChanged(bool busy);

    void sessionStateChanged(SessionState state, const QString& detail);
    /// The server is asking something only the user can answer. Exactly one
    /// of these is ever open at a time.
    void authPromptRequested(const QString& name, const QString& instruction,
                             const QVector<AuthPrompt>& prompts);
    void connectFinished(bool ok, const QString& error, ErrorKind kind);

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
    enum class Op { None, Connect, Probe, Upload, Submit, Monitor, Download };

    struct Request {
        Op op = Op::None;
        QString name;
        QJsonObject args;
    };

    /// Write the helper to disk (app-data) and return its path.
    QString ensureHelperScript();
    /// Start the helper if it is not already running. False = could not.
    bool ensureProcess();
    /// Send one request line; returns the id it was given (-1 on failure).
    int send(const Request& request);
    /// Issue now if connected, otherwise queue it and start connecting.
    int startOp(Op op, const QString& name, const QJsonObject& args);
    void flushQueue();
    void failQueued(const QString& error, ErrorKind kind);

    void readEvents();
    void handleEvent(const QJsonObject& event);
    void handleSessionEvent(const QJsonObject& event);
    void handleResult(int id, const QJsonObject& result);
    void deliverResult(Op op, int id, const QJsonObject& result);
    void setState(SessionState state, const QString& detail = QString());
    void updateBusy();
    /// Everything in flight dies with the helper.
    void onProcessGone(const QString& reason);

    static ErrorKind kindFromString(const QString& text);

    QString pythonExe_;
    QString helperPath_;
    QString helperOverride_;
    SshConfig config_;

    QProcess* process_ = nullptr;
    QString buffer_;
    int nextId_ = 1;

    QHash<int, Op> inflight_;
    QVector<Request> queued_; ///< issued while disconnected, run after connect
    int connectId_ = -1;
    int monitorId_ = -1;
    int promptId_ = 0;        ///< challenge currently open (0 = none)

    SessionState state_ = SessionState::Disconnected;
    bool interactiveUsed_ = false;
    bool busy_ = false;

    QHash<int, QString> downloadDirs_;
    QHash<int, QStringList> downloadedFiles_;
};

} // namespace calango::remote
