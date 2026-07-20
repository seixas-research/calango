#pragma once

#include <QDialog>

class QTableWidget;

namespace calango::gui {

class ViewportWidget;

/// Per-element visual settings: color override (QColorDialog) and an
/// individual sphere-radius scale for each chemical element — e.g. shrink
/// only hydrogens or recolor only carbon. Edits apply immediately;
/// per-row and global reset restore the Jmol CPK palette and 100% radius.
class ElementSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ElementSettingsDialog(ViewportWidget* viewport, QWidget* parent = nullptr);

private:
    void populate();
    void editColor(int atomicNumber);
    void resetElement(int atomicNumber);
    void resetAll();

    ViewportWidget* viewport_;
    QTableWidget* table_;
};

} // namespace calango::gui
