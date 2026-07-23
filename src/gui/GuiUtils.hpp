#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>

#include <vector>

class QJsonArray;
class QPainter;
class QPushButton;
class QRectF;

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

/// Draw `text` centered in `box`, rendering "_x" (or "_{xy}") as a
/// typographic subscript — smaller font, dropped baseline. Qt ships no LaTeX
/// engine and this project carries no QCustomPlot/MathJax dependency, so this
/// two-run layout is what actually produces "E − E_F (eV)" with a real
/// subscript rather than a literal underscore. Shared by the band/PDOS plot
/// and the effective-band heatmap.
void drawWithSubscripts(QPainter& painter, const QRectF& box,
                        const QString& text);

/// JSON number array -> std::vector<double>. Non-numeric entries become 0.0,
/// matching QJsonValue::toDouble()'s contract.
std::vector<double> toDoubleVector(const QJsonArray& array);

} // namespace calango::gui
