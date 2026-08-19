#include "gui/CitationsPanel.hpp"

#include "gui/BibtexViewerDialog.hpp"
#include "gui/CitationCatalog.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace calango::gui {

CitationsPanel::CitationsPanel(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* intro = new QLabel(
        tr("Works Calango's own functionality draws on. A paper using it "
           "typically cites ASE, whichever database(s) and calculator(s) it "
           "actually queried or ran, and Calango itself — the libraries "
           "further down are customary to cite where the venue expects it, "
           "not specific to any one run."),
        this);
    intro->setWordWrap(true);
    intro->setContentsMargins(8, 8, 8, 4);
    outer->addWidget(intro);

    // A real widget tree, not one HTML blob like the Third-Party Licenses
    // tab's: each reference needs its own interactive "BibTeX viewer…"
    // button, which a single QLabel cannot host.
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setSpacing(10);

    const auto& catalog = citationCatalog();
    QString currentCategory;
    int number = 0;
    for (const Citation& citation : catalog) {
        // One header per category, printed the moment the category first
        // changes — the catalog's own entries are already grouped, so this
        // needs no second pass or lookup table.
        if (citation.category != currentCategory) {
            currentCategory = citation.category;
            auto* header = new QLabel(
                QStringLiteral("<h4>%1</h4>")
                    .arg(currentCategory.toHtmlEscaped()),
                content);
            layout->addWidget(header);
        }

        // The reference number is this entry's position in the catalog,
        // computed here rather than stored with the data — inserting a new
        // citation anywhere in citationCatalog() therefore never means
        // renumbering the ones after it by hand.
        ++number;
        auto* row = new QWidget(content);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        QString text =
            QStringLiteral("%1. %2").arg(number).arg(citation.displayHtml);
        if (!citation.note.isEmpty()) {
            // Not toHtmlEscaped(): the note is authored HTML, like
            // displayHtml above (it carries &mdash; and the like), not
            // arbitrary text — escaping it a second time is what turned
            // "&mdash;" into the literal, un-rendered string "&mdash;".
            //
            // No colour override: an earlier `color: palette(mid)` span
            // read as a SECOND, muted tone against the reference text above
            // it, which has none — the two are one continuous citation, not
            // a primary line and a de-emphasised caption, so the note reads
            // in the label's own default text colour like everything else
            // in it. Italic is enough to mark it as the secondary line.
            text += QStringLiteral("<br><i>%1</i>").arg(citation.note);
        }
        auto* label = new QLabel(text, row);
        label->setTextFormat(Qt::RichText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rowLayout->addWidget(label, 1);

        auto* bibtexButton = new QPushButton(tr("BibTeX viewer…"), row);
        bibtexButton->setToolTip(
            tr("Show this entry as a complete BibTeX record, with Copy and "
               "Export… actions."));
        rowLayout->addWidget(bibtexButton, 0, Qt::AlignTop);
        layout->addWidget(row);

        connect(bibtexButton, &QPushButton::clicked, this, [this, citation] {
            BibtexViewerDialog dialog(citation.bibtex, citation.citekey, this);
            dialog.exec();
        });
    }
    layout->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll, 1);
}

} // namespace calango::gui
