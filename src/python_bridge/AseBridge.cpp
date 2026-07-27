#include "python_bridge/AseBridge.hpp"

#include "core/PdbxFile.hpp"

#include <pybind11/eval.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <set>
#include <cmath>
#include <stdexcept>
#include <utility>
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

    // Extra per-atom arrays (extxyz columns: charges, forces, ...) become
    // scalar fields for the custom color-mapping mode. 1D numeric arrays
    // import directly; (N, 3) vector arrays contribute their magnitude as
    // "|name|". Non-numeric arrays are skipped silently.
    const auto n = symbols.size();
    for (const auto& item : atoms.attr("arrays").cast<py::dict>()) {
        const auto name = item.first.cast<std::string>();
        if (name == "numbers" || name == "positions")
            continue;
        try {
            const auto values =
                np.attr("asarray")(item.second, py::arg("dtype") = "float64")
                    .cast<py::array_t<double>>();
            if (values.ndim() == 1 && static_cast<std::size_t>(values.shape(0)) == n) {
                const auto v = values.unchecked<1>();
                std::vector<double> field(n);
                for (std::size_t i = 0; i < n; ++i)
                    field[i] = v(static_cast<py::ssize_t>(i));
                structure.setScalarField(name, std::move(field));
            } else if (values.ndim() == 2
                       && static_cast<std::size_t>(values.shape(0)) == n
                       && values.shape(1) == 3) {
                // Vector arrays import twice: the full vectors (for arrow
                // rendering — forces, momenta, ...) and their magnitudes
                // (for scalar color mapping).
                const auto v = values.unchecked<2>();
                std::vector<core::Vec3> vectors(n);
                std::vector<double> magnitude(n);
                for (std::size_t i = 0; i < n; ++i) {
                    const auto row = static_cast<py::ssize_t>(i);
                    vectors[i] = {v(row, 0), v(row, 1), v(row, 2)};
                    magnitude[i] = vectors[i].norm();
                }
                structure.setVectorField(name, std::move(vectors));
                structure.setScalarField("|" + name + "|", std::move(magnitude));
            }
        } catch (const py::error_already_set&) {
            continue;
        }
    }

    // Calculator results. ASE's extended-XYZ reader does NOT put `forces`,
    // `magmoms`, `energies` etc. into atoms.arrays — it recognizes them as
    // computed properties and attaches a SinglePointCalculator instead.
    // Scanning only atoms.arrays (as this bridge used to) therefore dropped
    // exactly the columns a finished calculation cares about.
    try {
        const py::object calc = atoms.attr("calc");
        if (!calc.is_none()) {
            for (const auto& item : calc.attr("results").cast<py::dict>()) {
                const auto name = item.first.cast<std::string>();
                try {
                    const auto values =
                        np.attr("asarray")(item.second, py::arg("dtype") = "float64")
                            .cast<py::array_t<double>>();
                    if (values.ndim() == 1
                        && static_cast<std::size_t>(values.shape(0)) == n) {
                        const auto v = values.unchecked<1>();
                        std::vector<double> field(n);
                        for (std::size_t i = 0; i < n; ++i)
                            field[i] = v(static_cast<py::ssize_t>(i));
                        structure.setScalarField(name, std::move(field));
                    } else if (values.ndim() == 2
                               && static_cast<std::size_t>(values.shape(0)) == n
                               && values.shape(1) == 3) {
                        const auto v = values.unchecked<2>();
                        std::vector<core::Vec3> vectors(n);
                        std::vector<double> magnitude(n);
                        for (std::size_t i = 0; i < n; ++i) {
                            const auto row = static_cast<py::ssize_t>(i);
                            vectors[i] = {v(row, 0), v(row, 1), v(row, 2)};
                            magnitude[i] = vectors[i].norm();
                        }
                        structure.setVectorField(name, std::move(vectors));
                        structure.setScalarField("|" + name + "|",
                                                 std::move(magnitude));
                    }
                    // Scalars that are not per-atom (total energy, stress)
                    // are not per-site data and are skipped.
                } catch (const py::error_already_set&) {
                    continue;
                }
            }
        }
    } catch (const py::error_already_set&) {
        // No calculator attached — the common case for a plain structure.
    }

    // Collinear spin: calculators report one scalar moment per atom, but the
    // viewport's vector overlay needs a direction. Promote m -> (0, 0, m) so
    // spin-up and spin-down render as opposite arrows along z, the axis a
    // collinear calculation implicitly quantizes along. Non-collinear runs
    // already supplied an (N, 3) array and are left alone.
    //
    // Only the *computed* moments are promoted, never `initial_magmoms`:
    // those are an input guess (ASE seeds bulk Fe with 2.3 μB whether or not
    // anything was ever calculated) and drawing them as results would be
    // actively misleading.
    if (structure.vectorFields().count("magmoms") == 0) {
        const auto scalar = structure.scalarFields().find("magmoms");
        if (scalar != structure.scalarFields().end()
            && scalar->second.size() == n) {
            std::vector<core::Vec3> vectors(n);
            double largest = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                vectors[i] = {0.0, 0.0, scalar->second[i]};
                largest = std::max(largest, std::abs(scalar->second[i]));
            }
            if (largest > 1e-9)
                structure.setVectorField("magmoms", std::move(vectors));
        }
    }

    // Velocities derived from momenta/masses (ase get_velocities); only
    // stored when they carry information.
    try {
        const auto velocities = np.attr("asarray")(atoms.attr("get_velocities")(),
                                                   py::arg("dtype") = "float64")
                                    .cast<py::array_t<double>>();
        if (velocities.ndim() == 2
            && static_cast<std::size_t>(velocities.shape(0)) == n
            && velocities.shape(1) == 3) {
            const auto v = velocities.unchecked<2>();
            std::vector<core::Vec3> vectors(n);
            double largest = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const auto row = static_cast<py::ssize_t>(i);
                vectors[i] = {v(row, 0), v(row, 1), v(row, 2)};
                largest = std::max(largest, vectors[i].norm());
            }
            if (largest > 1e-12)
                structure.setVectorField("velocities", std::move(vectors));
        }
    } catch (const py::error_already_set&) {
        // no momenta / masses — fine
    }
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

    // Per-atom properties -> ase.Atoms.arrays, which the extended-XYZ writer
    // turns into named columns (`forces:R:3`, `magmoms:R:1`, ...). Without
    // this, every Calango-written trajectory silently dropped the forces and
    // magnetic moments it had just read in.
    const py::module_ np = py::module_::import("numpy");
    const auto atomCount = structure.size();

    for (const auto& [name, vectors] : structure.vectorFields()) {
        if (vectors.size() != atomCount)
            continue;
        // A vector field that shadows a same-named scalar is this bridge's
        // display-side promotion (collinear magmoms -> (0, 0, m)). Writing it
        // as an (N, 3) column would silently reinterpret a collinear result
        // as a non-collinear one; the scalar loop below emits the real data.
        if (structure.scalarFields().count(name) > 0)
            continue;
        py::list rows;
        for (const core::Vec3& v : vectors)
            rows.append(py::make_tuple(v.x, v.y, v.z));
        py::object array = np.attr("asarray")(rows, py::arg("dtype") = "float64");
        if (name == "velocities") {
            // ASE stores velocities as momenta (v · m); going through the
            // setter keeps that relationship intact so a reader recovers the
            // same velocities rather than treating them as a raw column.
            atoms.attr("set_velocities")(array);
        } else {
            atoms.attr("arrays")[py::str(name)] = std::move(array);
        }
    }

    for (const auto& [name, values] : structure.scalarFields()) {
        if (values.size() != atomCount)
            continue;
        // "|forces|"-style entries are magnitudes this bridge derives on
        // import; re-emitting them would both duplicate data and produce a
        // column name extended XYZ cannot represent.
        if (!name.empty() && name.front() == '|')
            continue;
        atoms.attr("arrays")[py::str(name)] =
            np.attr("asarray")(py::cast(values), py::arg("dtype") = "float64");
    }
    return atoms;
}

core::Structure AseBridge::readStructure(const std::string& path)
{
    // PDBx/mmCIF is intercepted before ASE sees it. Both it and the
    // small-molecule CIF that ASE reads end in `.cif`, but they are different
    // formats — Cartesian coordinates with residue annotation versus
    // fractional coordinates under a symmetry group — and ASE's reader raises
    // an opaque StopIteration on the PDBx flavour. The test is on content, so
    // a crystallographic `.cif` still takes the ASE path.
    if (core::PdbxFile::looksLikePdbx(path))
        return core::PdbxFile::read(path);
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

void AseBridge::writeTrajectory(
    const std::vector<std::shared_ptr<core::Structure>>& frames,
    const std::string& path, const std::string& format)
{
    try {
        py::list images;
        for (const auto& frame : frames) {
            if (frame)
                images.append(toAtoms(*frame));
        }
        const py::object write = py::module_::import("ase.io").attr("write");
        if (format.empty())
            write(path, images);
        else
            write(path, images, py::arg("format") = format);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Failed to write trajectory '" + path + "'");
    }
}

std::vector<core::Structure> AseBridge::readTrajectory(const std::string& path,
                                                       const std::string& format)
{
    // PDBx before ASE, for the same reason as readStructure — and this is the
    // path the GUI's File → Open actually takes, since it reads every file as a
    // possible trajectory. A PDBx entry is one model (the reader takes the
    // first of a multi-model NMR ensemble), so it comes back as a single frame.
    if (core::PdbxFile::looksLikePdbx(path))
        return {core::PdbxFile::read(path)};
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
        const py::object surface = py::module_::import("ase.build").attr("surface");
        // vacuum <= 0 is deprecated (future ValueError): omit the kwarg to
        // get a continuous bulk-like stack instead.
        const py::object slab = vacuum > 0.0
            ? surface(toAtoms(structure), py::make_tuple(h, k, l), layers,
                      py::arg("vacuum") = vacuum)
            : surface(toAtoms(structure), py::make_tuple(h, k, l), layers);
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

AseBridge::XrdResult AseBridge::simulateXrd(const core::Structure& structure,
                                            double wavelength, double twoThetaMin,
                                            double twoThetaMax, int points,
                                            int repeat)
{
    try {
        py::object atoms = toAtoms(structure);
        if (repeat > 1 && structure.cell().isDefined()) {
            const auto pbc = structure.cell().pbc();
            atoms = atoms.attr("repeat")(py::make_tuple(pbc[0] ? repeat : 1,
                                                        pbc[1] ? repeat : 1,
                                                        pbc[2] ? repeat : 1));
        }

        const py::module_ np = py::module_::import("numpy");
        const py::module_ xrdebye = py::module_::import("ase.utils.xrdebye");

        // Iwasa (Waasmaier-Kirfel q-dependent form factors) only covers a
        // handful of elements; for anything else XrDebye silently returns
        // zero. Fall back to constant f = Z when a species is missing:
        // peak positions stay exact, high-angle intensities approximate.
        const py::object table = xrdebye.attr("waasmaier");
        bool allTabulated = true;
        for (const core::Atom& atom : structure.atoms()) {
            if (!table.contains(py::str(atom.symbol()))) {
                allTabulated = false;
                break;
            }
        }
        const std::string method = allTabulated ? "Iwasa" : "Z";

        const py::object simulator =
            xrdebye.attr("XrDebye")(py::arg("atoms") = atoms,
                                    py::arg("wavelength") = wavelength,
                                    py::arg("method") = method,
                                    py::arg("warn") = false);
        const py::object grid =
            np.attr("linspace")(twoThetaMin, twoThetaMax, points);
        const auto pattern =
            np.attr("asarray")(simulator.attr("calc_pattern")(
                                   py::arg("x") = grid, py::arg("mode") = "XRD"),
                               py::arg("dtype") = "float64")
                .cast<py::array_t<double>>();
        const auto angles =
            np.attr("asarray")(grid, py::arg("dtype") = "float64")
                .cast<py::array_t<double>>();

        XrdResult result;
        result.method = method;
        const auto a = angles.unchecked<1>();
        const auto p = pattern.unchecked<1>();
        const auto count = std::min(a.shape(0), p.shape(0));
        result.twoTheta.reserve(static_cast<std::size_t>(count));
        result.intensity.reserve(static_cast<std::size_t>(count));
        for (py::ssize_t i = 0; i < count; ++i) {
            result.twoTheta.push_back(a(i));
            result.intensity.push_back(p(i));
        }
        return result;
    } catch (const py::error_already_set& e) {
        rethrow(e, "XRD simulation failed");
    }
}

AseBridge::SymmetryInfo AseBridge::symmetryInfo(const core::Structure& structure,
                                                double symprec)
{
    SymmetryInfo info;
    if (!structure.cell().isDefined()) {
        info.error = "no unit cell";
        return info;
    }
    try {
        py::dict locals;
        locals["atoms"] = toAtoms(structure);
        locals["symprec"] = symprec;
        // spglib's dataset changed from a dict (< 2.5) to a dataclass;
        // read fields tolerantly through a small helper.
        py::exec(R"PY(
try:
    import spglib
except ImportError:
    result = {"error": "spglib is not installed (pip install spglib)"}
else:
    cell = (atoms.cell[:], atoms.get_scaled_positions(), atoms.numbers)
    dataset = spglib.get_symmetry_dataset(cell, symprec=symprec)
    if dataset is None:
        result = {"error": "symmetry detection failed"}
    else:
        def field(name):
            return dataset[name] if isinstance(dataset, dict) else getattr(dataset, name)
        number = int(field("number"))
        systems = [(2, "triclinic"), (15, "monoclinic"), (74, "orthorhombic"),
                   (142, "tetragonal"), (167, "trigonal"), (194, "hexagonal"),
                   (230, "cubic")]
        crystal = next(name for bound, name in systems if number <= bound)
        result = {"symbol": str(field("international")), "number": number,
                  "pointgroup": str(field("pointgroup")), "system": crystal,
                  "error": ""}
        # Per-atom Wyckoff letters and equivalence classes come back in the
        # same dataset — no extra spglib call. Guard for API differences.
        try:
            result["wyckoffs"] = [str(w) for w in field("wyckoffs")]
            result["equivalent_atoms"] = [int(x) for x in field("equivalent_atoms")]
        except Exception:
            result["wyckoffs"] = []
            result["equivalent_atoms"] = []
        try:
            result["hall"] = int(field("hall_number"))
        except Exception:
            result["hall"] = 0
)PY",
                 // One dict as BOTH globals and locals: with separate
                 // dicts, field()'s body cannot see `dataset` (function
                 // name lookup skips exec locals) — same scoping pitfall
                 // fixed in SqsBuilder.
                 locals, locals);
        const py::dict result = locals["result"].cast<py::dict>();
        if (result.contains("error"))
            info.error = result["error"].cast<std::string>();
        if (!info.error.empty())
            return info;
        info.spaceGroupSymbol = result["symbol"].cast<std::string>();
        info.spaceGroupNumber = result["number"].cast<int>();
        info.pointGroup = result["pointgroup"].cast<std::string>();
        info.crystalSystem = result["system"].cast<std::string>();
        if (result.contains("hall"))
            info.hallNumber = result["hall"].cast<int>();
        if (result.contains("wyckoffs"))
            info.wyckoffLetters = result["wyckoffs"].cast<std::vector<std::string>>();
        if (result.contains("equivalent_atoms"))
            info.equivalentAtoms = result["equivalent_atoms"].cast<std::vector<int>>();
        std::set<int> classes(info.equivalentAtoms.begin(),
                              info.equivalentAtoms.end());
        info.uniqueSites = static_cast<int>(classes.size());
        return info;
    } catch (const py::error_already_set& e) {
        info.error = std::string("symmetry query failed: ") + e.what();
        return info;
    }
}

core::Structure AseBridge::standardizeCell(const core::Structure& structure,
                                           double symprec, bool toPrimitive,
                                           bool idealize)
{
    if (!structure.cell().isDefined())
        throw std::runtime_error(
            "Cell standardization needs a defined periodic unit cell.");
    try {
        py::dict scope;
        scope["atoms"] = toAtoms(structure);
        scope["symprec"] = symprec;
        scope["to_primitive"] = toPrimitive;
        scope["no_idealize"] = !idealize;
        py::exec(R"PY(
import spglib
from ase import Atoms

cell = (atoms.cell[:], atoms.get_scaled_positions(), atoms.numbers)
res = spglib.standardize_cell(cell, to_primitive=to_primitive,
                              no_idealize=no_idealize, symprec=symprec)
if res is None:
    raise RuntimeError("spglib.standardize_cell failed (symmetry undetectable "
                       "at this tolerance?)")
lattice, scaled, numbers = res
result_atoms = Atoms(numbers=numbers, scaled_positions=scaled, cell=lattice,
                     pbc=True)
)PY",
                 scope, scope);
        return fromAtoms(scope["result_atoms"]);
    } catch (const py::error_already_set& e) {
        rethrow(e, "Cell standardization failed (is spglib installed?)");
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

core::Structure AseBridge::makeSupercellMatrix(const core::Structure& structure,
                                               const int p[3][3])
{
    // Guard against a singular transformation up front: ase.build.make_supercell
    // raises deep inside numpy on a zero-determinant P, and the traceback is
    // opaque. det of an integer 3×3 is exact in long arithmetic.
    const long det =
        static_cast<long>(p[0][0]) * (static_cast<long>(p[1][1]) * p[2][2]
                                      - static_cast<long>(p[1][2]) * p[2][1])
        - static_cast<long>(p[0][1]) * (static_cast<long>(p[1][0]) * p[2][2]
                                        - static_cast<long>(p[1][2]) * p[2][0])
        + static_cast<long>(p[0][2]) * (static_cast<long>(p[1][0]) * p[2][1]
                                        - static_cast<long>(p[1][1]) * p[2][0]);
    if (det == 0)
        throw std::runtime_error(
            "Supercell matrix is singular (determinant = 0): the three "
            "transformed lattice vectors are coplanar or collinear.");

    try {
        const py::object makeSupercell =
            py::module_::import("ase.build").attr("make_supercell");
        py::list matrix;
        for (int i = 0; i < 3; ++i)
            matrix.append(py::make_tuple(p[i][0], p[i][1], p[i][2]));
        return fromAtoms(makeSupercell(toAtoms(structure), matrix));
    } catch (const py::error_already_set& e) {
        rethrow(e, "Failed to build non-diagonal supercell (is a unit cell defined?)");
    }
}

} // namespace calango::pybridge
