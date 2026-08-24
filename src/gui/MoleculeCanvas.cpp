#include "gui/MoleculeCanvas.hpp"

#include "core/Element.hpp"
#include "core/PhysicalConstants.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/PlotPalette.hpp"

#include <QBrush>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRectF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace calango::gui {
namespace {

using core::MolAtom;
using core::MolBond;
using core::MoleculeGraph;

/// How many undo steps a sketch keeps. A snapshot is a few hundred bytes, so
/// this is generous by design.
constexpr int kUndoDepth = 200;

/// Hit radius, in sketch units, for atoms and bonds.
constexpr double kAtomHit = 0.28;
constexpr double kBondHit = 0.16;

/// Beyond this many bond lengths from the anchor the pointer is taken to be
/// stretching a bond deliberately, and angle snapping stops fighting it.
constexpr double kSnapReach = 2.2;

/// The offset between the two lines of a double bond, in sketch units.
constexpr double kDoubleGap = 0.13;

/// The zoom at which the appearance settings mean what they say: a bond width
/// of 1.8 is 1.8 device pixels and a 12 pt label is 12 pt. Line widths and
/// font sizes scale from here, so zooming in enlarges the whole drawing rather
/// than thickening the lines around fixed-size atoms.
constexpr double kReferenceScale = 56.0;

/// One piece of a laid-out atom label: its text, its font, and where its
/// baseline-left corner sits relative to the label's own left edge.
struct LabelRun {
    QString text;
    bool subscript = false;
};

/// Split an atom label into runs at the "_n" subscript markers atomLabel()
/// writes. "NH_2" becomes ["NH", "2"(sub)]; a label with no marker is one run.
std::vector<LabelRun> splitLabel(const QString& label)
{
    std::vector<LabelRun> runs;
    int i = 0;
    while (i < label.size()) {
        const int marker = label.indexOf(QLatin1Char('_'), i);
        if (marker < 0) {
            runs.push_back({label.mid(i), false});
            break;
        }
        if (marker > i)
            runs.push_back({label.mid(i, marker - i), false});
        // Everything digit-like immediately after the marker is the subscript.
        int end = marker + 1;
        while (end < label.size() && label.at(end).isDigit())
            ++end;
        runs.push_back({label.mid(marker + 1, end - marker - 1), true});
        i = end;
    }
    return runs;
}

/// Total advance of `runs` under `font` (and its subscript variant).
double labelWidth(const std::vector<LabelRun>& runs, const QFont& font,
                  const QFont& subscriptFont)
{
    const QFontMetricsF metrics(font);
    const QFontMetricsF subscriptMetrics(subscriptFont);
    double width = 0.0;
    for (const LabelRun& run : runs) {
        width += run.subscript ? subscriptMetrics.horizontalAdvance(run.text)
                               : metrics.horizontalAdvance(run.text);
    }
    return width;
}

/// The subscript face: smaller and dropped below the baseline, which is what
/// makes "CH3" read as CH-three rather than as C-H-three.
QFont subscriptFontFor(const QFont& font)
{
    QFont small = font;
    small.setPointSizeF(font.pointSizeF() * 0.68);
    return small;
}

double angleOf(const QPointF& from, const QPointF& to)
{
    return std::atan2(to.y() - from.y(), to.x() - from.x());
}

/// Round `radians` to the nearest multiple of 30°.
double snapAngle(double radians)
{
    const double step = core::kPi / 6.0;
    return std::round(radians / step) * step;
}

/// The warning colour for an over-valent atom. Deliberately the plot palette's
/// reference orange rather than a pure red: this is a REMARK, not a rejection,
/// and a red that shouts reads as "the program refused the stroke".
const QColor& warningColor()
{
    static const QColor kWarning = PlotPalette::reference;
    return kWarning;
}

} // namespace

MoleculeCanvas::MoleculeCanvas(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setMinimumSize(360, 280);
    // paintEvent() fills the whole rect before it draws anything, so Qt need
    // not erase to the palette background first.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

// ---------------------------------------------------------------------------
// Coordinates
// ---------------------------------------------------------------------------

QPointF MoleculeCanvas::toScreen(double x, double y) const
{
    // y is flipped: sketch space is mathematical (y up), widget space is not.
    return {origin_.x() + x * scale_, origin_.y() - y * scale_};
}

QPointF MoleculeCanvas::toSketch(const QPointF& screen) const
{
    return {(screen.x() - origin_.x()) / scale_,
            (origin_.y() - screen.y()) / scale_};
}

// ---------------------------------------------------------------------------
// Sketch access
// ---------------------------------------------------------------------------

void MoleculeCanvas::setGraph(MoleculeGraph graph, const QString& what)
{
    pushUndo(what);
    graph_ = std::move(graph);
    selectedAtoms_.clear();
    selectedCaptions_.clear();
    zoomToFit();
    Q_EMIT selectionChanged();
    Q_EMIT sketchChanged();
    update();
}

std::vector<int> MoleculeCanvas::selectedAtoms() const
{
    return {selectedAtoms_.begin(), selectedAtoms_.end()};
}

void MoleculeCanvas::setTool(Tool tool)
{
    if (tool_ == tool)
        return;
    finishInlineEdit();
    tool_ = tool;
    setCursor(tool_ == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void MoleculeCanvas::setRingTemplate(core::RingTemplate ring)
{
    ringTemplate_ = ring;
}

void MoleculeCanvas::setActiveElement(int atomicNumber)
{
    if (atomicNumber > 0 && atomicNumber <= core::Elements::maxZ)
        activeElement_ = atomicNumber;
}

void MoleculeCanvas::setLineWidth(double width)
{
    lineWidth_ = std::clamp(width, 0.5, 6.0);
    update();
}

void MoleculeCanvas::setLabelPointSize(int points)
{
    labelPointSize_ = std::clamp(points, 6, 36);
    update();
}

void MoleculeCanvas::setElementColors(bool on)
{
    elementColors_ = on;
    update();
}

void MoleculeCanvas::setFollowsTheme(bool on)
{
    followTheme_ = on;
    update();
}

// ---------------------------------------------------------------------------
// Highlights
// ---------------------------------------------------------------------------

namespace {

/// The region-highlight palette: six hues that stay apart from one another on
/// a white page AND on a dark one, and that none of the CPK element colours
/// sits directly on top of. Painted at low alpha, so what matters is the hue.
struct HighlightEntry {
    const char* name;
    QColor color;
};

const std::vector<HighlightEntry>& highlightEntries()
{
    static const std::vector<HighlightEntry> entries = {
        {QT_TRANSLATE_NOOP("MoleculeCanvas", "Yellow"), QColor(0xf5, 0xd0, 0x3a)},
        {QT_TRANSLATE_NOOP("MoleculeCanvas", "Green"), QColor(0x53, 0xc1, 0x6a)},
        {QT_TRANSLATE_NOOP("MoleculeCanvas", "Blue"), QColor(0x4b, 0x9b, 0xe0)},
        {QT_TRANSLATE_NOOP("MoleculeCanvas", "Pink"), QColor(0xe8, 0x6a, 0xa8)},
        {QT_TRANSLATE_NOOP("MoleculeCanvas", "Purple"), QColor(0x9a, 0x74, 0xd6)},
        {QT_TRANSLATE_NOOP("MoleculeCanvas", "Orange"), QColor(0xef, 0x8b, 0x3f)},
    };
    return entries;
}

} // namespace

int MoleculeCanvas::highlightPaletteSize()
{
    return static_cast<int>(highlightEntries().size());
}

QColor MoleculeCanvas::highlightPaletteColor(int index)
{
    if (index < 0 || index >= highlightPaletteSize())
        return {};
    return highlightEntries()[static_cast<std::size_t>(index)].color;
}

QString MoleculeCanvas::highlightPaletteName(int index)
{
    if (index < 0 || index >= highlightPaletteSize())
        return {};
    return tr(highlightEntries()[static_cast<std::size_t>(index)].name);
}

void MoleculeCanvas::setAromaticHighlight(bool on)
{
    aromaticHighlight_ = on;
    update();
}

void MoleculeCanvas::setAromaticHighlightColor(const QColor& color)
{
    if (!color.isValid())
        return;
    aromaticColor_ = color;
    update();
}

bool MoleculeCanvas::hasHighlights() const
{
    for (const MolAtom& atom : graph_.atoms())
        if (atom.highlight >= 0)
            return true;
    return false;
}

void MoleculeCanvas::highlightSelection(int index)
{
    if (selectedAtoms_.empty()) {
        Q_EMIT statusMessage(tr("Select atoms first — a highlight colours a "
                                "region of the drawing, not the whole canvas."));
        return;
    }
    pushUndo(index < 0 ? tr("Clear highlight") : tr("Highlight"));
    for (int atom : selectedAtoms_) {
        if (atom >= 0 && atom < graph_.atomCount())
            graph_.atoms()[static_cast<std::size_t>(atom)].highlight = index;
    }
    commit(index < 0
               ? tr("Highlight cleared from %n atom(s).", nullptr,
                    static_cast<int>(selectedAtoms_.size()))
               : tr("%1 highlight on %n atom(s).", nullptr,
                    static_cast<int>(selectedAtoms_.size()))
                     .arg(highlightPaletteName(index)));
}

void MoleculeCanvas::clearCanvas()
{
    if (graph_.empty()) {
        Q_EMIT statusMessage(tr("The canvas is already empty."));
        return;
    }
    finishInlineEdit();
    const int atoms = graph_.atomCount();
    pushUndo(tr("Clear canvas"));
    graph_ = core::MoleculeGraph();
    selectedAtoms_.clear();
    selectedCaptions_.clear();
    hoverAtom_ = -1;
    hoverBond_ = -1;
    resetView();
    Q_EMIT selectionChanged();
    // One undo step, and the message says so — a wiped canvas that looks
    // unrecoverable is the thing this has to not be.
    commit(tr("Canvas cleared — %n atom(s) removed. Undo restores them.",
              nullptr, atoms));
}

// ---------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------

void MoleculeCanvas::resetView()
{
    scale_ = kReferenceScale;
    origin_ = QPointF(width() / 2.0, height() / 2.0);
    update();
}

void MoleculeCanvas::zoomBy(double factor)
{
    const QPointF centre(width() / 2.0, height() / 2.0);
    const QPointF anchor = toSketch(centre);
    scale_ = std::clamp(scale_ * factor, 12.0, 400.0);
    const QPointF after = toScreen(anchor.x(), anchor.y());
    origin_ += centre - after;
    update();
}

void MoleculeCanvas::zoomToFit()
{
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    if (!graph_.bounds(minX, minY, maxX, maxY)) {
        resetView();
        return;
    }
    const double spanX = std::max(maxX - minX, 1.5);
    const double spanY = std::max(maxY - minY, 1.5);
    const double margin = 1.2; // sketch units of air around the drawing
    scale_ = std::clamp(std::min(width() / (spanX + 2 * margin),
                                 height() / (spanY + 2 * margin)),
                        12.0, 400.0);
    const QPointF centre(width() / 2.0, height() / 2.0);
    origin_ = QPointF(centre.x() - 0.5 * (minX + maxX) * scale_,
                      centre.y() + 0.5 * (minY + maxY) * scale_);
    update();
}

// ---------------------------------------------------------------------------
// Undo, clipboard, bulk actions
// ---------------------------------------------------------------------------

void MoleculeCanvas::pushUndo(const QString& what)
{
    Q_UNUSED(what);
    undoStack_.push_back(graph_);
    if (static_cast<int>(undoStack_.size()) > kUndoDepth)
        undoStack_.pop_front();
    // Any new edit invalidates the redo branch — the same rule the 3D editor
    // follows, and the only one that cannot present a redo that no longer
    // applies to the current sketch.
    redoStack_.clear();
}

void MoleculeCanvas::commit(const QString& what)
{
    Q_EMIT sketchChanged();
    if (!what.isEmpty())
        Q_EMIT statusMessage(what);
    update();
}

void MoleculeCanvas::undo()
{
    if (undoStack_.empty())
        return;
    finishInlineEdit();
    redoStack_.push_back(graph_);
    graph_ = undoStack_.back();
    undoStack_.pop_back();
    selectedAtoms_.clear();
    selectedCaptions_.clear();
    Q_EMIT selectionChanged();
    commit(tr("Undo"));
}

void MoleculeCanvas::redo()
{
    if (redoStack_.empty())
        return;
    finishInlineEdit();
    undoStack_.push_back(graph_);
    graph_ = redoStack_.back();
    redoStack_.pop_back();
    selectedAtoms_.clear();
    selectedCaptions_.clear();
    Q_EMIT selectionChanged();
    commit(tr("Redo"));
}

void MoleculeCanvas::copySelection()
{
    if (selectedAtoms_.empty()) {
        Q_EMIT statusMessage(tr("Select something first."));
        return;
    }
    clipboard_ = graph_.subgraph(selectedAtoms());
    Q_EMIT statusMessage(tr("Copied %1 atoms.").arg(clipboard_.atomCount()));
}

void MoleculeCanvas::pasteClipboard()
{
    if (clipboard_.atomCount() == 0) {
        Q_EMIT statusMessage(tr("Nothing has been copied yet."));
        return;
    }
    pushUndo(tr("Paste"));
    // Offset by half a bond length down and right, the universal "this is the
    // copy, not the original" nudge.
    const int first = graph_.append(clipboard_, 0.5 * MoleculeGraph::kBondLength,
                                    -0.5 * MoleculeGraph::kBondLength);
    selectedAtoms_.clear();
    selectedCaptions_.clear();
    for (int i = 0; i < clipboard_.atomCount(); ++i)
        selectedAtoms_.insert(first + i);
    Q_EMIT selectionChanged();
    commit(tr("Pasted %1 atoms.").arg(clipboard_.atomCount()));
}

void MoleculeCanvas::deleteSelection()
{
    if (selectedAtoms_.empty() && selectedCaptions_.empty())
        return;
    pushUndo(tr("Delete"));
    // Captions first: removing atoms does not renumber captions, but doing it
    // the other way round would still work only by accident.
    for (auto it = selectedCaptions_.rbegin(); it != selectedCaptions_.rend(); ++it)
        graph_.removeCaption(*it);
    const int removed = static_cast<int>(selectedAtoms_.size());
    graph_.removeAtoms(selectedAtoms());
    selectedAtoms_.clear();
    selectedCaptions_.clear();
    Q_EMIT selectionChanged();
    commit(tr("Deleted %1 atoms.").arg(removed));
}

void MoleculeCanvas::selectAll()
{
    selectedAtoms_.clear();
    for (int i = 0; i < graph_.atomCount(); ++i)
        selectedAtoms_.insert(i);
    selectedCaptions_.clear();
    for (int i = 0; i < static_cast<int>(graph_.captions().size()); ++i)
        selectedCaptions_.insert(i);
    Q_EMIT selectionChanged();
    update();
}

void MoleculeCanvas::tidySelection()
{
    if (graph_.atomCount() < 2) {
        Q_EMIT statusMessage(tr("Draw something first."));
        return;
    }
    pushUndo(tr("Tidy"));
    core::tidy(graph_, selectedAtoms());
    commit(selectedAtoms_.empty()
               ? tr("Tidied the whole sketch.")
               : tr("Tidied %1 atoms.").arg(selectedAtoms_.size()));
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

QColor MoleculeCanvas::canvasColor() const
{
    // A sketch is a FIGURE — it is exported into papers and slides — so the
    // drawing surface follows the same rule every 2D plot in Calango does and
    // is white by default, whatever the application theme is. The dialog
    // around it stays themed. "Follow theme" is the escape hatch for someone
    // who wants a dark drawing surface on screen; it changes nothing about the
    // export, which always names its own background.
    if (!followTheme_)
        return PlotPalette::canvas;
    return palette().color(QPalette::Base);
}

QColor MoleculeCanvas::inkColor() const
{
    if (!followTheme_)
        return PlotPalette::text;
    return palette().color(QPalette::Text);
}

QColor MoleculeCanvas::atomColor(int atomIndex) const
{
    // The valence warning colours the LABEL and the dotted ring paintAtom()
    // draws — not the bonds, which take elementColor() below. Tinting five
    // bonds orange because one atom in the middle of them is over-valent reads
    // as a rendering fault rather than as a remark about that atom.
    if (graph_.valenceViolated(atomIndex))
        return warningColor();
    return elementColor(atomIndex);
}

QColor MoleculeCanvas::elementColor(int atomIndex) const
{
    if (!elementColors_)
        return inkColor();
    const int z = graph_.atoms()[static_cast<std::size_t>(atomIndex)].atomicNumber;
    if (z == 6)
        return inkColor(); // carbon in CPK is a mid grey, unreadable as a label
    QColor color = cpkColor(z);
    // On a white canvas, hydrogen's pure white and a few pale CPK colours are
    // invisible; darken anything that cannot carry text.
    if (!followTheme_ && color.lightnessF() > 0.72)
        color = color.darker(160);
    return color;
}

QString MoleculeCanvas::atomLabel(int atomIndex) const
{
    const MolAtom& atom = graph_.atoms()[static_cast<std::size_t>(atomIndex)];
    // Carbon vertices are UNLABELLED — a line junction is a carbon, and that
    // is the single strongest convention in chemical drawing. A carbon that is
    // charged, radical or completely isolated is labelled anyway, because a
    // bare dot at the end of nothing says nothing.
    const bool bareCarbon = atom.atomicNumber == 6 && atom.charge == 0
        && atom.radicalElectrons == 0 && !graph_.neighbors(atomIndex).empty();
    if (bareCarbon)
        return {};

    const QString symbol =
        QLatin1String(core::Elements::data(atom.atomicNumber).symbol);
    const int hydrogens = graph_.implicitHydrogens(atomIndex);
    if (hydrogens <= 0)
        return symbol;

    // Hydrogens go on whichever side faces AWAY from the bonds: "CH3" on a
    // methyl reached from the left, "H3C" on one reached from the right. A
    // label that puts its hydrogens between the atom and its own bond reads as
    // if the bond went to the hydrogen.
    double toNeighbors = 0.0;
    for (int neighbor : graph_.neighbors(atomIndex)) {
        toNeighbors +=
            graph_.atoms()[static_cast<std::size_t>(neighbor)].x - atom.x;
    }
    const QString count = hydrogens > 1 ? QString::number(hydrogens) : QString();
    // The count is marked with the same "_n" convention GuiUtils's
    // drawWithSubscripts uses; paintAtom() renders it as a real subscript and
    // labelClearance() measures it. Nothing outside this file ever sees the
    // marker.
    const QString hydrogenPart = count.isEmpty()
        ? QStringLiteral("H")
        : QStringLiteral("H_") + count;
    return toNeighbors > 0.0 ? hydrogenPart + symbol : symbol + hydrogenPart;
}

double MoleculeCanvas::labelClearance(int atomIndex, double scale) const
{
    const QString label = atomLabel(atomIndex);
    if (label.isEmpty())
        return 0.0;
    // Half the label's width plus a hair of air, converted to sketch units.
    // Exactly the font paintAtom() draws with — `scale / kReferenceScale`, not
    // `scale / scale_`. The two agree only at the default zoom, and anywhere
    // else the clearance was computed for a label of a different size than the
    // one on screen, so bonds stopped short of (or ran through) their labels
    // the moment the canvas was zoomed.
    QFont font = this->font();
    font.setPointSizeF(labelPointSize_ * scale / kReferenceScale);
    font.setBold(true);
    const double width =
        labelWidth(splitLabel(label), font, subscriptFontFor(font));
    return (0.5 * width + 3.0) / scale;
}

void MoleculeCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.fillRect(rect(), canvasColor());
    paintSketch(painter, size(), scale_, origin_, true);
}

void MoleculeCanvas::renderTo(QPainter& painter, const QSize& size,
                              const QColor& background)
{
    if (background.isValid())
        painter.fillRect(QRectF(0, 0, size.width(), size.height()), background);

    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    if (!graph_.bounds(minX, minY, maxX, maxY))
        return;
    const double margin = 0.8;
    const double spanX = std::max(maxX - minX, 1.0) + 2 * margin;
    const double spanY = std::max(maxY - minY, 1.0) + 2 * margin;
    const double scale =
        std::min(size.width() / spanX, size.height() / spanY);
    const QPointF origin(size.width() / 2.0 - 0.5 * (minX + maxX) * scale,
                         size.height() / 2.0 + 0.5 * (minY + maxY) * scale);
    // decorations = false: no selection halos, no hover highlight, no in-flight
    // rubber band. An export is the STRUCTURE, not the editing session.
    paintSketch(painter, size, scale, origin, false);
}

void MoleculeCanvas::paintHighlights(QPainter& painter, double scale)
{
    const std::vector<MolAtom>& atoms = graph_.atoms();
    if (atoms.empty())
        return;

    const QPainter::RenderHints hints = painter.renderHints();
    const QPen savedPen = painter.pen();
    const QBrush savedBrush = painter.brush();
    painter.setPen(Qt::NoPen);

    // -- Aromatic rings ------------------------------------------------------
    //
    // Drawn FIRST, so a region colour the user applied deliberately is never
    // buried under a fill the program decided to draw. Inset toward the ring
    // centre so the fill reads as being inside the ring rather than as a
    // thickening of its bonds.
    if (aromaticHighlight_) {
        QColor fill = aromaticColor_;
        fill.setAlpha(78);
        painter.setBrush(fill);
        for (const std::vector<int>& ring : graph_.perceiveAromaticRings()) {
            QPointF centre;
            for (int atom : ring) {
                const MolAtom& a = atoms[static_cast<std::size_t>(atom)];
                centre += toScreen(a.x, a.y);
            }
            centre /= static_cast<double>(ring.size());
            QPolygonF polygon;
            for (int atom : ring) {
                const MolAtom& a = atoms[static_cast<std::size_t>(atom)];
                polygon << centre + 0.78 * (toScreen(a.x, a.y) - centre);
            }
            painter.drawPolygon(polygon);
        }
    }

    // -- Region colours ------------------------------------------------------
    //
    // A round-capped band along every bond whose two atoms carry the SAME
    // colour, plus a disc behind each highlighted atom — so an isolated
    // highlighted atom still shows, and two adjacent regions of different
    // colours meet at the bond between them instead of blending across it.
    //
    // Built as ONE path per colour and filled once, rather than drawn shape by
    // shape. The shapes overlap heavily (a disc at every vertex, a band along
    // every bond), and painting them separately at the same alpha composites
    // each overlap twice: a marker pen with a dark blob at every atom, which
    // is exactly what a highlight must not look like. simplified() unions
    // them, so the band is one even tint however many pieces it is made of.
    const double radius = 0.27 * scale;
    for (int index = 0; index < highlightPaletteSize(); ++index) {
        QPainterPath discs;
        for (int i = 0; i < graph_.atomCount(); ++i) {
            if (atoms[static_cast<std::size_t>(i)].highlight != index)
                continue;
            const MolAtom& a = atoms[static_cast<std::size_t>(i)];
            discs.addEllipse(toScreen(a.x, a.y), radius, radius);
        }
        if (discs.isEmpty())
            continue;

        QPainterPath segments;
        for (const MolBond& bond : graph_.bonds()) {
            const MolAtom& a = atoms[static_cast<std::size_t>(bond.a)];
            const MolAtom& b = atoms[static_cast<std::size_t>(bond.b)];
            if (a.highlight != index || b.highlight != index)
                continue;
            segments.moveTo(toScreen(a.x, a.y));
            segments.lineTo(toScreen(b.x, b.y));
        }
        QPainterPath region = discs;
        if (!segments.isEmpty()) {
            QPainterPathStroker stroker;
            stroker.setWidth(2.0 * radius);
            stroker.setCapStyle(Qt::RoundCap);
            region = region.united(stroker.createStroke(segments));
        }

        QColor color = highlightPaletteColor(index);
        color.setAlpha(96);
        painter.setBrush(color);
        painter.drawPath(region.simplified());
    }

    painter.setPen(savedPen);
    painter.setBrush(savedBrush);
    painter.setRenderHints(hints);
}

void MoleculeCanvas::paintSketch(QPainter& painter, const QSize& size,
                                 double scale, const QPointF& origin,
                                 bool decorations)
{
    Q_UNUSED(size);
    // The paint helpers read scale_/origin_, so swap the view for the duration
    // of an export render and put it back. Cheaper and far less error-prone
    // than threading the transform through every helper.
    const double savedScale = scale_;
    const QPointF savedOrigin = origin_;
    scale_ = scale;
    origin_ = origin;

    // Annotations first, so everything else is drawn on top of them. Not
    // gated on `decorations`: a highlight is part of the drawing the user
    // made, unlike a selection halo or a rubber band, and an exported image
    // that dropped it would be missing something they put there.
    paintHighlights(painter, scale);

    if (decorations && !selectedAtoms_.empty()) {
        QColor halo = PlotPalette::highlight;
        halo.setAlpha(60);
        painter.setPen(Qt::NoPen);
        painter.setBrush(halo);
        for (int atom : selectedAtoms_) {
            const MolAtom& a = graph_.atoms()[static_cast<std::size_t>(atom)];
            painter.drawEllipse(toScreen(a.x, a.y), 0.28 * scale, 0.28 * scale);
        }
    }

    for (int i = 0; i < graph_.bondCount(); ++i)
        paintBond(painter, i, scale);
    for (int i = 0; i < graph_.atomCount(); ++i)
        paintAtom(painter, i, scale);

    // Captions.
    {
        QFont font = this->font();
        font.setPointSizeF(labelPointSize_ * 1.05);
        painter.setFont(font);
        painter.setPen(inkColor());
        for (int i = 0; i < static_cast<int>(graph_.captions().size()); ++i) {
            const core::MolCaption& caption =
                graph_.captions()[static_cast<std::size_t>(i)];
            const QPointF at = toScreen(caption.x, caption.y);
            if (decorations && selectedCaptions_.count(i)) {
                const QFontMetricsF metrics(font);
                QRectF box = metrics.boundingRect(
                    QString::fromStdString(caption.text));
                box.moveTopLeft(at);
                box.adjust(-3, -3, 3, 3);
                QColor halo = PlotPalette::highlight;
                halo.setAlpha(50);
                painter.fillRect(box, halo);
            }
            painter.drawText(at + QPointF(0, labelPointSize_),
                             QString::fromStdString(caption.text));
        }
    }

    if (decorations) {
        // In-flight feedback: the bond being dragged out, the chain preview,
        // the rubber band.
        QPen pen(PlotPalette::highlight);
        pen.setWidthF(lineWidth_);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        if (drag_ == Drag::Bond || drag_ == Drag::Chain) {
            painter.drawLine(toScreen(dragStart_.x(), dragStart_.y()),
                             toScreen(dragCurrent_.x(), dragCurrent_.y()));
        } else if (drag_ == Drag::RubberBand) {
            const QPointF a = toScreen(dragStart_.x(), dragStart_.y());
            const QPointF b = toScreen(dragCurrent_.x(), dragCurrent_.y());
            painter.drawRect(QRectF(a, b).normalized());
        }
        if (hoverBond_ >= 0 && hoverAtom_ < 0
            && (tool_ == Tool::Ring || tool_ == Tool::Eraser)) {
            const MolBond& bond = graph_.bonds()[static_cast<std::size_t>(hoverBond_)];
            const MolAtom& a = graph_.atoms()[static_cast<std::size_t>(bond.a)];
            const MolAtom& b = graph_.atoms()[static_cast<std::size_t>(bond.b)];
            QPen highlight(PlotPalette::highlight);
            highlight.setWidthF(lineWidth_ * 3.0);
            highlight.setCapStyle(Qt::RoundCap);
            QColor faded = PlotPalette::highlight;
            faded.setAlpha(90);
            highlight.setColor(faded);
            painter.setPen(highlight);
            painter.drawLine(toScreen(a.x, a.y), toScreen(b.x, b.y));
        }
    }

    scale_ = savedScale;
    origin_ = savedOrigin;
}

void MoleculeCanvas::paintBond(QPainter& painter, int bondIndex, double scale)
{
    const MolBond& bond = graph_.bonds()[static_cast<std::size_t>(bondIndex)];
    const MolAtom& a = graph_.atoms()[static_cast<std::size_t>(bond.a)];
    const MolAtom& b = graph_.atoms()[static_cast<std::size_t>(bond.b)];

    double ax = a.x;
    double ay = a.y;
    double bx = b.x;
    double by = b.y;
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length = std::hypot(dx, dy);
    if (length < 1e-9)
        return;
    const double ux = dx / length;
    const double uy = dy / length;

    // Stop the line at each end's label, so "OH" is not struck through.
    const double clearA = labelClearance(bond.a, scale);
    const double clearB = labelClearance(bond.b, scale);
    ax += ux * clearA;
    ay += uy * clearA;
    bx -= ux * clearB;
    by -= uy * clearB;

    // Two colours per bond when element colours are on: each half takes its own
    // atom's colour, which is how a C–O bond reads as a C–O bond.
    const QColor colorA = elementColor(bond.a);
    const QColor colorB = elementColor(bond.b);
    QPen pen;
    pen.setWidthF(lineWidth_ * scale / kReferenceScale);
    pen.setCapStyle(Qt::RoundCap);

    const QPointF pa = toScreen(ax, ay);
    const QPointF pb = toScreen(bx, by);
    const QPointF mid = 0.5 * (pa + pb);

    const auto drawHalves = [&](const QPointF& from, const QPointF& to) {
        const QPointF middle = 0.5 * (from + to);
        pen.setColor(colorA);
        painter.setPen(pen);
        painter.drawLine(from, middle);
        pen.setColor(colorB);
        painter.setPen(pen);
        painter.drawLine(middle, to);
    };

    if (bond.stereo == core::BondStereo::Wedge) {
        // A solid wedge, narrow at `a`.
        const double half = 0.11 * scale;
        const QPointF perpendicular(-(pb.y() - pa.y()), pb.x() - pa.x());
        const double norm = std::max(1e-9, std::hypot(perpendicular.x(),
                                                      perpendicular.y()));
        const QPointF offset = perpendicular / norm * half;
        QPainterPath path;
        path.moveTo(pa);
        path.lineTo(pb + offset);
        path.lineTo(pb - offset);
        path.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(colorA);
        painter.drawPath(path);
        painter.setBrush(Qt::NoBrush);
        return;
    }
    if (bond.stereo == core::BondStereo::Hash) {
        // The same wedge as a ladder of bars.
        const double half = 0.11 * scale;
        const QPointF along = pb - pa;
        const QPointF perpendicular(-along.y(), along.x());
        const double norm = std::max(1e-9, std::hypot(perpendicular.x(),
                                                      perpendicular.y()));
        const QPointF unit = perpendicular / norm;
        const int bars = std::max(3, static_cast<int>(
                                         std::hypot(along.x(), along.y()) / 6.0));
        pen.setColor(colorA);
        pen.setWidthF(lineWidth_ * scale / kReferenceScale);
        painter.setPen(pen);
        for (int i = 1; i <= bars; ++i) {
            const double t = static_cast<double>(i) / bars;
            const QPointF centre = pa + along * t;
            painter.drawLine(centre + unit * (half * t),
                             centre - unit * (half * t));
        }
        return;
    }

    if (bond.order <= 1) {
        drawHalves(pa, pb);
        return;
    }

    // Multiple bonds: parallel lines offset perpendicular to the bond. For a
    // RING bond the extra line goes on the inside, which is the convention
    // that makes a benzene read as a benzene rather than as a ladder.
    const QPointF along = pb - pa;
    const double alongNorm = std::max(1e-9, std::hypot(along.x(), along.y()));
    const QPointF unit(-along.y() / alongNorm, along.x() / alongNorm);
    const double gap = kDoubleGap * scale;

    if (bond.order == 2) {
        // Which side is "inside"? The side the other neighbours of both ends
        // are on. Where there are none (an isolated C=C, a terminal C=O) the
        // pair is drawn symmetric about the bond axis instead.
        double side = 0.0;
        for (int end : {bond.a, bond.b}) {
            for (int neighbor : graph_.neighbors(end)) {
                if (neighbor == bond.a || neighbor == bond.b)
                    continue;
                const MolAtom& n = graph_.atoms()[static_cast<std::size_t>(neighbor)];
                const QPointF at = toScreen(n.x, n.y);
                side += (at - mid).x() * unit.x() + (at - mid).y() * unit.y();
            }
        }
        if (std::fabs(side) < 1e-6) {
            drawHalves(pa + unit * (gap * 0.5), pb + unit * (gap * 0.5));
            drawHalves(pa - unit * (gap * 0.5), pb - unit * (gap * 0.5));
        } else {
            const QPointF inner = unit * (side > 0 ? gap : -gap);
            drawHalves(pa, pb);
            // The inner line is shortened at both ends — the drawn convention,
            // and what keeps a fused ring from looking like a box.
            const QPointF shorten = along * 0.14;
            drawHalves(pa + inner + shorten, pb + inner - shorten);
        }
        return;
    }

    // Triple: one on the axis, one either side.
    drawHalves(pa, pb);
    drawHalves(pa + unit * gap, pb + unit * gap);
    drawHalves(pa - unit * gap, pb - unit * gap);
}

void MoleculeCanvas::paintAtom(QPainter& painter, int atomIndex, double scale)
{
    const MolAtom& atom = graph_.atoms()[static_cast<std::size_t>(atomIndex)];
    const QPointF at = toScreen(atom.x, atom.y);
    const QString label = atomLabel(atomIndex);
    const QColor color = atomColor(atomIndex);

    QFont font = this->font();
    font.setPointSizeF(labelPointSize_ * scale / kReferenceScale);
    font.setBold(true);
    const QFontMetricsF metrics(font);

    const std::vector<LabelRun> runs = splitLabel(label);
    const QFont subscriptFont = subscriptFontFor(font);
    const double labelAdvance = label.isEmpty()
        ? 0.0
        : labelWidth(runs, font, subscriptFont);

    if (!label.isEmpty()) {
        // Clear the canvas behind the label so bonds do not show through the
        // counters of the glyphs.
        QRectF box(0, 0, labelAdvance, metrics.height());
        box.moveCenter(at);
        painter.fillRect(box.adjusted(-2, -1, 2, 1), canvasColor());

        // Two runs, drawn left to right from a shared baseline: the symbol at
        // full size, the hydrogen count smaller and dropped. Qt ships no rich
        // text on a QPainter path, and a literal "NH2" is not what a structure
        // drawing says.
        painter.setPen(color);
        const QFontMetricsF subscriptMetrics(subscriptFont);
        const double baseline = box.center().y() + 0.5 * metrics.xHeight();
        double x = box.left();
        for (const LabelRun& run : runs) {
            if (run.subscript) {
                painter.setFont(subscriptFont);
                painter.drawText(
                    QPointF(x, baseline + 0.28 * metrics.height()), run.text);
                x += subscriptMetrics.horizontalAdvance(run.text);
            } else {
                painter.setFont(font);
                painter.drawText(QPointF(x, baseline), run.text);
                x += metrics.horizontalAdvance(run.text);
            }
        }
    }

    // Formal charge as a superscript, up and to the right of the label (or of
    // the vertex, for a charged carbon that draws no label — which is why
    // atomLabel() labels one).
    if (atom.charge != 0) {
        QFont superscript = font;
        superscript.setPointSizeF(font.pointSizeF() * 0.72);
        superscript.setBold(true);
        painter.setFont(superscript);
        painter.setPen(color);
        const QString sign = atom.charge > 0 ? QStringLiteral("+")
                                             : QStringLiteral("−");
        const QString text = std::abs(atom.charge) == 1
            ? sign
            : QStringLiteral("%1%2").arg(std::abs(atom.charge)).arg(sign);
        const double dx =
            label.isEmpty() ? 0.06 * scale : 0.5 * labelAdvance + 2.0;
        painter.drawText(at + QPointF(dx, -0.5 * metrics.height()), text);
    }

    // Radical electrons as dots above the atom.
    if (atom.radicalElectrons > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const double radius = std::max(1.2, 0.022 * scale);
        for (int i = 0; i < atom.radicalElectrons; ++i) {
            const double offset = (i - (atom.radicalElectrons - 1) / 2.0) * 4.0
                * radius;
            painter.drawEllipse(at + QPointF(offset, -0.5 * metrics.height() - 4.0),
                                radius, radius);
        }
        painter.setBrush(Qt::NoBrush);
    }

    // An over-valent atom is CIRCLED, not refused. A pentavalent carbon is a
    // perfectly ordinary thing to sketch on the way to a mechanism.
    if (graph_.valenceViolated(atomIndex)) {
        QPen pen(warningColor());
        pen.setWidthF(std::max(1.0, lineWidth_ * scale / kReferenceScale));
        pen.setStyle(Qt::DotLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const double radius = std::max(6.0, 0.19 * scale);
        painter.drawEllipse(at, radius, radius);
    }
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

int MoleculeCanvas::orderForTool() const
{
    switch (tool_) {
    case Tool::DoubleBond: return 2;
    case Tool::TripleBond: return 3;
    default:               return 1;
    }
}

core::BondStereo MoleculeCanvas::stereoForTool() const
{
    switch (tool_) {
    case Tool::WedgeBond: return core::BondStereo::Wedge;
    case Tool::HashBond:  return core::BondStereo::Hash;
    default:              return core::BondStereo::None;
    }
}

QPointF MoleculeCanvas::snapBondEnd(const QPointF& from, const QPointF& to) const
{
    const double distance = std::hypot(to.x() - from.x(), to.y() - from.y());
    const double angle = snapAngle(angleOf(from, to));
    // Past kSnapReach the user is stretching on purpose; keep the snapped
    // ANGLE (a drawing with off-family angles never looks right) but honour
    // the length.
    const double length = distance > kSnapReach * MoleculeGraph::kBondLength
        ? distance
        : MoleculeGraph::kBondLength;
    return {from.x() + length * std::cos(angle),
            from.y() + length * std::sin(angle)};
}

double MoleculeCanvas::freeAngleAt(int atom) const
{
    const MolAtom& a = graph_.atoms()[static_cast<std::size_t>(atom)];
    std::vector<double> taken;
    for (int neighbor : graph_.neighbors(atom)) {
        const MolAtom& n = graph_.atoms()[static_cast<std::size_t>(neighbor)];
        taken.push_back(std::atan2(n.y - a.y, n.x - a.x));
    }
    if (taken.empty())
        return core::kPi / 6.0; // 30°, the top-right of a fresh zig-zag

    // Score the twelve 30° directions by how far each is from every occupied
    // one, and take the winner. Simple, and it produces exactly the chain a
    // chemist expects when clicking the same atom twice.
    double best = 0.0;
    double bestScore = -1e300;
    for (int k = 0; k < 12; ++k) {
        const double angle = k * core::kPi / 6.0;
        double score = 1e300;
        for (double occupied : taken) {
            double delta = std::fabs(angle - occupied);
            while (delta > core::kPi)
                delta = 2.0 * core::kPi - delta;
            score = std::min(score, delta);
        }
        if (score > bestScore) {
            bestScore = score;
            best = angle;
        }
    }
    return best;
}

void MoleculeCanvas::mousePressEvent(QMouseEvent* event)
{
    finishInlineEdit();
    setFocus();
    const QPointF sketch = toSketch(event->position());
    const int atom = graph_.atomAt(sketch.x(), sketch.y(), kAtomHit);
    const int bond = atom >= 0 ? -1
                               : graph_.bondAt(sketch.x(), sketch.y(), kBondHit);
    const int caption = (atom < 0 && bond < 0)
        ? graph_.captionAt(sketch.x(), sketch.y(), 0.4)
        : -1;

    dragStart_ = sketch;
    dragCurrent_ = sketch;
    dragMoved_ = false;
    dragAtom_ = atom;
    dragBond_ = bond;

    // Middle button always pans, whatever the tool — the one gesture that must
    // never be captured by a drawing mode.
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton
            && (event->modifiers() & Qt::AltModifier))) {
        drag_ = Drag::Pan;
        panAnchor_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    switch (tool_) {
    case Tool::Select: {
        if (atom >= 0) {
            if (event->modifiers() & Qt::ShiftModifier) {
                if (selectedAtoms_.count(atom))
                    selectedAtoms_.erase(atom);
                else
                    selectedAtoms_.insert(atom);
            } else if (!selectedAtoms_.count(atom)) {
                selectedAtoms_ = {atom};
                selectedCaptions_.clear();
            }
            drag_ = Drag::Move;
            pushUndo(tr("Move"));
            Q_EMIT selectionChanged();
        } else if (caption >= 0) {
            if (!(event->modifiers() & Qt::ShiftModifier)) {
                selectedAtoms_.clear();
                selectedCaptions_.clear();
            }
            selectedCaptions_.insert(caption);
            drag_ = Drag::Move;
            pushUndo(tr("Move"));
            Q_EMIT selectionChanged();
        } else {
            drag_ = Drag::RubberBand;
            bandBaseAtoms_ = (event->modifiers() & Qt::ShiftModifier)
                ? selectedAtoms_
                : std::set<int>{};
            selectedAtoms_ = bandBaseAtoms_;
            if (!(event->modifiers() & Qt::ShiftModifier))
                selectedCaptions_.clear();
            Q_EMIT selectionChanged();
        }
        break;
    }
    case Tool::SingleBond:
    case Tool::DoubleBond:
    case Tool::TripleBond:
    case Tool::WedgeBond:
    case Tool::HashBond: {
        if (bond >= 0) {
            // ChemDraw's defining gesture, handled on PRESS so a click that
            // does not move still cycles.
            pushUndo(tr("Bond order"));
            if (stereoForTool() != core::BondStereo::None) {
                MolBond& target = graph_.bonds()[static_cast<std::size_t>(bond)];
                // A stereo tool on an existing bond SETS that stereo, or
                // flips its direction when it is already set — the second
                // click is how a chemist reverses a wedge.
                if (target.stereo == stereoForTool())
                    std::swap(target.a, target.b);
                else
                    target.stereo = stereoForTool();
                commit(tr("Stereo bond"));
            } else if (orderForTool() != 1) {
                graph_.bonds()[static_cast<std::size_t>(bond)].order =
                    orderForTool();
                graph_.bonds()[static_cast<std::size_t>(bond)].stereo =
                    core::BondStereo::None;
                commit(tr("Bond order %1").arg(orderForTool()));
            } else {
                const int order = graph_.cycleBondOrder(bond);
                commit(tr("Bond order %1").arg(order));
            }
            drag_ = Drag::None;
            return;
        }
        drag_ = Drag::Bond;
        if (atom < 0) {
            // Starting in empty space creates the first atom right away, so
            // the drag has something to come from.
            pushUndo(tr("Draw bond"));
            dragAtom_ = graph_.addAtom(activeElement_, sketch.x(), sketch.y());
            dragStart_ = sketch;
            Q_EMIT sketchChanged();
        } else {
            pushUndo(tr("Draw bond"));
            const MolAtom& a = graph_.atoms()[static_cast<std::size_t>(atom)];
            dragStart_ = QPointF(a.x, a.y);
        }
        dragCurrent_ = snapBondEnd(dragStart_, sketch);
        break;
    }
    case Tool::Chain: {
        pushUndo(tr("Chain"));
        if (atom < 0)
            dragAtom_ = graph_.addAtom(activeElement_, sketch.x(), sketch.y());
        const MolAtom& a = graph_.atoms()[static_cast<std::size_t>(dragAtom_)];
        dragStart_ = QPointF(a.x, a.y);
        dragCurrent_ = dragStart_;
        chainLength_ = 0;
        drag_ = Drag::Chain;
        break;
    }
    case Tool::AtomLabel: {
        if (atom >= 0) {
            beginAtomLabelEdit(atom);
        } else {
            pushUndo(tr("Add atom"));
            const int created =
                graph_.addAtom(activeElement_, sketch.x(), sketch.y());
            commit(tr("Added %1").arg(QLatin1String(
                core::Elements::data(activeElement_).symbol)));
            beginAtomLabelEdit(created);
        }
        drag_ = Drag::None;
        break;
    }
    case Tool::Caption: {
        if (caption >= 0) {
            beginCaptionEdit(caption, sketch);
        } else {
            beginCaptionEdit(-1, sketch);
        }
        drag_ = Drag::None;
        break;
    }
    case Tool::Charge: {
        if (atom >= 0) {
            pushUndo(tr("Charge"));
            MolAtom& a = graph_.atoms()[static_cast<std::size_t>(atom)];
            a.charge += (event->button() == Qt::RightButton
                         || (event->modifiers() & Qt::ShiftModifier))
                ? -1
                : 1;
            a.charge = std::clamp(a.charge, -4, 4);
            commit(tr("Formal charge %1%2")
                       .arg(a.charge > 0 ? QStringLiteral("+") : QString())
                       .arg(a.charge));
        }
        drag_ = Drag::None;
        break;
    }
    case Tool::Eraser: {
        if (atom >= 0) {
            pushUndo(tr("Erase"));
            graph_.removeAtom(atom);
            selectedAtoms_.clear();
            selectedCaptions_.clear();
            Q_EMIT selectionChanged();
            commit(tr("Erased an atom and its bonds."));
        } else if (bond >= 0) {
            pushUndo(tr("Erase"));
            graph_.removeBond(bond);
            commit(tr("Erased a bond."));
        } else if (caption >= 0) {
            pushUndo(tr("Erase"));
            graph_.removeCaption(caption);
            commit(tr("Erased a caption."));
        }
        drag_ = Drag::None;
        break;
    }
    case Tool::Ring: {
        pushUndo(tr("Ring"));
        if (bond >= 0) {
            if (core::fuseRing(graph_, bond, ringTemplate_)) {
                commit(tr("Fused %1 onto a bond.")
                           .arg(QLatin1String(core::ringTemplateName(ringTemplate_))));
            } else {
                // Naphthalene (and anything else that cannot be fused edge-on)
                // is stamped free-standing rather than silently doing nothing.
                core::stampRing(graph_, ringTemplate_, sketch.x(), sketch.y());
                commit(tr("%1 cannot be fused onto a bond — stamped it "
                          "separately.")
                           .arg(QLatin1String(core::ringTemplateName(ringTemplate_))));
            }
        } else if (atom >= 0) {
            // Onto an ATOM: fuse across whichever of its bonds is emptiest, or
            // hang the ring off it when it has none.
            const std::vector<int> attached = graph_.bondsAt(atom);
            if (!attached.empty()
                && core::fuseRing(graph_, attached.front(), ringTemplate_)) {
                commit(tr("Fused %1.")
                           .arg(QLatin1String(core::ringTemplateName(ringTemplate_))));
            } else {
                const MolAtom& a = graph_.atoms()[static_cast<std::size_t>(atom)];
                const double angle = freeAngleAt(atom);
                const int size = core::ringTemplateSize(ringTemplate_);
                const double radius = MoleculeGraph::kBondLength
                    / (2.0 * std::sin(core::kPi / std::max(3, size)));
                core::stampRing(graph_, ringTemplate_,
                                a.x + radius * std::cos(angle),
                                a.y + radius * std::sin(angle));
                commit(tr("Stamped %1.")
                           .arg(QLatin1String(core::ringTemplateName(ringTemplate_))));
            }
        } else {
            core::stampRing(graph_, ringTemplate_, sketch.x(), sketch.y());
            commit(tr("Stamped %1.")
                       .arg(QLatin1String(core::ringTemplateName(ringTemplate_))));
        }
        drag_ = Drag::None;
        break;
    }
    }
    update();
}

void MoleculeCanvas::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF sketch = toSketch(event->position());

    if (drag_ == Drag::None) {
        const int atom = graph_.atomAt(sketch.x(), sketch.y(), kAtomHit);
        const int bond = atom >= 0
            ? -1
            : graph_.bondAt(sketch.x(), sketch.y(), kBondHit);
        if (atom != hoverAtom_ || bond != hoverBond_) {
            hoverAtom_ = atom;
            hoverBond_ = bond;
            update();
        }
        return;
    }

    dragMoved_ = true;

    switch (drag_) {
    case Drag::Pan: {
        origin_ += event->position() - panAnchor_;
        panAnchor_ = event->position();
        update();
        break;
    }
    case Drag::Bond: {
        dragCurrent_ = snapBondEnd(dragStart_, sketch);
        update();
        break;
    }
    case Drag::Chain: {
        // Live length count: one link per bond length dragged.
        const double distance = std::hypot(sketch.x() - dragStart_.x(),
                                           sketch.y() - dragStart_.y());
        const int wanted = std::clamp(
            static_cast<int>(std::round(distance / MoleculeGraph::kBondLength)),
            0, 60);
        dragCurrent_ = sketch;
        if (wanted != chainLength_) {
            chainLength_ = wanted;
            Q_EMIT statusMessage(tr("Chain: %n carbon(s)", "", chainLength_));
        }
        update();
        break;
    }
    case Drag::Move: {
        const QPointF delta = sketch - dragCurrent_;
        graph_.translate(selectedAtoms(),
                         {selectedCaptions_.begin(), selectedCaptions_.end()},
                         delta.x(), delta.y());
        dragCurrent_ = sketch;
        update();
        break;
    }
    case Drag::RubberBand: {
        dragCurrent_ = sketch;
        const double x0 = std::min(dragStart_.x(), dragCurrent_.x());
        const double x1 = std::max(dragStart_.x(), dragCurrent_.x());
        const double y0 = std::min(dragStart_.y(), dragCurrent_.y());
        const double y1 = std::max(dragStart_.y(), dragCurrent_.y());
        selectedAtoms_ = bandBaseAtoms_;
        for (int i = 0; i < graph_.atomCount(); ++i) {
            const MolAtom& a = graph_.atoms()[static_cast<std::size_t>(i)];
            if (a.x >= x0 && a.x <= x1 && a.y >= y0 && a.y <= y1)
                selectedAtoms_.insert(i);
        }
        Q_EMIT selectionChanged();
        update();
        break;
    }
    default:
        break;
    }
}

void MoleculeCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    const QPointF sketch = toSketch(event->position());

    switch (drag_) {
    case Drag::Pan:
        setCursor(tool_ == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        break;
    case Drag::Bond: {
        const int target = graph_.atomAt(sketch.x(), sketch.y(), kAtomHit);
        if (target >= 0 && target != dragAtom_) {
            graph_.addBond(dragAtom_, target, orderForTool(), stereoForTool());
            commit(tr("Bonded two atoms."));
        } else {
            // No drag at all: put the new bond in the emptiest free direction,
            // so repeated clicks on a carbon walk out a chain.
            QPointF end = dragCurrent_;
            if (!dragMoved_) {
                const double angle = freeAngleAt(dragAtom_);
                end = QPointF(
                    dragStart_.x() + MoleculeGraph::kBondLength * std::cos(angle),
                    dragStart_.y() + MoleculeGraph::kBondLength * std::sin(angle));
            }
            const int created = graph_.addAtom(activeElement_, end.x(), end.y());
            graph_.addBond(dragAtom_, created, orderForTool(), stereoForTool());
            commit(tr("Drew a bond."));
        }
        break;
    }
    case Drag::Chain: {
        if (chainLength_ > 0) {
            const double heading =
                angleOf(dragStart_, sketch) * 180.0 / core::kPi;
            core::growChain(graph_, dragAtom_, heading, chainLength_);
            commit(tr("Added a chain of %n carbon(s).", "", chainLength_));
        } else {
            // A click with no drag: nothing was added, so drop the undo step
            // rather than leaving an entry that undoes nothing visible.
            if (!undoStack_.empty()) {
                graph_ = undoStack_.back();
                undoStack_.pop_back();
            }
            update();
        }
        chainLength_ = 0;
        break;
    }
    case Drag::Move: {
        if (!dragMoved_ && !undoStack_.empty()) {
            // A plain click that selected without moving: no geometry changed.
            undoStack_.pop_back();
        } else if (dragMoved_) {
            commit(QString());
        }
        break;
    }
    case Drag::RubberBand: {
        Q_EMIT statusMessage(
            selectedAtoms_.empty()
                ? tr("Nothing selected.")
                : tr("%n atom(s) selected.", "", static_cast<int>(selectedAtoms_.size())));
        break;
    }
    default:
        break;
    }

    drag_ = Drag::None;
    dragAtom_ = -1;
    dragBond_ = -1;
    dragMoved_ = false;
    update();
}

void MoleculeCanvas::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (std::fabs(steps) < 1e-6)
        return;
    // Zoom about the POINTER, not the widget centre: zooming toward the
    // cursor is what every drawing program does and what makes a deep zoom
    // usable without a pan between every step.
    const QPointF anchor = toSketch(event->position());
    scale_ = std::clamp(scale_ * std::pow(1.15, steps), 12.0, 400.0);
    const QPointF after = toScreen(anchor.x(), anchor.y());
    origin_ += event->position() - after;
    event->accept();
    update();
}

void MoleculeCanvas::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelection();
        event->accept();
        return;
    case Qt::Key_Escape:
        finishInlineEdit();
        selectedAtoms_.clear();
        selectedCaptions_.clear();
        Q_EMIT selectionChanged();
        update();
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// Inline text entry
// ---------------------------------------------------------------------------

void MoleculeCanvas::beginAtomLabelEdit(int atom)
{
    finishInlineEdit();
    if (atom < 0 || atom >= graph_.atomCount())
        return;
    const MolAtom& a = graph_.atoms()[static_cast<std::size_t>(atom)];
    editingAtom_ = atom;
    editingCaption_ = -1;
    inlineEdit_ = new QLineEdit(this);
    inlineEdit_->setObjectName(QStringLiteral("moleculeAtomEdit"));
    inlineEdit_->setText(
        QLatin1String(core::Elements::data(a.atomicNumber).symbol));
    inlineEdit_->selectAll();
    inlineEdit_->setAlignment(Qt::AlignCenter);
    inlineEdit_->setFixedWidth(64);
    const QPointF at = toScreen(a.x, a.y);
    inlineEdit_->move(static_cast<int>(at.x()) - 32,
                      static_cast<int>(at.y()) - inlineEdit_->sizeHint().height() / 2);
    inlineEdit_->show();
    inlineEdit_->setFocus();
    connect(inlineEdit_, &QLineEdit::editingFinished, this,
            &MoleculeCanvas::finishInlineEdit);
    connect(inlineEdit_, &QLineEdit::returnPressed, this,
            &MoleculeCanvas::finishInlineEdit);
}

void MoleculeCanvas::beginCaptionEdit(int caption, const QPointF& sketchPosition)
{
    finishInlineEdit();
    editingAtom_ = -1;
    editingCaption_ = caption;
    editingPosition_ = sketchPosition;
    inlineEdit_ = new QLineEdit(this);
    inlineEdit_->setObjectName(QStringLiteral("moleculeCaptionEdit"));
    if (caption >= 0 && caption < static_cast<int>(graph_.captions().size())) {
        const core::MolCaption& existing =
            graph_.captions()[static_cast<std::size_t>(caption)];
        inlineEdit_->setText(QString::fromStdString(existing.text));
        editingPosition_ = QPointF(existing.x, existing.y);
    }
    inlineEdit_->selectAll();
    inlineEdit_->setFixedWidth(180);
    const QPointF at = toScreen(editingPosition_.x(), editingPosition_.y());
    inlineEdit_->move(static_cast<int>(at.x()), static_cast<int>(at.y()));
    inlineEdit_->show();
    inlineEdit_->setFocus();
    connect(inlineEdit_, &QLineEdit::editingFinished, this,
            &MoleculeCanvas::finishInlineEdit);
    connect(inlineEdit_, &QLineEdit::returnPressed, this,
            &MoleculeCanvas::finishInlineEdit);
}

void MoleculeCanvas::finishInlineEdit()
{
    if (!inlineEdit_)
        return;
    // Detach FIRST: deleteLater() plus a still-connected editingFinished()
    // re-enters this function while the widget is being torn down.
    QLineEdit* editor = inlineEdit_;
    inlineEdit_ = nullptr;
    editor->disconnect(this);
    const QString text = editor->text().trimmed();
    editor->deleteLater();

    const int atom = editingAtom_;
    const int caption = editingCaption_;
    editingAtom_ = -1;
    editingCaption_ = -1;

    if (atom >= 0 && atom < graph_.atomCount()) {
        if (text.isEmpty())
            return;
        const int z = core::Elements::atomicNumber(text.toStdString());
        if (z <= 0) {
            // The app's standard input-validation style: refuse, say what was
            // wrong, change nothing.
            Q_EMIT statusMessage(
                tr("\"%1\" is not a chemical element — the atom was left "
                   "unchanged.")
                    .arg(text));
            return;
        }
        if (graph_.atoms()[static_cast<std::size_t>(atom)].atomicNumber == z)
            return;
        pushUndo(tr("Relabel atom"));
        MolAtom& a = graph_.atoms()[static_cast<std::size_t>(atom)];
        a.atomicNumber = z;
        // Changing the element invalidates a pinned hydrogen count, which was
        // a statement about the OLD element.
        a.explicitHydrogens = -1;
        activeElement_ = z;
        commit(tr("Set the atom to %1.")
                   .arg(QLatin1String(core::Elements::data(z).symbol)));
        return;
    }

    if (caption >= 0 && caption < static_cast<int>(graph_.captions().size())) {
        pushUndo(tr("Edit caption"));
        if (text.isEmpty())
            graph_.removeCaption(caption);
        else
            graph_.captions()[static_cast<std::size_t>(caption)].text =
                text.toStdString();
        commit(QString());
        return;
    }

    if (!text.isEmpty()) {
        pushUndo(tr("Add caption"));
        core::MolCaption fresh;
        fresh.text = text.toStdString();
        fresh.x = editingPosition_.x();
        fresh.y = editingPosition_.y();
        graph_.captions().push_back(fresh);
        commit(tr("Added a caption."));
    }
}

} // namespace calango::gui
