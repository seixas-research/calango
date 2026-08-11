#pragma once

#include <QColor>
#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

#include <vector>

#include <QStringList>

#include <functional>

class QAbstractItemView;
class QCheckBox;
class QGroupBox;
class QJsonArray;
class QPainter;
class QPushButton;
class QRectF;
class QWidget;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Small helpers shared across the widget layer. Each of these existed as
/// an identical private copy in two or three files; they live here so a fix
/// (or a style change) lands everywhere at once.

/// Paint a color-swatch button — the "click to pick a color" buttons in the
/// Representation, Lighting and Unit Cell & Axes panels.
void setButtonColor(QPushButton* button, const QColor& color);

/// Hide/show the QFormLayout row (label + field) that `field` occupies inside
/// `group`'s form layout. No-op when the group has no form layout or the field
/// is not in it.
///
/// The reason every engine settings group reaches for this: an inapplicable
/// control that is merely DISABLED reads as broken, while one that is absent
/// reads as "this engine does not have that". A basis set greyed out under a
/// plane-wave module looks like a bug in the dialog; a basis set that is not
/// there looks like the truth, which it is.
void setFormRowVisible(QGroupBox* group, QWidget* field, bool visible);

/// The label a volumetric file registered from a finished run should carry in
/// the Volumetric Data dock — "Hartree potential" rather than
/// `potential_hartree.cube`.
///
/// Covers every field the generators write (core::densityFiles) plus the VASP
/// grids, which are recognized by name because they carry no extension. An
/// unknown name maps to ITSELF: a cube dropped in by hand is still a grid the
/// user wants to see, and its file name is the only honest label for it.
///
/// A free function rather than a table inside MainWindow so the mapping can be
/// pinned by a test — it drifted out of sync with the generator once already.
QString volumetricDisplayName(const QString& fileName);

/// Stop an editable table from opening its editor on a bare keystroke.
///
/// This is a CRASH FIX, not a preference. On macOS a dead key (´ ` ~ ^ — the
/// first stroke of an accented character, and unavoidable on a Portuguese or
/// Spanish layout) reaches an item view as a QInputMethodEvent. Qt's
/// QAbstractItemView::inputMethodEvent responds with
///
///     edit(currentIndex(), AnyKeyPressed, event)
///
/// which opens the cell editor and calls setFocus() on it. On the Cocoa
/// platform that makes QCocoaInputContext commit the pending dead key, which
/// sends the input-method event again — and because the editor has not become
/// the focus object yet, it arrives back at the VIEW. inputMethodEvent calls
/// edit() again, and the cycle repeats until the 8 MB stack is gone:
///
///     inputMethodEvent -> edit -> setFocus -> commit -> unmarkText
///                      -> inputMethodEvent -> ...
///
/// Removing AnyKeyPressed from the edit triggers breaks it at the first link:
/// edit() refuses the trigger and returns false, so nothing is focused and
/// nothing is committed. Double-click, Return and F2 still start an edit, and
/// typing INTO an open editor is unaffected — the editor is the focus object
/// by then, so the event never reaches the view.
///
/// Apply this to every item view whose items carry Qt::ItemIsEditable. A view
/// that is already NoEditTriggers cannot reach the loop and does not need it.
void disableTypeToEdit(QAbstractItemView* view);

/// The chemical species present in `structure`, sorted and unique.
///
/// Every wizard that offers the Hubbard editor needs this to seed its element
/// completer, and each had grown its own identical copy — a completer that
/// repeats "Fe" once per Fe atom is unusable, so all of them had to sort and
/// de-duplicate. A null structure gives an empty list rather than a crash,
/// because several callers hold one optionally.
QStringList structureElements(const core::Structure* structure);

/// The cell axis that carries the vacuum of a slab (0/1/2), or -1 when nothing
/// in the geometry looks like one.
///
/// A SEED, not an answer. A thick slab in a modest cell and a thin one in a
/// huge cell are not reliably distinguishable from the coordinates, so every
/// caller offers this as the initial value of a control the user can correct —
/// getting it wrong rescales every 2D quantity by the wrong length, silently,
/// and no wizard should decide that on the user's behalf.
///
/// Shared because two response modules (linear Optics and Nonlinear Optics)
/// divide the same vacuum thickness back out of the same kind of supercell
/// answer, and a second copy of the heuristic would be a second thing to keep
/// in step with the first.
int guessVacuumAxis(const core::Structure* structure);

/// Parse a user-typed atom index list — "0, 2, 5-8": commas and/or whitespace
/// separate entries, a dash makes a closed range. Out-of-range entries are
/// DROPPED rather than clamped — a typo that silently addressed a different
/// atom would be worse than one that visibly does nothing. `atomCount == 0`
/// disables the upper bound. Returns a sorted, deduplicated list; empty text
/// yields an empty vector (callers treat that as "every atom").
std::vector<int> parseAtomIndexList(const QString& text, int atomCount);

/// Write `body` to `path` atomically, reporting failure to the user.
///
/// The seven export actions in this layer (JSON, CSV, OBJ, plot data …) each
/// carried the same five lines: open a QSaveFile, warn and bail on failure,
/// stream through a QTextStream, commit. The differences were only the text
/// being written — so that is what the callback supplies, and the error path
/// exists once.
///
/// Returns false when the file could not be opened or committed; the warning
/// has already been shown in that case.
bool writeTextFile(QWidget* parent, const QString& path,
                   const std::function<void(QTextStream&)>& body);
/// Convenience overload for a body that is already a string.
bool writeTextFile(QWidget* parent, const QString& path, const QString& body);

/// The Jmol CPK colour of an element, straight from the element table.
///
/// Deliberately the RAW convention rather than the viewport's resolved colour:
/// the periodic-table dialog is opened from places that have no viewport (the
/// Edit Structure dialog among them), and a table that changed colour
/// depending on which window opened it would be worse than one that is always
/// the standard. Per-element user overrides therefore do NOT show here.
QColor cpkColor(int atomicNumber);

/// Near-black or white, whichever CONTRASTS MORE with `background` (WCAG
/// relative luminance).
///
/// CPK spans the full range — hydrogen is pure white, nitrogen a deep blue —
/// so a fixed text colour is unreadable at one end or the other. Choosing by
/// contrast rather than by a luminance threshold is both simpler and strictly
/// better; see the implementation for the measurement over all 119 elements.
QColor readableTextColor(const QColor& background);

/// Parse a JSON object from `path`. Returns an empty object when the file
/// cannot be opened or does not parse; callers treat "empty" as "no data"
/// (the result viewers are opened against job directories that may legally
/// be missing a given artifact).
QJsonObject readJsonObject(const QString& path);

/// Draw `text` centered in `box`, rendering "_x" (or "_{xy}") as a
/// typographic subscript — smaller font, dropped baseline. Qt ships no LaTeX
/// engine and this project carries no QCustomPlot/MathJax dependency, so this
/// two-run layout is what actually produces "E − E_F (eV)" with a real
/// subscript rather than a literal underscore. Shared by the band/PDOS plot
/// and the effective-band heatmap.
void drawWithSubscripts(QPainter& painter, const QRectF& box,
                        const QString& text);

/// A check box whose caption is RICH TEXT — the widget-layer counterpart of
/// drawWithSubscripts().
///
/// QCheckBox draws its text plainly, so "E<sub>F</sub>" would appear as literal
/// markup and the usual fallback, "E_F", as a literal underscore. This pairs an
/// unlabelled check box with a QLabel that does render the markup, and forwards
/// clicks on the caption to the box so it still behaves like one widget.
///
/// Returns the row widget to put in the layout; `box` receives the check box
/// itself, which is what callers connect to and read.
QWidget* richTextCheckBox(const QString& html, QCheckBox*& box,
                          QWidget* parent);

/// A QDoubleSpinBox that renders its value COMPACTLY: three significant
/// figures, switching to exponential notation ("1.23e-2") when a fixed-point
/// rendering would be misleading or too wide.
///
/// The physical properties the color-mapping bounds cover span many orders of
/// magnitude — partial charges around 1e-2 e, magnetic moments around 1 μB,
/// forces up to 1e2 eV/Å. A fixed `decimals` cannot serve them all: set it low
/// and small values collapse to "0.000"; set it high and large values overflow
/// the field. Formatting by significance instead keeps every value both
/// readable and honest at a fixed width.
class CompactDoubleSpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    explicit CompactDoubleSpinBox(QWidget* parent = nullptr);

protected:
    QString textFromValue(double value) const override;
    double valueFromText(const QString& text) const override;
    /// Accept exponential input, which the base class's fixed-notation
    /// validator rejects outright.
    QValidator::State validate(QString& input, int& pos) const override;
};

/// JSON number array -> std::vector<double>. Non-numeric entries become 0.0,
/// matching QJsonValue::toDouble()'s contract.
std::vector<double> toDoubleVector(const QJsonArray& array);

/// Whether the GPAW wavefunctions (.gpw) an MLWF run localized are still
/// reachable — the pre-flight every Wannier post-process (interpolation,
/// Fermi surface, topological invariants) runs before staging a job that
/// would otherwise die on its first line. An MLWF started from a single-point
/// baseline read that baseline's .gpw and wrote none of its own, so a bare
/// glob of the MLWF directory finds nothing; the recorded path in
/// wannier.json is checked first. On failure `reason` (if non-null) receives
/// the user-facing explanation.
bool mlwfWavefunctionsAvailable(const QString& jobDir, QString* reason);

// ---------------------------------------------------------------------------
// Structure file I/O: one set of filters for the whole application
// ---------------------------------------------------------------------------
//
// EXTENDED XYZ IS THE DEFAULT EVERYWHERE. It is the only format in this list
// that round-trips everything a Calango document actually carries — cell and
// pbc flags, per-atom magnetic moments and charges, forces and velocities, and
// arbitrary extra columns — so it is what a save should produce unless the
// user deliberately asks for something a downstream code needs. Plain .xyz
// silently drops the cell; CIF drops the calculator results; POSCAR drops the
// element names into a separate line and everything else on the floor.
//
// The filter lists therefore all start with Extended XYZ, which is what
// QFileDialog pre-selects. The umbrella "all supported structures" filter is
// one entry down in the open dialogs, so nothing becomes unreachable.

/// `stem` with the default structure suffix (".extxyz"), sanitized for use as
/// a file name.
/// Empty or extension-only stems fall back to "structure".
QString defaultStructureFileName(const QString& stem);

/// ";;"-joined QFileDialog filters for OPENING structure files, Extended XYZ
/// first.
QString structureOpenFilters();
/// The same for multi-frame trajectories.
QString trajectoryOpenFilters();

/// (filter text, explicit ASE format) pairs for SAVING. An empty format means
/// "infer from the extension"; every entry here names its format explicitly so
/// that a user who types a bare name still gets the format they picked.
/// Extended XYZ is first in both lists.
const QList<QPair<QString, QString>>& structureSaveFormats();
const QList<QPair<QString, QString>>& trajectorySaveFormats();

/// Look up the ASE format belonging to `filter` in `formats`. Falls back to
/// the FIRST entry's format, which is the extxyz default — an unmatched filter
/// means the dialog returned something unexpected, and inferring from the
/// extension there is how a file ends up written in a format the user did not
/// choose.
QString formatForFilter(const QList<QPair<QString, QString>>& formats,
                        const QString& filter);

/// Append `filter`'s primary extension to `path` when the user typed a name
/// without one.
///
/// The static QFileDialog helpers have no setDefaultSuffix(), so "Save As"
/// with a bare name would otherwise produce an extension-less file that
/// nothing — not the app's own open dialog, not ASE's format sniffing — can
/// identify afterwards.
QString withFilterSuffix(const QString& path, const QString& filter);

/// "3m (C<sub>3v</sub>)" — the Hermann-Mauguin symbol with its Schönflies
/// counterpart appended, falling back to the plain H-M symbol when the
/// Schönflies form is unknown. Shared by the Symmetry and Raman Modes
/// dialogs so the two labels cannot drift apart.
QString pointGroupDisplay(const QString& hermannMauguin);

} // namespace calango::gui
