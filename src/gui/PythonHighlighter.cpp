#include "gui/PythonHighlighter.hpp"

namespace calango::gui {

PythonHighlighter::PythonHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document)
    , tripleQuote_(QStringLiteral("\"\"\"|'''"))
{
    const auto makeFormat = [](const QColor& color, bool bold = false, bool italic = false) {
        QTextCharFormat format;
        format.setForeground(color);
        if (bold)
            format.setFontWeight(QFont::Bold);
        format.setFontItalic(italic);
        return format;
    };

    QTextCharFormat keywordFormat = makeFormat(QColor(0x35, 0x7A, 0xC9), true);
    rules_.push_back(
        {QRegularExpression(QStringLiteral(
             R"(\b(and|as|assert|break|class|continue|def|del|elif|else|except|)"
             R"(finally|for|from|global|if|import|in|is|lambda|None|nonlocal|)"
             R"(not|or|pass|raise|return|True|False|try|while|with|yield)\b)")),
         keywordFormat});

    rules_.push_back({QRegularExpression(QStringLiteral(R"(\b\d+(\.\d*)?([eE][+-]?\d+)?\b)")),
                      makeFormat(QColor(0x9C, 0x63, 0xB5))});

    rules_.push_back({QRegularExpression(QStringLiteral(R"(^\s*@\w+)")),
                      makeFormat(QColor(0xB0, 0x7D, 0x2A))});

    stringFormat_ = makeFormat(QColor(0x2E, 0x8B, 0x57));
    rules_.push_back(
        {QRegularExpression(QStringLiteral(R"([rRbBfFuU]{0,2}"[^"\\]*(\\.[^"\\]*)*")")),
         stringFormat_});
    rules_.push_back(
        {QRegularExpression(QStringLiteral(R"([rRbBfFuU]{0,2}'[^'\\]*(\\.[^'\\]*)*')")),
         stringFormat_});

    rules_.push_back({QRegularExpression(QStringLiteral("#[^\n]*")),
                      makeFormat(QColor(0x80, 0x80, 0x80), false, true)});
}

void PythonHighlighter::highlightBlock(const QString& text)
{
    for (const Rule& rule : rules_) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            setFormat(static_cast<int>(match.capturedStart()),
                      static_cast<int>(match.capturedLength()), rule.format);
        }
    }

    // Triple-quoted strings spanning multiple blocks (state 1 = inside).
    setCurrentBlockState(0);
    bool insideString = previousBlockState() == 1;
    int start = 0;
    if (!insideString) {
        const auto match = tripleQuote_.match(text);
        start = match.hasMatch() ? static_cast<int>(match.capturedStart()) : -1;
    }
    while (start >= 0) {
        // When continuing from the previous block there is no opening
        // delimiter in this block — search for the closer from `start`.
        const auto end = tripleQuote_.match(text, insideString ? start : start + 3);
        insideString = false;
        if (!end.hasMatch()) {
            setCurrentBlockState(1);
            setFormat(start, static_cast<int>(text.length()) - start, stringFormat_);
            break;
        }
        setFormat(start, static_cast<int>(end.capturedEnd()) - start, stringFormat_);
        const auto next = tripleQuote_.match(text, end.capturedEnd());
        start = next.hasMatch() ? static_cast<int>(next.capturedStart()) : -1;
    }
}

} // namespace calango::gui
