#pragma once

#include "core/Structure.hpp"

#include <string>

namespace calango::core {

/// Reader/writer for PDBx/mmCIF — the format the Protein Data Bank actually
/// distributes structures in today, and the successor to the fixed-column PDB
/// file.
///
/// This is NOT the same thing as the small-molecule CIF that a crystallography
/// program writes, even though both end in `.cif` and both use the STAR
/// syntax. A small-molecule CIF puts FRACTIONAL coordinates in
/// `_atom_site_fract_x` under a symmetry group that generates the rest of the
/// cell; a PDBx file puts CARTESIAN coordinates in `_atom_site.Cartn_x` for
/// every atom of every chain, with residue and chain annotation attached.
/// ASE's CIF reader understands the former and fails outright on the latter,
/// which is why this parser exists rather than another ase.io call.
///
/// Only the `_atom_site` loop is read — coordinates, element, and the residue /
/// chain annotation that makes a ribbon diagram or a chain colouring possible.
/// The rest of a PDBx file (experimental metadata, revision history, sequence
/// databases) is deliberately ignored: it is not structure.
namespace PdbxFile {

/// True when `path` looks like PDBx/mmCIF rather than a small-molecule CIF.
///
/// Decided by CONTENT, not by extension: both formats use `.cif`, so the only
/// honest test is which `_atom_site` flavour the file actually contains. A file
/// with `_atom_site.Cartn_x` is PDBx; one with `_atom_site_fract_x` is a
/// crystallographic CIF and belongs to ASE's reader.
bool looksLikePdbx(const std::string& path);

/// Parse the `_atom_site` loop into a Structure with residue annotation, and
/// the cell from `_cell.length_a` … when the file carries one (many
/// cryo-EM/NMR entries do not, and the structure then has no cell).
///
/// Throws std::runtime_error with a readable message when the file cannot be
/// opened or carries no atom sites.
Structure read(const std::string& path);

/// Write `structure` as a minimal but valid PDBx/mmCIF: the entry block, the
/// cell (when defined) and a full `_atom_site` loop. Residue annotation is
/// round-tripped when present; atoms without it are written as a single
/// synthetic chain of one-atom residues named after their element, which is
/// what keeps a non-protein structure loadable by other PDBx readers.
///
/// Throws std::runtime_error when the file cannot be written.
void write(const Structure& structure, const std::string& path);

} // namespace PdbxFile
} // namespace calango::core
