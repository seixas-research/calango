#pragma once

#include <QColor>
#include <QDoubleSpinBox>
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

/// A QDoubleSpinBox that renders its value COMPACTLY: three significant
/// figures, switching to exponential notation ("1.23e-2") when a fixed-point
/// rendering would be misleading or too wide.
///
/// The physical properties the color-mapping bounds cover span many orders of
/// magnitude — partial charges around 1e-2 e, magnetic moments around 1 μB,
/// forces up to 1e2 eV/Å. A fixed `decimals` cannot serve them all: set it low
/// and small values collapse to "0.000"; set it high and large values overflow
/// the field. Formatting by significance instead keeps every value both
/// readable and honest at a fixed width.
class CompactDoubleSpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    explicit CompactDoubleSpinBox(QWidget* parent = nullptr);

protected:
    QString textFromValue(double value) const override;
    double valueFromText(const QString& text) const override;
    /// Accept exponential input, which the base class's fixed-notation
    /// validator rejects outright.
    QValidator::State validate(QString& input, int& pos) const override;
};

/// JSON number array -> std::vector<double>. Non-numeric entries become 0.0,
/// matching QJsonValue::toDouble()'s contract.
std::vector<double> toDoubleVector(const QJsonArray& array);

} // namespace calango::gui
