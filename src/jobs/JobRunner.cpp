#include "jobs/JobRunner.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>

namespace calango::jobs {

JobRunner::JobRunner(QObject* parent)
    : QObject(parent)
{
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        flushChannel(stdoutBuffer_, process_.readAllStandardOutput(), false);
    });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        flushChannel(stderrBuffer_, process_.readAllStandardError(), true);
    });
    connect(&process_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                // Emit any trailing output that lacked a final newline.
                if (!stdoutBuffer_.isEmpty())
                    handleLine(stdoutBuffer_, false);
                if (!stderrBuffer_.isEmpty())
                    handleLine(stderrBuffer_, true);
                stdoutBuffer_.clear();
                stderrBuffer_.clear();
                Q_EMIT finished(exitCode, status == QProcess::CrashExit);
            });
}

bool JobRunner::isRunning() const
{
    return process_.state() != QProcess::NotRunning;
}

void JobRunner::start(const QString& pythonExe, const QString& scriptPath, const QString& workDir)
{
    if (isRunning())
        return;

    stdoutBuffer_.clear();
    stderrBuffer_.clear();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));

    // Make the selected environment self-contained: its bin directory wins
    // over any globally installed solver binaries.
    const QFileInfo interpreter(pythonExe);
    const QString binDir = interpreter.absolutePath();
    env.insert(QStringLiteral("PATH"),
               binDir + QDir::listSeparator() + env.value(QStringLiteral("PATH")));
    const QDir bin(binDir);
    if (bin.dirName() == QLatin1String("bin")
        || bin.dirName() == QLatin1String("Scripts")) {
        QDir prefix = bin;
        prefix.cdUp();
        if (QFileInfo::exists(prefix.filePath(QStringLiteral("conda-meta"))))
            env.insert(QStringLiteral("CONDA_PREFIX"), prefix.absolutePath());
    }

    process_.setProcessEnvironment(env);
    process_.setWorkingDirectory(workDir);
    process_.start(pythonExe, {scriptPath});

    Q_EMIT started(QStringLiteral("%1 %2  (in %3)")
                       .arg(pythonExe, scriptPath, QDir::toNativeSeparators(workDir)));
}

void JobRunner::terminate()
{
    if (!isRunning())
        return;
    process_.terminate();
    QTimer::singleShot(3000, &process_, [this] {
        if (isRunning())
            process_.kill();
    });
}

void JobRunner::flushChannel(QString& buffer, const QByteArray& chunk, bool isStderr)
{
    buffer += QString::fromLocal8Bit(chunk);
    qsizetype newlinePos = 0;
    while ((newlinePos = buffer.indexOf(QLatin1Char('\n'))) != -1) {
        const QString line = buffer.left(newlinePos);
        buffer.remove(0, newlinePos + 1);
        handleLine(line, isStderr);
    }
}

void JobRunner::handleLine(const QString& line, bool isStderr)
{
    if (isStderr) {
        Q_EMIT errorLine(line);
        return;
    }

    static const QRegularExpression progressRe(
        QStringLiteral(R"(^CALANGO_PROGRESS (\d+) (\d+)\s*$)"));
    if (const auto match = progressRe.match(line); match.hasMatch())
        Q_EMIT progress(match.captured(1).toInt(), match.captured(2).toInt());

    static const QRegularExpression energyRe(
        QStringLiteral(R"(^CALANGO_ENERGY (\d+) (-?[\d.]+(?:[eE][+-]?\d+)?)\s*$)"));
    if (const auto match = energyRe.match(line); match.hasMatch())
        Q_EMIT energySample(match.captured(1).toInt(), match.captured(2).toDouble());

    static const QRegularExpression temperatureRe(
        QStringLiteral(R"(^CALANGO_TEMP (\d+) (-?[\d.]+(?:[eE][+-]?\d+)?)\s*$)"));
    if (const auto match = temperatureRe.match(line); match.hasMatch())
        Q_EMIT temperatureSample(match.captured(1).toInt(),
                                 match.captured(2).toDouble());

    static const QRegularExpression fmaxRe(
        QStringLiteral(R"(^CALANGO_FMAX (\d+) (-?[\d.]+(?:[eE][+-]?\d+)?)\s*$)"));
    if (const auto match = fmaxRe.match(line); match.hasMatch())
        Q_EMIT maxForceSample(match.captured(1).toInt(),
                              match.captured(2).toDouble());

    static const QRegularExpression pressureRe(
        QStringLiteral(R"(^CALANGO_PRESSURE (\d+) (-?[\d.]+(?:[eE][+-]?\d+)?)\s*$)"));
    if (const auto match = pressureRe.match(line); match.hasMatch())
        Q_EMIT pressureSample(match.captured(1).toInt(),
                              match.captured(2).toDouble());

    static const QRegularExpression targetTempRe(
        QStringLiteral(R"(^CALANGO_TARGET_TEMP (-?[\d.]+(?:[eE][+-]?\d+)?)\s*$)"));
    if (const auto match = targetTempRe.match(line); match.hasMatch())
        Q_EMIT targetTemperature(match.captured(1).toDouble());

    static const QRegularExpression targetPressureRe(
        QStringLiteral(R"(^CALANGO_TARGET_PRESSURE (-?[\d.]+(?:[eE][+-]?\d+)?)\s*$)"));
    if (const auto match = targetPressureRe.match(line); match.hasMatch())
        Q_EMIT targetPressure(match.captured(1).toDouble());

    Q_EMIT outputLine(line);
}

} // namespace calango::jobs
