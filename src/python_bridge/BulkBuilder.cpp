#include "python_bridge/BulkBuilder.hpp"

#include "python_bridge/AseBridge.hpp"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <stdexcept>

namespace py = pybind11;

namespace calango::pybridge {

const std::vector<std::string>& BulkBuilder::prototypes()
{
    // ase.build.bulk's `crystalstructure` vocabulary, ordered most-used
    // first rather than alphabetically.
    //
    // Verified against ASE 3.29: "st", "mcl" and "rhombohedral" are listed in
    // some ASE docs but are not constructible through bulk() (unknown name,
    // "cannot create primitive cell", and a KeyError on 'basis_x'
    // respectively), so they are deliberately not offered here — the
    // space-group + Wyckoff mode covers those lattices instead.
    static const std::vector<std::string> kPrototypes = {
        "fcc",      "bcc",        "hcp",      "diamond",        "sc",
        "rocksalt", "zincblende", "wurtzite", "cesiumchloride", "fluorite",
        "bct",      "orthorhombic",
    };
    return kPrototypes;
}

bool BulkBuilder::usesCOverA(const std::string& crystalStructure)
{
    return crystalStructure == "hcp" || crystalStructure == "wurtzite"
        || crystalStructure == "bct" || crystalStructure == "orthorhombic";
}

bool BulkBuilder::usesB(const std::string& crystalStructure)
{
    // Only the fully anisotropic prototype has an independent b axis; every
    // other one derives b from a (and ASE errors out with b=nan if it is
    // omitted for this one).
    return crystalStructure == "orthorhombic";
}

core::Structure BulkBuilder::buildPrototype(const PrototypeSpec& spec)
{
    if (spec.name.empty())
        throw std::runtime_error(
            "Enter a chemical formula for the bulk crystal (e.g. Si, NaCl, Au)");
    if (spec.a < 0.0)
        throw std::runtime_error("The lattice constant a must be positive");

    try {
        py::dict locals;
        locals["name"] = spec.name;
        locals["crystalstructure"] = spec.crystalStructure;
        // ASE distinguishes "not given" (use the tabulated/ideal value) from
        // an explicit number, so unset parameters must reach it as None
        // rather than as 0.0.
        locals["a"] = spec.a > 0.0 ? py::cast(spec.a) : py::none();
        locals["b"] = spec.hasB && spec.b > 0.0 ? py::cast(spec.b) : py::none();
        locals["c"] = spec.hasC && spec.c > 0.0 ? py::cast(spec.c) : py::none();
        locals["covera"] =
            (!spec.hasC && spec.hasCovera && spec.covera > 0.0)
                ? py::cast(spec.covera) : py::none();
        locals["u"] = spec.hasU ? py::cast(spec.u) : py::none();
        locals["cubic"] = spec.cubic;
        locals["orthorhombic"] = spec.orthorhombic;

        py::exec(R"PY(
from ase.build import bulk

kwargs = {}
if a is not None:
    kwargs["a"] = a
if b is not None:
    kwargs["b"] = b
if c is not None:
    kwargs["c"] = c
if covera is not None:
    kwargs["covera"] = covera
if u is not None:
    kwargs["u"] = u
# `cubic` and `orthorhombic` are mutually exclusive in ASE and only accepted
# by some prototypes; only pass the one that is actually requested.
if cubic:
    kwargs["cubic"] = True
elif orthorhombic:
    kwargs["orthorhombic"] = True

try:
    atoms = bulk(name, crystalstructure, **kwargs)
except Exception as error:
    # ASE's messages are specific and actionable ("Please specify c or
    # c/a", "Cannot make orthorhombic cell for fcc") — surface them as-is
    # instead of a generic wrapper.
    raise RuntimeError(str(error)) from None
)PY",
                 // locals doubles as globals: script-defined functions must
                 // see the script's own names (see AseBridge::symmetryInfo).
                 locals, locals);

        return AseBridge::fromAtoms(locals["atoms"]);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("Bulk crystal build failed:\n")
                                 + e.what());
    }
}

core::Structure BulkBuilder::buildFromSpaceGroup(const SpaceGroupSpec& spec)
{
    if (spec.sites.empty())
        throw std::runtime_error(
            "Add at least one Wyckoff site (element + fractional coordinates)");
    if (spec.spaceGroup < 1 || spec.spaceGroup > 230)
        throw std::runtime_error("The space group number must be between 1 and 230");
    if (spec.a <= 0.0 || spec.b <= 0.0 || spec.c <= 0.0)
        throw std::runtime_error("Cell lengths a, b and c must be positive");

    try {
        std::vector<std::string> symbols;
        std::vector<std::vector<double>> basis;
        symbols.reserve(spec.sites.size());
        basis.reserve(spec.sites.size());
        for (const auto& site : spec.sites) {
            if (site.symbol.empty())
                throw std::runtime_error(
                    "Every Wyckoff site needs an element symbol");
            symbols.push_back(site.symbol);
            basis.push_back({site.u, site.v, site.w});
        }

        py::dict locals;
        locals["symbols"] = symbols;
        locals["basis"] = basis;
        locals["spacegroup"] = spec.spaceGroup;
        locals["cellpar"] = std::vector<double>{spec.a, spec.b, spec.c,
                                                spec.alpha, spec.beta, spec.gamma};
        locals["primitive"] = spec.primitive;

        py::exec(R"PY(
from ase.spacegroup import crystal

try:
    atoms = crystal(symbols, basis=basis, spacegroup=spacegroup,
                    cellpar=cellpar, primitive_cell=primitive)
except Exception as error:
    # Typically "spacegroup ... requires ..." or a site that is not
    # consistent with the group's symmetry — pass ASE's wording through.
    raise RuntimeError(str(error)) from None
)PY",
                 // locals doubles as globals: script-defined functions must
                 // see the script's own names (see AseBridge::symmetryInfo).
                 locals, locals);

        return AseBridge::fromAtoms(locals["atoms"]);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("Crystal build failed:\n") + e.what());
    }
}

} // namespace calango::pybridge
