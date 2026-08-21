#pragma once

#include "core/Structure.hpp"
#include "dft/DftTypes.hpp"

#include <string>

/// A MINIMAL extxyz reader, scoped to exactly what this engine's own
/// Python wrapper writes: `ase.io.write("structure.extxyz", atoms)` with no
/// extra per-atom arrays attached — species + Cartesian position only, plus
/// the standard `Lattice="..." pbc="..."` comment-line keys ASE always
/// emits. NOT a general extxyz parser (arbitrary extended Properties=,
/// per-atom extra columns, non-ASE writers' quoting quirks are all
/// unhandled — see FUTURE.md) — Calango's own generated Python controls the
/// writer side, so the reader only needs to trust ITS OWN output.
namespace calango::dftb {

dft::Outcome loadExtxyzStructure(const std::string& path, core::Structure& out);

} // namespace calango::dftb
