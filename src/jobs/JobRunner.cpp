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
                description_.clear();
                Q_EMIT finished(exitCode, status == QProcess::CrashExit);
            });
}

bool JobRunner::isRunning() const
{
    return process_.state() != QProcess::NotRunning;
}

qint64 JobRunner::processId() const
{
    return process_.state() == QProcess::NotRunning ? 0 : process_.processId();
}

void JobRunner::start(const QString& commandLine, const QString& pythonExe,
                      const QString& workDir,
                      const QMap<QString, QString>& extraEnv)
{
    if (isRunning())
        return;

    stdoutBuffer_.clear();
    stderrBuffer_.clear();
    pendingFrame_.reset();
    pendingAtoms_ = 0;
    pendingVectors_ = false;
    pendingForces_.clear();
    pendingVelocities_.clear();
    pendingCellValid_ = false;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));

    // Thread control (Preferences → Multi-Threading). A value > 0 pins the
    // per-process thread pools used by numpy/BLAS and OpenMP-parallel solvers;
    // 0 leaves the environment untouched (let the libraries auto-detect). Set
    // before the interpreter starts so it takes effect ahead of numpy import.
    // The fallback matches SettingsManager's managed default of 1 thread —
    // MPI ranks and a threaded BLAS would fight for the same cores.
    const int ompThreads = QSettings().value(QStringLiteral("jobs/ompThreads"),
                                             1).toInt();
    if (ompThreads > 0) {
        const QString value = QString::number(ompThreads);
        for (const auto* var : {"OMP_NUM_THREADS", "MKL_NUM_THREADS",
                                "OPENBLAS_NUM_THREADS", "NUMEXPR_NUM_THREADS",
                                "VECLIB_MAXIMUM_THREADS"})
            env.insert(QLatin1String(var), value);
    }

    // Pseudopotential libraries (Preferences → External Files). Exported as
    // the variable each engine already reads, so a run picks the configured
    // set up with no change to the generated input. Written only when the
    // preference is non-empty: a machine that already exports one in its shell
    // profile must keep it rather than have it silently blanked.
    //
    // These are read from QSettings directly, like the thread pinning above,
    // rather than threaded through the job config — they describe the MACHINE,
    // and every engine on it wants the same answer.
    {
        const QSettings settings;
        const std::pair<const char*, const char*> kPseudoVars[] = {
            {"pseudopotentials/vasp", "VASP_PP_PATH"},
            {"pseudopotentials/quantumEspresso", "ESPRESSO_PSEUDO"},
            {"pseudopotentials/siesta", "SIESTA_PP_PATH"},
            {"mlPotentials/directory", "CALANGO_ML_POTENTIALS"},
        };
        for (const auto& [key, variable] : kPseudoVars) {
            const QString value =
                settings.value(QLatin1String(key)).toString().trimmed();
            if (!value.isEmpty())
                env.insert(QLatin1String(variable), value);
        }
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

    // Per-engine hand-offs (ASE_ESPRESSO_COMMAND and friends) go in last so a
    // user-configured solver command wins over anything inherited.
    for (auto it = extraEnv.constBegin(); it != extraEnv.constEnd(); ++it)
        env.insert(it.key(), it.value());

    process_.setProcessEnvironment(env);
    process_.setWorkingDirectory(workDir);
#ifdef Q_OS_WIN
    process_.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), commandLine});
#else
    process_.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), commandLine});
#endif

    // The leading executable is the useful short label: the full command line
    // is a shell string with environment assignments and redirections in it,
    // which is unreadable in a status bar.
    description_ = commandLine.section(QLatin1Char(' '), 0, 0);
    for (const QString& token : commandLine.split(QLatin1Char(' '))) {
        if (!token.contains(QLatin1Char('=')) && !token.isEmpty()) {
            description_ = QFileInfo(token).fileName();
            break;
        }
    }

    Q_EMIT started(QStringLiteral("%1  (in %2)")
                       .arg(commandLine, QDir::toNativeSeparators(workDir)));
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
        // "<symbol> <x> <y> <z>", optionally followed by the per-atom force
        // and velocity components when the frame header carried "FV".
        const QStringList parts =
            line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const int expected = pendingVectors_ ? 10 : 4;
        bool okX = false, okY = false, okZ = false;
        const int z = parts.size() == expected
            ? core::Elements::atomicNumber(parts[0].toStdString())
            : 0;
        bool vectorsOk = true;
        if (z > 0) {
            const core::Vec3 position{parts[1].toDouble(&okX),
                                      parts[2].toDouble(&okY),
                                      parts[3].toDouble(&okZ)};
            if (okX && okY && okZ)
                pendingFrame_->addAtom({z, position});
            if (pendingVectors_) {
                bool ok[6] = {};
                const core::Vec3 force{parts[4].toDouble(&ok[0]),
                                       parts[5].toDouble(&ok[1]),
                                       parts[6].toDouble(&ok[2])};
                const core::Vec3 velocity{parts[7].toDouble(&ok[3]),
                                          parts[8].toDouble(&ok[4]),
                                          parts[9].toDouble(&ok[5])};
                for (const bool component : ok)
                    vectorsOk = vectorsOk && component;
                if (vectorsOk) {
                    pendingForces_.push_back(force);
                    pendingVelocities_.push_back(velocity);
                }
            }
        }
        if (z <= 0 || !okX || !okY || !okZ || !vectorsOk) { // malformed
            pendingFrame_.reset();
            pendingForces_.clear();
            pendingVelocities_.clear();
            pendingAtoms_ = 0;
            pendingVectors_ = false;
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
            // Attach the per-atom vectors so the viewport can draw arrows on
            // this frame immediately; the magnitudes double as scalar
            // color-mapping fields, matching what AseBridge produces when the
            // same trajectory is later re-read from disk.
            if (pendingVectors_
                && pendingForces_.size() == pendingFrame_->size()) {
                pendingFrame_->setVectorField("forces", pendingForces_);
                pendingFrame_->setVectorField("velocities", pendingVelocities_);
                std::vector<double> forceMagnitude(pendingForces_.size());
                for (std::size_t i = 0; i < pendingForces_.size(); ++i)
                    forceMagnitude[i] = pendingForces_[i].norm();
                pendingFrame_->setScalarField("|forces|",
                                              std::move(forceMagnitude));
            }
            pendingForces_.clear();
            pendingVelocities_.clear();
            pendingVectors_ = false;
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
    // "CALANGO_FRAME <natoms>" or "CALANGO_FRAME <natoms> FV", the latter
    // announcing 10-column atom lines (position + force + velocity).
    static const QRegularExpression frameRe(
        QStringLiteral(R"(^CALANGO_FRAME (\d+)(?: (FV))?\s*$)"));
    if (const auto match = frameRe.match(line); match.hasMatch()) {
        pendingAtoms_ = match.captured(1).toInt();
        pendingVectors_ = match.captured(2) == QLatin1String("FV");
        pendingFrame_ = std::make_unique<core::Structure>();
        pendingForces_.clear();
        pendingVelocities_.clear();
        if (pendingVectors_) {
            pendingForces_.reserve(static_cast<std::size_t>(pendingAtoms_));
            pendingVelocities_.reserve(static_cast<std::size_t>(pendingAtoms_));
        }
        if (pendingAtoms_ <= 0) {
            pendingFrame_.reset();
            pendingAtoms_ = 0;
            pendingVectors_ = false;
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
