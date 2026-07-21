#pragma once

#include <QMap>
#include <QString>

namespace calango::gui {

/// Minimal .env parser: `KEY=VALUE` lines, `#` comments, optional
/// `export ` prefixes, single/double quotes stripped. Returns an empty
/// map when the file is missing or unreadable.
QMap<QString, QString> parseEnvFile(const QString& path);

/// The .env file Calango reads at launch: the path stored in settings
/// ("config/envFilePath"), defaulting to ~/.env.
QString envFilePath();
void setEnvFilePath(const QString& path);

/// Loads the configured .env file and publishes MP_API_KEY into the
/// process environment. With `overrideExisting` false (startup), a value
/// already present in the shell environment wins. Returns true if the
/// key is now available.
bool loadEnvironmentFile(bool overrideExisting = false);

} // namespace calango::gui
