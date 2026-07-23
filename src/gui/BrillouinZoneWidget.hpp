#pragma once

#include "core/BrillouinZone.hpp"
#include "core/KPath.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QString>
#include <QWidget>

#include <vector>

class QListWidget;
class QSpinBox;

namespace calango::gui {

class BrillouinZoneView;

/// Self-contained, embeddable interactive Brillouin-zone k-path builder:
/// the 3D Wigner-Seitz cell with its high-symmetry points, the ordered
/// k-point sequence table, and the Suggested / Break / Undo / Remove / Clear
/// action bar.
///
/// This is the whole interaction, with no dialog around it — it is embedded
/// directly into Stage 1 of the Electronic Structure and Phonon wizards, and
/// hosted by BrillouinZoneDialog (which adds only the export actions and a
/// Close button). Extracting it is what removed the old
/// export-a-JSON-then-load-it-back round trip between the builder and the
/// wizards: the wizards now read pathString() straight off the widget as the
/// user edits it.
class BrillouinZoneWidget : public QWidget {
    Q_OBJECT

public:
    /// `compact` trims the chrome for embedding inside a wizard page: the
    /// viewport hint collapses to one line and the sequence table is shorter,
    /// so a whole wizard stage still fits without scrolling.
    BrillouinZoneWidget(const core::BrillouinZoneData& zone,
                        const pybridge::AseBridge::BandPathInfo& bandPath,
                        bool compact = false, QWidget* parent = nullptr);

    /// The built path as an ASE path string (concatenated high-symmetry
    /// labels, ',' between discontinuous sections, e.g. "GXWK,UX"). Empty
    /// when no path is defined.
    QString pathString() const;

    /// Ordered path entries: indices into specialPoints(), with -1 marking a
    /// section break. Used by the exporters.
    const std::vector<int>& path() const { return path_; }
    const std::vector<core::KPathPoint>& specialPoints() const
    {
        return specialPoints_;
    }
    const core::BrillouinZoneData& zone() const { return zone_; }

    /// Continuous sections of the current path (breaks split them; sections
    /// with fewer than two points are kept for display but skipped by
    /// exporters).
    core::KPathSegments segments() const;
    bool hasExportablePath() const;

    /// Sampling density along each segment, shared with the exporters.
    int pointsPerSegment() const;

    /// Replace the path from an ASE path string, so a wizard can seed the
    /// widget with a previously chosen path (project reload, Back navigation).
    void setPathString(const QString& path);

    /// The embedded 3D view, for callers that need to restyle or capture it
    /// (the dialog's appearance and figure-export actions).
    BrillouinZoneView* view() const { return view_; }

Q_SIGNALS:
    /// The path changed — by a click in the 3D view or any action button.
    /// Wizards connect this to refresh their generated script live.
    void pathChanged();

private Q_SLOTS:
    void appendPoint(int index);
    void addBreak();
    void undoLastPoint();
    void removeSelectedPoint();
    void clearPath();
    void useSuggestedPath();

private:
    void syncPathViews();

    core::BrillouinZoneData zone_;
    std::vector<core::KPathPoint> specialPoints_;
    QString suggestedPath_;
    std::vector<int> path_; ///< indices into specialPoints_; -1 = break

    BrillouinZoneView* view_;
    QListWidget* pathList_;
    QSpinBox* divisionsSpin_;
};

} // namespace calango::gui
