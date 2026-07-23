#include "gui/ScriptStaging.hpp"

#include "core/AseScriptGenerator.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace calango::gui {

namespace {

/// Write `text` to `path`, reporting short-write / flush failures too — a
/// truncated Python file fails at run time with a confusing SyntaxError
/// rather than an I/O message.
bool writeTextFile(const QString& path, const QString& text, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    stream.flush();
    file.close();
    if (file.error() != QFile::NoError) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace

bool writeLoggerModule(const QString& directory)
{
    const QString path =
        QDir(directory).filePath(
            QLatin1String(core::AseScriptGenerator::loggerModuleFileName()));
    return writeTextFile(
        path, QString::fromStdString(core::AseScriptGenerator::loggerModuleSource()),
        nullptr);
}

bool writeScriptWithLogger(const QString& scriptPath, const QString& text,
                           QString* error)
{
    if (!writeTextFile(scriptPath, text, error))
        return false;
    const QString directory = QFileInfo(scriptPath).absolutePath();
    if (!writeLoggerModule(directory)) {
        if (error) {
            *error = QStringLiteral(
                         "The script was saved, but its %1 helper module could "
                         "not be written to %2 — the script will fail on "
                         "`from calango_log import CalangoLog` until you copy "
                         "the module there.")
                         .arg(QLatin1String(
                                  core::AseScriptGenerator::loggerModuleFileName()),
                              directory);
        }
        return false;
    }
    return true;
}

} // namespace calango::gui
