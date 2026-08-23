#pragma once

#include "core/MoleculeGraph.hpp"

#include <QColor>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QWidget>

#include <deque>
#include <set>
#include <vector>

class QLineEdit;
class QPainter;

namespace calango::gui {

/// The 2D drawing surface of the Molecular Design dialog.
///
/// Owns the sketch (a core::MoleculeGraph), the view transform, the selection
/// and the undo stack; draws the structure with the standard chemical
/// conventions; and turns mouse and key events into graph edits. Everything
/// chemical it needs — valence, implicit hydrogens, ring templates, tidy —
/// comes from core/MoleculeGraph.hpp, so this file is presentation and
/// interaction only.
///
/// UNDO IS SNAPSHOT-BASED, the same pattern MainWindow uses for the 3D editor:
/// the whole graph is copied before each edit and restored wholesale. A
/// command stack would be the textbook answer, but a sketch is a few hundred
/// bytes and snapshotting has the property that matters here — there is no way
/// for an "undo" to be written wrong, because nothing is written at all.
class MoleculeCanvas : public QWidget {
    Q_OBJECT

public:
    /// The left sidebar's tools, in palette order.
    enum class Tool {
        Select,       ///< rubber-band / click select, drag to move
        SingleBond,
        DoubleBond,
        TripleBond,
        WedgeBond,
        HashBond,
        Chain,        ///< drag out a zig-zag alkyl chain
        AtomLabel,    ///< click an atom to type its element symbol
        Caption,      ///< free text on the canvas
        Charge,       ///< click to raise the formal charge, right-click lowers
        Eraser,
        Ring,         ///< stamp / fuse the active ring template
    };

    explicit MoleculeCanvas(QWidget* parent = nullptr);

    // -- The sketch ---------------------------------------------------------

    const core::MoleculeGraph& graph() const { return graph_; }
    /// Replace the whole sketch as ONE undo step (a SMILES import, a paste of
    /// a whole structure, "Clear").
    void setGraph(core::MoleculeGraph graph, const QString& what);

    /// The atoms currently selected, ascending. Empty when nothing is.
    std::vector<int> selectedAtoms() const;

    // -- Tools --------------------------------------------------------------

    Tool tool() const { return tool_; }
    void setTool(Tool tool);

    core::RingTemplate ringTemplate() const { return ringTemplate_; }
    void setRingTemplate(core::RingTemplate ring);

    /// The element the atom tool writes, as an atomic number.
    int activeElement() const { return activeElement_; }
    void setActiveElement(int atomicNumber);

    // -- Appearance ---------------------------------------------------------

    double lineWidth() const { return lineWidth_; }
    void setLineWidth(double width);
    int labelPointSize() const { return labelPointSize_; }
    void setLabelPointSize(int points);
    bool elementColors() const { return elementColors_; }
    void setElementColors(bool on);
    /// False (the default) paints the standard white figure canvas; true makes
    /// the drawing surface follow the application theme.
    bool followsTheme() const { return followTheme_; }
    void setFollowsTheme(bool on);

    // -- View ---------------------------------------------------------------

    /// Fit the whole sketch in the widget with a comfortable margin. No-op on
    /// an empty canvas.
    void zoomToFit();
    void zoomBy(double factor);
    void resetView();

    // -- Undo / clipboard ---------------------------------------------------

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    void undo();
    void redo();

    void copySelection();
    void pasteClipboard();
    void deleteSelection();
    void selectAll();
    /// Regularize the selection, or the whole sketch when nothing is selected.
    void tidySelection();

    /// Render the sketch into `painter` over `size` logical pixels, at the
    /// scale that fits it — the entry point both paintEvent() and the image
    /// export go through, so an exported figure IS the on-screen figure.
    /// `background` invalid paints nothing (a transparent export).
    void renderTo(QPainter& painter, const QSize& size, const QColor& background);

    QSize sizeHint() const override { return {760, 560}; }

Q_SIGNALS:
    /// The graph changed in any way — the right sidebar's formula read-out and
    /// the undo/redo buttons follow this.
    void sketchChanged();
    /// The selection changed (which also fires sketchChanged when an edit
    /// caused it).
    void selectionChanged();
    /// A transient message for the dialog's status line: "C6H6", "chain: 5",
    /// "unknown element \"Xy\"".
    void statusMessage(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    // -- Coordinates --------------------------------------------------------
    QPointF toScreen(double x, double y) const;
    QPointF toSketch(const QPointF& screen) const;

    // -- Painting -----------------------------------------------------------
    void paintSketch(QPainter& painter, const QSize& size, double scale,
                     const QPointF& origin, bool decorations);
    /// `scale` is passed rather than read from scale_ only so that the font
    /// and pen sizes stay tied to the render being done; the POSITION
    /// transform goes through toScreen(), which paintSketch() has already
    /// pointed at the right view.
    void paintBond(QPainter& painter, int bondIndex, double scale);
    void paintAtom(QPainter& painter, int atomIndex, double scale);
    /// The text an atom shows, or empty for a plain carbon vertex.
    QString atomLabel(int atomIndex) const;
    QColor inkColor() const;
    QColor canvasColor() const;
    /// The colour an atom's LABEL takes: its element colour, or the warning
    /// colour when its valence is impossible.
    QColor atomColor(int atomIndex) const;
    /// Its element colour alone — what the bonds reaching it are drawn in.
    QColor elementColor(int atomIndex) const;

    /// Radius (in sketch units) of the blank disc a labelled atom clears
    /// around itself, so a bond stops at the label instead of running through
    /// it. 0 for an unlabelled carbon vertex.
    double labelClearance(int atomIndex, double scale) const;

    // -- Editing ------------------------------------------------------------
    void pushUndo(const QString& what);
    void commit(const QString& what);
    /// Snap a free end to the 30° family about `from`, at the standard bond
    /// length — unless the pointer is far enough out that the user is clearly
    /// stretching the bond deliberately.
    QPointF snapBondEnd(const QPointF& from, const QPointF& to) const;
    /// The direction a new bond on `atom` should take: the free 60° slot that
    /// is emptiest, so clicking a carbon repeatedly walks a chain out rather
    /// than stacking bonds on one another.
    double freeAngleAt(int atom) const;
    void beginAtomLabelEdit(int atom);
    void beginCaptionEdit(int caption, const QPointF& sketchPosition);
    void finishInlineEdit();

    int orderForTool() const;
    core::BondStereo stereoForTool() const;

    core::MoleculeGraph graph_;
    std::deque<core::MoleculeGraph> undoStack_;
    std::deque<core::MoleculeGraph> redoStack_;
    core::MoleculeGraph clipboard_;

    Tool tool_ = Tool::SingleBond;
    core::RingTemplate ringTemplate_ = core::RingTemplate::Benzene;
    int activeElement_ = 6;

    double scale_ = 56.0;  ///< screen pixels per sketch unit
    QPointF origin_{0.0, 0.0}; ///< sketch (0,0) in widget pixels

    double lineWidth_ = 1.8;
    int labelPointSize_ = 12;
    bool elementColors_ = true;
    bool followTheme_ = false;

    std::set<int> selectedAtoms_;
    std::set<int> selectedCaptions_;

    // -- Drag state ---------------------------------------------------------
    enum class Drag { None, Bond, Chain, Move, RubberBand, Pan, RingFuse };
    Drag drag_ = Drag::None;
    QPointF dragStart_;      ///< sketch units
    QPointF dragCurrent_;    ///< sketch units
    QPointF panAnchor_;      ///< widget pixels
    int dragAtom_ = -1;      ///< the atom a bond/chain is being drawn from
    int dragBond_ = -1;      ///< the bond a ring is being fused onto
    int chainLength_ = 0;
    bool dragMoved_ = false;
    /// Selection as it was when a shift-drag started, so the rubber band
    /// extends rather than replaces.
    std::set<int> bandBaseAtoms_;

    int hoverAtom_ = -1;
    int hoverBond_ = -1;

    QLineEdit* inlineEdit_ = nullptr;
    int editingAtom_ = -1;
    int editingCaption_ = -1;
    QPointF editingPosition_;
};

} // namespace calango::gui
