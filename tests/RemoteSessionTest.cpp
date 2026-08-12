// The Qt half of the persistent SSH session: RemoteClient driven against a
// REAL SSH server that demands a one-time code.
//
// tests/remote_session_test.py already pins the helper's own behaviour. What
// it cannot see is the side that matters to the GUI: whether the challenge
// becomes an authPromptRequested signal, whether the answer gets back before
// the server's auth timeout, whether operations issued while disconnected
// wait for the login instead of being dropped on the floor, and — the one
// that would ruin the feature — whether anything in the Qt layer re-prompts
// on its own after a drop.
//
// So this test starts the same paramiko cluster (`remote_session_test.py
// --serve`), points RemoteClient at the helper in the source tree, and
// answers the challenge from a signal handler. Nothing is stubbed: the JSON
// on the pipe, the SSH transport and the multiplexing are the shipping ones.
// It self-skips when python3 or paramiko is missing, like the other
// python-backed tests here.

#include "remote/RemoteClient.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <string>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

/// Spin the event loop until `predicate` holds or `ms` elapse.
template <typename Predicate>
bool waitFor(Predicate predicate, int ms = 25000)
{
    QEventLoop loop;
    QTimer poll;
    bool satisfied = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (predicate()) {
            satisfied = true;
            loop.quit();
        }
    });
    poll.start(25);
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    if (predicate())
        return true;
    loop.exec();
    return satisfied;
}

/// argv[2] when the build system supplied one (that is the interpreter
/// Calango itself runs the helper with, and the only one certain to have
/// paramiko), otherwise whatever is on PATH.
QString findPython(int argc, char** argv)
{
    if (argc > 2) {
        const QString given = QString::fromLocal8Bit(argv[2]);
        if (QFile::exists(given))
            return given;
    }
    for (const QString& name :
         {QStringLiteral("python3"), QStringLiteral("python")}) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty())
            return path;
    }
    return {};
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using calango::remote::AuthPrompt;
    using calango::remote::RemoteClient;
    using calango::remote::SshConfig;

    // argv[1] is the source directory; the helper and the fake cluster both
    // live there and neither is copied into the build tree.
    const QString sourceDir = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral(".");
    const QString helper =
        sourceDir + QStringLiteral("/assets/remote/calango_remote.py");
    const QString serverScript =
        sourceDir + QStringLiteral("/tests/remote_session_test.py");
    if (!QFile::exists(helper) || !QFile::exists(serverScript)) {
        std::printf("SKIP: helper or fake cluster not found under %s\n",
                    qPrintable(sourceDir));
        return 0;
    }

    const QString python = findPython(argc, argv);
    if (python.isEmpty()) {
        std::printf("SKIP: no python3 on PATH\n");
        return 0;
    }
    {
        QProcess probe;
        probe.start(python, {QStringLiteral("-c"),
                             QStringLiteral("import paramiko")});
        probe.waitForFinished(20000);
        if (probe.exitCode() != 0) {
            std::printf("SKIP: paramiko not installed for %s\n",
                        qPrintable(python));
            return 0;
        }
    }

    QTemporaryDir workspace;
    const QString clusterRoot = workspace.filePath(QStringLiteral("cluster"));
    QDir().mkpath(clusterRoot);
    // known_hosts is written on first contact; keep that off the developer's
    // real one.
    qputenv("HOME", workspace.path().toLocal8Bit());
    QDir().mkpath(workspace.filePath(QStringLiteral(".ssh")));

    QProcess cluster;
    cluster.start(python, {QStringLiteral("-u"), serverScript,
                           QStringLiteral("--serve"), QStringLiteral("2fa"),
                           clusterRoot});
    if (!cluster.waitForStarted(10000)
        || !cluster.waitForReadyRead(30000)) {
        std::printf("SKIP: fake cluster did not start\n");
        return 0;
    }
    const QString announcement =
        QString::fromUtf8(cluster.readLine()).trimmed();
    const int port = announcement.startsWith(QLatin1String("PORT"))
        ? announcement.mid(5).toInt()
        : 0;
    if (port <= 0) {
        std::printf("SKIP: fake cluster announced %s\n",
                    qPrintable(announcement));
        return 0;
    }

    RemoteClient client(python);
    client.setHelperScriptPath(helper);

    SshConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = port;
    config.username = QStringLiteral("user");
    config.auth = SshConfig::Auth::Password;
    config.password = QStringLiteral("hunter2");
    client.setConfig(config);

    // The GUI's half of the 2FA contract, in three lines.
    int promptCount = 0;
    QString code = QStringLiteral("424242");
    QVector<AuthPrompt> lastPrompts;
    QObject::connect(&client, &RemoteClient::authPromptRequested, &client,
                     [&](const QString&, const QString&,
                         const QVector<AuthPrompt>& prompts) {
                         ++promptCount;
                         lastPrompts = prompts;
                         client.answerAuthPrompt({code});
                     });

    QVector<RemoteClient::SessionState> states;
    QObject::connect(&client, &RemoteClient::sessionStateChanged, &client,
                     [&](RemoteClient::SessionState state, const QString&) {
                         states.append(state);
                     });

    std::printf("Connecting with a one-time code:\n");
    bool probeOk = false;
    QString home;
    QString scheduler;
    QObject::connect(&client, &RemoteClient::probeFinished, &client,
                     [&](bool ok, const QString& message, const QString& found) {
                         probeOk = ok;
                         home = message;
                         scheduler = found;
                     });
    // probe() on a disconnected client must authenticate first and then run:
    // the panel has always been able to just ask for work.
    client.probe();
    check(waitFor([&] { return promptCount > 0; }),
          "an operation on a cold client raises the server's challenge");
    check(!lastPrompts.isEmpty() && !lastPrompts.first().echo,
          "the prompt says the answer must not be echoed");
    check(waitFor([&] { return !home.isEmpty() || !scheduler.isEmpty(); }),
          "and the queued probe runs once the login completes");
    check(probeOk && scheduler == QLatin1String("slurm"),
          "probe reports the cluster's scheduler");
    check(client.isConnected(), "the session is open afterwards");
    check(client.usedInteractiveAuth(),
          "and is flagged as having needed a human");

    std::printf("One login, many operations:\n");
    const QString staging = workspace.filePath(QStringLiteral("staging"));
    QDir().mkpath(staging);
    {
        QFile file(staging + QStringLiteral("/run.py"));
        file.open(QIODevice::WriteOnly);
        file.write("print('hi')\n");
    }
    bool uploaded = false;
    bool uploadOk = false;
    QObject::connect(&client, &RemoteClient::uploadFinished, &client,
                     [&](bool ok, const QString&) {
                         uploaded = true;
                         uploadOk = ok;
                     });
    client.upload(QStringLiteral("job1"),
                  {staging + QStringLiteral("/run.py")});
    check(waitFor([&] { return uploaded; }), "upload completes");
    check(uploadOk, "successfully");

    QString jobId;
    bool submitted = false;
    QObject::connect(&client, &RemoteClient::submitFinished, &client,
                     [&](bool, const QString& id, const QString&) {
                         submitted = true;
                         jobId = id;
                     });
    client.submit(QStringLiteral("job1"), QStringLiteral("sbatch job.sh"));
    check(waitFor([&] { return submitted; }), "submit completes");
    check(jobId == QLatin1String("4242"), "and returns the job id");
    check(promptCount == 1,
          "STILL exactly one challenge for connect + probe + upload + submit");

    std::printf("Monitoring:\n");
    QStringList jobStates;
    QObject::connect(&client, &RemoteClient::jobStateChanged, &client,
                     [&](const QString& state) { jobStates << state; });
    bool monitorDone = false;
    QString finalState;
    QObject::connect(&client, &RemoteClient::monitorFinished, &client,
                     [&](const QString& state) {
                         monitorDone = true;
                         finalState = state;
                     });
    client.startMonitor(QStringLiteral("job1"), QStringLiteral("slurm"),
                        QStringLiteral("4242"), 1);
    check(waitFor([&] { return !jobStates.isEmpty(); }),
          "the queue state reaches the panel");
    cluster.write("finish\n"); // the job leaves the queue
    cluster.waitForBytesWritten(2000);
    check(waitFor([&] { return monitorDone; }),
          "and the monitor ends when the job does");
    check(!finalState.startsWith(QLatin1String("MONITOR-FAILED")),
          "with a real final state, not a failure");
    check(promptCount == 1, "monitoring never asked for another code");

    std::printf("After the connection drops:\n");
    promptCount = 0;
    states.clear();
    cluster.write("drop\n");
    cluster.waitForBytesWritten(2000);
    // Give the server a moment to actually tear the transports down.
    waitFor([] { return false; }, 800);

    bool probeReturned = false;
    bool probeSucceeded = true;
    QObject::connect(&client, &RemoteClient::probeFinished, &client,
                     [&](bool ok, const QString&, const QString&) {
                         probeReturned = true;
                         probeSucceeded = ok;
                     });
    client.probe();
    check(waitFor([&] { return probeReturned; }),
          "an operation after the drop returns rather than hanging");
    check(!probeSucceeded, "and it fails");
    check(promptCount == 0,
          "NO code was demanded behind the user's back — the whole point");
    check(states.contains(RemoteClient::SessionState::NeedsReauth),
          "the panel is told the session needs re-authentication");

    std::printf("Re-authenticating on request:\n");
    probeReturned = false;
    client.probe(); // stands in for the user pressing Connect
    check(waitFor([&] { return probeReturned; }),
          "an explicit retry does prompt and reconnect");
    check(promptCount == 1, "exactly one new challenge");
    check(probeSucceeded, "and the operation then succeeds");

    std::printf("Editing the cluster address:\n");
    {
        SshConfig elsewhere = config;
        elsewhere.host = QStringLiteral("127.0.0.2");
        client.setConfig(elsewhere);
        check(!client.isConnected(),
              "a session to the old host is closed, not reused for the new one");
        client.setConfig(config);
    }

    // Hanging up cancels every open operation on the helper side, and the
    // monitor's cancellation once looked exactly like "the job finished" —
    // which made pressing Connect mid-run download a half-written directory
    // as though it were the result.
    std::printf("Disconnecting under a running monitor:\n");
    cluster.write("queue\n");
    cluster.waitForBytesWritten(2000);
    int finishes = 0;
    QObject::connect(&client, &RemoteClient::monitorFinished, &client,
                     [&](const QString&) { ++finishes; });
    jobStates.clear();
    client.startMonitor(QStringLiteral("job1"), QStringLiteral("slurm"),
                        QStringLiteral("4242"), 1);
    check(waitFor([&] { return !jobStates.isEmpty(); }),
          "the monitor runs again (logging in on the way)");
    client.disconnectFromHost();
    waitFor([] { return false; }, 2500);
    check(finishes == 0, "hanging up does not report the job as finished");

    std::printf("A rejected code:\n");
    client.disconnectFromHost();
    code = QStringLiteral("000000");
    promptCount = 0;
    bool connectDone = false;
    RemoteClient::ErrorKind kind = RemoteClient::ErrorKind::None;
    QObject::connect(&client, &RemoteClient::connectFinished, &client,
                     [&](bool ok, const QString&, RemoteClient::ErrorKind k) {
                         connectDone = true;
                         kind = ok ? RemoteClient::ErrorKind::None : k;
                     });
    client.connectToHost();
    check(waitFor([&] { return connectDone; }), "the attempt finishes");
    check(kind == RemoteClient::ErrorKind::Auth,
          "a wrong code is reported as an auth failure, not a network one");
    check(!client.isConnected(), "and leaves the session closed");

    cluster.write("quit\n");
    cluster.waitForFinished(5000);
    if (cluster.state() != QProcess::NotRunning)
        cluster.kill();

    std::printf("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
