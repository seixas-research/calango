#pragma once

#include "core/Structure.hpp"

namespace calango::core {

/// Options for completeWithHydrogens().
struct HydrogenCompletionOptions {
    /// Bond-perception cutoff factor, matching Structure::detectBonds() — the
    /// same value the viewport draws with, so what the user sees bonded is
    /// what counts against an atom's valence.
    double bondTolerance = 1.15;
    /// Distance-based bond perception on/off; manual bonds always count.
    bool autoBonds = true;
    /// X–H bond length in Å. 0 derives it from the covalent radii of the two
    /// elements, which gives ~1.09 Å for C–H and ~0.97 Å for O–H.
    double bondLength = 0.0;
};

/// What completeWithHydrogens() did, for the status line and the undo label.
struct HydrogenCompletionResult {
    /// Hydrogens appended to the structure.
    int added = 0;
    /// Heavy atoms that gained at least one hydrogen.
    int completedAtoms = 0;
    /// Atoms whose element has no tabulated organic valence (metals,
    /// transition metals, noble gases) and was therefore left alone. Reported
    /// rather than guessed at: the "right" hydrogen count on a Ru centre is a
    /// chemistry decision, not a geometry one.
    int skippedAtoms = 0;
};

/// Add the hydrogens implied by each heavy atom's standard valence.
///
/// For every atom of a main-group element with a well-defined organic valence
/// (C 4, N 3, O 2, halogens 1, and their heavier congeners), the perceived
/// bonds are summed — aromatic bonds counting 1.5 — and the shortfall against
/// that valence is filled with hydrogens. Atoms already saturated, hydrogens
/// themselves, and elements outside the table are untouched.
///
/// Placement is geometric, not tabulated. The existing neighbour directions
/// are held fixed on the unit sphere and the new ones relax against them under
/// mutual repulsion, so the result falls out at the geometry the coordination
/// number implies — tetrahedral on a CH2, trigonal on a carbonyl carbon,
/// bent on an oxygen — without a special case per hybridization. Perceived
/// bonds across a periodic boundary are taken at the bonded IMAGE, so an atom
/// at a cell face is not handed a hydrogen pointing into its own neighbour.
///
/// The structure is modified in place; hydrogens are appended, so existing
/// atom indices (and every selection, constraint and manual bond that refers
/// to them) stay valid.
HydrogenCompletionResult completeWithHydrogens(
    Structure& structure, const HydrogenCompletionOptions& options = {});

/// Standard organic valence of element `z`, or 0 when it has none tabulated —
/// the test for "can this atom be completed at all".
int standardValence(int z);

} // namespace calango::core
