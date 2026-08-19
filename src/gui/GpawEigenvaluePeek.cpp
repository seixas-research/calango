#include "gui/GpawEigenvaluePeek.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace calango::gui {

namespace {

// Deliberately NOT AseScriptGenerator::gpawRestartFromBaselineScript: that
// one is built to be embedded in a full job script (JSON-logger preamble,
// progress markers, CALANGO_RESULT). This is a five-line, single-purpose
// peek run via `python -c`, with its own minimal glob-and-restart — the
// same shape, kept deliberately separate rather than threading a "quiet
// mode" flag through the job-script generator for a caller that isn't one.
QString peekScript(const QString& baselineDir)
{
    // A raw Python string literal (r"...") for the path, exactly like every
    // generated job script's own `_base = r"..."` — sidesteps backslash
    // escaping with no round-trip through a second encoder for what is, in
    // practice, always a plain filesystem path with no embedded quote.
    return QStringLiteral(
        "import glob, json, os, sys\n"
        "_base = r\"%1\"\n"
        "_gpw = sorted(glob.glob(os.path.join(_base, '*.gpw')))\n"
        "if not _gpw:\n"
        "    print(json.dumps({'error': 'No .gpw found in ' + _base}))\n"
        "    sys.exit(1)\n"
        "os.environ.setdefault('GPAW_NEW', '1')\n"
        "from gpaw import GPAW\n"
        "calc = GPAW(_gpw[0], txt=None)\n"
        "nspins = calc.get_number_of_spins()\n"
        "weights = calc.get_k_point_weights()\n"
        "efermi = calc.get_fermi_level()\n"
        "states = []\n"
        "for s in range(nspins):\n"
        "    for k in range(len(weights)):\n"
        "        eigs = calc.get_eigenvalues(kpt=k, spin=s)\n"
        "        occs = calc.get_occupation_numbers(kpt=k, spin=s)\n"
        "        for n, e in enumerate(eigs):\n"
        "            occ = float(occs[n]) if occs is not None else -1.0\n"
        "            states.append({'spin': s, 'kpt': k, 'band': n,\n"
        "                          'energy_eV': float(e), 'occupation': occ,\n"
        "                          'weight': float(weights[k])})\n"
        "print(json.dumps({'states': states, 'efermi_eV': float(efermi),\n"
        "                  'nspins': int(nspins)}))\n")
        .arg(baselineDir);
}

} // namespace

GpawEigenvalueSpectrum peekGpawEigenvalues(const QString& pythonExecutable,
                                          const QString& baselineDir,
                                          int timeoutMs)
{
    GpawEigenvalueSpectrum result;
    if (pythonExecutable.trimmed().isEmpty()) {
        result.errorMessage =
            QObject::tr("No Python interpreter is configured.");
        return result;
    }

    QProcess probe;
    probe.start(pythonExecutable,
               {QStringLiteral("-c"), peekScript(baselineDir)});
    if (!probe.waitForStarted(timeoutMs)) {
        result.errorMessage =
            QObject::tr("%1 could not be started.").arg(pythonExecutable);
        return result;
    }
    if (!probe.waitForFinished(timeoutMs)) {
        probe.kill();
        probe.waitForFinished(2000);
        result.errorMessage =
            QObject::tr("Reading the baseline's eigenvalues under %1 timed "
                        "out.")
                .arg(pythonExecutable);
        return result;
    }

    const QByteArray stdoutBytes = probe.readAllStandardOutput();
    const QJsonObject root = QJsonDocument::fromJson(stdoutBytes).object();
    if (root.contains(QStringLiteral("error"))
        || probe.exitCode() != 0) {
        const QString stderrText =
            QString::fromUtf8(probe.readAllStandardError()).trimmed();
        result.errorMessage = root.contains(QStringLiteral("error"))
            ? root.value(QStringLiteral("error")).toString()
            : (stderrText.isEmpty()
                   ? QObject::tr("Could not read the baseline's "
                                "eigenvalues.")
                   : stderrText.section(QLatin1Char('\n'), -1));
        return result;
    }

    result.fermiLevelEv = root.value(QStringLiteral("efermi_eV")).toDouble();
    result.nspins = root.value(QStringLiteral("nspins")).toInt(1);
    for (const QJsonValue& v : root.value(QStringLiteral("states")).toArray()) {
        const QJsonObject o = v.toObject();
        GpawState state;
        state.spin = o.value(QStringLiteral("spin")).toInt();
        state.kpt = o.value(QStringLiteral("kpt")).toInt();
        state.band = o.value(QStringLiteral("band")).toInt();
        state.energyEv = o.value(QStringLiteral("energy_eV")).toDouble();
        state.occupation = o.value(QStringLiteral("occupation")).toDouble(-1.0);
        state.kWeight = o.value(QStringLiteral("weight")).toDouble(1.0);
        result.states.push_back(state);
    }
    result.ok = true;
    return result;
}

} // namespace calango::gui
