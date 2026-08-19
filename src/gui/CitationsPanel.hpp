#pragma once

#include <QWidget>

namespace calango::gui {

/// The About dialog's "Citations" tab: every work Calango's own
/// functionality draws on (see CitationCatalog.hpp for the data table this
/// reads), in one continuously-numbered Nature-format list grouped under
/// small category headers — Core, Databases, Calculators, Libraries.
///
/// Each reference carries a "BibTeX viewer…" button that opens a
/// BibtexViewerDialog parameterized with that one entry; the panel itself
/// holds no citation text of its own; it is a client of citationCatalog().
class CitationsPanel : public QWidget {
    Q_OBJECT

public:
    explicit CitationsPanel(QWidget* parent = nullptr);
};

} // namespace calango::gui
