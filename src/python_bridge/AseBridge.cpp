#include "python_bridge/AseBridge.hpp"

#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <stdexcept>
#include <vector>

namespace py = pybind11;

namespace calango::pybridge {

namespace {

[[noreturn]] void rethrow(const py::error_already_set& e, const std::string& context)
{
    throw std::runtime_error(context + ":\n" + e.what());
}

} // namespace

core::Structure AseBridge::fromAtoms(const py::handle& atoms)
{
    const auto symbols = atoms.attr("get_chemical_symbols")()
                             .cast<std::vector<std::string>>();
    const auto positions = atoms.attr("get_positions")()
                               .cast<py::array_t<double>>();
    const auto pos = positions.unchecked<2>();

    core::Structure structure;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const auto row = static_cast<py::ssize_t>(i);
        structure.addAtom({core::Elements::atomicNumber(symbols[i]),
                           {pos(row, 0), pos(row, 1), pos(row, 2)}});
    }

    // atoms.cell is an ase Cell object; go through numpy for a plain 3x3.
    const py::module_ np = py::module_::import("numpy");
    const auto cell = np.attr("asarray")(atoms.attr("get_cell")())
                          .cast<py::array_t<double>>();
    const auto c = cell.unchecked<2>();
    const auto pbcList = atoms.attr("get_pbc")().attr("tolist")()
                             .cast<std::vector<bool>>();

    structure.setCell(core::UnitCell(
        {c(0, 0), c(0, 1), c(0, 2)},
        {c(1, 0), c(1, 1), c(1, 2)},
        {c(2, 0), c(2, 1), c(2, 2)},
        {pbcList[0], pbcList[1], pbcList[2]}));
    return structure;
}

py::object AseBridge::toAtoms(const core::Structure& structure)
{
    py::list symbols;
    py::list positions;
    for (const core::Atom& atom : structure.atoms()) {
        symbols.append(atom.symbol());
        positions.append(py::make_tuple(atom.position.x, atom.position.y, atom.position.z));
    }

    const py::module_ ase = py::module_::import("ase");
    py::object atoms = ase.attr("Atoms")(py::arg("symbols") = symbols,
                                         py::arg("positions") = positions);

    const core::UnitCell& cell = structure.cell();
    if (cell.isDefined()) {
        const auto& v = cell.vectors();
        atoms.attr("set_cell")(py::make_tuple(
            py::make_tuple(v[0].x, v[0].y, v[0].z),
            py::make_tuple(v[1].x, v[1].y, v[1].z),
            py::make_tuple(v[2].x, v[2].y, v[2].z)));
        const auto pbc = cell.pbc();
        atoms.attr("set_pbc")(py::make_tuple(pbc[0], pbc[1], pbc[2]));
    }
    return atoms;
}

core::Structure AseBridge::readStructure(const std::string& path)
{
    try {
        const py::object atoms = py::module_::import("ase.io").attr("read")(path);
        return fromAtoms(atoms);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Failed to read '" + path + "'");
    }
}

void AseBridge::writeStructure(const core::Structure& structure,
                               const std::string& path,
                               const std::string& format)
{
    try {
        const py::object write = py::module_::import("ase.io").attr("write");
        const py::object atoms = toAtoms(structure);
        if (format.empty()) {
            write(path, atoms);
        } else if (format == "espresso-in") {
            // The QE writer requires a pseudopotential per species; emit
            // conventional "<El>.upf" placeholders the user adjusts to
            // their pseudo library.
            py::dict pseudopotentials;
            for (const core::Atom& atom : structure.atoms())
                pseudopotentials[py::str(atom.symbol())] =
                    std::string(atom.symbol()) + ".upf";
            write(path, atoms, py::arg("format") = format,
                  py::arg("pseudopotentials") = pseudopotentials);
        } else {
            write(path, atoms, py::arg("format") = format);
        }
    } catch (const py::error_already_set& e) {
        rethrow(e, "Failed to write '" + path + "'");
    }
}

std::vector<core::Structure> AseBridge::readTrajectory(const std::string& path,
                                                       const std::string& format)
{
    try {
        const py::object read = py::module_::import("ase.io").attr("read");
        const py::object images = format.empty()
            ? read(path, py::arg("index") = ":")
            : read(path, py::arg("index") = ":", py::arg("format") = format);
        std::vector<core::Structure> frames;
        if (py::isinstance<py::list>(images)) {
            for (const auto& frame : images.cast<py::list>())
                frames.push_back(fromAtoms(frame));
        } else {
            frames.push_back(fromAtoms(images));
        }
        return frames;
    } catch (const py::error_already_set& e) {
        rethrow(e, "Failed to read trajectory '" + path + "'");
    }
}

core::Structure AseBridge::makeSlab(const core::Structure& structure,
                                    int h, int k, int l, int layers, double vacuum)
{
    try {
        const py::object slab = py::module_::import("ase.build").attr("surface")(
            toAtoms(structure), py::make_tuple(h, k, l), layers,
            py::arg("vacuum") = vacuum);
        return fromAtoms(slab);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Failed to cleave surface (is a bulk unit cell defined?)");
    }
}

core::Structure AseBridge::buildGraphene(double a, int nx, int ny, double vacuum)
{
    try {
        const py::object atoms = py::module_::import("ase.build").attr("graphene")(
            py::arg("a") = a, py::arg("size") = py::make_tuple(nx, ny, 1),
            py::arg("vacuum") = vacuum);
        return fromAtoms(atoms);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Failed to build graphene sheet");
    }
}

core::Structure AseBridge::buildNanoribbon(int width, int length, bool zigzag,
                                           bool saturated, double vacuum)
{
    try {
        const py::object atoms =
            py::module_::import("ase.build").attr("graphene_nanoribbon")(
                width, length, py::arg("type") = (zigzag ? "zigzag" : "armchair"),
                py::arg("saturated") = saturated, py::arg("vacuum") = vacuum);
        return fromAtoms(atoms);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Failed to build graphene nanoribbon");
    }
}

core::Structure AseBridge::buildNanotube(int n, int m, int length, double bond,
                                         double vacuum)
{
    try {
        const py::object atoms = py::module_::import("ase.build").attr("nanotube")(
            n, m, py::arg("length") = length, py::arg("bond") = bond,
            py::arg("vacuum") = vacuum);
        return fromAtoms(atoms);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Failed to build nanotube (n, m must not both be zero)");
    }
}

core::Structure AseBridge::buildMx2(const std::string& formula, const std::string& phase,
                                    double a, double thickness, int nx, int ny,
                                    double vacuum)
{
    try {
        const py::object atoms = py::module_::import("ase.build").attr("mx2")(
            py::arg("formula") = formula, py::arg("kind") = phase, py::arg("a") = a,
            py::arg("thickness") = thickness,
            py::arg("size") = py::make_tuple(nx, ny, 1), py::arg("vacuum") = vacuum);
        return fromAtoms(atoms);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Failed to build " + formula + " (" + phase + ")");
    }
}

AseBridge::BandPathInfo AseBridge::bandPathInfo(const core::Structure& structure)
{
    try {
        const py::object bandpath = toAtoms(structure).attr("cell").attr("bandpath")();
        const py::module_ np = py::module_::import("numpy");

        BandPathInfo info;
        info.suggestedPath = bandpath.attr("path").cast<std::string>();
        for (const auto& item : bandpath.attr("special_points").cast<py::dict>()) {
            const auto frac = np.attr("asarray")(item.second).cast<py::array_t<double>>();
            const auto f = frac.unchecked<1>();
            info.specialPoints.push_back(
                {item.first.cast<std::string>(), {f(0), f(1), f(2)}});
        }
        return info;
    } catch (const py::error_already_set& e) {
        rethrow(e, "Could not determine the band path for this cell");
    }
}

core::Structure AseBridge::makeSupercell(const core::Structure& structure,
                                         int nx, int ny, int nz)
{
    try {
        const py::object repeated =
            toAtoms(structure).attr("repeat")(py::make_tuple(nx, ny, nz));
        return fromAtoms(repeated);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Failed to build supercell (is a unit cell defined?)");
    }
}

} // namespace calango::pybridge
