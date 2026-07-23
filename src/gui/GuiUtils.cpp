#include "gui/GuiUtils.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFontMetricsF>
#include <QPainter>
#include <QRectF>
#include <QPushButton>

#include <algorithm>
#include <vector>

namespace calango::gui {

void setButtonColor(QPushButton* button, const QColor& color)
{
    button->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #666;")
            .arg(color.name()));
}

/// Draw `text` centered in `box`, rendering "_x" as a typographic subscript
/// (smaller font, dropped baseline). Qt ships no LaTeX engine and this
/// project has no QCustomPlot / MathJax dependency, so the two-run layout
/// below is what actually produces "E − E_F (eV)" with a proper subscript
/// instead of a literal underscore.
void drawWithSubscripts(QPainter& painter, const QRectF& box,
                        const QString& text)
{
    // Split into (run, isSubscript) pairs: "_" introduces a one-character
    // subscript, "_{...}" a braced multi-character one.
    struct Run {
        QString text;
        bool subscript;
    };
    std::vector<Run> runs;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('_') && i + 1 < text.size()) {
            if (text.at(i + 1) == QLatin1Char('{')) {
                const int close = text.indexOf(QLatin1Char('}'), i + 2);
                if (close > 0) {
                    runs.push_back({text.mid(i + 2, close - i - 2), true});
                    i = close;
                    continue;
                }
            }
            runs.push_back({text.mid(i + 1, 1), true});
            ++i;
            continue;
        }
        if (runs.empty() || runs.back().subscript)
            runs.push_back({QString(), false});
        runs.back().text.append(text.at(i));
    }

    const QFont baseFont = painter.font();
    QFont subFont = baseFont;
    subFont.setPointSizeF(std::max(baseFont.pointSizeF() * 0.72, 6.0));
    const QFontMetricsF baseMetrics(baseFont);
    const QFontMetricsF subMetrics(subFont);

    double width = 0.0;
    for (const Run& run : runs) {
        width += (run.subscript ? subMetrics : baseMetrics)
                     .horizontalAdvance(run.text);
    }

    double x = box.center().x() - width / 2.0;
    const double baseline = box.center().y() + baseMetrics.ascent() / 2.0
        - baseMetrics.descent() / 2.0;
    const double drop = baseMetrics.descent() * 0.75;
    for (const Run& run : runs) {
        painter.setFont(run.subscript ? subFont : baseFont);
        painter.drawText(QPointF(x, run.subscript ? baseline + drop : baseline),
                         run.text);
        x += (run.subscript ? subMetrics : baseMetrics)
                 .horizontalAdvance(run.text);
    }
    painter.setFont(baseFont);
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
