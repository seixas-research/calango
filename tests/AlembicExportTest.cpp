// Alembic (.abc) export test.
//
// The exporter has two halves and they fail in completely different ways.
//
// The GEOMETRY half turns a Structure into polygon meshes. Its defects do not
// throw: a face index one past the end of the position array, a mesh filed
// under the wrong element, a bond emitted for a hydrogen the user hid — each
// produces a perfectly well-formed .abc that imports as garbage in Blender.
// Nothing but checking the arrays catches those, which is what most of this
// file does.
//
// The PYTHON half writes the archive. It cannot be exercised here because
// PyAlembic is not packaged for every platform (conda-forge has no build for
// arm64 macOS at all — only the unrelated SQLAlchemy `alembic`, which shares
// the import name). So this checks the two things that are checkable without
// it: that the embedded writer source is valid Python, and that the guard
// against exactly that name collision fires with a message naming it.
//
// GUI-free. Needs the embedded interpreter only for the guard checks.

#include "python_bridge/AlembicExporter.hpp"

#include "core/Structure.hpp"
#include "core/UnitCell.hpp"

#include <pybind11/embed.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace py = pybind11;
using namespace calango;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

/// Two water molecules in a box: two elements (so the per-element split is a
/// real grouping, not the trivial one-element case), hydrogens (so the
/// show-hydrogens filter has something to remove), bonds, and a cell.
core::Structure waterBox()
{
    core::Structure structure;
    structure.setCell(core::UnitCell({8, 0, 0}, {0, 8, 0}, {0, 0, 8}));
    const struct { int z; double x, y, zc; } sites[] = {
        {8, 2.00, 2.00, 2.00}, {1, 2.76, 2.59, 2.00}, {1, 1.24, 2.59, 2.00},
        {8, 5.00, 5.00, 5.00}, {1, 5.76, 5.59, 5.00}, {1, 4.24, 5.59, 5.00},
    };
    for (const auto& site : sites) {
        core::Atom atom;
        atom.atomicNumber = site.z;
        atom.position = {site.x, site.y, site.zc};
        structure.addAtom(atom);
    }
    return structure;
}

using Mesh = pybridge::AlembicExporter::Mesh;

/// Every structural invariant an Alembic PolyMesh has to satisfy. Violating
/// any of them yields a file that opens but is wrong, so they are checked on
/// every mesh the exporter emits rather than on a chosen one.
void checkMeshIsWellFormed(const Mesh& mesh, const std::string& name)
{
    const auto vertices = static_cast<int>(mesh.vertexCount());
    check(mesh.positions.size() % 3 == 0,
          name + ": the position stream is whole 3-vectors");
    check(vertices > 0, name + ": has vertices");

    // The face table's two arrays have to agree: counts says how many indices
    // each face consumes, so their sum IS the index count. A mismatch shifts
    // every face after the bad one — the classic exporter defect.
    const long long total =
        std::accumulate(mesh.counts.begin(), mesh.counts.end(), 0LL);
    check(total == static_cast<long long>(mesh.indices.size()),
          name + ": face counts sum to the index count");

    const bool inRange = std::all_of(
        mesh.indices.begin(), mesh.indices.end(),
        [vertices](int i) { return i >= 0 && i < vertices; });
    check(inRange, name + ": every face index is within the position array");

    const bool sane = std::all_of(mesh.counts.begin(), mesh.counts.end(),
                                  [](int c) { return c == 3 || c == 4; });
    check(sane, name + ": every face is a triangle or a quad");

    // An unreferenced vertex is dead weight in the file and usually means a
    // fan or ring was emitted with an off-by-one base offset.
    std::vector<bool> used(static_cast<std::size_t>(vertices), false);
    for (const int i : mesh.indices)
        if (i >= 0 && i < vertices)
            used[static_cast<std::size_t>(i)] = true;
    check(std::all_of(used.begin(), used.end(), [](bool u) { return u; }),
          name + ": no vertex is left unreferenced by any face");

    const bool finite = std::all_of(
        mesh.positions.begin(), mesh.positions.end(),
        [](float v) { return std::isfinite(v); });
    check(finite, name + ": every coordinate is finite");
}

pybridge::AlembicExporter::Options defaultOptions()
{
    pybridge::AlembicExporter::Options options;
    options.sphereSegments = 12; // small, so the arrays stay checkable
    options.cylinderSides = 8;
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    const core::Structure structure = waterBox();

    std::printf("Scene decomposition:\n");
    {
        const auto meshes =
            pybridge::AlembicExporter::buildMeshes(structure, defaultOptions());
        // One object per element plus bonds and the cell. The split is the
        // whole reason this exporter is not one merged blob: it is what lets a
        // material be assigned per species after import.
        check(meshes.count("atoms_O") == 1, "oxygens get their own mesh");
        check(meshes.count("atoms_H") == 1, "hydrogens get their own mesh");
        check(meshes.count("bonds") == 1, "bonds get a mesh");
        check(meshes.count("unit_cell") == 1, "the cell wireframe gets a mesh");
        check(meshes.size() == 4, "and nothing else is emitted");

        for (const auto& [name, mesh] : meshes)
            checkMeshIsWellFormed(mesh, name);

        // Two oxygens against four hydrogens, at the same tessellation: the
        // hydrogen mesh must carry exactly twice the geometry. This is what
        // catches atoms being filed under the wrong element.
        check(meshes.at("atoms_H").vertexCount()
                  == 2 * meshes.at("atoms_O").vertexCount(),
              "four hydrogens carry twice the geometry of two oxygens");

        // The cell is 12 edges of one capped cylinder each.
        const auto& cell = meshes.at("unit_cell");
        check(cell.counts.size() % 12 == 0,
              "the cell mesh divides evenly into its 12 edges");
    }

    std::printf("Colours follow the element palette:\n");
    {
        const auto meshes =
            pybridge::AlembicExporter::buildMeshes(structure, defaultOptions());
        const QColor oxygen = render::StructureRenderer::atomColor(
            8, defaultOptions().style);
        const auto& mesh = meshes.at("atoms_O");
        check(std::abs(mesh.color[0] - static_cast<float>(oxygen.redF())) < 1e-6f
                  && std::abs(mesh.color[1]
                              - static_cast<float>(oxygen.greenF())) < 1e-6f
                  && std::abs(mesh.color[2]
                              - static_cast<float>(oxygen.blueF())) < 1e-6f,
              "the oxygen mesh carries the CPK oxygen colour");
        // Distinct per element, or the whole per-element split buys nothing.
        const auto& hydrogen = meshes.at("atoms_H");
        check(std::abs(mesh.color[0] - hydrogen.color[0]) > 1e-3f
                  || std::abs(mesh.color[1] - hydrogen.color[1]) > 1e-3f
                  || std::abs(mesh.color[2] - hydrogen.color[2]) > 1e-3f,
              "and a different one from hydrogen");
    }

    std::printf("Options are honoured:\n");
    {
        auto options = defaultOptions();
        options.includeBonds = false;
        const auto meshes =
            pybridge::AlembicExporter::buildMeshes(structure, options);
        check(meshes.count("bonds") == 0, "bonds off emits no bond mesh");
        check(meshes.count("atoms_O") == 1, "and leaves the atoms alone");
    }
    {
        auto options = defaultOptions();
        options.includeCell = false;
        const auto meshes =
            pybridge::AlembicExporter::buildMeshes(structure, options);
        check(meshes.count("unit_cell") == 0, "cell off emits no cell mesh");
    }
    {
        // Hiding hydrogens is a DISPLAY filter, and the export must match what
        // the viewport shows — including dropping the bonds that terminate on
        // a hidden hydrogen, or the file contains sticks growing out of nothing.
        auto options = defaultOptions();
        options.style.showHydrogens = false;
        const auto meshes =
            pybridge::AlembicExporter::buildMeshes(structure, options);
        check(meshes.count("atoms_H") == 0, "hidden hydrogens are not exported");
        check(meshes.count("bonds") == 0,
              "and neither are the bonds that ended on them");
    }
    {
        // Detail is the file-size knob; it has to actually do something.
        auto coarse = defaultOptions();
        coarse.sphereSegments = 8;
        auto fine = defaultOptions();
        fine.sphereSegments = 32;
        const auto coarseMeshes =
            pybridge::AlembicExporter::buildMeshes(structure, coarse);
        const auto fineMeshes =
            pybridge::AlembicExporter::buildMeshes(structure, fine);
        check(fineMeshes.at("atoms_O").vertexCount()
                  > 4 * coarseMeshes.at("atoms_O").vertexCount(),
              "raising the sphere detail multiplies the vertex count");
        checkMeshIsWellFormed(fineMeshes.at("atoms_O"), "atoms_O (fine)");
        checkMeshIsWellFormed(coarseMeshes.at("atoms_O"), "atoms_O (coarse)");
    }
    {
        // A structure with no cell must not invent one.
        core::Structure molecule;
        core::Atom atom;
        atom.atomicNumber = 6;
        molecule.addAtom(atom);
        const auto meshes =
            pybridge::AlembicExporter::buildMeshes(molecule, defaultOptions());
        check(meshes.count("unit_cell") == 0,
              "a structure with no cell exports no cell mesh");
        check(meshes.count("atoms_C") == 1, "but still exports its atom");
    }

    std::printf("Geometry tracks the structure:\n");
    {
        // An animated cache is a sequence of these; if the mesh did not move
        // with the atoms every frame would be identical and the animation
        // would be a still.
        core::Structure moved = structure;
        moved.atoms()[0].position.x += 1.5;
        const auto before =
            pybridge::AlembicExporter::buildMeshes(structure, defaultOptions());
        const auto after =
            pybridge::AlembicExporter::buildMeshes(moved, defaultOptions());
        check(before.at("atoms_O").positions.size()
                  == after.at("atoms_O").positions.size(),
              "moving an atom keeps the topology identical");
        check(before.at("atoms_O").positions != after.at("atoms_O").positions,
              "and moves the vertices");
    }

    // -- The Python half ----------------------------------------------------
    {
        py::scoped_interpreter interpreter;

        std::printf("The embedded writer is valid Python:\n");
        {
            bool compiled = true;
            std::string error;
            try {
                py::module_::import("builtins").attr("compile")(
                    py::str(pybridge::AlembicExporter::writerSource()),
                    py::str("<alembic writer>"), py::str("exec"));
            } catch (const py::error_already_set& e) {
                compiled = false;
                error = e.what();
            }
            check(compiled, "the writer class byte-compiles" + error);
        }

        std::printf("The alembic name collision is caught:\n");
        {
            // `pip install alembic` installs the SQLAlchemy DATABASE MIGRATION
            // tool, which imports under the same name and has nothing to do
            // with geometry. It is by far the most likely thing a user ends up
            // with, and without this guard the failure surfaces as a
            // bewildering AttributeError deep inside the writer.
            QTemporaryDir fake;
            check(fake.isValid(), "scratch directory for the stub module");
            QFile stub(fake.filePath(QStringLiteral("alembic.py")));
            if (stub.open(QIODevice::WriteOnly | QIODevice::Text)) {
                stub.write("# stands in for the SQLAlchemy migration tool:\n"
                           "# same import name, no Abc submodule.\n"
                           "__version__ = '1.13.0'\n");
                stub.close();
            }
            py::module_::import("sys").attr("path").attr("insert")(
                0, py::str(fake.path().toStdString()));

            std::string message;
            try {
                pybridge::AlembicExporter::exportScene(
                    {std::make_shared<core::Structure>(structure)},
                    fake.filePath(QStringLiteral("out.abc")), defaultOptions());
            } catch (const std::exception& e) {
                message = e.what();
            }
            check(!message.empty(), "exporting against the wrong module throws");
            check(message.find("SQLAlchemy") != std::string::npos,
                  "and the message names the package actually installed");
            check(message.find("conda-forge") != std::string::npos,
                  "and gives the install line for the real one");
            check(!QFile::exists(fake.filePath(QStringLiteral("out.abc"))),
                  "and no half-written archive is left behind");
        }

        std::printf("An empty scene is refused:\n");
        {
            std::string message;
            try {
                pybridge::AlembicExporter::exportScene({}, QStringLiteral("x.abc"),
                                                       defaultOptions());
            } catch (const std::exception& e) {
                message = e.what();
            }
            check(!message.empty(), "exporting no frames throws rather than "
                                    "writing an empty archive");
        }
    }

    std::printf(failures == 0 ? "\nAll Alembic export checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
