#pragma once

#include "core/Structure.hpp"

#include <cstddef>
#include <vector>

namespace calango::core {

/// Whole-structure geometric transformations.
///
/// These used to live as private slots of the Edit Structure dialog, operating
/// on its working copy. They are now reachable from the Structure panel as
/// well, which mutates the DOCUMENT through the undo stack — so the operations
/// themselves had to stop being tied to a dialog's widgets. Pure functions over
/// a Structure: no Qt, no widgets, no undo, which is also what makes them
/// checkable without a GUI.

/// Translate every atom so the structure's centroid sits at the centre of its
/// cell. A no-op without a cell (there is no centre to move to) or with no
/// atoms.
void centerInCell(Structure& structure);

/// How much vacuum to add, and along which lattice directions.
struct VacuumOptions {
    double thickness = 10.0; ///< Å added along each selected direction
    /// Which lattice vectors to extend. Lattice directions rather than
    /// Cartesian axes: vacuum has to grow along the cell vector to stay
    /// commensurate with a non-orthogonal cell.
    bool axes[3] = {false, false, true};
    /// Split the added length evenly, leaving the structure centred along that
    /// direction — the usual choice for slabs and clusters. Off puts the whole
    /// amount past the structure on the far side only.
    bool bothSides = true;
    /// Clear pbc along the padded directions. Vacuum is normally added
    /// precisely to decouple periodic images, and this makes that explicit to
    /// the calculators.
    bool clearPbc = true;
};

/// Extend the cell along the selected lattice directions. Returns false (and
/// changes nothing) without a cell, with a non-positive thickness, or with no
/// direction selected.
bool addVacuum(Structure& structure, const VacuumOptions& options);

/// Translate the given atoms by whole lattice vectors until their fractional
/// coordinates lie in [0, 1). An empty `indices` wraps every atom.
///
/// Only the PERIODIC axes wrap: folding a slab's vacuum direction back into the
/// box would push atoms through the vacuum they were placed in. A cell with no
/// periodic axis at all is a plain bounding box, and there the drawn boundaries
/// are exactly what "within the cell" means, so all three wrap.
///
/// Returns the number of atoms actually moved, so a caller can tell "nothing to
/// do" from "done" — they look identical on screen.
int wrapIntoCell(Structure& structure, const std::vector<std::size_t>& indices);

} // namespace calango::core
