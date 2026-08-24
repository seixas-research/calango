#pragma once

#include "core/Structure.hpp"

#include <QDialog>
#include <QString>

#include <map>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace calango::gui {

class MoleculeCanvas;

/// "Molecular Design" — a 2D molecular sketcher in the ChemDraw idiom, and the
/// pipeline that turns what is drawn into a real 3D structure.
///
/// THREE ZONES, arranged as the reference screenshot has them:
///
///   * a LEFT SIDEBAR of drawing tools, as a narrow multi-column grid of
///     icon-only buttons — bonds and stereo bonds, the atom/caption text
///     tools, charge, eraser, selection, chain, and the ring-template palette;
///   * the CANVAS in the centre, which is the star and gets every pixel the
///     other two do not need;
///   * a RIGHT SIDEBAR, deliberately compact, holding the two outputs ("Send
///     to 3D Viewport", "Export image…") and the appearance controls.
///
/// MODELESS AND SINGLE-INSTANCE, like the Point of View dialog: the whole
/// point is to have the 3D viewport visible beside the sketch, and a second
/// copy would fight the first over the same drawing. MainWindow keeps the
/// pointer and re-shows it, so a session's sketch survives closing the window.
class MolecularDesignDialog : public QDialog {
    Q_OBJECT

public:
    explicit MolecularDesignDialog(QWidget* parent = nullptr);

    /// The canvas, for tests and for the host window.
    MoleculeCanvas* canvas() const { return canvas_; }

    /// Run the 2D -> 3D pipeline and emit structureReady(). Exposed so the
    /// end-to-end test can drive exactly the code path the button does,
    /// rather than a re-implementation of it. Returns false (having reported
    /// why on the status line) when there is nothing to send.
    bool sendToViewport();

    /// Load a SMILES string into the canvas as one undo step, exactly as the
    /// SMILES field does. Returns false and leaves the canvas untouched on a
    /// parse error, with the explanation on the status line.
    bool loadSmiles(const QString& text);

Q_SIGNALS:
    /// A finished 3D structure, ready for a new viewport tab, and the name
    /// that tab should carry (the molecular formula).
    void structureReady(std::shared_ptr<core::Structure> structure,
                        const QString& name);

protected:
    /// Re-tint the ring palette when the application theme changes.
    ///
    /// Every other icon here goes through IconManager::bind(), which keeps a
    /// (widget, name) pair and re-tints on a theme switch. A QComboBox ITEM is
    /// not a widget, so its icon can only come from IconManager::icon(), which
    /// bakes the tint that was current when it was called — the white-on-white
    /// bug IconManager.hpp warns about, and the ring palette is the one place
    /// in this dialog exposed to it.
    ///
    /// Only the APPLICATION-level palette/theme events are acted on, for the
    /// reason IconManager's own watcher gives: setting an icon restyles the
    /// widget, and Qt reports that as a per-widget PaletteChange, so reacting
    /// to that one closes an infinite refresh loop.
    void changeEvent(QEvent* event) override;

private Q_SLOTS:
    void exportImage();
    void refreshReadouts();

private:
    QWidget* buildToolSidebar();
    QWidget* buildOutputSidebar();
    /// One icon-only tool button, registered in the exclusive tool group.
    QToolButton* addToolButton(QGridLayout* grid, int row, int column,
                               const QString& icon, const QString& tip,
                               int toolId);
    void applyShortcuts();
    void selectTool(int toolId);
    /// (Re)build the ring combo's item icons against the current theme.
    void refreshRingIcons();

    MoleculeCanvas* canvas_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* formula_ = nullptr;
    QLineEdit* smilesEdit_ = nullptr;
    QToolButton* undoButton_ = nullptr;
    QToolButton* redoButton_ = nullptr;
    QComboBox* ringCombo_ = nullptr;
    QToolButton* elementButton_ = nullptr;
    QDoubleSpinBox* lineWidthBox_ = nullptr;
    QSpinBox* fontSizeBox_ = nullptr;
    QCheckBox* elementColorsBox_ = nullptr;
    QCheckBox* followThemeBox_ = nullptr;
    QCheckBox* aromaticBox_ = nullptr;
    QPushButton* aromaticColorButton_ = nullptr;
    QCheckBox* transparentBox_ = nullptr;
    QCheckBox* addHydrogensBox_ = nullptr;
    QCheckBox* relaxBox_ = nullptr;

    /// Tool id (the MoleculeCanvas::Tool enum, as an int so the header need
    /// not include the canvas) -> its button, for the keyboard shortcuts and
    /// for keeping the group exclusive.
    std::map<int, QToolButton*> toolButtons_;
};

} // namespace calango::gui
