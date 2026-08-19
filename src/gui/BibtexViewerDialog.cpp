#include "gui/BibtexViewerDialog.hpp"

#include "gui/GuiUtils.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

BibtexViewerDialog::BibtexViewerDialog(const QString& bibtex,
                                       const QString& citekey, QWidget* parent)
    : QDialog(parent), bibtex_(bibtex), citekey_(citekey)
{
    setWindowTitle(tr("BibTeX Viewer — %1").arg(citekey));
    auto* layout = new QVBoxLayout(this);

    // Monospaced and unwrapped, exactly like the About dialog's own License
    // tab: a .bib entry's line breaks are part of the format (each field on
    // its own line), and a proportional, reflowed rendering would read as
    // something other than what Export... actually writes to disk.
    auto* view = new QPlainTextEdit(this);
    view->setReadOnly(true);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setPlainText(bibtex_);
    layout->addWidget(view, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* copyButton =
        buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    auto* exportButton =
        buttons->addButton(tr("Export…"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(copyButton, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(bibtex_); });
    connect(exportButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export BibTeX Entry"),
            citekey_ + QStringLiteral(".bib"), tr("BibTeX (*.bib)"));
        if (path.isEmpty())
            return;
        writeTextFile(this, path, bibtex_);
    });
    layout->addWidget(buttons);

    resize(560, 360);
}

} // namespace calango::gui
