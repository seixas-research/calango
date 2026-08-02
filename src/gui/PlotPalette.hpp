#pragma once

#include <QColor>

namespace calango::gui {

/// The one light palette every 2D data plot in Calango paints with.
///
/// The plots used to follow the application chrome: a near-black canvas with
/// pale ink, which reads well beside a dark viewport and badly everywhere a
/// scientific plot actually goes. A band structure dropped into a paper, a
/// slide or a referee reply is expected on white — the convention is old enough
/// that an inverted figure reads as an artifact of the tool rather than as a
/// deliberate choice — and every print path (PNG export, SVG export, the
/// system print dialog) either wastes ink on the fill or silently loses thin
/// pale curves on white paper.
///
/// So the CANVAS is fixed and the INTERFACE is not. The window around the plot
/// — docks, toolbars, tabs, the results panel — still follows the user's
/// Dark / Light / System theme; what is standardized here is only the rectangle
/// the data is drawn in, and only its defaults: the viewers that expose a
/// "Customize Appearance…" dialog (band/PDOS, optics, convergence) still write
/// whatever the user picks into their own style struct. This is the starting
/// point they open on, not a ceiling.
///
/// Values are shared rather than repeated per widget because the plots are
/// routinely read side by side — a dispersion beside its PDOS, an energy trace
/// beside a force trace — and a grid that is one shade different in each panel
/// is visible as a seam even when no single panel looks wrong.
namespace PlotPalette {

/// The plot canvas. Pure white, not an off-white: it is the same surface the
/// exported PNG/SVG lands on, and a near-white fill turns into a visible grey
/// rectangle the moment the figure is placed on a white page.
inline const QColor canvas{255, 255, 255};

/// Axis frame (the "spines") and the tick marks on them — the darkest ink in
/// the figure after the curves themselves.
inline const QColor spine{60, 64, 72};

/// In-plot grid lines. Light enough to sit behind the data rather than
/// competing with it: a grid that is legible on its own is too strong.
inline const QColor grid{214, 218, 224};

/// Tick-label numbers.
inline const QColor tickText{74, 79, 87};

/// Axis titles, legends, in-plot annotations — the text that must be read.
inline const QColor text{27, 30, 35};

/// "No data yet" / "press Compute" placeholders, and any other text that
/// describes the widget rather than the data.
inline const QColor placeholder{122, 127, 136};

/// Default first curve. The matplotlib "tab10" blue, so a Calango figure and a
/// matplotlib one made from the same exported data look like the same plot.
inline const QColor series{0x1f, 0x77, 0xb4};

/// Second series / second spin channel: tab10's orange-red, chosen against the
/// blue for deuteranopia as well as for print.
inline const QColor seriesAlt{0xd6, 0x27, 0x28};

/// Reference lines the data is read against — E_F, ω = 0, a convergence
/// target. Drawn dashed, so the colour only has to be distinguishable from the
/// curves, not louder than them.
inline const QColor reference{0xd9, 0x53, 0x0e};

/// Cursor / hover highlight: the crosshair and read-out marker that follow the
/// pointer. Deliberately not `reference` — a transient pointer artifact that
/// looked like the Fermi level was a real misreading.
inline const QColor highlight{0x7f, 0x3f, 0xbf};

/// Fill behind an in-plot read-out box, so a label crossing a curve stays
/// legible. Semi-transparent white rather than opaque: the curve underneath
/// should still be traceable.
inline const QColor readoutFill{255, 255, 255, 220};

} // namespace PlotPalette

} // namespace calango::gui
