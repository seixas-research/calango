#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>

#include <vector>

class QJsonArray;
class QPushButton;

namespace calango::gui {

/// Small helpers shared across the widget layer. Each of these existed as
/// an identical private copy in two or three files; they live here so a fix
/// (or a style change) lands everywhere at once.

/// Paint a color-swatch button — the "click to pick a color" buttons in the
/// Representation, Lighting and Unit Cell & Axes panels.
void setButtonColor(QPushButton* button, const QColor& color);

/// Parse a JSON object from `path`. Returns an empty object when the file
/// cannot be opened or does not parse; callers treat "empty" as "no data"
/// (the result viewers are opened against job directories that may legally
/// be missing a given artifact).
QJsonObject readJsonObject(const QString& path);

/// JSON number array -> std::vector<double>. Non-numeric entries become 0.0,
/// matching QJsonValue::toDouble()'s contract.
std::vector<double> toDoubleVector(const QJsonArray& array);

} // namespace calango::gui
