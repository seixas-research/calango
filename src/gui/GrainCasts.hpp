#pragma once

#include <QColor>

#include <vector>

namespace calango::gui {

/// Turning a builder's per-atom grain tags into one coloured cast per grain.
///
/// Split out of MainWindow::applyGrainCasts so the part with the physics and
/// the arithmetic in it — how many grains there are, which atom belongs to
/// which, and what colour each gets — can be tested without constructing a
/// main window, an OpenGL viewport and a document. What stays in MainWindow is
/// the part that is genuinely about the viewport: writing these results into
/// the render style and switching the scene to cast colouring.
///
/// Depends on QColor and nothing else. In particular NOT on
/// render::StructureRenderer, whose header pulls the whole OpenGL stack in
/// with it.
struct GrainCastAssignment {
    /// Number of grains found, or 0 when there is nothing worth colouring.
    ///
    /// Zero for an empty or ragged field, and also for a SINGLE grain: one
    /// grain is not a polycrystal, and a lone "Grain 1" cast covering every
    /// atom is a control that does nothing.
    int grainCount = 0;
    /// One colour per grain, `grainCount` long.
    std::vector<QColor> colors;
    /// Per atom, the cast index to assign: 1-based, so 0 stays free as the
    /// fallback cast an untagged atom falls into. Same length as the field.
    std::vector<int> atomCasts;
};

/// The colour for grain `index`, counting from 0.
///
/// Golden-angle hue rotation: consecutive grains land ~137.5 deg apart, so any
/// PREFIX of the sequence is well separated. That matters because the grain
/// count is whatever the user asked for — 2 to dozens — and a fixed palette
/// would either run out or spend its most distinguishable colours on a
/// two-grain structure that needs the least help.
///
/// Saturation and value alternate on a 3-cycle as well: past roughly a dozen
/// grains hue alone starts to repeat perceptually, and two TOUCHING grains of
/// the same apparent colour is precisely the thing grain casts exist to
/// prevent.
QColor grainCastColor(int index);

/// Read a per-atom grain field (as the builders write it into
/// `Structure::scalarFields()["grain"]`) into cast assignments.
///
/// The field is taken as authoritative rather than re-deriving the
/// tessellation: the assignment the geometry was BUILT from is the only one
/// guaranteed to agree with it, and a second nearest-seed pass would disagree
/// exactly at the seams, which is where it matters.
///
/// Grain ids are expected to be contiguous from 0 — which is what
/// SolidInterfaceBuilder guarantees and its test pins. An id outside
/// [0, grainCount) is dropped to the fallback cast rather than widening the
/// palette, so a malformed field cannot silently invent grains.
GrainCastAssignment grainCastsFor(const std::vector<double>& grainField);

} // namespace calango::gui
