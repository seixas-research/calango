#include "gui/ScriptViewerDialog.hpp"

#include "gui/PythonHighlighter.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

ScriptViewerDialog::ScriptViewerDialog(const QString& title,
                                       const QString& script, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title.isEmpty() ? tr("ASE Script Viewer") : title);
    resize(720, 620);

    auto* layout = new QVBoxLayout(this);

    auto* caption = new QLabel(
        tr("The exact Python/ASE script this process executed (run.py):"),
        this);
    caption->setWordWrap(true);
    layout->addWidget(caption);

    auto* editor = new QPlainTextEdit(this);
    editor->setReadOnly(true);
    editor->setPlainText(script);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new PythonHighlighter(editor->document());
    layout->addWidget(editor, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* copyButton =
        buttons->addButton(tr("Copy to Clipboard"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(copyButton, &QPushButton::clicked, this, [script] {
        QApplication::clipboard()->setText(script);
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace calango::gui
