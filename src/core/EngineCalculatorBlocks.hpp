#pragma once

#include "core/CalculatorConfig.hpp"

#include <sstream>

namespace calango::core {

/// Calculator-construction blocks for the engines added after the original
/// set — ABINIT, FHI-aims, NWChem, OpenMX, FLEUR, CP2K, Amber.
///
/// A file of their own rather than seven more functions in
/// AseScriptGenerator.cpp (already ~2600 lines): each block is self-contained,
/// they share nothing but the CalculatorConfig they read, and the dispatch in
/// AseScriptGenerator is one line per engine either way.
///
/// Every function appends to `out` and leaves `atoms.calc` bound to a real ASE
/// calculator, which is the contract the rest of the generated script depends
/// on — it is what lets the ASE optimizers, molecular dynamics and vibrational
/// modules drive these codes without any of them being special-cased.
///
/// Six come from `ase.calculators.*` directly. FLEUR comes from the `ase-fleur`
/// package, which is where ASE's own `ase.calculators.fleur` stub redirects: an
/// ASE calculator like the rest, distributed separately.
namespace EngineBlocks {

void emitAbinit(std::ostringstream& out, const CalculatorConfig& config);
void emitAims(std::ostringstream& out, const CalculatorConfig& config);
void emitNwChem(std::ostringstream& out, const CalculatorConfig& config);
void emitOpenMx(std::ostringstream& out, const CalculatorConfig& config);
void emitFleur(std::ostringstream& out, const CalculatorConfig& config);
void emitCp2k(std::ostringstream& out, const CalculatorConfig& config);
void emitAmber(std::ostringstream& out, const CalculatorConfig& config);

} // namespace EngineBlocks

} // namespace calango::core
