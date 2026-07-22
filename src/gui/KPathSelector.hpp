#pragma once

#include <QWidget>

#include <memory>

class QLineEdit;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Reusable k-path definition control: an editable high-symmetry path field
/// pre-filled with ASE's suggested path plus a "Load k-Path File
/// (kpath.json)…" button that reads a Calango kpath.json (exported from the
/// Brillouin Zone Builder) and loads its high-symmetry path into the field.
/// Shared by the Electronic Structure and Phonon wizards so both offer an
/// identical workflow.
class KPathSelector : public QWidget {
    Q_OBJECT

public:
    explicit KPathSelector(std::shared_ptr<const core::Structure> structure,
                           QWidget* parent = nullptr);

    /// The current path string (concatenated labels, ',' between sections).
    QString path() const;

private Q_SLOTS:
    void loadKpathFile();

private:
    std::shared_ptr<const core::Structure> structure_;
    QLineEdit* pathEdit_;
};

} // namespace calango::gui
