#include "gui/ClusterPreset.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

namespace calango::gui {

QJsonObject ClusterPreset::toJson() const
{
    // Note the absence of any password field — see the header.
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("host"), host},
        {QStringLiteral("port"), port},
        {QStringLiteral("username"), username},
        {QStringLiteral("auth"), auth},
        {QStringLiteral("keyPath"), keyPath},
        {QStringLiteral("remoteDir"), remoteDir},
        {QStringLiteral("scheduler"), scheduler},
        {QStringLiteral("queue"), queue},
        {QStringLiteral("nodes"), nodes},
        {QStringLiteral("tasksPerNode"), tasksPerNode},
        {QStringLiteral("memoryMbPerNode"), memoryMbPerNode},
        {QStringLiteral("walltime"), walltime},
        {QStringLiteral("parallelEnvironment"), parallelEnvironment},
        {QStringLiteral("setupLines"), setupLines},
        {QStringLiteral("vaspPotcarPath"), vaspPotcarPath},
        {QStringLiteral("account"), account},
        {QStringLiteral("qos"), qos},
        {QStringLiteral("cpusPerTask"), cpusPerTask},
        {QStringLiteral("gpusPerNode"), gpusPerNode},
        {QStringLiteral("nodeList"), nodeList},
        {QStringLiteral("extraDirectives"), extraDirectives},
        {QStringLiteral("command"), command},
    };
}

ClusterPreset ClusterPreset::fromJson(const QJsonObject& json)
{
    ClusterPreset preset;
    // Each field falls back to the default it was constructed with, so a
    // preset written before a field existed reads back usable rather than
    // zeroed. A missing walltime of "" would be submitted verbatim and
    // rejected by the scheduler; a missing one of "01:00:00" just runs.
    const auto str = [&json](const char* key, const QString& fallback) {
        const auto value = json.value(QLatin1String(key));
        return value.isString() ? value.toString() : fallback;
    };
    const auto num = [&json](const char* key, int fallback) {
        const auto value = json.value(QLatin1String(key));
        return value.isDouble() ? value.toInt() : fallback;
    };
    preset.name = str("name", preset.name);
    preset.host = str("host", preset.host);
    preset.port = num("port", preset.port);
    preset.username = str("username", preset.username);
    preset.auth = num("auth", preset.auth);
    preset.keyPath = str("keyPath", preset.keyPath);
    preset.remoteDir = str("remoteDir", preset.remoteDir);
    preset.scheduler = num("scheduler", preset.scheduler);
    preset.queue = str("queue", preset.queue);
    preset.nodes = num("nodes", preset.nodes);
    preset.tasksPerNode = num("tasksPerNode", preset.tasksPerNode);
    preset.memoryMbPerNode = num("memoryMbPerNode", preset.memoryMbPerNode);
    preset.walltime = str("walltime", preset.walltime);
    preset.parallelEnvironment =
        str("parallelEnvironment", preset.parallelEnvironment);
    preset.setupLines = str("setupLines", preset.setupLines);
    preset.vaspPotcarPath = str("vaspPotcarPath", preset.vaspPotcarPath);
    preset.account = str("account", preset.account);
    preset.qos = str("qos", preset.qos);
    preset.cpusPerTask = num("cpusPerTask", preset.cpusPerTask);
    preset.gpusPerNode = num("gpusPerNode", preset.gpusPerNode);
    preset.nodeList = str("nodeList", preset.nodeList);
    preset.extraDirectives = str("extraDirectives", preset.extraDirectives);
    preset.command = str("command", preset.command);
    return preset;
}

bool ClusterPreset::operator==(const ClusterPreset& other) const
{
    return name == other.name && host == other.host && port == other.port
        && username == other.username && auth == other.auth
        && keyPath == other.keyPath && remoteDir == other.remoteDir
        && scheduler == other.scheduler && queue == other.queue
        && nodes == other.nodes && tasksPerNode == other.tasksPerNode
        && memoryMbPerNode == other.memoryMbPerNode
        && walltime == other.walltime
        && parallelEnvironment == other.parallelEnvironment
        && setupLines == other.setupLines
        && vaspPotcarPath == other.vaspPotcarPath
        && account == other.account && qos == other.qos
        && cpusPerTask == other.cpusPerTask
        && gpusPerNode == other.gpusPerNode && nodeList == other.nodeList
        && extraDirectives == other.extraDirectives
        && command == other.command;
}

namespace ClusterPresets {

QString toJsonText(const QVector<ClusterPreset>& presets)
{
    QJsonArray array;
    for (const ClusterPreset& preset : presets)
        array.append(preset.toJson());
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<ClusterPreset> fromJsonText(const QString& text)
{
    QVector<ClusterPreset> presets;
    if (text.trimmed().isEmpty())
        return presets;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isArray())
        return presets; // corrupt or hand-edited into something else
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject())
            continue;
        ClusterPreset preset = ClusterPreset::fromJson(value.toObject());
        // A nameless preset cannot be selected, saved over or deleted — it
        // would be a permanent unreachable row in the combo.
        if (!preset.name.trimmed().isEmpty())
            presets.push_back(preset);
    }
    return presets;
}

QVector<ClusterPreset> load()
{
    return fromJsonText(
        QSettings().value(QLatin1String(kSettingsKey)).toString());
}

void save(const QVector<ClusterPreset>& presets)
{
    QSettings().setValue(QLatin1String(kSettingsKey), toJsonText(presets));
}

int indexOf(const QVector<ClusterPreset>& presets, const QString& name)
{
    for (int i = 0; i < presets.size(); ++i)
        if (presets[i].name.compare(name, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

int upsert(QVector<ClusterPreset>& presets, const ClusterPreset& preset)
{
    const int existing = indexOf(presets, preset.name);
    if (existing >= 0) {
        presets[existing] = preset;
        return existing;
    }
    presets.push_back(preset);
    return presets.size() - 1;
}

bool remove(QVector<ClusterPreset>& presets, const QString& name)
{
    const int existing = indexOf(presets, name);
    if (existing < 0)
        return false;
    presets.remove(existing);
    return true;
}

} // namespace ClusterPresets

} // namespace calango::gui
