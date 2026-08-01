#pragma once

#include "core/Structure.hpp"

#include <QString>
#include <QWidget>

#include <memory>

class QLineEdit;

namespace calango::gui {

class BrillouinZoneWidget;

/// Stage-1 k-path editor for the Electronic Structure and Phonon wizards:
/// the interactive Brillouin-zone builder embedded directly in the page,
/// with a text mirror of the resulting ASE path string.
///
/// Replaces the old export-to-kpath.json / reopen-a-dialog round trip. The
/// path is read straight off the embedded widget via path(), and pathChanged()
/// fires as the user clicks, so the wizard's script preview stays live and
/// "Next ›" needs no intermediate file.
///
/// A structure with no unit cell (or one whose reciprocal cell cannot be
/// built) has no Brillouin zone; the editor then degrades to the plain text
/// field so the wizard remains usable.
class EmbeddedKPathEditor : public QWidget {
    Q_OBJECT

public:
    explicit EmbeddedKPathEditor(std::shared_ptr<const core::Structure> structure,
                                 QWidget* parent = nullptr);

    /// Current ASE path string ("GXWK,UX"). Empty means "use ASE's
    /// suggestion", which every generator already handles.
    QString path() const;

    /// Sampling density along each path segment, from the embedded builder.
    /// Falls back to a sane default when there is no 3D builder.
    int pointsPerSegment() const;
    /// Number of continuous segments in the current path (a path with a
    /// break counts each side separately). At least 1.
    int segmentCount() const;

Q_SIGNALS:
    void pathChanged();

private:
    BrillouinZoneWidget* zoneWidget_ = nullptr;
    QLineEdit* pathEdit_ = nullptr;
};

} // namespace calango::gui
