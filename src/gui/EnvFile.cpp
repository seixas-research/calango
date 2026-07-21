#include "gui/EnvFile.hpp"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTextStream>

namespace calango::gui {

namespace {
const auto kEnvPathSetting = QStringLiteral("config/envFilePath");
}

QMap<QString, QString> parseEnvFile(const QString& path)
{
    QMap<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return values;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1String("export ")))
            line = line.mid(7).trimmed();
        const auto equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2
            && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
                || (value.startsWith(QLatin1Char('\''))
                    && value.endsWith(QLatin1Char('\'')))))
            value = value.mid(1, value.size() - 2);
        values.insert(key, value);
    }
    return values;
}

QString envFilePath()
{
    const QString stored = QSettings().value(kEnvPathSetting).toString();
    return stored.isEmpty() ? QDir::homePath() + QStringLiteral("/.env") : stored;
}

void setEnvFilePath(const QString& path)
{
    QSettings().setValue(kEnvPathSetting, path);
}

bool loadEnvironmentFile(bool overrideExisting)
{
    const auto key = QByteArrayLiteral("MP_API_KEY");
    if (!overrideExisting && qEnvironmentVariableIsSet(key.constData())
        && !qEnvironmentVariable(key.constData()).isEmpty())
        return true; // the shell environment wins at startup

    const auto values = parseEnvFile(envFilePath());
    const auto it = values.constFind(QStringLiteral("MP_API_KEY"));
    if (it == values.constEnd() || it->isEmpty())
        return qEnvironmentVariableIsSet(key.constData());
    qputenv(key.constData(), it->toUtf8());
    return true;
}

} // namespace calango::gui
