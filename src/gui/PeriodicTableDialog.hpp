#pragma once

#include <QDialog>

namespace calango::gui {

/// Graphical periodic table (Z = 1..118) with the standard 18-group
/// layout (lanthanide/actinide f-block rows below), colored by chemical
/// family. A single click selects the element and accepts the dialog.
class PeriodicTableDialog : public QDialog {
    Q_OBJECT

public:
    explicit PeriodicTableDialog(int currentZ = 0, QWidget* parent = nullptr);

    int selectedElement() const { return selectedZ_; }

    /// Convenience: open the dialog and return the chosen atomic number,
    /// or 0 when cancelled. `currentZ` is highlighted as the active element.
    static int pickElement(QWidget* parent, int currentZ = 0);

private:
    int selectedZ_ = 0;
};

} // namespace calango::gui
