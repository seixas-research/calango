#include "gui/RunCommands.hpp"

#include "gui/EnginePresets.hpp"
#include "gui/SettingsManager.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>

namespace calango::gui {

namespace RunCommands {

namespace {

QJsonObject readMap()
{
    const QString raw =
        QSettings().value(QLatin1String(SettingsManager::kRunCommands)).toString();
    return QJsonDocument::fromJson(raw.toUtf8()).object();
}

void writeMap(const QJsonObject& map)
{
    QSettings().setValue(
        QLatin1String(SettingsManager::kRunCommands),
        QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact)));
    SettingsManager::save(); // mirror to ~/.calango/settings.json immediately
}

} // namespace

QString defaultTemplate(core::CalculatorKind kind)
{
    switch (kind) {
    case core::CalculatorKind::Gpaw:
        // GPAW parallelizes by running the SCRIPT under MPI; `gpaw -P n python`
        // is its own launcher wrapper. OMP_NUM_THREADS=1 is deliberate: GPAW's
        // own MPI decomposition and a threaded BLAS underneath it fight for the
        // same cores and the run gets slower, not faster.
        return QStringLiteral(
            "OMP_NUM_THREADS=1 gpaw -P {cores} python {script}");
    case core::CalculatorKind::QuantumEspresso:
        return QStringLiteral("mpirun -np {cores} pw.x -in {input} > {output}");
    case core::CalculatorKind::Siesta:
        return QStringLiteral("mpirun -np {cores} siesta < {input} > {output}");
    case core::CalculatorKind::Vasp:
        return QStringLiteral("mpirun -np {cores} vasp_std");
    case core::CalculatorKind::Orca:
        // ORCA reads its own %pal block for parallelism and must be invoked
        // with a full path, so the rank count does not belong on this line.
        return QStringLiteral("orca {input} > {output}");
    default:
        break;
    }
    // Everything else — the ML potentials, EMT/LJ, ASAP — is a single Python
    // process; threading is controlled by OMP_NUM_THREADS (Preferences →
    // General) and by the model's own device selection.
    return QStringLiteral("{python} {script}");
}

QString templateFor(core::CalculatorKind kind)
{
    const QString stored =
        readMap().value(EnginePresets::presetName(kind)).toString();
    return stored.trimmed().isEmpty() ? defaultTemplate(kind) : stored;
}

void setTemplateFor(core::CalculatorKind kind, const QString& commandTemplate)
{
    QJsonObject map = readMap();
    const QString key = EnginePresets::presetName(kind);
    // Storing a value identical to the default would freeze this engine at
    // today's default; clearing lets it track future releases.
    if (commandTemplate.trimmed().isEmpty()
        || commandTemplate.trimmed() == defaultTemplate(kind).trimmed())
        map.remove(key);
    else
        map[key] = commandTemplate.trimmed();
    writeMap(map);
}

int cores()
{
    return QSettings().value(QLatin1String(SettingsManager::kRunCores), 1).toInt();
}

void setCores(int coreCount)
{
    QSettings().setValue(QLatin1String(SettingsManager::kRunCores),
                         std::max(1, coreCount));
    SettingsManager::save();
}

QString solverCommandVariable(core::CalculatorKind kind)
{
    switch (kind) {
    case core::CalculatorKind::QuantumEspresso:
        return QStringLiteral("ASE_ESPRESSO_COMMAND");
    case core::CalculatorKind::Siesta:
        return QStringLiteral("ASE_SIESTA_COMMAND");
    case core::CalculatorKind::Vasp:
        return QStringLiteral("ASE_VASP_COMMAND");
    case core::CalculatorKind::Orca:
        return QStringLiteral("ASE_ORCA_COMMAND");
    default:
        break;
    }
    return QString();
}

Resolved resolve(core::CalculatorKind kind, const Context& context,
                 const QString& commandTemplate)
{
    const QString text = commandTemplate.trimmed().isEmpty()
        ? templateFor(kind)
        : commandTemplate.trimmed();

    const auto substitute = [&context](QString value) {
        value.replace(QLatin1String("{cores}"), QString::number(std::max(1, context.cores)));
        value.replace(QLatin1String("{script}"), context.scriptFile);
        value.replace(QLatin1String("{python}"),
                      context.pythonExecutable.isEmpty()
                          ? QStringLiteral("python")
                          : context.pythonExecutable);
        return value;
    };

    Resolved resolved;
    if (text.contains(QLatin1String("{script}"))) {
        // A script launcher: this line starts the job process itself.
        resolved.commandLine = substitute(text);
        return resolved;
    }

    // A solver command. {input}/{output} are NOT substituted here — ASE fills
    // them in per calculation (PREFIX.in / PREFIX.out), so they must survive
    // into the environment variable verbatim.
    const QString variable = solverCommandVariable(kind);
    if (!variable.isEmpty())
        resolved.environment.insert(variable, substitute(text));
    // The job itself is still one Python process driving that solver.
    resolved.commandLine = substitute(QStringLiteral("{python} {script}"));
    return resolved;
}

QString displayCommand(core::CalculatorKind kind, const Context& context)
{
    // The field shows (and the user edits) the TEMPLATE with the launch-time
    // values filled in — not the derived process line. For a solver command
    // the interesting part is the mpirun invocation, and substituting it away
    // to "python run.py" would hide exactly what the user came to check.
    // {input}/{output} stay symbolic: ASE fills those per calculation.
    QString text = templateFor(kind);
    text.replace(QLatin1String("{cores}"),
                 QString::number(std::max(1, context.cores)));
    text.replace(QLatin1String("{script}"), context.scriptFile);
    text.replace(QLatin1String("{python}"),
                 context.pythonExecutable.isEmpty()
                     ? QStringLiteral("python")
                     : context.pythonExecutable);
    return text;
}

} // namespace RunCommands

} // namespace calango::gui
