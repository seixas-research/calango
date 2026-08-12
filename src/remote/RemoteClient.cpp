#include "remote/RemoteClient.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

namespace calango::remote {

namespace {

QJsonObject configJson(const SshConfig& config)
{
    return {
        {QStringLiteral("host"), config.host},
        {QStringLiteral("port"), config.port},
        {QStringLiteral("username"), config.username},
        {QStringLiteral("auth"),
         config.auth == SshConfig::Auth::Password ? QStringLiteral("password")
                                                  : QStringLiteral("key")},
        {QStringLiteral("key_path"), config.keyPath},
        {QStringLiteral("password"), config.password},
    };
}

} // namespace

RemoteClient::RemoteClient(QString pythonExe, QObject* parent)
    : QObject(parent)
    , pythonExe_(std::move(pythonExe))
{
}

RemoteClient::~RemoteClient()
{
    if (!process_)
        return;
    // Ask before killing: a clean shutdown lets the helper close the
    // transport, which spares the cluster a pile of half-open sessions when
    // Calango is quit and reopened all day.
    process_->disconnect(this);
    if (process_->state() == QProcess::Running) {
        process_->write("{\"id\":0,\"op\":\"shutdown\"}\n");
        process_->closeWriteChannel();
        if (!process_->waitForFinished(1500))
            process_->kill();
    }
}

void RemoteClient::setConfig(const SshConfig& config)
{
    // A live session outlives this call, so an endpoint change has to end it:
    // otherwise a user who edits the host in the form and immediately submits
    // uploads to the PREVIOUS cluster — silently, and successfully, which is
    // the worst way for it to be wrong. The password is excluded on purpose;
    // it cannot change an already-authenticated session.
    const bool sameEndpoint = config.host == config_.host
        && config.port == config_.port
        && config.username == config_.username
        && config.auth == config_.auth
        && config.keyPath == config_.keyPath;
    config_ = config;
    if (!sameEndpoint && state_ != SessionState::Disconnected)
        disconnectFromHost();
}

RemoteClient::ErrorKind RemoteClient::kindFromString(const QString& text)
{
    if (text == QLatin1String("auth"))
        return ErrorKind::Auth;
    if (text == QLatin1String("auth_required"))
        return ErrorKind::AuthRequired;
    if (text == QLatin1String("hostkey"))
        return ErrorKind::HostKey;
    if (text == QLatin1String("network"))
        return ErrorKind::Network;
    if (text == QLatin1String("remote"))
        return ErrorKind::Remote;
    if (text == QLatin1String("internal"))
        return ErrorKind::Internal;
    return ErrorKind::None;
}

QString RemoteClient::ensureHelperScript()
{
    if (!helperOverride_.isEmpty())
        return helperOverride_;
    if (!helperPath_.isEmpty() && QFile::exists(helperPath_))
        return helperPath_;

    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/remote");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/calango_remote.py");

    // Rewrite on every launch so the on-disk copy always matches the
    // helper this build ships in its resources.
    QFile resource(QStringLiteral(":/assets/remote/calango_remote.py"));
    resource.open(QIODevice::ReadOnly);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    out.write(resource.readAll());
    out.close();

    helperPath_ = path;
    return helperPath_;
}

bool RemoteClient::ensureProcess()
{
    if (process_ && process_->state() != QProcess::NotRunning)
        return true;
    if (process_) {
        process_->deleteLater();
        process_ = nullptr;
    }

    const QString helper = ensureHelperScript();
    if (helper.isEmpty())
        return false;

    auto* process = new QProcess(this);
    // -u: unbuffered, so JSON events stream in real time.
    process->start(pythonExe_, {QStringLiteral("-u"), helper});
    if (!process->waitForStarted(5000)) {
        process->deleteLater();
        return false;
    }
    process_ = process;
    buffer_.clear();

    connect(process, &QProcess::readyReadStandardOutput,
            this, &RemoteClient::readEvents);
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
                if (process_ != process)
                    return;
                const QString stderrText =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                onProcessGone(
                    status == QProcess::CrashExit
                        ? QStringLiteral("the SSH session helper crashed")
                        : QStringLiteral("the SSH session helper exited "
                                         "(code %1)%2")
                              .arg(exitCode)
                              .arg(stderrText.isEmpty()
                                       ? QString()
                                       : QStringLiteral(": ") + stderrText));
            });
    return true;
}

void RemoteClient::onProcessGone(const QString& reason)
{
    QProcess* dead = process_;
    process_ = nullptr;
    if (dead)
        dead->deleteLater();

    // The session died with it, so every id that was still open has to be
    // answered — a wizard waiting on uploadFinished() would otherwise sit
    // there forever with no error to show.
    const QHash<int, Op> pending = inflight_;
    inflight_.clear();
    if (connectId_ >= 0)
        Q_EMIT connectFinished(false, reason, ErrorKind::Internal);
    connectId_ = -1;
    monitorId_ = -1;
    promptId_ = 0;
    const QJsonObject failure{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), reason},
        {QStringLiteral("kind"), QStringLiteral("internal")}};
    for (auto it = pending.begin(); it != pending.end(); ++it)
        deliverResult(it.value(), it.key(), failure);
    failQueued(reason, ErrorKind::Internal);
    // A crashed helper took the authenticated transport with it. Whether it
    // may be rebuilt silently is the same question as for a dropped TCP
    // connection, and it has the same answer.
    setState(interactiveUsed_ ? SessionState::NeedsReauth
                              : SessionState::Disconnected,
             reason);
    updateBusy();
}

int RemoteClient::send(const Request& request)
{
    if (!ensureProcess())
        return -1;
    const int id = nextId_++;
    QJsonObject payload{{QStringLiteral("id"), id},
                        {QStringLiteral("op"), request.name}};
    if (request.op == Op::Connect)
        payload.insert(QStringLiteral("config"), configJson(config_));
    else
        payload.insert(QStringLiteral("args"), request.args);
    process_->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    process_->write("\n");
    inflight_.insert(id, request.op);
    if (request.op == Op::Download) {
        // The destination is bound to the id HERE rather than in download(),
        // because an operation issued while disconnected has no id until the
        // login finishes and flushQueue() finally sends it.
        downloadDirs_.insert(
            id, request.args.value(QStringLiteral("local_dir")).toString());
        downloadedFiles_.insert(id, {});
    }
    return id;
}

void RemoteClient::connectToHost()
{
    if (state_ == SessionState::Connecting
        || state_ == SessionState::Authenticating)
        return;
    interactiveUsed_ = false;
    setState(SessionState::Connecting, config_.host);
    const int id = send({Op::Connect, QStringLiteral("connect"), {}});
    if (id < 0) {
        const QString error =
            QStringLiteral("could not start the SSH helper (python: %1)")
                .arg(pythonExe_);
        setState(SessionState::Disconnected, error);
        Q_EMIT connectFinished(false, error, ErrorKind::Internal);
        failQueued(error, ErrorKind::Internal);
        updateBusy();
        return;
    }
    connectId_ = id;
    updateBusy();
}

void RemoteClient::disconnectFromHost()
{
    queued_.clear();
    if (!process_ || process_->state() == QProcess::NotRunning) {
        monitorId_ = -1;
        setState(SessionState::Disconnected);
        return;
    }
    // Forget the monitor BEFORE hanging up. The helper cancels every open
    // operation on disconnect, and the monitor's cancellation result is
    // indistinguishable at this end from a job that finished — which made
    // pressing Connect during a run download a half-finished directory as
    // though the job were done.
    stopMonitor();
    send({Op::None, QStringLiteral("disconnect"), {}});
    interactiveUsed_ = false;
    setState(SessionState::Disconnected);
}

void RemoteClient::answerAuthPrompt(const QStringList& responses)
{
    if (!process_ || promptId_ == 0)
        return;
    QJsonArray array;
    for (const QString& response : responses)
        array.append(response);
    const QJsonObject payload{
        {QStringLiteral("id"), 0},
        {QStringLiteral("op"), QStringLiteral("auth_response")},
        {QStringLiteral("prompt_id"), promptId_},
        {QStringLiteral("responses"), array}};
    process_->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    process_->write("\n");
    // Forgotten immediately and deliberately: the answer to a challenge is
    // single-use, so the only place it should ever exist is the wire.
    promptId_ = 0;
}

void RemoteClient::cancelAuthPrompt()
{
    if (!process_ || promptId_ == 0)
        return;
    const QJsonObject payload{
        {QStringLiteral("id"), 0},
        {QStringLiteral("op"), QStringLiteral("auth_cancel")},
        {QStringLiteral("prompt_id"), promptId_}};
    process_->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    process_->write("\n");
    promptId_ = 0;
}

int RemoteClient::startOp(Op op, const QString& name, const QJsonObject& args)
{
    if (state_ == SessionState::Connected)
        return send({op, name, args});

    // Not connected: remember what was asked for and authenticate. This is
    // what lets the panel keep calling upload() straight after setConfig()
    // the way it always did, without every caller learning to connect first.
    //
    // It also means an operation issued from NeedsReauth will challenge the
    // user again — which is correct, because every slot here except the
    // monitor is a button the user just pressed. The monitor is the one
    // background caller and it never re-arms itself: when the session dies
    // under it, it reports MONITOR-FAILED once and stops. That asymmetry is
    // the invariant "no prompt the user did not ask for" rests on.
    queued_.append({op, name, args});
    if (state_ != SessionState::Connecting
        && state_ != SessionState::Authenticating
        && state_ != SessionState::Reconnecting)
        connectToHost();
    updateBusy();
    return -1;
}

void RemoteClient::flushQueue()
{
    const QVector<Request> pending = queued_;
    queued_.clear();
    for (const Request& request : pending) {
        const int id = send(request);
        if (request.op == Op::Monitor)
            monitorId_ = id;
    }
    updateBusy();
}

void RemoteClient::failQueued(const QString& error, ErrorKind kind)
{
    const QVector<Request> pending = queued_;
    queued_.clear();
    const QJsonObject failure{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), error},
        {QStringLiteral("kind"), static_cast<int>(kind)}};
    for (const Request& request : pending)
        deliverResult(request.op, -1, failure);
}

void RemoteClient::readEvents()
{
    if (!process_)
        return;
    buffer_ += QString::fromUtf8(process_->readAllStandardOutput());
    int newline = -1;
    while ((newline = buffer_.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = buffer_.left(newline).trimmed();
        buffer_.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        const QJsonObject event = QJsonDocument::fromJson(line.toUtf8()).object();
        if (!event.isEmpty())
            handleEvent(event);
    }
}

void RemoteClient::handleEvent(const QJsonObject& event)
{
    const QString kind = event.value(QStringLiteral("event")).toString();
    const int id = event.value(QStringLiteral("id")).toInt();

    if (kind == QLatin1String("hello"))
        return;
    if (kind == QLatin1String("session")) {
        handleSessionEvent(event);
        return;
    }
    if (kind == QLatin1String("auth_prompt")) {
        promptId_ = event.value(QStringLiteral("prompt_id")).toInt();
        QVector<AuthPrompt> prompts;
        const QJsonArray array =
            event.value(QStringLiteral("prompts")).toArray();
        for (const QJsonValue& value : array) {
            const QJsonObject object = value.toObject();
            prompts.append({object.value(QStringLiteral("prompt")).toString(),
                            object.value(QStringLiteral("echo")).toBool()});
        }
        Q_EMIT authPromptRequested(
            event.value(QStringLiteral("name")).toString(),
            event.value(QStringLiteral("instruction")).toString(), prompts);
        return;
    }
    if (kind == QLatin1String("result")) {
        handleResult(id, event);
        return;
    }

    // Progress events, routed by the operation that owns the id.
    const Op op = inflight_.value(id, Op::None);
    if (op == Op::Upload && kind == QLatin1String("uploaded")) {
        Q_EMIT fileUploaded(event.value(QStringLiteral("file")).toString());
    } else if (op == Op::Download && kind == QLatin1String("downloaded")) {
        const QString name = event.value(QStringLiteral("file")).toString();
        downloadedFiles_[id] << name;
        Q_EMIT fileDownloaded(name);
    } else if (op == Op::Monitor) {
        if (kind == QLatin1String("state"))
            Q_EMIT jobStateChanged(event.value(QStringLiteral("state")).toString());
        else if (kind == QLatin1String("log"))
            Q_EMIT remoteLog(event.value(QStringLiteral("stream")).toString(),
                             event.value(QStringLiteral("text")).toString());
    }
}

void RemoteClient::handleSessionEvent(const QJsonObject& event)
{
    const QString state = event.value(QStringLiteral("state")).toString();
    const QString detail = event.value(QStringLiteral("reason")).toString();
    if (state == QLatin1String("connecting"))
        setState(SessionState::Connecting, config_.host);
    else if (state == QLatin1String("authenticating"))
        setState(SessionState::Authenticating, config_.host);
    else if (state == QLatin1String("reconnecting"))
        setState(SessionState::Reconnecting, config_.host);
    else if (state == QLatin1String("connected")) {
        interactiveUsed_ = event.value(QStringLiteral("interactive")).toBool();
        setState(SessionState::Connected, config_.host);
    } else if (state == QLatin1String("needs_reauth"))
        setState(SessionState::NeedsReauth, detail);
    else if (state == QLatin1String("disconnected"))
        setState(SessionState::Disconnected, detail);
}

void RemoteClient::handleResult(int id, const QJsonObject& result)
{
    const Op op = inflight_.take(id);
    if (id == monitorId_)
        monitorId_ = -1;

    if (id == connectId_) {
        connectId_ = -1;
        const bool ok = result.value(QStringLiteral("ok")).toBool();
        const QString error = result.value(QStringLiteral("error")).toString();
        const ErrorKind kind =
            kindFromString(result.value(QStringLiteral("kind")).toString());
        if (ok) {
            interactiveUsed_ =
                result.value(QStringLiteral("interactive")).toBool();
            setState(SessionState::Connected, config_.host);
            Q_EMIT connectFinished(true, QString(), ErrorKind::None);
            flushQueue();
        } else {
            promptId_ = 0;
            setState(SessionState::Disconnected, error);
            Q_EMIT connectFinished(false, error, kind);
            // Whatever was waiting behind the login cannot run; say so once
            // per queued operation rather than leaving them pending.
            failQueued(error, kind);
        }
        updateBusy();
        return;
    }

    if (op != Op::None)
        deliverResult(op, id, result);
    updateBusy();
}

void RemoteClient::deliverResult(Op op, int id, const QJsonObject& result)
{
    const bool ok = result.value(QStringLiteral("ok")).toBool();
    const QString error = result.value(QStringLiteral("error")).toString();
    switch (op) {
    case Op::Probe:
        Q_EMIT probeFinished(
            ok, ok ? result.value(QStringLiteral("home")).toString() : error,
            result.value(QStringLiteral("scheduler")).toString());
        break;
    case Op::Upload:
        Q_EMIT uploadFinished(ok, error);
        break;
    case Op::Submit:
        Q_EMIT submitFinished(
            ok, result.value(QStringLiteral("job_id")).toString(),
            ok ? result.value(QStringLiteral("raw")).toString() : error);
        break;
    case Op::Monitor:
        // A monitor that was cancelled says nothing about the job, so it must
        // not look like completion to whoever downloads results on that
        // signal.
        if (result.value(QStringLiteral("cancelled")).toBool())
            break;
        Q_EMIT monitorFinished(
            ok ? result.value(QStringLiteral("state")).toString()
               : QStringLiteral("MONITOR-FAILED: %1").arg(error));
        break;
    case Op::Download:
        Q_EMIT downloadFinished(ok, downloadDirs_.take(id),
                                downloadedFiles_.take(id), error);
        break;
    case Op::Connect:
    case Op::None:
        break;
    }
}

void RemoteClient::setState(SessionState state, const QString& detail)
{
    if (state_ == state)
        return;
    state_ = state;
    Q_EMIT sessionStateChanged(state, detail);
}

void RemoteClient::updateBusy()
{
    // "Busy" means a one-shot operation the user is waiting on. The monitor
    // is excluded: it runs for the length of the job, and blocking the panel
    // for hours because a job is queued would be absurd.
    bool busy = connectId_ >= 0 || !queued_.isEmpty();
    for (auto it = inflight_.begin(); !busy && it != inflight_.end(); ++it)
        busy = it.value() != Op::Monitor && it.value() != Op::None;
    if (busy == busy_)
        return;
    busy_ = busy;
    Q_EMIT busyChanged(busy);
}

void RemoteClient::probe()
{
    startOp(Op::Probe, QStringLiteral("probe"), {});
    updateBusy();
}

void RemoteClient::upload(const QString& remoteDir, const QStringList& localFiles)
{
    startOp(Op::Upload, QStringLiteral("upload"),
            {{QStringLiteral("remote_dir"), remoteDir},
             {QStringLiteral("files"), QJsonArray::fromStringList(localFiles)}});
    updateBusy();
}

void RemoteClient::submit(const QString& remoteDir, const QString& command)
{
    startOp(Op::Submit, QStringLiteral("submit"),
            {{QStringLiteral("remote_dir"), remoteDir},
             {QStringLiteral("command"), command}});
    updateBusy();
}

void RemoteClient::download(const QString& remoteDir, const QString& localDir,
                            const QStringList& patterns)
{
    startOp(Op::Download, QStringLiteral("download"),
            {{QStringLiteral("remote_dir"), remoteDir},
             {QStringLiteral("local_dir"), localDir},
             {QStringLiteral("patterns"), QJsonArray::fromStringList(patterns)}});
    updateBusy();
}

void RemoteClient::abort()
{
    for (auto it = inflight_.begin(); it != inflight_.end(); ++it) {
        if (it.value() == Op::Monitor || it.value() == Op::None)
            continue;
        send({Op::None, QStringLiteral("cancel"),
              {{QStringLiteral("target"), it.key()}}});
    }
    queued_.clear();
    updateBusy();
}

void RemoteClient::startMonitor(const QString& remoteDir,
                                const QString& schedulerKey,
                                const QString& jobId, int pollSeconds)
{
    stopMonitor();
    const int id = startOp(Op::Monitor, QStringLiteral("monitor"),
                           {{QStringLiteral("remote_dir"), remoteDir},
                            {QStringLiteral("scheduler"), schedulerKey},
                            {QStringLiteral("job_id"), jobId},
                            {QStringLiteral("poll_s"), pollSeconds}});
    monitorId_ = id;
}

void RemoteClient::stopMonitor()
{
    if (monitorId_ < 0)
        return;
    send({Op::None, QStringLiteral("cancel"),
          {{QStringLiteral("target"), monitorId_}}});
    inflight_.remove(monitorId_);
    monitorId_ = -1;
}

} // namespace calango::remote
