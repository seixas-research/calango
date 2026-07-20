#pragma once

#include <QDialog>

class QTableWidget;

namespace calango::gui {

class ViewportWidget;

/// "Atom Color Editor": per-element overrides of the default CPK palette,
/// picked via QColorDialog. Edits apply immediately to the viewport;
/// per-row and global reset restore Jmol CPK defaults.
class AtomColorDialog : public QDialog {
    Q_OBJECT

public:
    explicit AtomColorDialog(ViewportWidget* viewport, QWidget* parent = nullptr);

private:
    void populate();
    void editColor(int atomicNumber);
    void resetColor(int atomicNumber);
    void resetAll();

    ViewportWidget* viewport_;
    QTableWidget* table_;
};

} // namespace calango::gui
