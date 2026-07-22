#include "python_bridge/PubChem.hpp"

#include "python_bridge/AseBridge.hpp"

#include <pybind11/embed.h>

#include <stdexcept>

namespace py = pybind11;

namespace calango::pybridge {

core::Structure PubChem::fetchStructure(const std::string& query,
                                        const std::string& field)
{
    if (query.empty())
        throw std::runtime_error("Enter a PubChem query.");
    try {
        py::dict scope;
        scope["query"] = query;
        scope["field"] = field;
        // ase.data.pubchem.pubchem_atoms_search(name=/cid=/smiles=) hits the
        // PubChem PUG REST endpoint and returns an Atoms with the 3D conformer.
        // Executed with a single dict as both globals and locals.
        py::exec(R"PY(
from ase.data.pubchem import pubchem_atoms_search

result_atoms = pubchem_atoms_search(**{field: query})
)PY",
                 scope, scope);
        return AseBridge::fromAtoms(scope["result_atoms"]);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(
            std::string("PubChem search failed (no match, or network error):\n")
            + e.what());
    }
}

} // namespace calango::pybridge
