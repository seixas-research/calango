#include "python_bridge/MaterialsProject.hpp"

#include "python_bridge/AseBridge.hpp"

#include <pybind11/embed.h>

#include <algorithm>
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

# Query-parameter form: the path-suffix form (/summary/<id>/) was
# retired by Materials Project in 2026 and now returns an
# "upgrade to mp-api" error blob instead of data.
url = ("https://api.materialsproject.org/materials/summary/"
       f"?material_ids={mp_id}&_fields=structure")
# An explicit User-Agent is required: the API's edge blocks urllib's
# default "Python-urllib/x.y" agent with HTTP 403.
request = urllib.request.Request(
    url, headers={"X-API-KEY": api_key, "accept": "application/json",
                  "User-Agent": "calango (materials modeling GUI)"})
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
                 // locals doubles as globals: script-defined functions must
                 // see the script's own names (see AseBridge::symmetryInfo).
                 locals, locals);

        return AseBridge::fromAtoms(locals["atoms"]);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("Materials Project fetch failed:\n")
                                 + e.what());
    }
}

std::vector<MaterialsProject::SearchHit>
MaterialsProject::search(const std::string& query, const std::string& apiKey,
                         bool asFormula, bool exactSystem, int limit)
{
    if (query.empty())
        throw std::runtime_error(
            "Enter a chemical system (e.g. Li-Fe-O) or a formula (e.g. LiFePO4)");
    if (apiKey.empty())
        throw std::runtime_error(
            "Enter your Materials Project API key (materialsproject.org/api)");

    try {
        py::dict locals;
        locals["query"] = query;
        locals["api_key"] = apiKey;
        locals["as_formula"] = asFormula;
        locals["exact_system"] = exactSystem;
        locals["limit"] = std::max(1, std::min(limit, 1000));

        // Same rationale as fetchStructure: the plain REST endpoint keeps
        // mp-api/pymatgen out of the dependency set. Only summary fields are
        // requested here — structures are pulled per selection afterwards,
        // so a 200-row search stays a single small response.
        py::exec(R"PY(
import json
import re
import urllib.error
import urllib.parse
import urllib.request

raw = query.strip()

# "Li-Fe-O", "Li Fe O", "Li,Fe,O" and "Li, Fe, O" all describe the same
# chemical system; normalize to the API's dash-separated form. A formula
# query ("LiFePO4") is passed through untouched.
if as_formula:
    params = {"formula": raw}
else:
    elements = [token for token in re.split(r"[\s,\-;/]+", raw) if token]
    if not elements:
        raise RuntimeError("Enter at least one element.")
    # Shape check only (catches "hello world" / lowercase typos); whether a
    # well-formed symbol actually exists is left to the API, which simply
    # returns no matches.
    bad = [e for e in elements if not re.fullmatch(r"[A-Z][a-z]?", e)]
    if bad:
        raise RuntimeError(
            "Not valid element symbols: " + ", ".join(bad) +
            "\nUse a system like 'Li-Fe-O', or switch the search mode to Formula.")
    if exact_system:
        # chemsys matches phases made of exactly these elements.
        params = {"chemsys": "-".join(sorted(elements))}
    else:
        # elements matches phases containing (at least) these elements.
        params = {"elements": ",".join(elements)}

params.update({
    "_fields": ",".join([
        "material_id", "formula_pretty", "symmetry", "band_gap",
        "energy_above_hull", "nsites", "is_stable",
    ]),
    "_limit": str(limit),
    "_sort_fields": "energy_above_hull",
})

url = ("https://api.materialsproject.org/materials/summary/?"
       + urllib.parse.urlencode(params))
# An explicit User-Agent is required: the API's edge blocks urllib's
# default "Python-urllib/x.y" agent with HTTP 403.
request = urllib.request.Request(
    url, headers={"X-API-KEY": api_key, "accept": "application/json",
                  "User-Agent": "calango (materials modeling GUI)"})
try:
    with urllib.request.urlopen(request, timeout=30) as response:
        payload = json.load(response)
except urllib.error.HTTPError as error:
    if error.code in (401, 403):
        raise RuntimeError("Materials Project rejected the API key "
                           f"(HTTP {error.code}).") from None
    if error.code == 429:
        raise RuntimeError("Materials Project rate limit reached — "
                           "wait a moment and search again.") from None
    raise RuntimeError(f"Materials Project search failed (HTTP {error.code}).") from None
except urllib.error.URLError as error:
    raise RuntimeError(f"Network error contacting Materials Project: {error.reason}") from None

hits = []
for entry in payload.get("data") or []:
    symmetry = entry.get("symmetry") or {}
    hits.append({
        "material_id": entry.get("material_id") or "",
        "formula": entry.get("formula_pretty") or "",
        "space_group": symmetry.get("symbol") or "",
        "space_group_number": int(symmetry.get("number") or 0),
        # band_gap / energy_above_hull are legitimately null for entries
        # without the corresponding calculation; keep "unknown" distinct
        # from "zero" so the table can show a dash instead of 0.00 eV.
        "band_gap": float(entry["band_gap"]) if entry.get("band_gap") is not None else 0.0,
        "has_band_gap": entry.get("band_gap") is not None,
        "e_above_hull": (float(entry["energy_above_hull"])
                         if entry.get("energy_above_hull") is not None else 0.0),
        "has_e_above_hull": entry.get("energy_above_hull") is not None,
        "nsites": int(entry.get("nsites") or 0),
        "is_stable": bool(entry.get("is_stable")),
    })
)PY",
                 // locals doubles as globals: script-defined functions must
                 // see the script's own names (see AseBridge::symmetryInfo).
                 locals, locals);

        std::vector<SearchHit> results;
        for (const auto& item : locals["hits"].cast<py::list>()) {
            const auto row = item.cast<py::dict>();
            SearchHit hit;
            hit.materialId = row["material_id"].cast<std::string>();
            hit.formula = row["formula"].cast<std::string>();
            hit.spaceGroup = row["space_group"].cast<std::string>();
            hit.spaceGroupNumber = row["space_group_number"].cast<int>();
            hit.bandGap = row["band_gap"].cast<double>();
            hit.hasBandGap = row["has_band_gap"].cast<bool>();
            hit.energyAboveHull = row["e_above_hull"].cast<double>();
            hit.hasEnergyAboveHull = row["has_e_above_hull"].cast<bool>();
            hit.nSites = row["nsites"].cast<int>();
            hit.isStable = row["is_stable"].cast<bool>();
            results.push_back(std::move(hit));
        }
        return results;
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("Materials Project search failed:\n")
                                 + e.what());
    }
}

} // namespace calango::pybridge
