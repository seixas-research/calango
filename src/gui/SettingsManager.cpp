#include "gui/SettingsManager.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QThread>
#include <QVariant>

#include <array>

namespace calango::gui {

namespace {

/// One managed preference: its "group/key" (the QSettings key), the default
/// value (whose type also drives JSON ↔ QVariant conversion), and an optional
/// top-level JSON member name that overrides the group/key nesting.
struct Managed {
    const char* key;
    QVariant defaultValue;
    const char* jsonName = nullptr;
};

std::array<Managed, 11> managedKeys()
{
    return {{
        {SettingsManager::kTheme, QStringLiteral("system")},
        // 0 = "auto" (leave the environment untouched); >0 pins OMP_NUM_THREADS.
        {SettingsManager::kOmpThreads, QThread::idealThreadCount()},
        {SettingsManager::kCondaDir, QString()},
        {SettingsManager::kEnvironmentPath, QString()},
        // Per-calculator env presets, stored as a JSON-object string.
        {SettingsManager::kEnvironmentPresets, QString()},
        // Per-calculator launch command templates (Preferences → "Run"), also
        // a JSON-object string. Empty = every engine uses its shipped default.
        {SettingsManager::kRunCommands, QString()},
        {SettingsManager::kRunCores, 1},
        // Exposed as a top-level `show_welcome_screen` boolean in settings.json.
        {SettingsManager::kShowWelcome, true, "show_welcome_screen"},
        {SettingsManager::kEnvFilePath, QString()},
        {SettingsManager::kMaterialsProjectApiKey, QString()},
        // Encoded camera state restored by "Reset camera"; empty = auto-frame.
        {SettingsManager::kDefaultPointOfView, QString()},
    }};
}

/// Split "group/key" into its two JSON nesting levels.
QPair<QString, QString> splitKey(const QString& key)
{
    const int slash = key.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return {QStringLiteral("misc"), key};
    return {key.left(slash), key.mid(slash + 1)};
}

QVariant jsonToVariant(const QJsonValue& value, const QVariant& prototype)
{
    switch (prototype.typeId()) {
    case QMetaType::Bool:
        return value.toBool(prototype.toBool());
    case QMetaType::Int:
        return value.toInt(prototype.toInt());
    case QMetaType::Double:
        return value.toDouble(prototype.toDouble());
    default:
        return value.toString();
    }
}

/// Serialize `value` to JSON using `prototype`'s type (QVariant's own string
/// parsing coerces values the native QSettings backend hands back as strings).
QJsonValue variantToJson(const QVariant& value, const QVariant& prototype)
{
    switch (prototype.typeId()) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
        return value.toInt();
    case QMetaType::Double:
        return value.toDouble();
    default:
        return value.toString();
    }
}

} // namespace

QString SettingsManager::directory()
{
    return QDir::homePath() + QStringLiteral("/.calango");
}

QString SettingsManager::filePath()
{
    return directory() + QStringLiteral("/settings.json");
}

void SettingsManager::loadOrInitialize()
{
    QDir().mkpath(directory());

    QFile file(filePath());
    if (!file.exists()) {
        // First run: seed QSettings with any missing defaults, then persist.
        QSettings settings;
        for (const auto& managed : managedKeys()) {
            const QString key = QString::fromUtf8(managed.key);
            if (!settings.contains(key))
                settings.setValue(key, managed.defaultValue);
        }
        save();
        return;
    }

    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return;

    // JSON is authoritative: apply each present key into QSettings.
    const QJsonObject root = doc.object();
    QSettings settings;
    for (const auto& managed : managedKeys()) {
        const QString key = QString::fromUtf8(managed.key);
        QJsonValue value;
        if (managed.jsonName) {
            value = root.value(QString::fromUtf8(managed.jsonName));
        } else {
            const auto [group, name] = splitKey(key);
            const QJsonValue groupValue = root.value(group);
            if (!groupValue.isObject())
                continue;
            value = groupValue.toObject().value(name);
        }
        if (value.isUndefined() || value.isNull())
            continue;
        settings.setValue(key, jsonToVariant(value, managed.defaultValue));
    }
}

void SettingsManager::save()
{
    QDir().mkpath(directory());

    QSettings settings;
    QJsonObject root;
    for (const auto& managed : managedKeys()) {
        const QString key = QString::fromUtf8(managed.key);
        const QJsonValue json =
            variantToJson(settings.value(key, managed.defaultValue),
                          managed.defaultValue);
        if (managed.jsonName) {
            root.insert(QString::fromUtf8(managed.jsonName), json);
        } else {
            const auto [group, name] = splitKey(key);
            QJsonObject groupObject = root.value(group).toObject();
            groupObject.insert(name, json);
            root.insert(group, groupObject);
        }
    }

    QFile file(filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

} // namespace calango::gui
