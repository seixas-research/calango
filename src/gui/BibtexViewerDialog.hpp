#pragma once

#include <QDialog>
#include <QString>

namespace calango::gui {

/// Shows one BibTeX entry as it should appear in a .bib file — a read-only
/// monospaced view, a Copy button, and an Export... button that saves it as
/// its own .bib file.
///
/// One dialog CLASS reused for every citation in the About dialog's
/// Citations tab, parameterized by the entry it is asked to show, rather
/// than a dialog subclass per citation — the "BibTeX viewer…" button beside
/// each reference constructs one of these, exec()s it, and lets it go.
class BibtexViewerDialog : public QDialog {
    Q_OBJECT

public:
    /// `bibtex` is the complete entry text; `citekey` names the suggested
    /// .bib file stem for Export... (and appears in the window title).
    explicit BibtexViewerDialog(const QString& bibtex, const QString& citekey,
                                QWidget* parent = nullptr);

private:
    QString bibtex_;
    QString citekey_;
};

} // namespace calango::gui
