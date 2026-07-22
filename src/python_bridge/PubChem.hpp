#pragma once

#include "core/Structure.hpp"

#include <string>

namespace calango::pybridge {

/// Online PubChem (PUG REST) structure retrieval via ASE's ase.data.pubchem
/// wrapper. GUI-thread, blocking; throws std::runtime_error on failure (no
/// match, network error, ambiguous query). No API key required.
class PubChem {
public:
    /// Fetch a 3D molecular conformer from PubChem. `field` is one of
    /// "name", "cid" or "smiles"; `query` is the corresponding value.
    static core::Structure fetchStructure(const std::string& query,
                                          const std::string& field);
};

} // namespace calango::pybridge
