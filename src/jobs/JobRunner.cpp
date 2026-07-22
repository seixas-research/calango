#include "jobs/JobRunner.hpp"

#include "core/Element.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
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
    pendingFrame_.reset();
    pendingAtoms_ = 0;
    pendingCellValid_ = false;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));

    // Thread control (Preferences → Multi-Threading). A value > 0 pins the
    // per-process thread pools used by numpy/BLAS and OpenMP-parallel solvers;
    // 0 leaves the environment untouched (let the libraries auto-detect). Set
    // before the interpreter starts so it takes effect ahead of numpy import.
    const int ompThreads = QSettings().value(QStringLiteral("jobs/ompThreads"),
                                             0).toInt();
    if (ompThreads > 0) {
        const QString value = QString::number(ompThreads);
        for (const auto* var : {"OMP_NUM_THREADS", "MKL_NUM_THREADS",
                                "OPENBLAS_NUM_THREADS", "NUMEXPR_NUM_THREADS",
                                "VECLIB_MAXIMUM_THREADS"})
            env.insert(QLatin1String(var), value);
    }

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

    // --- Live geometry stream (CALANGO_CELL / CALANGO_FRAME) ---------------
    if (pendingAtoms_ > 0) {
        // Expecting "<symbol> <x> <y> <z>" — consumed silently.
        const QStringList parts =
            line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        bool okX = false, okY = false, okZ = false;
        const int z = parts.size() == 4
            ? core::Elements::atomicNumber(parts[0].toStdString())
            : 0;
        if (z > 0) {
            const core::Vec3 position{parts[1].toDouble(&okX),
                                      parts[2].toDouble(&okY),
                                      parts[3].toDouble(&okZ)};
            if (okX && okY && okZ)
                pendingFrame_->addAtom({z, position});
        }
        if (z <= 0 || !okX || !okY || !okZ) { // malformed — abandon frame
            pendingFrame_.reset();
            pendingAtoms_ = 0;
            Q_EMIT outputLine(line);
            return;
        }
        if (--pendingAtoms_ == 0) {
            if (pendingCellValid_) {
                pendingFrame_->setCell(core::UnitCell(
                    {pendingCell_[0], pendingCell_[1], pendingCell_[2]},
                    {pendingCell_[3], pendingCell_[4], pendingCell_[5]},
                    {pendingCell_[6], pendingCell_[7], pendingCell_[8]},
                    {true, true, true}));
                pendingCellValid_ = false;
            }
            Q_EMIT frameStreamed(
                std::shared_ptr<core::Structure>(std::move(pendingFrame_)));
        }
        return;
    }
    static const QRegularExpression cellRe(QStringLiteral(
        R"(^CALANGO_CELL((?: -?[\d.]+(?:[eE][+-]?\d+)?){9})\s*$)"));
    if (const auto match = cellRe.match(line); match.hasMatch()) {
        const QStringList values =
            match.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (int i = 0; i < 9; ++i)
            pendingCell_[i] = values[i].toDouble();
        pendingCellValid_ = true;
        return;
    }
    static const QRegularExpression frameRe(
        QStringLiteral(R"(^CALANGO_FRAME (\d+)\s*$)"));
    if (const auto match = frameRe.match(line); match.hasMatch()) {
        pendingAtoms_ = match.captured(1).toInt();
        pendingFrame_ = std::make_unique<core::Structure>();
        if (pendingAtoms_ <= 0) {
            pendingFrame_.reset();
            pendingAtoms_ = 0;
        }
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
