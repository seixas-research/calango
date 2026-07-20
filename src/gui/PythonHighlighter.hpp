#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <vector>

namespace calango::gui {

/// Lightweight Python syntax highlighting for the editable ASE script
/// pane: keywords, strings (incl. triple-quoted across blocks), comments,
/// numbers and decorators. Colors are mid-tone so they read on both light
/// and dark palettes.
class PythonHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit PythonHighlighter(QTextDocument* document);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    std::vector<Rule> rules_;
    QRegularExpression tripleQuote_;
    QTextCharFormat stringFormat_;
};

} // namespace calango::gui
