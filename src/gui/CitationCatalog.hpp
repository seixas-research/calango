#pragma once

#include <QString>

#include <vector>

namespace calango::gui {

/// One work Calango's own functionality draws on, in whatever form the
/// About dialog's Citations tab and its BibTeX Viewer both read from.
///
/// This is the single table the task that added it was written to leave
/// behind: every reference lives here, once, as data — adding one later
/// means appending an entry, not touching CitationsPanel.cpp or
/// BibtexViewerDialog.cpp. The category groups the tab's headers; the
/// running reference NUMBER is the entry's position in the list, computed
/// where it is displayed rather than stored here, so inserting a new
/// citation never means renumbering the ones after it by hand.
struct Citation {
    /// "Core", "Databases", "Calculators" or "Libraries" — the four section
    /// headers the Citations tab groups entries under, in that order.
    QString category;
    /// The Nature-format reference, as HTML: authors as "Surname, F. M.",
    /// the journal (or conference proceedings) name in <i>italics</i>,
    /// abbreviated per convention, volume in <b>bold</b>, pages or an
    /// article number, and the year in parentheses. Rendered by a QLabel in
    /// RichText mode, matching every other formatted label in this dialog.
    QString displayHtml;
    /// A short parenthetical shown under the reference in a muted colour —
    /// "cite together with ASE, above", "for the default GFN2-xTB method"
    /// — for the handful of entries whose applicability is conditional or
    /// tied to another entry. Empty for a reference that always applies.
    QString note;
    /// A complete, ready-to-paste BibTeX entry: correct @article / @inproceedings
    /// / @misc / @software type, a citekey unique across this table, and
    /// every field the Nature-format reference above states.
    QString bibtex;
    /// The BibTeX citekey, reused as the suggested file stem when exporting
    /// the entry as its own .bib file.
    QString citekey;
};

/// Every citation the About dialog's Citations tab lists, in display order
/// (category, then within a category the order that reads best — e.g. a
/// database's two founding-then-progress papers stay adjacent).
///
/// Compiled by going through Calango's own integrations (ase.db-backed
/// database browsing, the calculators/engines the wizards generate scripts
/// for, and the handful of Python libraries a materials paper customarily
/// cites) and checking EACH entry's current canonical citation against the
/// project's own "how to cite" guidance rather than trusting an older paper
/// from memory — several have changed in the last few years (Materials
/// Project's citation is no longer the 2013 APL Materials commentary;
/// phonopy's is no longer the 2015 Scripta Materialia paper; GPAW's is no
/// longer the 2005/2010 pair). See docs/sphinx/source/citing.md for the
/// verification notes this table was built from.
const std::vector<Citation>& citationCatalog();

} // namespace calango::gui
