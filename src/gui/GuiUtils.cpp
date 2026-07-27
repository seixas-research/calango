#include "gui/GuiUtils.hpp"

#include "core/Element.hpp"
#include "core/Structure.hpp"

#include <cmath>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFontMetricsF>
#include <QPainter>
#include <QRectF>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTextStream>
#include <QWidget>

#include <algorithm>
#include <set>
#include <vector>

namespace calango::gui {

QStringList structureElements(const core::Structure* structure)
{
    if (!structure)
        return {};
    // std::set rather than sorting a list afterwards: it de-duplicates and
    // orders in one pass, and the counts here are tens of species at most.
    std::set<QString> symbols;
    for (const core::Atom& atom : structure->atoms())
        symbols.insert(QString::fromLatin1(atom.symbol()));
    QStringList result;
    result.reserve(static_cast<qsizetype>(symbols.size()));
    for (const QString& symbol : symbols)
        result << symbol;
    return result;
}

bool writeTextFile(QWidget* parent, const QString& path,
                   const std::function<void(QTextStream&)>& body)
{
    // QSaveFile, not QFile: an export that fails halfway through must not
    // leave a truncated file where the previous good one was.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(
            parent, parent ? parent->windowTitle() : QString(),
            QCoreApplication::translate("calango::gui",
                                        "Could not write %1")
                .arg(path));
        return false;
    }
    {
        QTextStream out(&file);
        body(out);
    }
    if (!file.commit()) {
        QMessageBox::warning(
            parent, parent ? parent->windowTitle() : QString(),
            QCoreApplication::translate("calango::gui",
                                        "Could not write %1")
                .arg(path));
        return false;
    }
    return true;
}

bool writeTextFile(QWidget* parent, const QString& path, const QString& body)
{
    return writeTextFile(parent, path,
                         [&body](QTextStream& out) { out << body; });
}


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

QColor cpkColor(int atomicNumber)
{
    const calango::core::ElementData& data =
        calango::core::Elements::data(atomicNumber);
    return {data.rgb[0], data.rgb[1], data.rgb[2]};
}

QColor readableTextColor(const QColor& background)
{
    // WCAG relative luminance, then simply take whichever of black and white
    // CONTRASTS MORE. No threshold to tune, and provably the better of the two
    // choices for every possible background.
    //
    // A luma threshold was tried first and is not good enough here: Rec. 601
    // weights green at 0.587, so a saturated bright green like thulium's
    // #00D452 scores below any sensible cut-off and gets white text at a 1.99
    // contrast ratio — illegible. Measured over all 119 CPK colours, choosing
    // by contrast lifts the worst case to 4.12 and puts every element above
    // the 3.0 WCAG AA bar for large/bold text (which is what these 36x32 bold
    // swatch buttons are).
    const auto channel = [](double c) {
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const auto relativeLuminance = [&channel](const QColor& c) {
        return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF())
            + 0.0722 * channel(c.blueF());
    };
    const auto contrast = [](double a, double b) {
        return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
    };

    // Not pure black: #202020 matches the text colour used elsewhere in the
    // widget layer and is only a hair less contrasty.
    const QColor dark(0x20, 0x20, 0x20);
    const QColor light(0xFF, 0xFF, 0xFF);
    const double luminance = relativeLuminance(background);
    return contrast(relativeLuminance(dark), luminance)
            >= contrast(relativeLuminance(light), luminance)
        ? dark
        : light;
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


// ---------------------------------------------------------------------------
// CompactDoubleSpinBox
// ---------------------------------------------------------------------------

CompactDoubleSpinBox::CompactDoubleSpinBox(QWidget* parent)
    : QDoubleSpinBox(parent)
{
    // The base class rounds the STORED value to `decimals` places, so a low
    // setting would quantize the value itself and not merely its display. Keep
    // full precision internally and do all the shortening in textFromValue().
    setDecimals(12);
}

QString CompactDoubleSpinBox::textFromValue(double value) const
{
    if (value == 0.0)
        return QStringLiteral("0");
    const double magnitude = std::abs(value);
    // Outside this window a fixed-point rendering is either all leading zeros
    // or unreadably long, so switch to exponential.
    if (magnitude < 1e-3 || magnitude >= 1e5)
        return QString::number(value, 'e', 2); // 1.23e-02
    // Three significant figures: 'g' picks the shorter of fixed/exponential and
    // drops trailing zeros, which is exactly the compact form wanted here.
    return QString::number(value, 'g', 3);
}

double CompactDoubleSpinBox::valueFromText(const QString& text) const
{
    QString cleaned = text;
    cleaned.remove(prefix()).remove(suffix());
    bool ok = false;
    const double parsed = cleaned.trimmed().toDouble(&ok);
    return ok ? parsed : value();
}

QValidator::State CompactDoubleSpinBox::validate(QString& input, int& pos) const
{
    Q_UNUSED(pos);
    QString cleaned = input;
    cleaned.remove(prefix()).remove(suffix());
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty() || cleaned == QLatin1String("-")
        || cleaned == QLatin1String("+"))
        return QValidator::Intermediate;
    bool ok = false;
    const double parsed = cleaned.toDouble(&ok);
    if (!ok) {
        // A half-typed exponent ("1.2e", "1e-") is not a number yet but is on
        // its way to one; rejecting it would block the keystroke.
        return cleaned.endsWith(QLatin1Char('e'), Qt::CaseInsensitive)
                || cleaned.endsWith(QLatin1String("e-"), Qt::CaseInsensitive)
                || cleaned.endsWith(QLatin1String("e+"), Qt::CaseInsensitive)
            ? QValidator::Intermediate
            : QValidator::Invalid;
    }
    return parsed < minimum() || parsed > maximum() ? QValidator::Intermediate
                                                    : QValidator::Acceptable;
}

} // namespace calango::gui
