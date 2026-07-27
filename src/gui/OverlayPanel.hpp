#pragma once

#include "gui/OverlayModel.hpp"

#include <QWidget>

#include <vector>

class QListWidget;
class QPushButton;

namespace calango::gui {

class ViewportWidget;

/// "Additional overlays" dock: one list of everything drawn over the structure
/// that is not the structure — lattice planes, text annotations and geometric
/// primitives — with Add / Remove / Edit / Reset beneath it.
///
/// It replaces two modeless dialogs (Lattice Plane Settings and the Custom
/// Overlay Manager). Those each kept a private list and a private viewport
/// channel, so a scene with a plane and a labelled sphere meant two floating
/// windows, no single place showing what was on screen, and no way to reorder
/// or temporarily hide one thing without hunting for the window that owned it.
///
/// The panel is the single writer of the viewport's MANAGED overlay channel;
/// it deliberately does not touch setCustomOverlay(), which belongs to the
/// Volumetric Data panel and the MLWF viewer.
class OverlayPanel : public QWidget {
    Q_OBJECT

public:
    explicit OverlayPanel(ViewportWidget* viewport, QWidget* parent = nullptr);

    /// Re-push everything. Called when the structure changes, since a lattice
    /// plane is defined against the cell it is drawn in.
    void refresh();

private Q_SLOTS:
    void addOverlay();
    void removeOverlay();
    void editOverlay();
    void resetOverlays();
    void onSelectionChanged();
    /// A text overlay was dragged in the viewport; write the new anchor back.
    void onTextOverlayMoved(int id, const core::Vec3& position);

private:
    /// Rebuild the list widget from `overlays_`, keeping the selection.
    void refreshList();
    /// Tessellate every overlay and push both the geometry and the text set.
    void pushToViewport();
    /// Run the editor over `overlay`, applying live and restoring on cancel.
    bool editInDialog(Overlay& overlay, const QString& title);
    Overlay* current();

    ViewportWidget* viewport_;
    std::vector<Overlay> overlays_;
    /// Monotonic, never reused: the viewport reports drags by id, and a
    /// recycled id would move the wrong overlay after a delete.
    int nextId_ = 1;

    QListWidget* list_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* editButton_ = nullptr;
    QPushButton* resetButton_ = nullptr;
};

} // namespace calango::gui
