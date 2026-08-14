#include "python_bridge/SurfaceScience.hpp"

#include "core/AdsorptionSites.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PyError.hpp"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <stdexcept>

namespace py = pybind11;

namespace calango::pybridge {

namespace {

/// Adsorbate molecule template: resolve the name and hand back the ASE
/// molecule as an Atoms object plus the index of the anchor atom (the atom
/// that binds to the surface). Only the molecular *database* is Python —
/// the anchor map mirrors the classic ACAT conventions. All placement
/// geometry happens natively in core::placeAdsorbate. Executed with a
/// single dict as both globals and locals so the helper sees script names.
constexpr const char* kMoleculeScript = R"PY(
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
    # Fall back to the "lowest" atom in ASE's standard orientation, which is
    # the surface-facing one for the common hydride/oxide adsorbates.
    anchor = int(np.argmin(mol.get_positions()[:, 2]))

result_molecule = mol
result_anchor = int(anchor)
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

core::Structure SurfaceScience::polyhedralNanoparticle(
    const std::string& symbol, const std::string& shape, int size, int p, int q,
    int r, double latticeConstant)
{
    try {
        py::dict scope;
        scope["symbol"] = symbol;
        scope["shape"] = shape;
        scope["size"] = size;
        scope["p"] = p;
        scope["q"] = q;
        scope["r"] = r;
        scope["a"] = latticeConstant;
        py::exec(R"PY(
from ase.cluster import Icosahedron, Octahedron, Decahedron
from ase.cluster.cubic import FaceCenteredCubic

lc = a if a > 0 else None
n = int(size)
if shape == 'icosahedron':
    if n < 1:
        raise RuntimeError("icosahedron needs at least 1 shell")
    cluster = Icosahedron(symbol, noshells=n, latticeconstant=lc)
elif shape == 'octahedron':
    if n < 2:
        raise RuntimeError("octahedron edge length must be >= 2")
    cluster = Octahedron(symbol, length=n, cutoff=0, latticeconstant=lc)
elif shape == 'cuboctahedron':
    if n < 2:
        raise RuntimeError("cuboctahedron edge length must be >= 2")
    # A regular cuboctahedron is an octahedron truncated exactly halfway
    # along each edge: cutoff = (length - 1) // 2.
    cluster = Octahedron(symbol, length=n, cutoff=(n - 1) // 2, latticeconstant=lc)
elif shape == 'decahedron':
    if int(p) < 1 or int(q) < 1:
        raise RuntimeError("decahedron needs p >= 1 and q >= 1")
    cluster = Decahedron(symbol, int(p), int(q), int(r), latticeconstant=lc)
elif shape == 'rhombic-dodecahedron':
    if n < 1:
        raise RuntimeError("rhombic dodecahedron needs at least 1 layer")
    # The FCC equilibrium form bounded solely by {110} facets is the
    # rhombic dodecahedron; a single symmetric layer count carves it.
    cluster = FaceCenteredCubic(symbol, surfaces=[(1, 1, 0)], layers=[n],
                                latticeconstant=lc)
else:
    raise RuntimeError("unknown cluster shape: " + str(shape))

if len(cluster) == 0:
    raise RuntimeError("the requested size produced an empty cluster")
cluster.center(vacuum=6.0)
cluster.pbc = False
result_atoms = cluster
)PY",
                 scope, scope);
        return AseBridge::fromAtoms(scope["result_atoms"]);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Faceted cluster construction failed");
    }
}

std::vector<SurfaceScience::AdsorptionSite>
SurfaceScience::detectSites(const core::Structure& slab)
{
    // Native C++ site detection (no Python) — see core::detectAdsorptionSites.
    const auto coreSites = core::detectAdsorptionSites(slab);
    if (coreSites.empty())
        throw std::runtime_error(
            "No adsorption sites found. Provide a surface with an outer layer "
            "of undercoordinated atoms (a slab or a nanoparticle); repeat a "
            "very small slab in-plane so bridge/hollow sites can form.");
    std::vector<AdsorptionSite> sites;
    sites.reserve(coreSites.size());
    for (const auto& s : coreSites)
        sites.push_back({s.type, s.position.x, s.position.y, s.position.z,
                         s.normal.x, s.normal.y, s.normal.z});
    return sites;
}

core::Structure SurfaceScience::placeAdsorbates(
    const core::Structure& slab, const std::vector<AdsorptionSite>& sites,
    const std::string& adsorbate, double height)
{
    // The adsorbate molecule template + anchor come from ASE's molecule
    // database; all placement geometry is native C++ (core::placeAdsorbate),
    // so adsorbates follow each site's outward normal.
    core::Structure molecule;
    int anchor = 0;
    try {
        py::dict scope;
        scope["adsorbate"] = adsorbate;
        py::exec(kMoleculeScript, scope, scope);
        molecule = AseBridge::fromAtoms(scope["result_molecule"]);
        anchor = scope["result_anchor"].cast<int>();
    } catch (const py::error_already_set& e) {
        rethrow(e, "Could not build the adsorbate molecule");
    }

    std::vector<core::AdsorptionSite> coreSites;
    coreSites.reserve(sites.size());
    for (const auto& s : sites)
        coreSites.push_back({s.type,
                             {s.x, s.y, s.z},
                             {s.nx, s.ny, s.nz},
                             {}});
    return core::placeAdsorbate(slab, coreSites, molecule, anchor, height);
}

SurfaceScience::MoleculeTemplate SurfaceScience::moleculeTemplate(
    const std::string& name)
{
    // Exactly the resolution the coverage/site-scan path uses, exposed on its
    // own so the "Add adsorbate…" builder places the SAME geometry the
    // Adsorption & Catalysis module would — two molecule databases that
    // disagree about where CO's carbon sits is how two runs of "the same"
    // system stop being comparable.
    MoleculeTemplate result;
    try {
        py::dict scope;
        scope["adsorbate"] = name;
        py::exec(kMoleculeScript, scope, scope);
        result.structure = AseBridge::fromAtoms(scope["result_molecule"]);
        result.anchorIndex = scope["result_anchor"].cast<int>();
    } catch (const py::error_already_set& e) {
        rethrow(e, "Could not build the adsorbate molecule");
    }
    return result;
}

std::vector<std::string> SurfaceScience::moleculeNames()
{
    try {
        py::dict scope;
        py::exec(R"PY(
from ase.collections import g2

# The G2/97 set is a thermochemistry benchmark, so it carries closed-shell
# molecules and misses precisely the open-shell fragments a surface binds.
# These are the ones ase.build.molecule also knows (the "extra" table) plus the
# radicals surface science reaches for constantly.
_EXTRA = ["OH", "OOH", "O", "H", "N", "C", "S", "CH3", "CH2", "CH", "NH2",
          "NH", "COOH", "HCOO", "CN", "NO", "NO2", "SH", "O2", "N2", "H2",
          "CO", "CO2", "H2O", "NH3", "CH4"]

result_names = sorted(set(list(g2.names) + _EXTRA))
)PY",
                 scope, scope);
        return scope["result_names"].cast<std::vector<std::string>>();
    } catch (const py::error_already_set&) {
        // A missing / broken ASE must not stop the dialog from opening: the
        // name field is editable, so a typed formula still works.
        return {};
    }
}

} // namespace calango::pybridge
