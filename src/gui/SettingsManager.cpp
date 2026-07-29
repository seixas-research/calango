#include "gui/SettingsManager.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QVariant>

#include <algorithm>
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

std::array<Managed, 19> managedKeys()
{
    return {{
        {SettingsManager::kTheme, QStringLiteral("system")},
        // 0 = "auto" (leave the environment untouched); >0 pins OMP_NUM_THREADS.
        // Default 1: the engines parallelize over MPI ranks, and a threaded
        // BLAS underneath an MPI decomposition fights it for the same cores.
        {SettingsManager::kOmpThreads, 1},
        {SettingsManager::kCondaDir, QString()},
        // Empty means "use the shipped default", which is resolved at read
        // time rather than baked in here: the home directory is not knowable
        // when this table is written, and storing a resolved absolute path
        // would follow a settings.json copied to another machine or another
        // user and point at a home that is not theirs.
        {SettingsManager::kSimulationsDir, QString()},
        {SettingsManager::kEnvironmentPath, QString()},
        // Per-calculator env presets, stored as a JSON-object string.
        {SettingsManager::kEnvironmentPresets, QString()},
        // Per-calculator launch command templates (Preferences → "Run"), also
        // a JSON-object string. Empty = every engine uses its shipped default.
        {SettingsManager::kRunCommands, QString()},
        // Half the machine's cores: enough parallelism to be useful out of the
        // box without claiming a workstation that is running anything else.
        {SettingsManager::kRunCores,
         std::max(1, QThread::idealThreadCount() / 2)},
        // Exposed as a top-level `show_welcome_screen` boolean in settings.json.
        {SettingsManager::kShowWelcome, true, "show_welcome_screen"},
        {SettingsManager::kEnvFilePath, QString()},
        // External libraries installed on the machine (Preferences ->
        // "External Files"). All empty by default: an unset path means "do not
        // touch the environment", and guessing a location would be worse than
        // asking — a wrong POTCAR set produces a plausible number, not an
        // error.
        {SettingsManager::kPseudopotentialsVasp, QString()},
        {SettingsManager::kPseudopotentialsEspresso, QString()},
        {SettingsManager::kPseudopotentialsSiesta, QString()},
        {SettingsManager::kMlPotentialsDir, QString()},
        {SettingsManager::kMaterialsProjectApiKey, QString()},
        // Encoded camera state restored by "Reset camera"; empty = auto-frame.
        {SettingsManager::kDefaultPointOfView, QString()},
        // Which shader profile draws each class of geometry (Preferences →
        // "Rendering"). Stored by id and validated against the driver on every
        // read — a settings file is portable between machines, and the one
        // that has to run it is not the one that wrote it.
        //
        // Spelled literally rather than pulled from render::ShaderRegistry:
        // mirroring settings to JSON is not a rendering concern, and taking
        // the keys from there would drag QOpenGLContext into every target that
        // merely wants to read a preference. ShaderRegistry::settingsKey()
        // remains the single source of truth for the renderer and the
        // Preferences tab; these three strings must match it, and the
        // settings_shader_keys test asserts that they do.
        // Impostors are OPT-IN while the profile is under review: it changes
        // how every atom and bond is rasterized, and a rendering change of
        // that size should be a decision rather than a surprise after an
        // update. Switch these to "impostor" to make them the shipped default.
        {"render/atomShaderProfile", QStringLiteral("legacy")},
        {"render/bondShaderProfile", QStringLiteral("legacy")},
        {"render/isosurfaceShaderProfile", QStringLiteral("lit")},
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
    // CALANGO_CONFIG_DIR redirects every config file this class (and its
    // neighbours, e.g. CalculatorParameters) reads or writes. It exists for
    // test isolation: the unit tests construct real dialogs whose code paths
    // call save(), and before this override a `ctest` run silently rewrote
    // the developer's own ~/.calango/settings.json with whatever the test
    // QSettings happened to hold. CMake points every test at a scratch
    // directory inside the build tree; a user never needs to set this.
    const QByteArray override = qgetenv("CALANGO_CONFIG_DIR");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
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

QString SettingsManager::defaultSimulationsDirectory()
{
    // QDir::homePath() rather than a platform switch: it is the user's home on
    // both targets, and the folder name is deliberately plain so it reads the
    // same in Finder and in a Linux file manager.
    return QDir::homePath() + QStringLiteral("/My Simulations");
}

QString SettingsManager::mlPotentialsStartPath(const QString& currentValue)
{
    if (!currentValue.trimmed().isEmpty())
        return currentValue.trimmed();
    const QString configured =
        QSettings().value(QLatin1String(kMlPotentialsDir)).toString().trimmed();
    // Checked rather than trusted: the value is hand-editable and portable
    // between machines, and handing a non-existent path to a file dialog puts
    // the user somewhere arbitrary rather than where they asked.
    if (!configured.isEmpty() && QFileInfo(configured).isDir())
        return configured;
    return {};
}

QStringList SettingsManager::mlModelFiles()
{
    const QString directory = mlPotentialsStartPath();
    if (directory.isEmpty())
        return {};
    QStringList files;
    const QDir dir(directory);
    for (const QFileInfo& info :
         dir.entryInfoList({QStringLiteral("*.model"), QStringLiteral("*.pt"),
                            QStringLiteral("*.pth"), QStringLiteral("*.pb")},
                           QDir::Files | QDir::Readable, QDir::Name))
        files.append(info.absoluteFilePath());
    return files;
}

QString SettingsManager::simulationsDirectory()
{
    QString configured =
        QSettings().value(QLatin1String(kSimulationsDir)).toString().trimmed();
    if (configured.isEmpty())
        configured = defaultSimulationsDirectory();

    // Usable means: it exists as a directory (or can be created) and is
    // writable. Checked rather than assumed, because the failure mode of not
    // checking is a run that produces no output and reports no reason.
    const QFileInfo info(configured);
    if (info.exists() && !info.isDir()) {
        qWarning("Calango: the configured simulations directory %s is a file, "
                 "not a directory; falling back to the application data store.",
                 qPrintable(configured));
    } else if (QDir().mkpath(configured)
               && QFileInfo(configured).isWritable()) {
        return configured;
    } else {
        qWarning("Calango: could not use the simulations directory %s "
                 "(not creatable or not writable); falling back to the "
                 "application data store.",
                 qPrintable(configured));
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/jobs");
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
