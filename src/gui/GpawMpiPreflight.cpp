#include "gui/GpawMpiPreflight.hpp"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>

namespace calango::gui {

namespace {

/// mpirun/mpiexec next to `pythonExecutable` first (a conda-installed MPI
/// build), then wherever the system PATH resolves it — the same two places
/// JobRunner::start() itself looks, since it prepends the interpreter's own
/// bin/ to PATH before running the job's shell command.
QString findMpiLauncher(const QString& pythonExecutable)
{
    const QString interpreterDir = QFileInfo(pythonExecutable).absolutePath();
    for (const QString& name : {QStringLiteral("mpirun"), QStringLiteral("mpiexec")}) {
        const QString candidate = QDir(interpreterDir).filePath(name);
        if (QFileInfo(candidate).isExecutable())
            return candidate;
    }
    for (const QString& name : {QStringLiteral("mpirun"), QStringLiteral("mpiexec")}) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            return found;
    }
    return QString();
}

} // namespace

GpawMpiPreflightResult checkGpawMpi(const QString& pythonExecutable, int cores,
                                    int timeoutMs)
{
    GpawMpiPreflightResult result;
    if (cores <= 1) {
        result.ok = true; // nothing to parallelize
        return result;
    }
    if (pythonExecutable.trimmed().isEmpty()) {
        result.errorMessage =
            QObject::tr("No Python interpreter is configured.");
        return result;
    }

    result.launcherPath = findMpiLauncher(pythonExecutable);
    result.launcherFound = !result.launcherPath.isEmpty();

    QProcess probe;
    probe.start(pythonExecutable,
               {QStringLiteral("-c"),
                QStringLiteral(
                    "import gpaw.cgpaw as c\n"
                    "print('MPI=' + str(int(bool(getattr(c, 'have_mpi', False)))))\n")});
    QString probeError;
    if (!probe.waitForStarted(timeoutMs)) {
        probeError =
            QObject::tr("%1 could not be started.").arg(pythonExecutable);
    } else if (!probe.waitForFinished(timeoutMs)) {
        probe.kill();
        probe.waitForFinished(2000);
        probeError = QObject::tr("Checking GPAW's MPI support under %1 timed out.")
                         .arg(pythonExecutable);
    } else if (probe.exitCode() != 0) {
        const QString stderrText =
            QString::fromUtf8(probe.readAllStandardError()).trimmed();
        probeError = stderrText.isEmpty()
            ? QObject::tr("GPAW is not importable under %1.").arg(pythonExecutable)
            : stderrText.section(QLatin1Char('\n'), -1);
    } else {
        const QString output =
            QString::fromUtf8(probe.readAllStandardOutput());
        result.mpiEnabled = output.contains(QStringLiteral("MPI=1"));
    }

    if (!probeError.isEmpty()) {
        result.errorMessage = probeError;
        return result;
    }

    if (result.mpiEnabled && result.launcherFound) {
        result.ok = true;
        return result;
    }

    // Name the SPECIFIC problem(s) found -- one, or both.
    QStringList problems;
    if (!result.mpiEnabled)
        problems << QObject::tr(
            "this GPAW build has no MPI support compiled in — launching it "
            "under mpirun would run %1 completely independent copies of the "
            "SAME calculation rather than %1 ranks of one parallel run "
            "(worse than serial, not just equivalent to it)")
                        .arg(cores);
    if (!result.launcherFound)
        problems << QObject::tr(
            "no mpirun/mpiexec launcher was found next to %1 or on PATH")
                        .arg(pythonExecutable);
    result.errorMessage = QObject::tr(
        "Requested %1 cores for a GPAW job, but %2.")
            .arg(cores).arg(problems.join(QObject::tr(" — and ")));
    return result;
}

} // namespace calango::gui
