#include "python_bridge/SurfaceScience.hpp"

#include "python_bridge/AseBridge.hpp"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <stdexcept>

namespace py = pybind11;

namespace calango::pybridge {

namespace {

[[noreturn]] void rethrow(const py::error_already_set& e, const char* what)
{
    throw std::runtime_error(std::string(what) + ":\n" + e.what());
}

/// Site detection + placement helpers, executed with a single dict as
/// both globals and locals (function bodies must see the script names).
constexpr const char* kSiteScript = R"PY(
import numpy as np

pos = atoms.get_positions()
cell = np.array(atoms.cell[:])
inv_cell = np.linalg.inv(cell)

def mic(vecs):
    """In-plane minimum image (slab: periodic along a and b only)."""
    frac = vecs @ inv_cell
    frac[:, :2] -= np.round(frac[:, :2])
    return frac @ cell

ztol = 0.6
ztop = pos[:, 2].max()
top_idx = np.flatnonzero(pos[:, 2] > ztop - ztol)
below = pos[pos[:, 2] <= ztop - ztol]
second = None
if len(below):
    zsecond = below[:, 2].max()
    second = below[np.abs(below[:, 2] - zsecond) < ztol]

top = pos[top_idx]
n = len(top)
if n == 0:
    raise RuntimeError("no surface layer found")

# Neighbor VECTORS per top atom over explicit in-plane images: small
# cells connect atoms through several images (including an atom to its
# own periodic copy), so a plain pair graph undercounts bonds. Duplicate
# sites from double-counted bonds collapse in the dedup pass below.
images = [da * cell[0] + db * cell[1]
          for da in (-1, 0, 1) for db in (-1, 0, 1)]
distances = []
for i in range(n):
    for j in range(n):
        for image in images:
            d = np.linalg.norm((top[j] + image - top[i])[:2])
            if d > 1e-6:
                distances.append(d)
if not distances:
    raise RuntimeError("need at least two surface atoms for bridge/hollow "
                       "sites — repeat the slab in-plane first")
dmin = min(distances)
nbr = [[] for _ in range(n)]
for i in range(n):
    for j in range(n):
        for image in images:
            v = top[j] + image - top[i]
            if 1e-6 < np.linalg.norm(v[:2]) < dmin * 1.15:
                nbr[i].append(v)

sites = []  # (type, x, y)
for i in range(n):
    sites.append(("top", top[i, 0], top[i, 1]))
for i in range(n):
    for v in nbr[i]:
        mid = top[i] + 0.5 * v
        sites.append(("bridge", mid[0], mid[1]))

# Threefold hollows: neighbor-vector pairs of one atom whose tips are
# themselves nearest neighbors; fcc vs hcp decided by a second-layer
# atom directly underneath the centroid.
for i in range(n):
    for a_idx in range(len(nbr[i])):
        for b_idx in range(a_idx + 1, len(nbr[i])):
            v1, v2 = nbr[i][a_idx], nbr[i][b_idx]
            if np.linalg.norm((v2 - v1)[:2]) > dmin * 1.15:
                continue
            centroid = top[i] + (v1 + v2) / 3.0
            kind = "hollow"
            if second is not None and len(second):
                d2 = mic(second - centroid)
                if np.min(np.linalg.norm(d2[:, :2], axis=1)) < 0.5 * dmin:
                    kind = "hcp"
                else:
                    kind = "fcc"
            sites.append((kind, centroid[0], centroid[1]))

# Wrap in-plane into the home cell and deduplicate periodic copies.
unique = []
seen = []
for kind, x, y in sites:
    frac = np.array([x, y, 0.0]) @ inv_cell
    frac[:2] %= 1.0
    cart = frac @ cell
    key_new = True
    for kk, cc in seen:
        if kk == kind:
            d = mic(np.array([cart - cc]))[0]
            if np.linalg.norm(d[:2]) < 0.15:
                key_new = False
                break
    if key_new:
        seen.append((kind, cart))
        unique.append((kind, float(cart[0]), float(cart[1]), float(ztop)))
result_sites = unique
)PY";

constexpr const char* kPlaceScript = R"PY(
import numpy as np
from ase import Atoms
from ase.build import molecule

_NAME_MAP = {"CHO": "HCO", "*OH": "OH", "*O": "O", "*CO": "CO",
             "*CHO": "HCO", "*H": "H"}
_ANCHORS = {"OH": "O", "H2O": "O", "CO": "C", "HCO": "C", "NO": "N",
            "CH3": "C", "NH3": "N", "OOH": "O"}

name = _NAME_MAP.get(adsorbate, adsorbate)
try:
    mol = molecule(name)
except Exception:
    mol = Atoms(name)  # plain formula, atoms stacked at the origin

symbols = mol.get_chemical_symbols()
anchor_symbol = _ANCHORS.get(name)
if anchor_symbol and anchor_symbol in symbols:
    anchor = symbols.index(anchor_symbol)
else:
    anchor = int(np.argmin(mol.get_positions()[:, 2]))

mol.translate(-mol.get_positions()[anchor])
if len(mol) > 1:
    others = np.delete(mol.get_positions(), anchor, axis=0)
    if others[:, 2].mean() < 0:  # molecule points down — flip upright
        mol.rotate(180, "x")
        mol.translate(-mol.get_positions()[anchor])

combined = atoms.copy()
for x, y, z in site_positions:
    copy = mol.copy()
    copy.translate((x, y, z + height))
    combined += copy
result_atoms = combined
)PY";

} // namespace

core::Structure SurfaceScience::wulffNanoparticle(
    const std::string& symbol, const std::string& lattice,
    const std::vector<WulffFacet>& facets, int atomCount,
    double latticeConstant, const std::string& rounding)
{
    try {
        py::dict scope;
        scope["symbol"] = symbol;
        scope["lattice"] = lattice;
        py::list surfaces, energies;
        for (const auto& facet : facets) {
            surfaces.append(py::make_tuple(facet.h, facet.k, facet.l));
            energies.append(facet.energy);
        }
        scope["surfaces"] = surfaces;
        scope["energies"] = energies;
        scope["size"] = atomCount;
        scope["a"] = latticeConstant;
        scope["rounding"] = rounding;
        py::exec(R"PY(
from ase.cluster import wulff_construction

cluster = wulff_construction(symbol, surfaces=[tuple(s) for s in surfaces],
                             energies=list(energies), size=int(size),
                             structure=lattice, rounding=rounding,
                             latticeconstant=(a if a > 0 else None))
cluster.center(vacuum=6.0)
cluster.pbc = False
result_atoms = cluster
)PY",
                 scope, scope);
        return AseBridge::fromAtoms(scope["result_atoms"]);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Wulff construction failed");
    }
}

core::Structure SurfaceScience::sphericalNanoparticle(const std::string& symbol,
                                                      const std::string& lattice,
                                                      double radiusA,
                                                      double latticeConstant)
{
    try {
        py::dict scope;
        scope["symbol"] = symbol;
        scope["lattice"] = lattice;
        scope["radius"] = radiusA;
        scope["a"] = latticeConstant;
        py::exec(R"PY(
import numpy as np
from ase.build import bulk

unit = bulk(symbol, lattice, a=(a if a > 0 else None))
span = max(np.linalg.norm(v) for v in unit.cell)
n = int(np.ceil(2.0 * radius / span)) + 2
super_cell = unit.repeat((n, n, n))
center = super_cell.get_positions().mean(axis=0)
keep = np.linalg.norm(super_cell.get_positions() - center, axis=1) <= radius
cluster = super_cell[keep]
if len(cluster) == 0:
    raise RuntimeError("radius too small — no atoms inside the sphere")
cluster.center(vacuum=6.0)
cluster.pbc = False
result_atoms = cluster
)PY",
                 scope, scope);
        return AseBridge::fromAtoms(scope["result_atoms"]);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Spherical cluster construction failed");
    }
}

std::vector<SurfaceScience::AdsorptionSite>
SurfaceScience::detectSites(const core::Structure& slab)
{
    try {
        py::dict scope;
        scope["atoms"] = AseBridge::toAtoms(slab);
        py::exec(kSiteScript, scope, scope);
        std::vector<AdsorptionSite> sites;
        for (const auto& entry : scope["result_sites"].cast<py::list>()) {
            const auto tuple = entry.cast<py::tuple>();
            sites.push_back({tuple[0].cast<std::string>(),
                             tuple[1].cast<double>(), tuple[2].cast<double>(),
                             tuple[3].cast<double>()});
        }
        return sites;
    } catch (const py::error_already_set& e) {
        rethrow(e, "Adsorption site detection failed");
    }
}

core::Structure SurfaceScience::placeAdsorbates(
    const core::Structure& slab, const std::vector<AdsorptionSite>& sites,
    const std::string& adsorbate, double height)
{
    try {
        py::dict scope;
        scope["atoms"] = AseBridge::toAtoms(slab);
        scope["adsorbate"] = adsorbate;
        scope["height"] = height;
        py::list positions;
        for (const auto& site : sites)
            positions.append(py::make_tuple(site.x, site.y, site.z));
        scope["site_positions"] = positions;
        py::exec(kPlaceScript, scope, scope);
        return AseBridge::fromAtoms(scope["result_atoms"]);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Adsorbate placement failed");
    }
}

} // namespace calango::pybridge
