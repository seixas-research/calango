#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

namespace calango::jobs {

/// Runs one generated ASE script as a local subprocess (QProcess) and
/// streams its output back to the GUI, line by line, in real time.
///
/// Lines matching "CALANGO_PROGRESS <step> <total>" additionally emit
/// progress() so views can drive a progress bar. Running out-of-process
/// (instead of via the embedded interpreter) keeps the GUI responsive and
/// isolates simulation crashes from the application.
class JobRunner : public QObject {
    Q_OBJECT

public:
    explicit JobRunner(QObject* parent = nullptr);

    bool isRunning() const;

public Q_SLOTS:
    /// Launch `pythonExe scriptPath` with `workDir` as the working directory.
    void start(const QString& pythonExe, const QString& scriptPath, const QString& workDir);

    /// Politely terminate; escalates to kill() after a grace period.
    void terminate();

Q_SIGNALS:
    void started(const QString& description);
    void outputLine(const QString& line);
    void errorLine(const QString& line);
    void progress(int step, int total);
    /// One sample per "CALANGO_ENERGY <step> <energy_eV>" marker.
    void energySample(int step, double energyEv);
    void finished(int exitCode, bool crashed);

private:
    void flushChannel(QString& buffer, const QByteArray& chunk, bool isStderr);
    void handleLine(const QString& line, bool isStderr);

    QProcess process_;
    QString stdoutBuffer_;
    QString stderrBuffer_;
};

} // namespace calango::jobs
