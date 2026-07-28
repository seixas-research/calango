#pragma once

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;

namespace calango::gui {

class ViewportWidget;

/// "Show Neighboring Cells" — the fractional-coordinate window of periodic
/// images drawn around the home cell, opened from the Spatial References dock's
/// "Unit cell" tab.
///
/// Six inputs in two columns, minima on the left and maxima on the right, one
/// row per axis: the shape the same setting has in every crystallography tool
/// that offers it, and the one that makes "widen x to 2" a single edit rather
/// than a hunt through a flat list of six spin boxes.
///
/// Modeless and live: every edit re-renders immediately, because the only way
/// to choose a window is to see the result. There is no OK/Cancel pair for the
/// same reason — "Reset" puts the default 0 → 1 back.
///
/// The dialog holds no state of its own. It reads and writes
/// render::Style::neighborCells through the viewport, so reopening it always
/// shows what is actually on screen.
class NeighborCellsDialog : public QDialog {
    Q_OBJECT

public:
    explicit NeighborCellsDialog(ViewportWidget* viewport,
                                 QWidget* parent = nullptr);

private:
    /// Push the six spin boxes and the check box into the viewport style and
    /// rebuild the geometry buffers.
    void apply();
    /// Refresh the summary line under the grid — how many cells the current
    /// window draws, which is the number that actually matters and the one a
    /// range of numbers does not state.
    void updateSummary();

    ViewportWidget* viewport_;
    QDoubleSpinBox* minSpin_[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* maxSpin_[3] = {nullptr, nullptr, nullptr};
    QCheckBox* edgesCheck_ = nullptr;
    QLabel* summary_ = nullptr;
    /// Set while apply() is repopulating the widgets, so the resulting
    /// valueChanged storm does not re-enter apply() once per spin box.
    bool updating_ = false;
};

} // namespace calango::gui
