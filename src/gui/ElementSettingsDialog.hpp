#pragma once

#include <QDialog>

class QTableWidget;

namespace calango::gui {

class ViewportWidget;

/// Per-element visual settings: color override (QColorDialog) and an
/// individual sphere-radius scale for each chemical element — e.g. shrink
/// only hydrogens or recolor only carbon. Edits apply immediately;
/// per-row and global reset restore the Jmol CPK palette and 100% radius.
///
/// The whole override set can be saved to and loaded from a JSON preset, so a
/// house style (journal palette, presentation colors) survives across sessions
/// and structures and can be shared with collaborators. Only the OVERRIDES are
/// written — an element left at its CPK default is absent from the file, so a
/// preset stays a diff against the built-in palette rather than a frozen copy
/// of it.
class ElementSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ElementSettingsDialog(ViewportWidget* viewport, QWidget* parent = nullptr);

private:
    void populate();
    void editColor(int atomicNumber);
    void resetElement(int atomicNumber);
    void resetAll();
    void loadPresets();
    void savePresets();

    ViewportWidget* viewport_;
    QTableWidget* table_;
};

} // namespace calango::gui
