#pragma once

#include <QWidget>

namespace calango::gui {

class ViewportWidget;

/// "Unit Cell & Axes" dock panel: cell wireframe visibility, color and
/// line width (widths > 1 render lit tubes), axes triad visibility, style
/// (Cartesian vs. lattice vectors) and on-screen size.
class CellAxesPanel : public QWidget {
    Q_OBJECT

public:
    explicit CellAxesPanel(ViewportWidget* viewport, QWidget* parent = nullptr);

private:
    ViewportWidget* viewport_;
};

} // namespace calango::gui
