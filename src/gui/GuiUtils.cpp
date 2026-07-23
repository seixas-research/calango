#include "gui/GuiUtils.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>

namespace calango::gui {

void setButtonColor(QPushButton* button, const QColor& color)
{
    button->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #666;")
            .arg(color.name()));
}

QJsonObject readJsonObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

std::vector<double> toDoubleVector(const QJsonArray& array)
{
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array)
        values.push_back(value.toDouble());
    return values;
}

} // namespace calango::gui
