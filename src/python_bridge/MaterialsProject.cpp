#include "python_bridge/MaterialsProject.hpp"

#include "python_bridge/AseBridge.hpp"

#include <pybind11/embed.h>

#include <stdexcept>

namespace py = pybind11;

namespace calango::pybridge {

core::Structure MaterialsProject::fetchStructure(const std::string& materialId,
                                                 const std::string& apiKey)
{
    if (materialId.empty())
        throw std::runtime_error("Enter a Materials Project ID (e.g. mp-149)");
    if (apiKey.empty())
        throw std::runtime_error(
            "Enter your Materials Project API key (materialsproject.org/api)");

    try {
        py::dict locals;
        locals["mp_id"] = materialId;
        locals["api_key"] = apiKey;

        // The summary endpoint returns the structure as a pymatgen dict;
        // we convert it to ase.Atoms manually (lattice matrix + sites), so
        // neither pymatgen nor mp-api needs to be installed.
        py::exec(R"PY(
import json
import urllib.error
import urllib.request

import ase

url = f"https://api.materialsproject.org/materials/summary/{mp_id}/?_fields=structure"
request = urllib.request.Request(
    url, headers={"X-API-KEY": api_key, "accept": "application/json"})
try:
    with urllib.request.urlopen(request, timeout=20) as response:
        payload = json.load(response)
except urllib.error.HTTPError as error:
    if error.code in (401, 403):
        raise RuntimeError("Materials Project rejected the API key "
                           f"(HTTP {error.code}).") from None
    if error.code == 404:
        raise RuntimeError(f"No entry found for '{mp_id}'.") from None
    raise RuntimeError(f"Materials Project request failed (HTTP {error.code}).") from None
except urllib.error.URLError as error:
    raise RuntimeError(f"Network error contacting Materials Project: {error.reason}") from None

data = payload.get("data") or []
if not data or "structure" not in data[0]:
    raise RuntimeError(f"No structure returned for '{mp_id}'.")

s = data[0]["structure"]
lattice = s["lattice"]["matrix"]
symbols = []
fractional = []
for site in s["sites"]:
    species = max(site["species"], key=lambda entry: entry.get("occu", 1.0))
    symbols.append(species["element"])
    fractional.append(site["abc"])

atoms = ase.Atoms(symbols=symbols, scaled_positions=fractional,
                  cell=lattice, pbc=True)
)PY",
                 py::globals(), locals);

        return AseBridge::fromAtoms(locals["atoms"]);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("Materials Project fetch failed:\n")
                                 + e.what());
    }
}

} // namespace calango::pybridge
