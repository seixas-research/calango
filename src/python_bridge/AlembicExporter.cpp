#include "python_bridge/AlembicExporter.hpp"

#include "core/Element.hpp"
#include "core/UnitCell.hpp"
#include "render/RenderGeometry.hpp"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <QColor>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>

namespace py = pybind11;

namespace calango::pybridge {

namespace {

using Mesh = AlembicExporter::Mesh;

std::size_t addVertex(Mesh& mesh, const QVector3D& p)
{
    mesh.positions.insert(mesh.positions.end(), {p.x(), p.y(), p.z()});
    return mesh.vertexCount() - 1;
}

void addTriangle(Mesh& mesh, std::size_t a, std::size_t b, std::size_t c)
{
    mesh.indices.push_back(static_cast<int>(a));
    mesh.indices.push_back(static_cast<int>(b));
    mesh.indices.push_back(static_cast<int>(c));
    mesh.counts.push_back(3);
}

void addQuad(Mesh& mesh, std::size_t a, std::size_t b, std::size_t c,
             std::size_t d)
{
    mesh.indices.push_back(static_cast<int>(a));
    mesh.indices.push_back(static_cast<int>(b));
    mesh.indices.push_back(static_cast<int>(c));
    mesh.indices.push_back(static_cast<int>(d));
    mesh.counts.push_back(4);
}

/// UV sphere at `center`. Quads over the body, triangle fans at the poles —
/// the layout every DCC subdivides cleanly.
void appendSphere(Mesh& mesh, const QVector3D& center, float radius,
                  int segments)
{
    const int lon = std::max(6, segments);
    const int lat = std::max(3, segments / 2);
    const auto pi = static_cast<float>(std::numbers::pi);
    const std::size_t base = mesh.vertexCount();

    // Poles are single vertices; the rings between them carry `lon` each.
    const std::size_t north = addVertex(mesh, center + QVector3D(0, radius, 0));
    for (int ring = 1; ring < lat; ++ring) {
        const float theta = pi * static_cast<float>(ring) / static_cast<float>(lat);
        const float y = std::cos(theta);
        const float r = std::sin(theta);
        for (int slice = 0; slice < lon; ++slice) {
            const float phi =
                2.0f * pi * static_cast<float>(slice) / static_cast<float>(lon);
            addVertex(mesh,
                      center
                          + QVector3D(r * std::cos(phi), y, r * std::sin(phi))
                              * radius);
        }
    }
    const std::size_t south = addVertex(mesh, center - QVector3D(0, radius, 0));

    const auto ringVertex = [base](int ring, int slice, int lonCount) {
        return base + 1 + static_cast<std::size_t>((ring - 1) * lonCount + slice);
    };
    for (int slice = 0; slice < lon; ++slice) {
        const int next = (slice + 1) % lon;
        addTriangle(mesh, north, ringVertex(1, next, lon),
                    ringVertex(1, slice, lon));
        addTriangle(mesh, south, ringVertex(lat - 1, slice, lon),
                    ringVertex(lat - 1, next, lon));
    }
    for (int ring = 1; ring < lat - 1; ++ring) {
        for (int slice = 0; slice < lon; ++slice) {
            const int next = (slice + 1) % lon;
            addQuad(mesh, ringVertex(ring, slice, lon),
                    ringVertex(ring, next, lon),
                    ringVertex(ring + 1, next, lon),
                    ringVertex(ring + 1, slice, lon));
        }
    }
}

/// Capped cylinder from `from` to `to`.
void appendCylinder(Mesh& mesh, const QVector3D& from, const QVector3D& to,
                    float radius, int sides)
{
    const QVector3D axis = to - from;
    const float length = axis.length();
    if (length < 1e-6f)
        return;
    const int n = std::max(3, sides);
    const QVector3D dir = axis / length;
    const QVector3D u = render::perpendicularTo(dir);
    const QVector3D v = QVector3D::crossProduct(dir, u);
    const auto pi = static_cast<float>(std::numbers::pi);

    const std::size_t base = mesh.vertexCount();
    for (int i = 0; i < n; ++i) {
        const float phi = 2.0f * pi * static_cast<float>(i) / static_cast<float>(n);
        const QVector3D offset =
            (u * std::cos(phi) + v * std::sin(phi)) * radius;
        addVertex(mesh, from + offset);
        addVertex(mesh, to + offset);
    }
    for (int i = 0; i < n; ++i) {
        const int next = (i + 1) % n;
        addQuad(mesh, base + static_cast<std::size_t>(2 * i),
                base + static_cast<std::size_t>(2 * next),
                base + static_cast<std::size_t>(2 * next + 1),
                base + static_cast<std::size_t>(2 * i + 1));
    }
    // Caps as fans around a center vertex, so a bond reads as solid even where
    // no atom sphere covers its end (licorice, or a wrapped bond's stub).
    const std::size_t bottom = addVertex(mesh, from);
    const std::size_t top = addVertex(mesh, to);
    for (int i = 0; i < n; ++i) {
        const int next = (i + 1) % n;
        addTriangle(mesh, bottom, base + static_cast<std::size_t>(2 * next),
                    base + static_cast<std::size_t>(2 * i));
        addTriangle(mesh, top, base + static_cast<std::size_t>(2 * i + 1),
                    base + static_cast<std::size_t>(2 * next + 1));
    }
}

void setColor(Mesh& mesh, const QColor& color)
{
    mesh.color[0] = static_cast<float>(color.redF());
    mesh.color[1] = static_cast<float>(color.greenF());
    mesh.color[2] = static_cast<float>(color.blueF());
}

} // namespace

std::map<std::string, AlembicExporter::Mesh> AlembicExporter::buildMeshes(
    const core::Structure& structure, const Options& options)
{
    using render::StructureRenderer;
    std::map<std::string, Mesh> meshes;
    const auto& atoms = structure.atoms();
    const std::vector<StructureRenderer::CastStyle> casts =
        StructureRenderer::atomCastStyles(&structure, options.style);

    for (std::size_t index = 0; index < atoms.size(); ++index) {
        const core::Atom& atom = atoms[index];
        if (!options.style.showHydrogens && atom.atomicNumber == 1)
            continue;
        const StructureRenderer::CastStyle& cast = casts[index];
        // Split per element: one material per species is how a molecular scene
        // is shaded in every DCC, and a single merged mesh would make that
        // impossible without re-selecting faces by hand.
        const std::string name =
            std::string("atoms_") + core::Elements::data(atom.atomicNumber).symbol;
        Mesh& mesh = meshes[name];
        if (mesh.vertexCount() == 0)
            setColor(mesh, StructureRenderer::atomColor(atom.atomicNumber,
                                                        options.style));
        float radius = StructureRenderer::displayRadius(atom.atomicNumber, cast);
        if (const auto it =
                options.style.radiusScaleOverrides.find(atom.atomicNumber);
            it != options.style.radiusScaleOverrides.end())
            radius *= it->second;
        appendSphere(mesh, render::toQt(atom.position), radius,
                     options.sphereSegments);
    }

    if (options.includeBonds) {
        Mesh& bonds = meshes["bonds"];
        setColor(bonds, QColor(190, 190, 195));
        for (const core::Bond& bond :
             structure.detectBonds(options.style.bondTolerance,
                                   options.style.autoBonds)) {
            const auto i = static_cast<std::size_t>(bond.i);
            const auto j = static_cast<std::size_t>(bond.j);
            if (!options.style.showHydrogens
                && (atoms[i].atomicNumber == 1 || atoms[j].atomicNumber == 1))
                continue;
            const QVector3D pa = render::toQt(atoms[i].position);
            const QVector3D pb =
                render::toQt(atoms[j].position + bond.imageOffset);
            std::vector<float> lateral;
            float radiusScale = 1.0f;
            render::multiBondLayout(bond.order, lateral, radiusScale);
            const float baseRadius = options.style.bondRadius
                * 0.5f
                * (casts[i].bondWidthFactor + casts[j].bondWidthFactor);
            const QVector3D perp = render::perpendicularTo((pb - pa).normalized());
            for (const float units : lateral) {
                const QVector3D shift = perp * (units * baseRadius);
                appendCylinder(bonds, pa + shift, pb + shift,
                               baseRadius * radiusScale, options.cylinderSides);
            }
        }
        if (bonds.vertexCount() == 0)
            meshes.erase("bonds");
    }

    if (options.includeCell && structure.cell().isDefined()) {
        Mesh& cell = meshes["unit_cell"];
        setColor(cell, options.style.cellColor);
        const auto corners = structure.cell().corners();
        for (const auto& [a, b] : core::UnitCell::edges()) {
            appendCylinder(cell, render::toQt(corners[static_cast<std::size_t>(a)]),
                           render::toQt(corners[static_cast<std::size_t>(b)]),
                           static_cast<float>(options.cellTubeRadius),
                           std::max(6, options.cylinderSides / 2));
        }
    }
    return meshes;
}

namespace {

/// Import PyAlembic, distinguishing it from the unrelated PyPI package of the
/// same import name.
///
/// `pip install alembic` gets the SQLAlchemy database-migration tool, which
/// imports as `alembic` and has nothing to do with geometry. Without this check
/// the failure surfaces as a bewildering AttributeError deep in the writer, so
/// the collision is detected up front and named.
void requireAlembic()
{
    const char* kInstallHint =
        "Alembic export needs PyAlembic (the geometry library) in the embedded\n"
        "Python environment. Install it with:\n"
        "    conda install -c conda-forge pyalembic imath\n";
    py::module_ module;
    try {
        module = py::module_::import("alembic");
    } catch (const py::error_already_set&) {
        throw std::runtime_error(kInstallHint);
    }
    if (!py::hasattr(module, "Abc")) {
        throw std::runtime_error(
            std::string("The installed 'alembic' module is the SQLAlchemy "
                        "database-migration\ntool, not PyAlembic — the two "
                        "share an import name.\n\n")
            + kInstallHint);
    }
    try {
        py::module_::import("imath");
    } catch (const py::error_already_set&) {
        throw std::runtime_error(kInstallHint);
    }
}

/// Writer driven from C++ one mesh sample at a time. Kept as a Python-side
/// helper class rather than as a pile of pybind calls because the PyAlembic
/// API is object-heavy and the imath array types have to be filled elementwise
/// anyway.
constexpr const char* kWriterSource = R"PY(
import alembic
import imath
from alembic.Abc import OArchive
from alembic.AbcCoreAbstract import TimeSampling
from alembic.AbcGeom import OPolyMesh, OPolyMeshSchemaSample


class _Writer:
    """One Alembic archive, one PolyMesh per named object."""

    def __init__(self, path, fps):
        self.archive = OArchive(str(path))
        self.top = self.archive.getTop()
        self.meshes = {}
        # A cache with one sample is a still; give it the default sampling
        # rather than inventing a rate for a single frame.
        self.ts_index = 0
        if fps and fps > 0:
            try:
                self.ts_index = self.archive.addTimeSampling(
                    TimeSampling(1.0 / float(fps), 0.0))
            except Exception:
                self.ts_index = 0

    def _mesh(self, name):
        mesh = self.meshes.get(name)
        if mesh is None:
            mesh = OPolyMesh(self.top, name)
            if self.ts_index:
                try:
                    mesh.getSchema().setTimeSampling(self.ts_index)
                except Exception:
                    pass
            self.meshes[name] = mesh
        return mesh

    def add_sample(self, name, positions, indices, counts, color):
        """One time sample of `name`. Empty streams write an empty sample,
        which is how an object absent from this frame keeps its place in the
        archive's uniform sample count."""
        points = imath.V3fArray(len(positions) // 3)
        for i in range(len(positions) // 3):
            points[i] = imath.V3f(positions[3 * i],
                                  positions[3 * i + 1],
                                  positions[3 * i + 2])
        face_indices = imath.IntArray(len(indices))
        for i, value in enumerate(indices):
            face_indices[i] = int(value)
        face_counts = imath.IntArray(len(counts))
        for i, value in enumerate(counts):
            face_counts[i] = int(value)

        mesh = self._mesh(name)
        schema = mesh.getSchema()
        schema.set(OPolyMeshSchemaSample(points, face_indices, face_counts))
        # A constant display colour, so the import lands with something other
        # than default grey. Optional: the arbitrary-geom-param spelling has
        # moved between PyAlembic builds, and a missing colour is a far smaller
        # loss than a failed export.
        if color is not None:
            try:
                self._set_color(mesh, color)
            except Exception:
                pass

    def _set_color(self, mesh, color):
        from alembic.AbcGeom import (OC3fGeomParam, OC3fGeomParamSample,
                                     GeometryScope)
        key = id(mesh)
        params = getattr(self, "_color_params", None)
        if params is None:
            params = self._color_params = {}
        param = params.get(key)
        if param is None:
            param = OC3fGeomParam(mesh.getSchema().getArbGeomParams(), "Cd",
                                  False, GeometryScope.kConstantScope, 1)
            params[key] = param
        values = imath.C3fArray(1)
        values[0] = imath.Color3f(color[0], color[1], color[2])
        param.set(OC3fGeomParamSample(values, GeometryScope.kConstantScope))


def make_writer(path, fps):
    return _Writer(path, fps)
)PY";

} // namespace

const char* AlembicExporter::writerSource()
{
    return kWriterSource;
}

void AlembicExporter::exportScene(
    const std::vector<std::shared_ptr<const core::Structure>>& frames,
    const QString& path, const Options& options)
{
    if (frames.empty())
        throw std::runtime_error("No structure to export");
    requireAlembic();

    // Build every frame first. The archive needs the same set of objects on
    // every sample, and that set is only known once all the frames are in —
    // a bond that only exists in frame 40 still has to own a mesh from
    // frame 0, or the samples stop lining up.
    std::vector<std::map<std::string, Mesh>> built;
    built.reserve(frames.size());
    std::vector<std::string> names;
    for (const auto& frame : frames) {
        if (!frame || frame->empty())
            continue;
        built.push_back(buildMeshes(*frame, options));
        for (const auto& [name, mesh] : built.back()) {
            if (std::find(names.begin(), names.end(), name) == names.end())
                names.push_back(name);
        }
    }
    if (built.empty())
        throw std::runtime_error("No atoms to export");
    std::sort(names.begin(), names.end());

    try {
        py::dict scope;
        py::exec(kWriterSource, scope, scope);
        py::object writer = scope["make_writer"](
            path.toStdString(), built.size() > 1 ? options.fps : 0.0);

        for (const auto& frame : built) {
            for (const std::string& name : names) {
                const auto it = frame.find(name);
                static const Mesh kEmpty;
                const Mesh& mesh = it != frame.end() ? it->second : kEmpty;
                writer.attr("add_sample")(
                    name, py::cast(mesh.positions), py::cast(mesh.indices),
                    py::cast(mesh.counts),
                    py::make_tuple(mesh.color[0], mesh.color[1], mesh.color[2]));
            }
        }
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("Alembic export failed:\n")
                                 + e.what());
    }
}

} // namespace calango::pybridge
