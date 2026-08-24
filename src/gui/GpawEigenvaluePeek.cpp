#include "gui/GpawEigenvaluePeek.hpp"

#include "core/LocaleSafeNumber.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

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


namespace {

/// The Fermi level, from DOSCAR's sixth line (emax emin nedos efermi 1.0)
/// or, failing that, OUTCAR's last `E-fermi :` line. Both are written by
/// every ordinary VASP run; DOSCAR is tried first because it is a fixed
/// position rather than a scan of a file that can reach hundreds of MB.
bool readVaspFermiLevel(const QString& baselineDir, double& out)
{
    QFile doscar(baselineDir + QStringLiteral("/DOSCAR"));
    if (doscar.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&doscar);
        for (int i = 0; i < 5 && !in.atEnd(); ++i)
            in.readLine();
        const QStringList f = in.readLine().split(QRegularExpression(
            QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (f.size() >= 4) {
            double value = 0.0;
            if (core::localeSafeParse(f.at(3).toStdString(), &value)) {
                out = value;
                return true;
            }
        }
    }

    QFile outcar(baselineDir + QStringLiteral("/OUTCAR"));
    if (!outcar.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QTextStream in(&outcar);
    bool found = false;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (!line.contains(QStringLiteral("E-fermi")))
            continue;
        const QStringList f = line.split(QRegularExpression(
            QStringLiteral("[\\s:]+")), Qt::SkipEmptyParts);
        // "E-fermi :   5.1234     XC(G=0): ..." — the token after "E-fermi".
        for (int i = 0; i + 1 < f.size(); ++i) {
            if (f.at(i) != QStringLiteral("E-fermi"))
                continue;
            double value = 0.0;
            if (core::localeSafeParse(f.at(i + 1).toStdString(), &value)) {
                out = value;   // keep scanning: the LAST one is the converged
                found = true;  // value, an ionic relaxation writes several
            }
        }
    }
    return found;
}

} // namespace

GpawEigenvalueSpectrum peekVaspEigenvalues(const QString& baselineDir)
{
    GpawEigenvalueSpectrum result;

    QFile eigenval(baselineDir + QStringLiteral("/EIGENVAL"));
    if (!eigenval.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QObject::tr(
            "No EIGENVAL in %1. LDOS reads the parent's eigenvalue spectrum "
            "from it to draw the energy window — re-run the parent, or pick "
            "one that completed.").arg(baselineDir);
        return result;
    }

    QTextStream in(&eigenval);
    const auto fields = [](const QString& line) {
        return line.split(QRegularExpression(QStringLiteral("\\s+")),
                          Qt::SkipEmptyParts);
    };

    // Line 1 field 4 is ISPIN; lines 2-5 are cell/temperature/CAR/system.
    const QStringList header = fields(in.readLine());
    result.nspins = header.size() >= 4 ? header.at(3).toInt() : 1;
    if (result.nspins < 1 || result.nspins > 2)
        result.nspins = 1;
    for (int i = 0; i < 4 && !in.atEnd(); ++i)
        in.readLine();

    // Line 6: NELECT  NKPTS  NBANDS.
    const QStringList counts = fields(in.readLine());
    if (counts.size() < 3) {
        result.errorMessage = QObject::tr(
            "EIGENVAL in %1 is truncated: its sixth line should carry "
            "NELECT / NKPTS / NBANDS.").arg(baselineDir);
        return result;
    }
    const int nkpts = counts.at(1).toInt();
    const int nbands = counts.at(2).toInt();
    if (nkpts <= 0 || nbands <= 0) {
        result.errorMessage = QObject::tr(
            "EIGENVAL in %1 reports %2 k-points and %3 bands — the parent "
            "run did not finish.").arg(baselineDir).arg(nkpts).arg(nbands);
        return result;
    }

    result.states.reserve(static_cast<std::size_t>(nkpts) * nbands
                          * result.nspins);
    for (int k = 0; k < nkpts; ++k) {
        // A blank line, then "kx ky kz weight".
        QStringList kline;
        while (!in.atEnd() && kline.size() < 4)
            kline = fields(in.readLine());
        if (kline.size() < 4)
            break;
        double weight = 1.0;
        const bool ok =
            core::localeSafeParse(kline.at(3).toStdString(), &weight);
        for (int b = 0; b < nbands && !in.atEnd(); ++b) {
            const QStringList row = fields(in.readLine());
            // "band  E_up [E_down]  occ_up [occ_down]" — one energy and one
            // occupation column per spin, band index first.
            if (row.size() < 1 + result.nspins)
                continue;
            for (int s = 0; s < result.nspins; ++s) {
                GpawState state;
                state.spin = s;
                state.kpt = k;
                state.band = b;
                if (!core::localeSafeParse(row.at(1 + s).toStdString(),
                                           &state.energyEv))
                    continue;
                const int occCol = 1 + result.nspins + s;
                state.occupation = occCol < row.size()
                    ? core::localeSafeToDouble(row.at(occCol).toStdString(),
                                               -1.0)
                    : -1.0;
                state.kWeight = ok ? weight : 1.0;
                result.states.push_back(state);
            }
        }
    }

    if (result.states.empty()) {
        result.errorMessage = QObject::tr(
            "EIGENVAL in %1 holds no eigenvalues.").arg(baselineDir);
        return result;
    }

    if (!readVaspFermiLevel(baselineDir, result.fermiLevelEv)) {
        result.errorMessage = QObject::tr(
            "Read %1 states from EIGENVAL in %2, but neither DOSCAR nor "
            "OUTCAR reports a Fermi level — an energy window relative to "
            "E_F cannot be placed.")
            .arg(result.states.size()).arg(baselineDir);
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace calango::gui
