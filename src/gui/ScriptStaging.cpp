#include "gui/ScriptStaging.hpp"

#include <QFile>
#include <QTextStream>

namespace calango::gui {

bool writeScript(const QString& scriptPath, const QString& text, QString* error)
{
    QFile file(scriptPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(scriptPath, file.errorString());
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    stream.flush();
    file.close();
    if (file.error() != QFile::NoError) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(scriptPath, file.errorString());
        return false;
    }
    return true;
}

} // namespace calango::gui
