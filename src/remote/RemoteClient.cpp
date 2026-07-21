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

bool RemoteClient::isBusy() const
{
    return opProcess_ != nullptr;
}

bool RemoteClient::isMonitoring() const
{
    return monitorProcess_ != nullptr;
}

QString RemoteClient::ensureHelperScript()
{
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

QProcess* RemoteClient::spawn(const QString& op, const QJsonObject& args)
{
    const QString helper = ensureHelperScript();
    if (helper.isEmpty())
        return nullptr;

    auto* process = new QProcess(this);
    // -u: unbuffered, so JSON events stream in real time.
    process->start(pythonExe_, {QStringLiteral("-u"), helper});
    if (!process->waitForStarted(5000)) {
        process->deleteLater();
        return nullptr;
    }

    const QJsonObject request{
        {QStringLiteral("config"), configJson(config_)},
        {QStringLiteral("op"), op},
        {QStringLiteral("args"), args},
    };
    process->write(QJsonDocument(request).toJson(QJsonDocument::Compact));
    process->write("\n");
    return process;
}

void RemoteClient::startOp(Op op, const QString& opName, const QJsonObject& args)
{
    if (opProcess_)
        return; // one one-shot operation at a time; UI guards this too

    QProcess* process = spawn(opName, args);
    if (!process) {
        const QJsonObject error{{QStringLiteral("ok"), false},
                                {QStringLiteral("error"),
                                 QStringLiteral("could not start the SSH helper "
                                                "(python: %1)").arg(pythonExe_)}};
        finishOp(op, error);
        return;
    }

    opProcess_ = process;
    currentOp_ = op;
    lineBuffer_.clear();
    Q_EMIT busyChanged(true);

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, op] {
        lineBuffer_ += QString::fromUtf8(process->readAllStandardOutput());
        int newline = -1;
        while ((newline = lineBuffer_.indexOf(QLatin1Char('\n'))) >= 0) {
            const QString line = lineBuffer_.left(newline).trimmed();
            lineBuffer_.remove(0, newline + 1);
            if (line.isEmpty())
                continue;
            const QJsonObject event =
                QJsonDocument::fromJson(line.toUtf8()).object();
            if (event.value(QStringLiteral("event")).toString()
                == QLatin1String("result")) {
                finishOp(op, event);
            } else {
                handleLine(op, event);
            }
        }
    });
    connect(process, &QProcess::finished, this,
            [this, process, op](int exitCode, QProcess::ExitStatus status) {
                // Normal completion is driven by the "result" event; this
                // path only fires for crashes / kills without one.
                if (opProcess_ == process) {
                    QJsonObject error{
                        {QStringLiteral("ok"), false},
                        {QStringLiteral("error"),
                         status == QProcess::CrashExit
                             ? QStringLiteral("SSH helper crashed")
                             : QStringLiteral("SSH helper exited (code %1): %2")
                                   .arg(exitCode)
                                   .arg(QString::fromUtf8(
                                       process->readAllStandardError())
                                            .trimmed())}};
                    finishOp(op, error);
                }
                process->deleteLater();
            });
}

void RemoteClient::handleLine(Op op, const QJsonObject& event)
{
    const QString kind = event.value(QStringLiteral("event")).toString();
    if (op == Op::Upload && kind == QLatin1String("uploaded"))
        Q_EMIT fileUploaded(event.value(QStringLiteral("file")).toString());
    else if (op == Op::Download && kind == QLatin1String("downloaded")) {
        const QString name = event.value(QStringLiteral("file")).toString();
        downloadedFiles_ << name;
        Q_EMIT fileDownloaded(name);
    }
}

void RemoteClient::finishOp(Op op, const QJsonObject& result)
{
    if (opProcess_) {
        opProcess_->disconnect(this);
        // Let the process end on its own; it has nothing left to say.
        connect(opProcess_, &QProcess::finished,
                opProcess_, &QProcess::deleteLater);
        if (opProcess_->state() != QProcess::NotRunning)
            opProcess_->closeWriteChannel();
        opProcess_ = nullptr;
    }
    currentOp_ = Op::None;

    const bool ok = result.value(QStringLiteral("ok")).toBool();
    const QString error = result.value(QStringLiteral("error")).toString();
    switch (op) {
    case Op::Probe:
        Q_EMIT probeFinished(
            ok,
            ok ? result.value(QStringLiteral("home")).toString() : error,
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
    case Op::Download:
        Q_EMIT downloadFinished(ok, downloadDir_, downloadedFiles_, error);
        break;
    case Op::None:
        break;
    }
    Q_EMIT busyChanged(false);
}

void RemoteClient::probe()
{
    startOp(Op::Probe, QStringLiteral("probe"), {});
}

void RemoteClient::upload(const QString& remoteDir, const QStringList& localFiles)
{
    startOp(Op::Upload, QStringLiteral("upload"),
            {{QStringLiteral("remote_dir"), remoteDir},
             {QStringLiteral("files"), QJsonArray::fromStringList(localFiles)}});
}

void RemoteClient::submit(const QString& remoteDir, const QString& command)
{
    startOp(Op::Submit, QStringLiteral("submit"),
            {{QStringLiteral("remote_dir"), remoteDir},
             {QStringLiteral("command"), command}});
}

void RemoteClient::download(const QString& remoteDir, const QString& localDir,
                            const QStringList& patterns)
{
    downloadDir_ = localDir;
    downloadedFiles_.clear();
    startOp(Op::Download, QStringLiteral("download"),
            {{QStringLiteral("remote_dir"), remoteDir},
             {QStringLiteral("local_dir"), localDir},
             {QStringLiteral("patterns"), QJsonArray::fromStringList(patterns)}});
}

void RemoteClient::abort()
{
    if (opProcess_) {
        QProcess* process = opProcess_;
        opProcess_ = nullptr;
        currentOp_ = Op::None;
        process->disconnect(this);
        process->kill();
        connect(process, &QProcess::finished, process, &QProcess::deleteLater);
        Q_EMIT busyChanged(false);
    }
}

void RemoteClient::startMonitor(const QString& remoteDir,
                                const QString& schedulerKey,
                                const QString& jobId, int pollSeconds)
{
    stopMonitor();

    QProcess* process = spawn(
        QStringLiteral("monitor"),
        {{QStringLiteral("remote_dir"), remoteDir},
         {QStringLiteral("scheduler"), schedulerKey},
         {QStringLiteral("job_id"), jobId},
         {QStringLiteral("poll_s"), pollSeconds}});
    if (!process) {
        Q_EMIT monitorFinished(QStringLiteral("MONITOR-FAILED"));
        return;
    }
    monitorProcess_ = process;
    monitorBuffer_.clear();
    lastState_.clear();

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        monitorBuffer_ += QString::fromUtf8(process->readAllStandardOutput());
        int newline = -1;
        while ((newline = monitorBuffer_.indexOf(QLatin1Char('\n'))) >= 0) {
            const QString line = monitorBuffer_.left(newline).trimmed();
            monitorBuffer_.remove(0, newline + 1);
            if (line.isEmpty())
                continue;
            const QJsonObject event =
                QJsonDocument::fromJson(line.toUtf8()).object();
            const QString kind = event.value(QStringLiteral("event")).toString();
            if (kind == QLatin1String("state")) {
                lastState_ = event.value(QStringLiteral("state")).toString();
                Q_EMIT jobStateChanged(lastState_);
            } else if (kind == QLatin1String("log")) {
                Q_EMIT remoteLog(event.value(QStringLiteral("stream")).toString(),
                                 event.value(QStringLiteral("text")).toString());
            } else if (kind == QLatin1String("result")) {
                Q_EMIT monitorFinished(
                    event.value(QStringLiteral("state")).toString());
            }
        }
    });
    connect(process, &QProcess::finished, this, [this, process] {
        if (monitorProcess_ == process)
            monitorProcess_ = nullptr;
        process->deleteLater();
    });
}

void RemoteClient::stopMonitor()
{
    if (!monitorProcess_)
        return;
    QProcess* process = monitorProcess_;
    monitorProcess_ = nullptr;
    process->disconnect(this);
    process->kill();
    connect(process, &QProcess::finished, process, &QProcess::deleteLater);
}

} // namespace calango::remote
