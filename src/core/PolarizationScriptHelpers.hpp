#pragma once

#include <string>

namespace calango::core {

/// Shared Python source for the GPAW Berry-phase TOTAL polarization phase —
/// the one piece of the modern (Berry-phase) theory of polarization that
/// every DFT-level consumer in this codebase needs verbatim.
///
/// Factored out of BornChargesScriptGenerator (which pioneered this GPAW
/// calling-convention shim for its atomic-displacement finite difference)
/// so that PiezoelectricScriptGenerator's cell-strain finite difference
/// reuses the identical polarization evaluation rather than a second,
/// independently-written copy that could silently drift onto a different
/// GPAW version's calling convention. Each generator keeps its OWN
/// branch-fixing logic on top of this, because the two differ in kind: Born
/// charges only ever needs a single +/- pair's signed difference
/// (BornChargesScriptGenerator's own `phase_difference`), while the
/// piezoelectric tensor differences a whole ORDERED series of strain points
/// (`np.unwrap`, in PiezoelectricScriptGenerator) — sharing that part too
/// would force one of the two use cases into the other's shape.
///
/// The two functions below assume the caller's preamble has already put
/// `numpy as np`, `os`, `pathlib.Path` and GPAW's own imports in scope —
/// see AseScriptGenerator::gpawImports() and jsonLoggerPreamble(). Emit
/// berryPhaseImportShim() once, then polarizationPhaseCFunction() once; the
/// generated `polarization_phase_c(calc, tag)` is then callable for every
/// displaced/strained geometry that follows.
std::string berryPhaseImportShim();
std::string polarizationPhaseCFunction();

} // namespace calango::core
