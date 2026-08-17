#include "gui/PythonPackagePreflight.hpp"

#include <QProcess>

namespace calango::gui {

PythonPackagePreflightResult checkPythonPackage(const QString& pythonExecutable,
                                                const QString& moduleName,
                                                int timeoutMs)
{
    PythonPackagePreflightResult result;
    if (pythonExecutable.trimmed().isEmpty()) {
        result.errorMessage =
            QObject::tr("No Python interpreter is configured.");
        return result;
    }

    QProcess probe;
    probe.start(pythonExecutable,
               {QStringLiteral("-c"),
                QStringLiteral("import %1; print(getattr(%1, '__version__', ''))")
                    .arg(moduleName)});
    if (!probe.waitForStarted(timeoutMs)) {
        result.errorMessage =
            QObject::tr("%1 could not be started.").arg(pythonExecutable);
        return result;
    }
    if (!probe.waitForFinished(timeoutMs)) {
        probe.kill();
        probe.waitForFinished(2000);
        result.errorMessage =
            QObject::tr("Checking for %1 under %2 timed out.")
                .arg(moduleName, pythonExecutable);
        return result;
    }
    if (probe.exitCode() != 0) {
        // The interpreter's own ImportError text, trimmed to its last line
        // (a traceback's actual message) rather than the whole dump.
        const QString stderrText =
            QString::fromUtf8(probe.readAllStandardError()).trimmed();
        result.errorMessage = stderrText.isEmpty()
            ? QObject::tr("%1 is not importable under %2.")
                  .arg(moduleName, pythonExecutable)
            : stderrText.section(QLatin1Char('\n'), -1);
        return result;
    }
    result.available = true;
    result.version =
        QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
    return result;
}

TorchDeviceAvailability probeTorchDevices(const QString& pythonExecutable,
                                          int timeoutMs)
{
    TorchDeviceAvailability result;
    if (pythonExecutable.trimmed().isEmpty())
        return result;

    QProcess probe;
    probe.start(
        pythonExecutable,
        {QStringLiteral("-c"),
         QStringLiteral(
             "import torch\n"
             "print('cuda=' + str(int(torch.cuda.is_available())))\n"
             "mps = getattr(torch.backends, 'mps', None)\n"
             "print('mps=' + str(int(bool(mps) and mps.is_available())))\n")});
    if (!probe.waitForStarted(timeoutMs) || !probe.waitForFinished(timeoutMs)) {
        probe.kill();
        probe.waitForFinished(2000);
        return result;
    }
    if (probe.exitCode() != 0)
        return result;

    const QString output = QString::fromUtf8(probe.readAllStandardOutput());
    result.probeSucceeded = true;
    result.cpu = true; // a successful torch import always allows cpu
    result.cuda = output.contains(QStringLiteral("cuda=1"));
    result.mps = output.contains(QStringLiteral("mps=1"));
    return result;
}

} // namespace calango::gui
